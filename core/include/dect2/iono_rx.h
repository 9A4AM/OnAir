// Ionospheric sounders (HF chirp ionosondes and oblique sounders, as the GNU Radio chirpsounder finds them): no decoder, a tool that
// detects the linear sweeps passing through the captured span, measures their rate, period and start offset, and builds an ionogram
// (group range against frequency) of each sounder from the echoes. The processing is in iono_dsp.h.
// Engine standard 54 (active 53). Written to the engine's contract: configure() once the sample rate is known, feed() from the analysis
// thread (never blocks), telemetry() and the setters from the interface thread.
#pragma once
#include "mode_tuning.h"
#include "ring.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dect2 {

struct IonoTelemetry;   // iono_tel.h

class IonoReceiver {
public:
    IonoReceiver();
    ~IonoReceiver();

    void configure(double inputRateHz);
    void setSignalOffset(double) {}
    void setCenterHz(double hz);         // the radio's frequency (0 .. 30 MHz is placed from it)
    void setTimeOrigin(double sec);      // the time of the first sample: UTC seconds for a radio, 0 for a file or the test signal
    bool ready() const;
    void reset();                        // forget the sounders, ionograms and coverage
    void feed(const cf32* x, size_t n);
    bool telemetry(IonoTelemetry& out, uint64_t lastSeq);
    void setLogCallback(std::function<void(const std::string&)> cb);
    void setSilent(bool) {}

    void setLocked(bool on);             // sweeps start on whole seconds (GPS-timed sounders); off: delays relative to the first echo
    void setSelected(int index);         // the sounder whose ionogram the telemetry carries (-1: the latest heard)

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning ionoTuning();
std::string ionoSummary(const IonoTelemetry& t);

} // namespace dect2
