// CubeSat telemetry receiver: amateur satellites on 2 m / 70 cm. One FM channel (the packet front end, packet_demod.h, with a wider filter
// and AFC for the Doppler left over), and every framing decoded at the same time (cubesat_proto.h):
//  - AX.25 at 1200 baud AFSK (the APRS demodulators) and at 9600 / 4800 baud G3RUH (descrambler, NRZI, HDLC);
//  - AX100 Mode 6: an HDLC frame of the G3RUH demodulators that fails the AX.25 FCS is tried as a Reed-Solomon block;
//  - AX100 Mode 5 and CCSDS: plain GMSK / FSK slicers at 9600 and 4800 baud hunt for the sync words (either polarity, two bit errors).
// Frames are named after the satellite table (callsign, the satellite the pass planner follows, the dial frequency with a matching framing) and can go out to the
// data feeds' KISS server (feeds.h).
//
// Written to the engine's contract: configure() once the sample rate is known, feed() from the analysis thread (never blocks),
// telemetry() and the setters from the interface thread.
#pragma once
#include "cubesat_proto.h"
#include "dect2/engine/mode_tuning.h"
#include "dect2/common/ring.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dect2 {

struct CubeTelemetry;   // cubesat_tel.h

class CubesatReceiver {
public:
    CubesatReceiver();
    ~CubesatReceiver();
    void configure(double inputRateHz);
    void setSignalOffset(double hz);
    void setDialHz(double hz);                              // the frequency tuned (0: unknown, a file): names frames from the table
    void setSatellites(const std::vector<cubesat::SatDef>& sats);
    void setTracked(int norad, const std::string& name);    // the satellite the pass planner follows (0: none)
    void setForwardKiss(bool on);                           // frames to the KISS feed while that feed is on (default on)
    void setCcsdsLength(int bytes);                         // the CCSDS code block length, 33 .. 255 (default 255)
    void setWallClock(bool on);                             // stamp frames with the computer clock (a radio; off for a file)
    bool ready() const;
    void reset();                                           // the demodulators; the frame log stays
    void clearFrames();
    void feed(const cf32* x, size_t n);
    bool telemetry(CubeTelemetry& out, uint64_t lastSeq);
    // The frame list stay through reset(), configure() and retunes: only clearResults() empties them
    void clearResults();
    std::string saveResults() const;                     // for the results file (results_io.h)
    bool loadResults(const std::string& blob);           // false: not this build's format, nothing changes
    void setLogCallback(std::function<void(const std::string&)> cb);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning cubesatTuning();

} // namespace dect2
