// TETRAPOL receiver (PAS 0001-2): GMSK 8 kbit/s, 20 ms data frames, D_SYSTEM_INFO when the 66-bit payload carries it.
#include "dect2/tetrapol/tetrapol_rx.h"
#include "dect2/tetrapol/tetrapol_codec.h"
#include "dect2/tetrapol/tetrapol_tel.h"
#include "dect2/results/results_io.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <mutex>
#include <vector>

namespace dect2 {

ModeTuning tetrapolTuning() {
    ModeTuning t;
    t.stdMode = 81; t.id = "tetrapol"; t.name = "TETRAPOL";
    t.minMhz = 68; t.maxMhz = 520; t.defMhz = 392.0;
    t.sampleRate = 250000;
    t.basebandHz = 200000;
    t.bandwidthMhz = 0.2;
    t.minSampleRate = 64000;
    return t;
}

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kBaud = 8000.0;
constexpr int kSps = 8;
constexpr double kWork = kBaud * kSps;

void mixDecim(const cf32* x, size_t n, double fs, double shift, std::vector<cf32>& acc, double& hold, cf32& rot, cf32 step) {
    const double adv = kWork / fs;
    for (size_t i = 0; i < n; i++) {
        const cf32 y = x[i] * rot;
        rot *= step;
        hold += adv;
        while (hold >= 1.0) {
            acc.push_back(y);
            hold -= 1.0;
        }
    }
    rot /= std::abs(rot);
}

std::vector<uint8_t> demodBits(const std::vector<cf32>& x, int phase) {
    std::vector<uint8_t> b;
    if (x.size() < (size_t)(kSps * 20)) return b;
    cf32 prev = x[(size_t)phase];
    for (size_t i = (size_t)phase + (size_t)kSps; i + (size_t)kSps < x.size(); i += (size_t)kSps) {
        cf32 acc(0.f, 0.f);
        for (int k = 0; k < kSps; k++) acc += x[i + (size_t)k];
        const cf32 d = acc * std::conj(prev);
        b.push_back(d.imag() < 0.f ? 1 : 0);   // gmskBurst NRZ is +1 when (bit XOR prev) is 0, so imag>0 is a 0-bit
        prev = acc;
    }
    return b;
}

float rmsDb(const std::vector<cf32>& x) {
    if (x.empty()) return -200.f;
    double p = 0;
    for (const cf32& v : x) p += (double)std::norm(v);
    p /= (double)x.size();
    return p > 1e-20 ? (float)(10.0 * std::log10(p)) : -200.f;
}
} // namespace

struct TetrapolReceiver::Impl {
    std::mutex mu;
    TetrapolTelemetry tel;
    std::function<void(const std::string&)> log;
    double fs = 0, offHz = 0, centreHz = 0;
    int spacing = 12500;
    int scrambleHint = -1;
    std::vector<cf32> acc;
    double hold = 0;
    cf32 rot{1.f, 0.f}, step{1.f, 0.f};
    uint8_t diffPrev = 0;
    uint64_t samp = 0;
    bool rateSet = false;

    void retune() {
        const double w = -2 * kPi * offHz / std::max(fs, 1.0);
        step = cf32((float)std::cos(w), (float)std::sin(w));
    }

    void applyFrame(const tetrapol::DataFrame& f, float level, float snr, double cfo) {
        tel.blocksOk++;
        tel.dataValid = true;
        tel.snrDb = snr;
        tel.levelDb = level;
        tel.cfoHz = cfo;
        tel.state = 2;
        int country = -1, sys = -1, bn = -1, cell = -1, la = -1, ch = -1, band = 0;
        const bool si = tetrapol::parseSysinfo(f.payload, country, sys, bn, cell, la, ch, band);
        TetrapolCell* c = nullptr;
        for (auto& e : tel.cells) {
            if (si && e.cellId == cell && e.systemId == sys) { c = &e; break; }
            if (!si && e.scramble == f.scramble) { c = &e; break; }
        }
        if (!c) {
            if (tel.cells.size() >= kTetrapolMax) tel.cells.pop_back();
            tel.cells.insert(tel.cells.begin(), TetrapolCell());
            c = &tel.cells.front();
            c->firstWall = (int64_t)std::time(nullptr);
        }
        c->freqHz = centreHz;
        c->cfoHz = cfo;
        c->scramble = f.scramble;
        c->levelDb = level;
        c->snrDb = snr;
        c->lastWall = (int64_t)std::time(nullptr);
        c->lastSec = tel.timeSec;
        c->framesOk++;
        if (si) {
            c->hasSysinfo = true;
            c->country = country;
            c->systemId = sys;
            c->bnId = bn;
            c->cellId = cell;
            c->locArea = la;
            c->channelId = ch;
            c->band = band;
            if (tel.events.size() >= kTetrapolLogMax) tel.events.pop_back();
            char line[160];
            snprintf(line, sizeof line, "D_SYSTEM_INFO country %d system %d BN %d cell %d LA %d ch %d scramble %d",
                     country, sys, bn, cell, la, ch, f.scramble);
            tel.events.insert(tel.events.begin(), TetrapolEvent{c->lastWall, tel.timeSec, line});
            if (log) log(line);
        }
        scrambleHint = f.scramble;
        if (tel.carriers.empty()) tel.carriers.push_back({centreHz, level, true});
        else { tel.carriers[0].freqHz = centreHz; tel.carriers[0].levelDb = level; tel.carriers[0].synced = true; }
    }

    void process() {
        if (acc.size() < (size_t)(kSps * tetrapol::kFrameBits * 3)) return;
        const float level = rmsDb(acc);
        bool got = false;
        for (int ph = 0; ph < kSps && !got; ph++) {
            std::vector<uint8_t> bits = demodBits(acc, ph);
            if ((int)bits.size() < tetrapol::kFrameBits + 8) continue;
            tetrapol::undiffBits(bits.data(), (int)bits.size(), 0);
            int off = -1;
            tetrapol::DataFrame f;
            if (tetrapol::huntFrame(bits.data(), (int)bits.size(), scrambleHint, off, f)) {
                applyFrame(f, level, 20.f, offHz);
                got = true;
            }
        }
        if (!got) {
            tel.blocksBad++;
            tel.state = tel.cells.empty() ? 1 : 2;
        }
        // keep a little overlap for the next hunt
        if (acc.size() > (size_t)(kSps * tetrapol::kFrameBits))
            acc.erase(acc.begin(), acc.end() - (size_t)(kSps * tetrapol::kFrameBits));
        tel.seq++;
        tel.blocks++;
        tel.spacingHz = spacing;
        tel.centerHz = centreHz;
        tel.inputRate = fs;
    }
};

TetrapolReceiver::TetrapolReceiver() : p_(std::make_unique<Impl>()) {}
TetrapolReceiver::~TetrapolReceiver() = default;

void TetrapolReceiver::configure(double inputRateHz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->fs = inputRateHz;
    p_->rateSet = inputRateHz >= tetrapolTuning().minSampleRate - 1;
    p_->retune();
    p_->tel.inputRate = inputRateHz;
}
void TetrapolReceiver::setSignalOffset(double hz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->offHz = hz;
    p_->retune();
}
void TetrapolReceiver::setCenterHz(double hz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->centreHz = hz;
    p_->tel.centerHz = hz;
}
void TetrapolReceiver::setSpacingHz(int hz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->spacing = (hz == 10000) ? 10000 : 12500;
}
bool TetrapolReceiver::ready() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return p_->rateSet;
}
void TetrapolReceiver::reset() {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->acc.clear();
    p_->hold = 0;
    p_->rot = cf32(1.f, 0.f);
    p_->diffPrev = 0;
    p_->scrambleHint = -1;
    p_->samp = 0;
    p_->tel = TetrapolTelemetry();
    p_->tel.inputRate = p_->fs;
    p_->tel.centerHz = p_->centreHz;
    p_->tel.spacingHz = p_->spacing;
    p_->tel.state = p_->rateSet ? 1 : 0;
}
void TetrapolReceiver::clearCells() {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->tel.cells.clear();
    p_->tel.events.clear();
    p_->tel.carriers.clear();
    p_->tel.seq++;
}
void TetrapolReceiver::feed(const cf32* x, size_t n) {
    if (!x || !n) return;
    std::lock_guard<std::mutex> lk(p_->mu);
    if (!p_->rateSet) { p_->tel.state = 0; return; }
    if (p_->tel.state == 0) p_->tel.state = 1;
    mixDecim(x, n, p_->fs, p_->offHz, p_->acc, p_->hold, p_->rot, p_->step);
    p_->samp += n;
    p_->tel.timeSec = p_->fs > 0 ? (double)p_->samp / p_->fs : 0;
    while (p_->acc.size() >= (size_t)(kSps * tetrapol::kFrameBits * 4)) p_->process();
}
bool TetrapolReceiver::telemetry(TetrapolTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->tel.seq <= lastSeq) return false;
    out = p_->tel;
    return true;
}
void TetrapolReceiver::clearResults() { clearCells(); }
std::string TetrapolReceiver::saveResults() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return results::pack(p_->tel.timeSec, p_->tel.cells, p_->tel.events);
}
bool TetrapolReceiver::loadResults(const std::string& blob) {
    double t0 = 0;
    TetrapolTelemetry l;
    if (!results::unpack(blob, t0, l.cells, l.events)) return false;
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->tel.cells = std::move(l.cells);
    p_->tel.events = std::move(l.events);
    p_->tel.seq++;
    return true;
}
void TetrapolReceiver::setLogCallback(std::function<void(const std::string&)> cb) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->log = std::move(cb);
}
bool TetrapolReceiver::busy() const { return false; }

} // namespace dect2
