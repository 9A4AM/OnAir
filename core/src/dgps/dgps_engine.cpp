// DGPS beacons in the engine: mixes the beacon to 0 Hz, decimates to about 4.8 kHz, feeds dgps::Receiver one second at a time.
#include "dect2/dgps/dgps_engine.h"
#include <cmath>
#include <cstdio>
#include <mutex>

namespace dect2 {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTargetFs = 4800.0;     // the decoder's working rate (12 samples per bit at 200 bit/s is 2400; 4800 keeps some slack)
}

struct DgpsReceiver::Impl {
    mutable std::mutex mu;
    dgps::Receiver rx;
    double rate = 0;          // input sample rate
    int decim = 1;            // input samples averaged into one working sample
    double fsEff = 0;         // working sample rate after decimation
    double offsetHz = 0;      // beacon position in the input (0 Hz is the dial)
    double centerHz = 0;
    double phase = 0;         // mixer phase, radians
    std::complex<float> acc{0, 0};
    int accN = 0;
    std::vector<std::complex<float>> block;   // one second of working samples
    uint64_t seq = 0;
    uint64_t framesSeen = 0;
    DgpsTelemetry snap;
    std::function<void(const std::string&)> log;

    void emit(const std::string& s) { if (log) log(s); }

    void clearState() {
        rx.reset();
        block.clear();
        acc = {0, 0};
        accN = 0;
        phase = 0;
        framesSeen = 0;
        snap = DgpsTelemetry();
        snap.seq = seq;
        snap.inputRate = rate;
        snap.centerHz = centerHz;
    }

    void processBlock() {
        rx.process(block.data(), block.size(), fsEff);
        block.clear();
        const dgps::Stats& st = rx.stats();
        snap.cfoHz = st.carrierOffsetHz;
        snap.levelDb = (float)st.levelDb;
        snap.blocksOk = (uint64_t)st.framesOk;
        snap.dataValid = st.framesOk > 0;
        snap.state = st.framesOk > 0 ? 2 : 1;
        snap.typesSeen.assign(st.typesSeen.begin(), st.typesSeen.end());
        if ((uint64_t)st.framesOk != framesSeen) {
            framesSeen = (uint64_t)st.framesOk;
            snap.stationId = st.last.stationId;
            snap.lastType = st.last.type;
            snap.zcount = st.last.zcount;
            snap.health = st.last.health;
            char b[120];
            snprintf(b, sizeof b, "DGPS: frame %llu, station %d, type %d", (unsigned long long)framesSeen, st.last.stationId, st.last.type);
            emit(b);
        }
        snap.seq = ++seq;
    }
};

DgpsReceiver::DgpsReceiver() : p_(new Impl) {}
DgpsReceiver::~DgpsReceiver() = default;

void DgpsReceiver::configure(double inputRateHz) {
    std::lock_guard<std::mutex> l(p_->mu);
    p_->rate = inputRateHz;
    p_->decim = inputRateHz > kTargetFs ? (int)std::lround(inputRateHz / kTargetFs) : 1;
    if (p_->decim < 1) p_->decim = 1;
    p_->fsEff = inputRateHz / p_->decim;
    p_->clearState();
}

void DgpsReceiver::setSignalOffset(double hz) {
    std::lock_guard<std::mutex> l(p_->mu);
    p_->offsetHz = hz;
}

void DgpsReceiver::setCenterHz(double hz) {
    std::lock_guard<std::mutex> l(p_->mu);
    p_->centerHz = hz;
    p_->snap.centerHz = hz;
}

bool DgpsReceiver::ready() const {
    std::lock_guard<std::mutex> l(p_->mu);
    return p_->rate >= dgpsTuning().minSampleRate && p_->fsEff > 0;
}

void DgpsReceiver::reset() {
    std::lock_guard<std::mutex> l(p_->mu);
    p_->clearState();
}

void DgpsReceiver::feed(const std::complex<float>* x, size_t n) {
    std::lock_guard<std::mutex> l(p_->mu);
    if (p_->fsEff <= 0) return;
    const double step = -2.0 * kPi * p_->offsetHz / p_->rate;   // mixes the beacon at offsetHz down to 0 Hz
    const size_t want = (size_t)std::llround(p_->fsEff);        // one second per block
    for (size_t i = 0; i < n; i++) {
        const std::complex<float> m = x[i] * std::complex<float>((float)std::cos(p_->phase), (float)std::sin(p_->phase));
        p_->phase += step;
        if (p_->phase > kPi) p_->phase -= 2 * kPi;
        if (p_->phase < -kPi) p_->phase += 2 * kPi;
        p_->acc += m;
        if (++p_->accN == p_->decim) {
            p_->block.push_back(p_->acc / (float)p_->decim);
            p_->acc = {0, 0};
            p_->accN = 0;
            if (p_->block.size() >= want) p_->processBlock();
        }
    }
}

bool DgpsReceiver::telemetry(DgpsTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> l(p_->mu);
    if (p_->snap.seq == 0 || p_->snap.seq == lastSeq) return false;
    out = p_->snap;
    out.inputRate = p_->rate;
    out.centerHz = p_->centerHz;
    return true;
}

void DgpsReceiver::setLogCallback(std::function<void(const std::string&)> cb) {
    std::lock_guard<std::mutex> l(p_->mu);
    p_->log = std::move(cb);
}

ModeTuning dgpsTuning() {
    ModeTuning t;
    t.stdMode = 122;
    t.id = "dgps";
    t.name = "DGPS beacon (WIP)";
    t.minMhz = 0.2835;
    t.maxMhz = 0.325;
    t.defMhz = 0.300;
    t.sampleRate = 960000;
    t.basebandHz = 0;
    t.bandwidthMhz = 0.02;
    t.minSampleRate = 48000;
    t.tuneOffsetHz = 50000;     // the radio sits 50 kHz above the beacon, the receiver mixes it out
    return t;
}

std::string dgpsSummary(const DgpsTelemetry& t) {
    char b[160];
    if (t.state == 0) return "DGPS: waiting for samples";
    if (!t.dataValid) {
        snprintf(b, sizeof b, "DGPS: listening (%.0f dBFS)", t.levelDb);
        return b;
    }
    snprintf(b, sizeof b, "DGPS: station %d, type %d, %llu frames", t.stationId, t.lastType, (unsigned long long)t.blocksOk);
    return b;
}

}  // namespace dect2
