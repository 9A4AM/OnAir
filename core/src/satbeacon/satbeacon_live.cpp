// Satellite beacon receiver for the engine (see satbeacon_live.h).
#include "dect2/satbeacon/satbeacon_live.h"
#include "dect2/satbeacon/satbeacon_dsp.h"
#include "dect2/satbeacon/satbeacon_tel.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace dect2 {

struct SatbeaconLiveReceiver::Impl {
    std::mutex mu;
    std::function<void(const std::string&)> log;
    double rateSet = 0;
    std::atomic<bool> resetReq{false}, logReq{false};
    std::string logPath, logPathReq;
    sb::Beacon beacon;
    std::vector<cf32> clean;
    uint64_t nIn = 0, nextPub = 0;
    double centreHz = 0, lastLogSec = -10;
    uint32_t lastFrameId = 0;
    int lastState = -1;
    SbTelemetry pub;

    void publish() {
        pub.seq++;
        beacon.fill(pub);
        pub.centreHz = centreHz;
        beacon.analyzer().fill(pub, centreHz > 0 ? centreHz + pub.cfoHz : 0.0);
        pub.logging = false;
        if (pub.state != lastState) {
            lastState = pub.state;
            static const char* names[] = {"no carrier", "carrier locked", "telemetry clock locked", "frames found"};
            if (log) { char b[160]; snprintf(b, sizeof b, "TV sat beacon decoder: %s", names[std::max(0, std::min(3, pub.state))]); log(b); }
        }
        if (!logPath.empty()) writeLog();
    }

    void writeLog() {
        FILE* f = fopen(logPath.c_str(), "ab");
        if (!f) return;
        fseek(f, 0, SEEK_END);
        if (ftell(f) == 0) fputs("kind,time_s,freq_hz,level_dbfs,cn0_dbhz,drift_hz_per_min,baud,frame_bits,errors,bytes_hex\n", f);
        if (pub.timeSec - lastLogSec >= 1.0 && pub.state > 0) {
            lastLogSec = pub.timeSec;
            fprintf(f, "carrier,%.1f,%.2f,%.1f,%.1f,%.2f,%.2f,%d,,\n", pub.timeSec, centreHz + pub.cfoHz, (double)pub.levelDb, (double)pub.snrDb, pub.driftHzPerMin, pub.baud, pub.frameBits);
        }
        for (auto it = pub.frames.rbegin(); it != pub.frames.rend() && pub.state >= 3; ++it) {
            if (it->id <= lastFrameId) continue;
            lastFrameId = it->id;
            fprintf(f, "frame,%.1f,,,,,%.2f,%d,%d,", pub.timeSec, pub.baud, pub.frameBits, it->fixedErrors);
            for (uint8_t b : it->bytes) fprintf(f, "%02X", b);
            fputc('\n', f);
        }
        fclose(f);
    }
};

SatbeaconLiveReceiver::SatbeaconLiveReceiver() : p_(std::make_unique<Impl>()) {}
SatbeaconLiveReceiver::~SatbeaconLiveReceiver() = default;

void SatbeaconLiveReceiver::configure(double inputRateHz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->rateSet = inputRateHz;
    p_->resetReq = true;
}
void SatbeaconLiveReceiver::setSignalOffset(double) {}
void SatbeaconLiveReceiver::setCenterHz(double hz) { std::lock_guard<std::mutex> lk(p_->mu); p_->centreHz = hz; }
bool SatbeaconLiveReceiver::ready() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return p_->rateSet >= satbeaconTuning().minSampleRate - 1;
}
void SatbeaconLiveReceiver::reset() { p_->resetReq = true; }
void SatbeaconLiveReceiver::setLogFile(const std::string& path) { std::lock_guard<std::mutex> lk(p_->mu); p_->logPathReq = path; p_->logReq = true; }
void SatbeaconLiveReceiver::setLogCallback(std::function<void(const std::string&)> cb) { std::lock_guard<std::mutex> lk(p_->mu); p_->log = std::move(cb); }

bool SatbeaconLiveReceiver::telemetry(SbTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->pub.seq <= lastSeq) return false;
    out = p_->pub;
    out.logging = !p_->logPath.empty();
    out.logPath = p_->logPath;
    return true;
}

void SatbeaconLiveReceiver::feed(const cf32* x, size_t n) {
    Impl& m = *p_;
    if (m.logReq.exchange(false)) { std::lock_guard<std::mutex> lk(m.mu); m.logPath = m.logPathReq; }
    if (m.resetReq.exchange(false)) {
        std::lock_guard<std::mutex> lk(m.mu);
        m.beacon.configure(m.rateSet > 0 ? m.rateSet : 2e6);
        m.nIn = 0; m.nextPub = 0; m.lastFrameId = 0; m.lastLogSec = -10; m.lastState = -1;
        m.publish();
    }
    if (n == 0 || m.rateSet < satbeaconTuning().minSampleRate - 1) return;
    const cf32* src = x;
    for (size_t i = 0; i < n; i++) {   // a NaN or infinity must not poison the loops
        if (!std::isfinite(x[i].real()) || !std::isfinite(x[i].imag())) {
            m.clean.assign(x, x + n);
            for (auto& v : m.clean) if (!std::isfinite(v.real()) || !std::isfinite(v.imag())) v = cf32(0, 0);
            src = m.clean.data();
            break;
        }
    }
    m.beacon.process(src, n);
    m.nIn += n;
    if (m.nIn >= m.nextPub) {
        m.nextPub = m.nIn + (uint64_t)(0.25 * m.rateSet);
        std::lock_guard<std::mutex> lk(m.mu);
        m.publish();
    }
}

ModeTuning satbeaconTuning() {
    ModeTuning t;
    t.stdMode = 109; t.id = "satbeacon"; t.name = "TV sat beacon decoder";
    t.minMhz = 1000; t.maxMhz = 20000; t.defMhz = 11705.250;
    t.sampleRate = 2e6;
    t.basebandHz = 0;
    t.bandwidthMhz = 1.75;
    t.minSampleRate = 250e3;
    t.tuneOffsetHz = 0;
    t.carrierAtCentre = false;
    return t;
}

} // namespace dect2
