// KAAL voice-layer receiver (streaming C++ port of the Python prototype kaal_rx.py): IQ in, 16 kHz mono speech out.
// Pipeline: DC removal, sweep (signature) removal per 0.5 s period, matched filter, symbol timing, preamble frame sync, per-frame complex
// gain fit on preamble + pilots with a per-frame noise estimate, max-log 16-QAM demap, pseudo-random deinterleave (4-frame blocks),
// LDPC decode, voice codec. The first 3 s of a stream are used to estimate the static parameters (sweep period, symbol clock, CFO), then the
// receiver runs frame by frame and tracks the sweep and the symbol clock. Decoding starts at the first interleaver block boundary.
#pragma once
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dect2::kaal {

struct RxStats {
    bool locked = false;
    uint64_t framesGood = 0, framesBad = 0;
    float snrDb = 0, cfoHz = 0, sweepDb = 0;
    uint64_t samplesIn = 0;
};

class VoiceRx {
public:
    VoiceRx();
    ~VoiceRx();
    VoiceRx(const VoiceRx&) = delete;
    VoiceRx& operator=(const VoiceRx&) = delete;

    void configure(double sampleRate);          // input sample rate; the receiver resamples to 48 kHz internally if it differs
    void reset();
    void feed(const std::complex<float>* x, size_t n);   // IQ in, any chunk size
    size_t popAudio(int16_t* out, size_t maxSamples);    // decoded 16 kHz mono speech, delay already removed
    RxStats stats() const;
    void setLogCallback(std::function<void(const std::string&)> cb);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace dect2::kaal
