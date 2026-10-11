// ISM sensors receiver: everyday 433.92 / 868 / 915 MHz devices (weather stations, tyre pressure sensors, remotes, energy and water
// meters), the rtl_433 idea.
//
// The whole sample band is watched: short FFTs (128 us) against a noise floor kept per bin find bursts; a bin that stays up (a continuous
// carrier) raises its own floor and is not taken for a burst. When a burst ends, every signal in it (bins grouped within 120 kHz, several
// signals when they lie apart) is mixed to 0 Hz, filtered to +-110 kHz and brought to about 500 kHz. There the level is set from the
// burst itself (the noise before it and the strongest part of it), and the burst is sliced into pulses: on / off for OOK, the upper /
// lower tone for FSK (a burst whose carrier stays up and whose frequency takes two values). The pulses go to the protocol layer
// (ism_proto.h); a burst no protocol accepts is kept as a raw pulse list with a guess of its line code (PWM, PPM, Manchester, NRZ).
//
// Written to the engine's contract: configure() once the sample rate is known, feed() from the analysis thread (never blocks),
// telemetry() and the setters from the interface thread.
#pragma once
#include "mode_tuning.h"
#include "ring.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dect2 {

struct IsmTelemetry;   // ism_tel.h
struct IsmReading;     // ism_proto.h

class IsmReceiver {
public:
    IsmReceiver();
    ~IsmReceiver();

    void configure(double inputRateHz);
    void setSignalOffset(double hz);   // where the user's (dial) frequency sits relative to 0 Hz in the input
    void setCenterHz(double hz);       // the radio's centre frequency (0: unknown, for files)
    bool ready() const;                // false when the input rate is too low for this mode
    void reset();                      // after a retune: forget the devices and the logs
    void clearDevices();               // the user's Clear button
    void feed(const cf32* x, size_t n);
    bool telemetry(IsmTelemetry& out, uint64_t lastSeq);
    void setLogCallback(std::function<void(const std::string&)> cb);
    void setThresholdDb(float db);     // burst detection over the floor, per bin, 8 .. 25 dB (default 13)
    void setReadingTap(std::function<void(const IsmReading&, double freqHz)> cb);   // tests: every reading (repeats too), from the receiver thread

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning ismTuning();

} // namespace dect2
