#include "dect2/engine.h"
#include <chrono>
#include <cstdio>

namespace dect2 {

Engine::Engine() : t0_(std::chrono::steady_clock::now()) {}
Engine::~Engine() { stop(); }

void Engine::log(const std::string& line) {
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
    char buf[32];
    snprintf(buf, sizeof buf, "%7.2fs ", t);
    std::lock_guard<std::mutex> lk(logMu_);
    log_.push_back(buf + line);
    if (log_.size() > 5000) log_.erase(log_.begin(), log_.begin() + 1000);
}

std::vector<std::string> Engine::logSnapshot(size_t& total) {
    std::lock_guard<std::mutex> lk(logMu_);
    total = log_.size();
    return log_;
}

void Engine::clearLog() {
    std::lock_guard<std::mutex> lk(logMu_);
    log_.clear();
}

bool Engine::start(const DeviceInfo& dev, const TuneSettings& tune, const FileOptions& file) {
    stop();
    ring_.clear();
    std::string err;
    if (dev.kind == DeviceInfo::File)
        src_ = makeFileSource(file.path, file.format, file.sampleRate, file.loop);
    else
        src_ = makeSource(dev);
    if (!src_) { log("no source"); return false; }
    if (!src_->start(tune, ring_, err)) {
        log("source start failed: " + err);
        src_.reset();
        return false;
    }
    rate_ = src_->sampleRate();
    logP1Count_ = 0; logGi_ = -2; logState_ = -1; logFrameSyms_ = 0;
    rx_.configure(rate_, tune.bandwidthMhz);
    rx_.setPlpCallback([this](const PlpResult& r) { if (plpDump_) plpDump_(r); });
    {
        RxTelemetry t;
        if (!rx_.telemetry(t, 0)) {}
        if (t.seq && !t.rateOk) log("sample rate is not 1x or 2x the native rate for this bandwidth - only spectrum available");
    }
    char b[160];
    snprintf(b, sizeof b, "source started: %s  fs=%.4f Msps", dev.name.c_str(), rate_.load() / 1e6);
    log(b);
    if (dev.kind == DeviceInfo::HackRF) {
        snprintf(b, sizeof b, "tuned %.3f MHz  LNA %d dB  VGA %d dB  amp %s", tune.centerHz / 1e6, tune.lnaDb, tune.vgaDb, tune.ampOn ? "on" : "off");
        log(b);
    }
    tSpec_ = tRx_ = 0; nSamp_ = 0;
    stopReq_ = false;
    running_ = true;
    th_ = std::thread([this] { analysisLoop(); });
    return true;
}

void Engine::stop() {
    if (!running_ && !src_) return;
    stopReq_ = true;
    if (th_.joinable()) th_.join();
    if (src_) {
        src_->stop(); src_.reset();
        char b[160];
        double dur = nSamp_ / std::max(1.0, rate_.load());
        snprintf(b, sizeof b, "source stopped (CPU load: spectrum %.0f%%, receiver %.0f%% of real time)", 100 * tSpec_ / std::max(1e-9, dur), 100 * tRx_ / std::max(1e-9, dur));
        log(b);
    }
    running_ = false;
}

bool Engine::retune(const TuneSettings& tune) {
    if (!src_) return false;
    std::string err;
    if (!src_->retune(tune, err)) { log("retune failed: " + err); return false; }
    return true;
}

bool Engine::latestRx(RxTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(rxMu_);
    if (rxTel_.seq <= lastSeq) return false;
    out = rxTel_;
    return true;
}

void Engine::logRxEvents(const RxTelemetry& t) {
    char b[200];
    if (t.p1Count != logP1Count_ && t.p1Count > 0) {
        bool first = logP1Count_ == 0 || t.p1.s2field1 != logS2_ || t.p1.s1 != logS1_;
        if (first) {
            snprintf(b, sizeof b, "P1 found: %s, S2 FFT %s%s, CFO %+.1f Hz, conf %.2f", s1Name(t.p1.s1),
                     fftModeFromS2(t.p1.s2field1) ? fftModeFromS2(t.p1.s2field1)->name : "?", t.p1.mixed ? " (mixed)" : "",
                     t.p1.cfoHz, t.p1.conf);
            log(b);
        }
        logP1Count_ = t.p1Count;
        logS2_ = t.p1.s2field1;
        logS1_ = t.p1.s1;
    }
    if (t.giIdx != logGi_) {
        if (t.giIdx >= 0) {
            snprintf(b, sizeof b, "guard interval %s detected (margin %.2fx), %d symbols locked", guardName(t.giIdx), t.giMargin, (int)t.symbols);
            log(b);
        }
        logGi_ = t.giIdx;
    }
    if (t.state != logState_) {
        static const char* nm[] = {"searching for P1", "waiting for guard interval", "locked"};
        log(std::string("sync: ") + nm[t.state]);
        logState_ = t.state;
    }
    if (t.symbolsPerFrame != logFrameSyms_ && t.symbolsPerFrame > 0) {
        snprintf(b, sizeof b, "frame length %.2f ms = %d symbols after P1, SRO %+.1f ppm", t.frameMs, t.symbolsPerFrame, t.sroPpm);
        log(b);
        logFrameSyms_ = t.symbolsPerFrame;
    }
}

bool Engine::latestSpectrum(SpectrumFrame& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(specMu_);
    if (spec_.seq <= lastSeq || spec_.dbfs.empty()) return false;
    out = spec_;
    return true;
}

void Engine::analysisLoop() {
    std::vector<cf32> buf(1 << 16);
    auto next = std::chrono::steady_clock::now();
    SpectrumFrame f;
    uint64_t rxSeq = 0;
    while (!stopReq_) {
        size_t n;
        while ((n = ring_.read(buf.data(), buf.size())) > 0) { auto a0 = std::chrono::steady_clock::now(); analyzer_.feed(buf.data(), n); auto a1 = std::chrono::steady_clock::now(); rx_.feed(buf.data(), n); auto a2 = std::chrono::steady_clock::now(); tSpec_ += std::chrono::duration<double>(a1 - a0).count(); tRx_ += std::chrono::duration<double>(a2 - a1).count(); nSamp_ += n; }
        next += std::chrono::milliseconds(33);
        {
            RxTelemetry t;
            if (rx_.telemetry(t, rxSeq)) {
                rxSeq = t.seq;
                logRxEvents(t);
                std::lock_guard<std::mutex> lk(rxMu_);
                rxTel_ = std::move(t);
            }
        }
        if (analyzer_.takeFrame(f)) {
            std::lock_guard<std::mutex> lk(specMu_);
            spec_ = f;
        }
        // keep reading while we wait so the ring never fills
        while (std::chrono::steady_clock::now() < next && !stopReq_) {
            size_t m = ring_.read(buf.data(), buf.size());
            if (m) { auto a0 = std::chrono::steady_clock::now(); analyzer_.feed(buf.data(), m); auto a1 = std::chrono::steady_clock::now(); rx_.feed(buf.data(), m); auto a2 = std::chrono::steady_clock::now(); tSpec_ += std::chrono::duration<double>(a1 - a0).count(); tRx_ += std::chrono::duration<double>(a2 - a1).count(); nSamp_ += m; }
            else std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

} // namespace dect2
