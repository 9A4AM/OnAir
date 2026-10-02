// Engine: owns the source, the sample ring and the analysis thread. UI talks only to this.
#pragma once
#include "source.h"
#include "spectrum.h"
#include "t2rx.h"
#include "bbunpack.h"
#include "ts.h"
#include "tsout.h"
#include "player.h"
#include "teletext.h"
#include <atomic>
#include <functional>
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
    // Retune and flush every stage (receiver, spectrum, transport stream) - used by the channel scanner.
    bool retuneReset(const TuneSettings& tune);
    uint64_t resetCount() const { return resets_.load(); }
    bool running() const { return running_; }
    void setComputeMode(int m) { rx_.setComputeMode(m); }
    void selectPlp(int id) { rx_.selectPlp(id); }
    double sampleRate() const { return rate_; }
    uint64_t droppedSamples() const { return ring_.dropped(); }

    // Latest spectrum snapshot; returns true if newer than `lastSeq`.
    bool latestSpectrum(SpectrumFrame& out, uint64_t lastSeq);

    bool latestRx(RxTelemetry& out, uint64_t lastSeq);
    void setPlpDump(std::function<void(const PlpResult&)> cb) { plpDump_ = std::move(cb); }

    // Transport stream (phase 4)
    TsSnapshot tsSnapshot();
    BbStats bbStats();
    void setOutputs(const OutputConfig& c) { outputs_.configure(c); }
    OutputConfig outputConfig() const { return outputs_.config(); }
    OutputStats outputStats() const { return outputs_.stats(); }
    Player& player() { return player_; }
    TeletextDecoder& teletext() { return ttx_; }

    // Log
    void log(const std::string& line);
    std::vector<std::string> logSnapshot(size_t& total);
    void clearLog();

private:
    void analysisLoop();
    void logRxEvents(const RxTelemetry& t);
    std::function<void(const PlpResult&)> plpDump_;
    void onPlp(const PlpResult& r);
    std::mutex tsMu_;
    BbUnpacker unpack_;
    TsDemux demux_;
    TsSnapshot tsSnap_;
    OutputManager outputs_;
    Player player_;
    TeletextDecoder ttx_;
    int lastT2Frame_ = -1;
    std::atomic<bool> resetReq_{false};
    std::atomic<uint64_t> resets_{0};
    void applyReset();
    std::vector<uint8_t> frameBuf_;
    double tSpec_ = 0, tRx_ = 0;
    double nSamp_ = 0;
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
