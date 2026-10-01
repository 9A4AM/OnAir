// Engine: owns the source, the sample ring and the analysis thread. UI talks only to this.
#pragma once
#include "source.h"
#include "spectrum.h"
#include "t2rx.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dect2 {

struct FileOptions {
    std::string path;
    FileFormat format = FileFormat::CS8;
    double sampleRate = 18285714.2857;
    bool loop = true;
};

class Engine {
public:
    Engine();
    ~Engine();

    bool start(const DeviceInfo& dev, const TuneSettings& tune, const FileOptions& file);
    void stop();
    bool retune(const TuneSettings& tune);
    bool running() const { return running_; }
    double sampleRate() const { return rate_; }
    uint64_t droppedSamples() const { return ring_.dropped(); }

    // Latest spectrum snapshot; returns true if newer than `lastSeq`.
    bool latestSpectrum(SpectrumFrame& out, uint64_t lastSeq);

    bool latestRx(RxTelemetry& out, uint64_t lastSeq);

    // Log
    void log(const std::string& line);
    std::vector<std::string> logSnapshot(size_t& total);
    void clearLog();

private:
    void analysisLoop();
    void logRxEvents(const RxTelemetry& t);
    uint64_t logP1Count_ = 0;
    int logS1_ = -1, logS2_ = -1, logGi_ = -2, logState_ = -1, logFrameSyms_ = 0;

    IqRing ring_;
    std::unique_ptr<IqSource> src_;
    SpectrumAnalyzer analyzer_{4096};
    T2Receiver rx_;
    std::mutex rxMu_;
    RxTelemetry rxTel_;
    std::thread th_;
    std::atomic<bool> running_{false}, stopReq_{false};
    std::atomic<double> rate_{0};

    std::mutex specMu_;
    SpectrumFrame spec_;

    std::mutex logMu_;
    std::vector<std::string> log_;
    std::chrono::steady_clock::time_point t0_;
};

} // namespace dect2
