// Neural noise remover for speech (RNNoise, BSD-3-Clause; third_party/rnnoise). One instance per audio channel.
//
// The audio of a voice mode (8, 12, 16, 24 or 48 kHz: any rate that divides 48000) is brought to 48 kHz, cut into 10 ms frames (480
// samples) for RNNoise, and taken back to its own rate. process() works in place and returns as many samples as it was given; the
// result is delayed by latency() samples (the dry signal is delayed the same, so the blend stays aligned). Strength 0..1 blends the
// dry (0) and the cleaned (1) signal, since RNNoise has no strength control of its own. Everything is allocated in the constructor:
// process() never allocates. Off (the default), process() does not touch the samples and costs nothing.
// Thread safety: setEnabled() / setStrength() / vad() may be called from any thread; process() and reset() from one (the audio) thread.
// For speech only: music and data modes are damaged by it.
#pragma once
#include <atomic>
#include <cstddef>
#include <memory>

namespace dect2 {

class NeuralNr {
public:
    explicit NeuralNr(int sampleRateHz);
    ~NeuralNr();
    NeuralNr(const NeuralNr&) = delete;
    NeuralNr& operator=(const NeuralNr&) = delete;

    static bool rateSupported(int sampleRateHz);   // 48000 / rate is a whole number in 1..6
    bool supported() const { return ok_; }          // false: the rate is not supported, process() is a pass-through
    int sampleRate() const { return rate_; }

    void setEnabled(bool on) { on_.store(on, std::memory_order_relaxed); }
    bool enabled() const { return on_.load(std::memory_order_relaxed); }
    void setStrength(float s01);                    // 0 = dry, 1 = fully cleaned (clamped)
    float strength() const { return str_.load(std::memory_order_relaxed); }
    float vad() const { return vad_.load(std::memory_order_relaxed); }   // speech probability of the last 10 ms frame, 0..1

    // Samples (at the instance's rate) the output lags the input by while on.
    int latency() const { return lat_; }
    void reset();                                   // forget the audio held (audio thread); done on its own when switched on
    void process(float* x, size_t n);               // in place, +/-1 full scale

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    int rate_ = 0;
    int lat_ = 0;
    bool ok_ = false;
    std::atomic<bool> on_{false};
    std::atomic<float> str_{1.f};
    std::atomic<float> vad_{0.f};
};

} // namespace dect2
