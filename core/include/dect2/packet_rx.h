// APRS and AX.25 packet radio on one 25 kHz FM channel (144.800 MHz in Europe): 1200 baud AFSK and 9600 baud G3RUH at the same time.
// Written to the engine's contract: configure() once the sample rate is known, feed() from the analysis thread (never blocks),
// telemetry() and the setters from the interface thread.
#pragma once
#include "mode_tuning.h"
#include "packet_tel.h"
#include "ring.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dect2 {

class PacketReceiver {
public:
    PacketReceiver();
    ~PacketReceiver();

    void configure(double inputRateHz);
    void setSignalOffset(double hz);                     // where the user's frequency sits relative to 0 Hz in the input
    bool ready() const;                                  // false when the input rate is too low for this mode
    void reset();                                        // after a retune: forget the signal (the frames and stations stay: clearResults)
    void feed(const cf32* x, size_t n);
    bool telemetry(PacketTelemetry& out, uint64_t lastSeq);
    // The frame list and the station table stay through reset(), configure() and retunes: only clearResults() empties them
    void clearResults();
    std::string saveResults() const;                     // for the results file (results_io.h)
    bool loadResults(const std::string& blob);           // false: not this build's format, nothing changes
    void setLogCallback(std::function<void(const std::string&)> cb);   // events for the log pane; may be called from the receiver thread

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning packetTuning();

} // namespace dect2
