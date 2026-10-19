// EDACS receiver: FM front end at 76.8 kHz (8 samples per bit), dotting search at every phase, then the 240 payload bits are sliced for
// the three alignments the dotting leaves open (one bit earlier, exact, one later) and the one whose three copies agree best is voted.
#include "dect2/edacs/edacs_rx.h"
#include "dect2/dsp/fm_front.h"
#include "dect2/edacs/edacs_proto.h"
#include "dect2/edacs/edacs_tel.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <mutex>

namespace dect2 {

ModeTuning edacsTuning() {
    ModeTuning t;
    t.stdMode = 132;
    t.id = "edacs";
    t.name = "EDACS trunking (WIP)";
    t.minMhz = 29.7;
    t.maxMhz = 960.0;
    t.defMhz = 856.0;
    t.sampleRate = 480000;
    t.basebandHz = 300000;
    t.bandwidthMhz = 0.025;
    t.minSampleRate = 307200;
    t.tuneOffsetHz = 100000;
    return t;
}

namespace {
constexpr double kWork = 76800.0;
constexpr int kSps = 8;
constexpr int kRing = 4096, kMask = kRing - 1;
constexpr int kPayloadBits = 240;
constexpr int kMaxDisagree = 4;           // per message, of 40 bits
constexpr double kMinRangeHz = 400.0;
constexpr double kLockSec = 5.0;
int pop(uint64_t x) { return __builtin_popcountll(x); }
}

struct EdacsReceiver::Impl {
    std::mutex mu;
    EdacsTelemetry tel;
    std::function<void(const std::string&)> log;
    FmFront fm;
    FmBiquad post;
    EyeEnvelope env;
    std::vector<float> disc;
    double fs = 0, offHz = 0, centreHz = 0;
    bool rateSet = false, inverted = false;
    uint64_t n = 0;
    float ring[kRing] = {};
    uint64_t reg[kSps] = {};               // last 48 bits per phase
    std::vector<uint64_t> hits;            // sample counts (n after increment) of dotting hits
    uint64_t firstHit = 0, evalAt = 0, centre = 0;
    bool pending = false, haveStart = false, predictedMode = false;
    uint64_t lastStart = 0;
    double lastGoodSec = -1e9, lastPub = 0;
    std::map<uint32_t, uint64_t> wordCount;

    double timeSec() const { return (double)n / kWork; }

    void clearState() {
        fm.reset(); post.clear(); env.clear();
        n = 0; for (auto& r : ring) r = 0; for (auto& r : reg) r = 0;
        hits.clear(); pending = false; haveStart = false; firstHit = evalAt = centre = 0; lastGoodSec = -1e9; lastPub = 0; wordCount.clear();
        const uint64_t s = tel.seq;
        tel = EdacsTelemetry();
        tel.seq = s + 1; tel.inputRate = fs; tel.centerHz = centreHz; tel.state = rateSet ? 1 : 0; tel.inverted = inverted;
    }

    double soft(uint64_t idx) const { return (ring[idx & kMask] + ring[(idx - 1) & kMask] + ring[(idx - 2) & kMask]) / 3.0; }

    void evaluate() {
        // centre: sample index (1-based count) of the median dotting hit; the last sync bit was sampled at centre - 1
        const double thr = env.mid();
        // Frames follow each other every 288 bits, so after a good frame the next one is looked for where the clock says (predicted). The dotting alone
        // cannot tell neighbouring bit alignments apart (the three copies agree under a shift) and the Barker word is not public.
        const bool predicted = predictedMode;
        struct Cand { int off, score, ea, eb; uint32_t a, b; };
        Cand c[6];
        const int order[6] = {0, -1, 1, 2, -2, 3};   // the dotting looks the same two bits later or earlier, so these alignments are tried
        const int nOrd = predicted ? 3 : 6;
        for (int oi = 0; oi < nOrd; oi++) {
            const int off = order[oi];
            const int64_t last = (int64_t)centre - 1 + (int64_t)off * kSps;
            uint8_t bits[kPayloadBits];
            for (int i = 0; i < kPayloadBits; i++) {
                const bool one = soft((uint64_t)(last + (int64_t)(i + 1) * kSps)) > thr;
                bits[i] = (uint8_t)(one != inverted);
            }
            // the 48 bits in front of the payload should be dotting at both ends: count the breaks in the first and last 16 of them
            uint8_t pre[edacs::kSyncBits];
            for (int j = 0; j < edacs::kSyncBits; j++) pre[edacs::kSyncBits - 1 - j] = soft((uint64_t)(last - (int64_t)j * kSps)) > thr ? 1 : 0;
            int dot = 0;
            for (int j = 1; j < 16; j++) dot += pre[j] == pre[j - 1];
            for (int j = edacs::kSyncBits - 15; j < edacs::kSyncBits; j++) dot += pre[j] == pre[j - 1];
            int ea, eb;
            const uint64_t va = edacs::vote(bits, ea), vb = edacs::vote(bits + 120, eb);
            c[oi] = {off, dot + ea + eb, ea, eb, (uint32_t)(va >> 12) & 0xFFFFFFF, (uint32_t)(vb >> 12) & 0xFFFFFFF};
        }
        int best = 0;
        for (int i = 1; i < nOrd; i++) if (c[i].score < c[best].score) best = i;
        if (predicted && c[0].score <= c[best].score + 1) best = 0;
        const uint32_t bestA = c[best].a, bestB = c[best].b;
        const int bestEa = c[best].ea, bestEb = c[best].eb;
        const uint64_t chosenStart = centre + (uint64_t)((int64_t)c[best].off * kSps);
        tel.syncs++;
        if (env.range() < kMinRangeHz) return;
        const bool good = bestEa <= kMaxDisagree && bestEb <= kMaxDisagree;
        if (!good) { tel.blocksBad++; haveStart = false; return; }
        lastStart = chosenStart; haveStart = true;
        EdacsFrame f;
        f.wallTime = (int64_t)std::time(nullptr);
        f.timeSec = timeSec();
        f.msgA = bestA; f.msgB = bestB; f.errA = bestEa; f.errB = bestEb; f.good = true;
        if (tel.frames.size() >= kEdacsLogMax) tel.frames.pop_back();
        tel.frames.insert(tel.frames.begin(), f);
        wordCount[bestA]++; wordCount[bestB]++;
        std::vector<EdacsWordCount> w;
        for (auto& kv : wordCount) w.push_back({kv.first, kv.second});
        std::sort(w.begin(), w.end(), [](const EdacsWordCount& a, const EdacsWordCount& b) { return a.count > b.count; });
        if (w.size() > 24) w.resize(24);
        tel.words = std::move(w);
        tel.blocksOk++;
        tel.dataValid = true;
        lastGoodSec = f.timeSec;
        if (log) { char b[80]; snprintf(b, sizeof b, "EDACS frame %07X %07X", bestA, bestB); log(b); }
    }

    static int alternationMismatch(uint64_t a16) { return 15 - pop((a16 ^ (a16 >> 1)) & 0x7FFF); }

    void sample(double s) {
        ring[n & kMask] = (float)s;
        env.push(s);
        const double thr = env.mid();
        const int ph = (int)(n % kSps);
        reg[ph] = (reg[ph] << 1) | (soft(n) > thr ? 1ULL : 0ULL);
        n++;
        if (pending) {
            if (n >= evalAt) {
                pending = false;
                evaluate();
                hits.clear();
                if (haveStart) {   // the next frame follows 288 bits after this one
                    centre = lastStart + (uint64_t)edacs::kFrameBits * kSps;
                    evalAt = centre + (uint64_t)(kPayloadBits + 2) * kSps;
                    predictedMode = true;
                    pending = evalAt > n;
                } else predictedMode = false;
            }
            return;
        }
        if (n > 1000 && env.range() >= kMinRangeHz) {
            const uint64_t r = reg[ph];
            if (alternationMismatch((r >> 32) & 0xFFFF) + alternationMismatch(r & 0xFFFF) <= 1) {
                if (hits.empty()) firstHit = n;
                hits.push_back(n);
            }
        }
        if (!hits.empty() && n >= firstHit + kSps / 2) {
            centre = hits[hits.size() / 2];
            hits.clear();
            pending = true;
            predictedMode = false;
            evalAt = centre + (uint64_t)(kPayloadBits + 5) * kSps;
        }
    }

    void publish() {
        tel.levelDb = (float)fm.levelDb();
        tel.deviationHz = (float)env.range();
        tel.timeSec = timeSec();
        tel.locked = timeSec() - lastGoodSec < kLockSec;
        tel.state = rateSet ? (tel.locked ? 2 : 1) : 0;
        tel.seq++;
    }
};

EdacsReceiver::EdacsReceiver() : p_(std::make_unique<Impl>()) {}
EdacsReceiver::~EdacsReceiver() = default;

void EdacsReceiver::configure(double inputRateHz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->fs = inputRateHz;
    p_->rateSet = inputRateHz >= edacsTuning().minSampleRate - 1 && p_->fm.configure(inputRateHz, kWork, 9000.0);
    p_->fm.setOffset(p_->offHz);
    p_->post.lowpass(6000.0, kWork, 0.7071);
    p_->clearState();
}
void EdacsReceiver::setSignalOffset(double hz) { std::lock_guard<std::mutex> lk(p_->mu); p_->offHz = hz; p_->fm.setOffset(hz); }
void EdacsReceiver::setCenterHz(double hz) { std::lock_guard<std::mutex> lk(p_->mu); p_->centreHz = hz; p_->tel.centerHz = hz; }
void EdacsReceiver::setInverted(bool inv) { std::lock_guard<std::mutex> lk(p_->mu); p_->inverted = inv; p_->tel.inverted = inv; p_->tel.seq++; }
bool EdacsReceiver::ready() const { return p_->rateSet; }
void EdacsReceiver::reset() { std::lock_guard<std::mutex> lk(p_->mu); p_->clearState(); }
void EdacsReceiver::clearMessages() {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->tel.frames.clear(); p_->tel.words.clear(); p_->wordCount.clear(); p_->tel.dataValid = false; p_->tel.seq++;
}
void EdacsReceiver::feed(const cf32* x, size_t n) {
    if (!x || !n) return;
    std::lock_guard<std::mutex> lk(p_->mu);
    if (!p_->rateSet) { p_->tel.state = 0; return; }
    p_->disc.clear();
    p_->fm.feed(x, n, p_->disc);
    for (float d : p_->disc) p_->sample(p_->post.run(d));
    if (p_->timeSec() - p_->lastPub >= 0.25) { p_->lastPub = p_->timeSec(); p_->publish(); }
}
bool EdacsReceiver::telemetry(EdacsTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->tel.seq <= lastSeq) return false;
    out = p_->tel;
    return true;
}
void EdacsReceiver::setLogCallback(std::function<void(const std::string&)> cb) { std::lock_guard<std::mutex> lk(p_->mu); p_->log = std::move(cb); }

} // namespace dect2
