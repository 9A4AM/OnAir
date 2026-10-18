// COSPAS-SARSAT 406 MHz distress beacon receiver: burst finder (a narrow carrier that appears for half a second), carrier frequency estimate from the
// 160 ms unmodulated preamble, then 400 bit/s Manchester phase demodulation (+-1.1 rad) with sync search over timing, bit rate and polarity, and the
// message layer in sarsat_proto.h. Receive only.
//
// Written to the engine's contract: configure() once the sample rate is known, feed() from the analysis thread,
// telemetry() and the setters from the interface thread.
#pragma once
#include "dect2/engine/mode_tuning.h"
#include "dect2/common/ring.h"
#include "dect2/sarsat/sarsat_proto.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dect2 {

struct SarsatTelemetry;

namespace sarsat {
constexpr double kFs = 32000.0;   // the rate the burst processing works at
// The demodulator on its own: x holds a burst at kFs with its carrier at carrierHz; fills the message when a sync pattern and the first BCH check pass.
bool demodBurst(const std::vector<cf32>& x, double carrierHz, Beacon& out);
}

class SarsatReceiver {
public:
    SarsatReceiver();
    ~SarsatReceiver();

    void configure(double inputRateHz);
    void setSignalOffset(double hz);
    bool ready() const;
    void reset();
    void clearResults();
    void feed(const cf32* x, size_t n);
    bool telemetry(SarsatTelemetry& out, uint64_t lastSeq);
    void setLogCallback(std::function<void(const std::string&)> cb);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning sarsatTuning();

} // namespace dect2
