// FM broadcast radio receiver (87.5 - 108 MHz): channel filter, discriminator, stereo decoder (19 kHz pilot) and RDS (57 kHz).
// The station must be in the centre of the input; any input rate from 500 kHz up works.
#pragma once
#include "fm_tel.h"
#include "ring.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace dect2 {

class FmReceiver {
public:
    FmReceiver();
    ~FmReceiver();

    void configure(double inputRateHz);
    bool ready() const;                                  // false when the input rate is below 500 kHz
    void reset();                                        // after a retune: forget the station, drop queued audio
    void feed(const cf32* x, size_t n);
    bool telemetry(FmTelemetry& out, uint64_t lastSeq);

    void setVolume(float v);                             // 0 .. 1
    void setMuted(bool m);
    void setMono(bool on);                               // force mono: the 19 kHz pilot and the stereo decoder are ignored (less noise on a weak station); off = stereo when the pilot is locked
    void setDeemphasis(double microseconds);             // 50 (Europe, Middle East, most of the world) or 75 (Americas, South Korea)
    // The channel filter's pass band, the whole width in Hz (0 = the standard 220 kHz); held to kMin/kMaxChannelWidthHz. Narrower keeps a
    // strong neighbour out at the cost of distortion on loud passages and of RDS; it takes effect with the next samples (any thread).
    static constexpr double kDefaultChannelWidthHz = 220e3, kMinChannelWidthHz = 100e3, kMaxChannelWidthHz = 300e3;
    void setChannelWidth(double hz);
    void setStationFinder(bool on);                      // look for a station off the centre and move to it (recordings); off for a live radio
    double channelWidth() const;
    void setSilent(bool s);                              // decode but do not open the sound device (tests, command line)
    // Tests: the demodulated audio (left, right) at 48 kHz, called from feed()
    void setAudioTap(std::function<void(const float* left, const float* right, size_t n)> cb);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace dect2
