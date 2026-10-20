// Satellite beacon receiver for the engine: IQ in, carrier measurements and telemetry frames out.
// Finds the narrow carrier (and the other narrow lines) in the capture, locks to the strongest, measures its level, offset and drift, follows its phase
// modulation, finds the telemetry tones, recovers the bits and learns the frame structure from the repetition. Optionally logs the carrier
// measurements and the frames to a CSV file.
//
// Written to the engine's contract: configure() once the sample rate is known, feed() from the analysis thread (never blocks),
// telemetry() and the setters from the interface thread.
#pragma once
#include "dect2/engine/mode_tuning.h"
#include "dect2/common/ring.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dect2 {

struct SbTelemetry;   // satbeacon_tel.h

class SatbeaconLiveReceiver {
public:
    SatbeaconLiveReceiver();
    ~SatbeaconLiveReceiver();
    void configure(double inputRateHz);
    void setSignalOffset(double hz);            // not used: the carrier is searched in the whole capture
    void setCenterHz(double hz);                // the radio's centre, for the CSV and the screen
    bool ready() const;
    void reset();                               // the whole chain: carrier, subcarrier, bit clock, learned frames
    void setLogFile(const std::string& path);   // append one CSV line per second and every frame ("" off)
    void feed(const cf32* x, size_t n);
    bool telemetry(SbTelemetry& out, uint64_t lastSeq);
    void setLogCallback(std::function<void(const std::string&)> cb);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning satbeaconTuning();

} // namespace dect2
