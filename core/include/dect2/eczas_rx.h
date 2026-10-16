// e-Czas PCSK225 receiver: frequency-discriminator PCM (int16) at any integer multiple of 500 Hz,
// or 500 Hz directly (10 samples per bit, 50 bit/s). Ported from sp5wwp/e-Czas main.c.
#pragma once
#include "dect2/eczas_proto.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace dect2::eczas {

constexpr double kDiscRateHz = 500.0; // samples/s expected by the bit slicer (10 per bit @ 50 baud)

struct EczasRxPacket {
    TimePacket time;
    uint8_t frame[kFrameBytes]{};
};

// Streaming decoder: configure() with the PCM sample rate, then feed() int16 discriminator samples.
class EczasReceiver {
public:
    EczasReceiver();
    ~EczasReceiver();
    EczasReceiver(const EczasReceiver&) = delete;
    EczasReceiver& operator=(const EczasReceiver&) = delete;

    void reset();
    void configure(double pcmRateHz, int decimPhase = 0);
    void setPeakHint(int32_t peakAbsSample);
    void feed(const int16_t* x, size_t n);
    bool pop(EczasRxPacket& out);

private:
    double pcmRate_ = kDiscRateHz;
    int decim_ = 1;
    int decimPhase_ = 0;
    uint64_t inCount_ = 0;
    struct Impl;
    std::unique_ptr<Impl> demod_;
    std::vector<EczasRxPacket> queue_;
};

// Decode all time packets found in a buffer (useful for tests and offline files).
std::vector<EczasRxPacket> decodeDiscPcm(const int16_t* x, size_t n, double pcmRateHz, int decimPhase = 0);

// BFO recording (8 kHz / 48 kHz mono) to 500 Hz discriminator samples (for tests/tools).
std::vector<int16_t> eczasAudioToDisc(const int16_t* pcm, size_t n, double pcmRateHz);

// BFO recording (8 kHz / 48 kHz mono) or 500 Hz discriminator PCM.
std::vector<EczasRxPacket> eczasDecodeRecording(const int16_t* pcm, size_t n, double pcmRateHz);

} // namespace dect2::eczas
