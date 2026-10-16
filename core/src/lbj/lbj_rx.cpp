// LBJ receiver (lbj_rx.h): FM channel, level and AFC tracking; frame decode via lbj_proto (stub).
#include "dect2/lbj_rx.h"
#include "dect2/lbj_proto.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <mutex>
#include <vector>

namespace dect2 {

ModeTuning lbjTuning() {
    ModeTuning t;
    t.stdMode = 83;
    t.id = "lbj";
    t.name = "LBJ";
    t.minMhz = 821.0;
    t.maxMhz = 821.5;
    t.defMhz = lbj::kDefaultMhz;
    t.sampleRate = 960000;
    t.basebandHz = 35000;
    t.bandwidthMhz = 0.035;
    t.minSampleRate = 256000;
    t.tuneOffsetHz = 50000;   // keep DC out of the passband (common on rtl_tcp setups)
    return t;
}

namespace {

constexpr double kPi = 3.14159265358979323846;

float rmsDb(const cf32* x, size_t n) {
    if (!n) return -200.f;
    double p = 0;
    for (size_t i = 0; i < n; i++) p += (double)std::norm(x[i]);
    p /= (double)n;
    return p > 1e-20 ? (float)(10.0 * std::log10(p)) : -200.f;
}

} // namespace

struct LbjReceiver::Impl {
    std::mutex mu;
    LbjTelemetry tel;
    std::function<void(const std::string&)> log;
    double fs = 0, offHz = 0, centreHz = 0;
    bool rateSet = false;
    uint64_t samp = 0;
    cf32 rot{1.f, 0.f}, step{1.f, 0.f};
    cf32 prev{1.f, 0.f};
    std::vector<uint8_t> bitBuf;
    double nextPub = 0;

    void retune() {
        const double w = -2 * kPi * offHz / std::max(fs, 1.0);
        step = cf32((float)std::cos(w), (float)std::sin(w));
    }

    void pushMessage(LbjMessage m) {
        m.wallTime = (int64_t)std::time(nullptr);
        m.timeSec = tel.timeSec;
        if (log) {
            char line[256];
            snprintf(line, sizeof line, "LBJ %s km %s loco %s", m.train.c_str(), m.kmMarker.c_str(), m.loco.c_str());
            log(line);
        }
        if (tel.messages.size() >= kLbjLogMax) tel.messages.pop_back();
        tel.messages.insert(tel.messages.begin(), std::move(m));
        tel.dataValid = true;
        tel.state = 2;
        tel.blocksOk++;
        tel.seq++;
    }

    void processBlock(const cf32* x, size_t n) {
        const float level = rmsDb(x, n);
        tel.levelDb = level;
        tel.cfoHz = offHz;
        tel.blocks++;
        if (level < -90.f) {
            tel.state = tel.messages.empty() ? 1 : 2;
            return;
        }
        // FM discriminator at input rate (feeds the future 48 kHz / 1200 baud chain)
        for (size_t i = 0; i < n; i++) {
            const cf32 y = x[i] * rot;
            rot *= step;
            const cf32 d = y * std::conj(prev);
            prev = y;
            const float disc = std::atan2(d.imag(), d.real());
            bitBuf.push_back(disc > 0.f ? 1 : 0);
            if (bitBuf.size() > 8192) bitBuf.erase(bitBuf.begin(), bitBuf.begin() + 4096);
        }
        rot /= std::abs(rot);
        LbjMessage m;
        if (lbj::tryDecodeFrame(bitBuf.data(), bitBuf.size(), m, tel.timeSec, centreHz)) pushMessage(std::move(m));
        else tel.state = tel.messages.empty() ? 1 : 2;
    }

    void maybePublish() {
        if (tel.timeSec < nextPub) return;
        nextPub = tel.timeSec + 0.25;
        tel.seq++;
    }
};

LbjReceiver::LbjReceiver() : p_(std::make_unique<Impl>()) {}
LbjReceiver::~LbjReceiver() = default;

void LbjReceiver::configure(double inputRateHz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->fs = inputRateHz;
    p_->rateSet = inputRateHz >= lbjTuning().minSampleRate - 1;
    p_->retune();
    p_->tel.inputRate = inputRateHz;
    p_->tel.state = p_->rateSet ? 1 : 0;
}
void LbjReceiver::setSignalOffset(double hz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->offHz = hz;
    p_->retune();
}
void LbjReceiver::setCenterHz(double hz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->centreHz = hz;
    p_->tel.centerHz = hz;
}
bool LbjReceiver::ready() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return p_->rateSet;
}
void LbjReceiver::reset() {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->rot = cf32(1.f, 0.f);
    p_->prev = cf32(1.f, 0.f);
    p_->bitBuf.clear();
    p_->samp = 0;
    p_->nextPub = 0;
    p_->tel = LbjTelemetry();
    p_->tel.inputRate = p_->fs;
    p_->tel.centerHz = p_->centreHz;
    p_->tel.state = p_->rateSet ? 1 : 0;
}
void LbjReceiver::clearMessages() {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->tel.messages.clear();
    p_->tel.dataValid = false;
    p_->tel.state = p_->rateSet ? 1 : 0;
    p_->tel.seq++;
}
void LbjReceiver::feed(const cf32* x, size_t n) {
    if (!x || !n) return;
    std::lock_guard<std::mutex> lk(p_->mu);
    if (!p_->rateSet) {
        p_->tel.state = 0;
        return;
    }
    p_->processBlock(x, n);
    p_->samp += n;
    p_->tel.timeSec = p_->fs > 0 ? (double)p_->samp / p_->fs : 0;
    p_->maybePublish();
}
bool LbjReceiver::telemetry(LbjTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->tel.seq <= lastSeq) return false;
    out = p_->tel;
    return true;
}
void LbjReceiver::clearResults() { clearMessages(); }
std::string LbjReceiver::saveResults() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    std::string s = "lbj1\n";
    for (const auto& m : p_->tel.messages) {
        s += m.train;
        s += '\t';
        s += m.kmMarker;
        s += '\t';
        s += m.loco;
        s += '\n';
    }
    return s;
}
bool LbjReceiver::loadResults(const std::string& blob) {
    if (blob.compare(0, 5, "lbj1\n") != 0) return false;
    std::vector<LbjMessage> msgs;
    size_t i = 5;
    while (i < blob.size() && msgs.size() < kLbjLogMax) {
        size_t e = blob.find('\n', i);
        if (e == std::string::npos) e = blob.size();
        const std::string line = blob.substr(i, e - i);
        size_t a = line.find('\t');
        size_t b = line.find('\t', a + 1);
        if (a != std::string::npos && b != std::string::npos) {
            LbjMessage m;
            m.train = line.substr(0, a);
            m.kmMarker = line.substr(a + 1, b - a - 1);
            m.loco = line.substr(b + 1);
            msgs.push_back(std::move(m));
        }
        i = e + 1;
    }
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->tel.messages = std::move(msgs);
    p_->tel.dataValid = !p_->tel.messages.empty();
    p_->tel.seq++;
    return true;
}
void LbjReceiver::setLogCallback(std::function<void(const std::string&)> cb) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->log = std::move(cb);
}
bool LbjReceiver::busy() const { return false; }

} // namespace dect2
