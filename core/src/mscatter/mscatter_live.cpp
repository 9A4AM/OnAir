// Meteor scatter detector for the engine (see mscatter_live.h).
#include "dect2/mscatter/mscatter_live.h"
#include "dect2/mscatter/mscatter_dsp.h"
#include "dect2/mscatter/mscatter_tel.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace dect2 {

struct MscatterLiveReceiver::Impl {
    std::mutex mu;
    std::function<void(const std::string&)> log;
    double rateSet = 0;
    std::atomic<double> centreReq{0}, thrReq{13};
    std::atomic<bool> resetReq{false}, clearReq{false}, logReq{false};
    std::atomic<int64_t> startReq{0};
    std::string logPath, logPathReq;
    ms::Front front;
    ms::Detector det;
    ms::Counts counts;
    std::vector<MsPing> pings;        // newest first
    std::vector<std::complex<float>> work;
    std::vector<cf32> clean;
    uint32_t nextId = 1;
    int64_t baseUtc = 0;
    uint64_t nIn = 0, nextPub = 0, okN = 0;
    double sessionSec = 0;
    bool dirty = true;
    MsTelemetry pub;

    static int64_t nowUtc() { return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

    void resetState() {
        front.configure(rateSet > 0 ? rateSet : 2e6);
        det.configure(front.workRate());
        det.setThresholdDb(thrReq);
        const int64_t s = startReq.load();
        baseUtc = s ? s : nowUtc();
        nIn = 0; nextPub = 0;
        dirty = true;
    }

    void addPing(const ms::RawPing& r) {
        MsPing p;
        p.id = nextId++;
        p.wallTime = baseUtc + (int64_t)std::floor(r.startSec);
        p.durationSec = (float)r.durSec; p.peakSnrDb = (float)r.peakSnrDb; p.dopplerHz = (float)r.dopplerHz;
        p.driftHzPerSec = (float)r.driftHzPerSec; p.kind = r.kind; p.profile = r.profile;
        counts.add(p.wallTime);
        okN++;
        pings.insert(pings.begin(), p);
        if (pings.size() > kMsPingMax) pings.pop_back();
        const double bhz = pub.centreHz > 0 ? pub.centreHz + det.beaconHz() : 0.0;
        if (!logPath.empty()) {
            FILE* f = fopen(logPath.c_str(), "ab");
            if (f) {
                fseek(f, 0, SEEK_END);
                if (ftell(f) == 0) { const std::string h = ms::csvHeader(); fwrite(h.data(), 1, h.size(), f); }
                const std::string l = ms::csvLine(p, bhz);
                fwrite(l.data(), 1, l.size(), f);
                fclose(f);
            }
        }
        if (log) {
            char b[160];
            snprintf(b, sizeof b, "Meteor scatter: ping %.1f s, %.0f dB, %+.0f Hz (%s)", (double)p.durationSec, (double)p.peakSnrDb, (double)p.dopplerHz, p.kind ? "overdense" : "underdense");
            log(b);
        }
        dirty = true;
    }

    void publish() {
        const int64_t now = baseUtc + (int64_t)std::floor(det.timeSec());
        pub.seq++;
        pub.state = okN ? 2 : (det.beaconFound() ? 1 : 0);
        pub.cfoHz = det.beaconHz();
        pub.snrDb = (float)det.beaconSnrDb();
        pub.levelDb = (float)det.beaconDbfs();
        pub.dataValid = det.beaconFound();
        pub.blocksOk = okN; pub.blocksBad = det.rejected();
        pub.rejected = det.rejected();
        pub.timeSec = det.timeSec(); pub.inputRate = rateSet; pub.workRate = front.workRate(); pub.binHz = det.binHz();
        pub.noiseDb = (float)det.noiseDbfs();
        pub.thresholdDb = (float)det.thresholdDb();
        pub.active = det.active(); pub.activeSnrDb = (float)det.activeSnr();
        pub.pings = pings;
        counts.fill(now, pub);
        int last = 0;
        for (const auto& p : pings) { if (p.wallTime > now - 3600) last++; else break; }
        pub.ratePerHour = (float)(last * 3600.0 / std::max(300.0, std::min(3600.0, det.timeSec())));
        msShowersFill(now);
        det.waterfall(pub.waterfall, pub.spectrum);
        pub.logging = !logPath.empty(); pub.logPath = logPath;
        dirty = false;
    }
    void msShowersFill(int64_t now) { ms::msShowers(now, pub.showers); }
};

MscatterLiveReceiver::MscatterLiveReceiver() : p_(std::make_unique<Impl>()) {}
MscatterLiveReceiver::~MscatterLiveReceiver() = default;

void MscatterLiveReceiver::configure(double inputRateHz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->rateSet = inputRateHz;
    p_->resetReq = true;
}
void MscatterLiveReceiver::setSignalOffset(double) {}
void MscatterLiveReceiver::setCenterHz(double hz) { p_->centreReq = hz; std::lock_guard<std::mutex> lk(p_->mu); p_->pub.centreHz = hz; }
bool MscatterLiveReceiver::ready() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return p_->rateSet >= mscatterTuning().minSampleRate - 1;
}
void MscatterLiveReceiver::reset() { p_->resetReq = true; }
void MscatterLiveReceiver::clearResults() { p_->clearReq = true; }
void MscatterLiveReceiver::setThresholdDb(double db) { p_->thrReq = std::max(6.0, std::min(30.0, db)); p_->resetReq = true; }
void MscatterLiveReceiver::setLogFile(const std::string& path) { std::lock_guard<std::mutex> lk(p_->mu); p_->logPathReq = path; p_->logReq = true; }
void MscatterLiveReceiver::setStartUtc(int64_t utcSec) { p_->startReq = utcSec; p_->resetReq = true; }
void MscatterLiveReceiver::setLogCallback(std::function<void(const std::string&)> cb) { std::lock_guard<std::mutex> lk(p_->mu); p_->log = std::move(cb); }

bool MscatterLiveReceiver::telemetry(MsTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->pub.seq <= lastSeq) return false;
    out = p_->pub;
    return true;
}

void MscatterLiveReceiver::feed(const cf32* x, size_t n) {
    Impl& m = *p_;
    if (m.logReq.exchange(false)) { std::lock_guard<std::mutex> lk(m.mu); m.logPath = m.logPathReq; m.dirty = true; }
    if (m.resetReq.exchange(false)) {
        std::lock_guard<std::mutex> lk(m.mu);
        m.resetState();
        m.publish();
    }
    if (m.clearReq.exchange(false)) {
        std::lock_guard<std::mutex> lk(m.mu);
        m.pings.clear(); m.counts.clear(); m.okN = 0; m.nextId = 1;
        m.dirty = true;
    }
    if (n == 0 || m.rateSet < mscatterTuning().minSampleRate - 1) return;
    m.work.resize(n / (size_t)std::max(1, m.front.decim()) + 2);
    const cf32* src = x;
    for (size_t i = 0; i < n; i++) {   // a NaN or infinity must not poison the filters
        if (!std::isfinite(x[i].real()) || !std::isfinite(x[i].imag())) {
            m.clean.assign(x, x + n);
            for (auto& v : m.clean) if (!std::isfinite(v.real()) || !std::isfinite(v.imag())) v = cf32(0, 0);
            src = m.clean.data();
            break;
        }
    }
    m.nIn += n;
    const size_t k = m.front.process(src, n, m.work.data());
    m.det.push(m.work.data(), k);
    std::vector<ms::RawPing> got = m.det.take();
    std::lock_guard<std::mutex> lk(m.mu);
    for (const auto& r : got) m.addPing(r);
    if (m.dirty || m.nIn >= m.nextPub) {
        m.nextPub = m.nIn + (uint64_t)(0.25 * m.rateSet);
        m.publish();
    }
}

ModeTuning mscatterTuning() {
    ModeTuning t;
    t.stdMode = 106; t.id = "mscatter"; t.name = "Meteor scatter";
    t.minMhz = 30; t.maxMhz = 300; t.defMhz = 143.050;
    t.sampleRate = 2e6;
    t.basebandHz = 0;
    t.bandwidthMhz = 1.75;
    t.minSampleRate = 24e3;
    t.tuneOffsetHz = 0;
    t.carrierAtCentre = true;     // the beacon is received at the centre: the DC removal would take it with the spike
    return t;
}

} // namespace dect2
