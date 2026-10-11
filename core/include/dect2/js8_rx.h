// JS8Call receiver: decodes every JS8 signal in the audio passband of an upper sideband receiver (dial + 200 Hz .. dial + 3.2 kHz),
// normal (15 s slots), fast (10 s) and turbo (6 s) at once, and puts the frames together into messages.
//
// The channel is mixed to 0 Hz and brought to 24 kHz complex (marine::Front), then to 4 kHz complex centred on dial + 1750 Hz. The
// slots come from the system clock (UTC), as JS8Call's, unless a start time is given (a recording, the tests). At the end of each slot
// (and a second more for late starts) its samples go to a thread of its own: a spectrogram of symbol-long windows a quarter symbol apart,
// the Costas sync searched over the whole band and -1.5 .. +2.5 s of time offset (fast and turbo less), the best candidates refined in
// time and frequency by direct DFTs, soft decisions of the 3 bits of each 8-FSK symbol, belief propagation on the (174,87) code, CRC-12;
// a decoded signal is subtracted and the slot searched once more. js8_proto.h has the message layer and the sources.
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

struct Js8Telemetry;   // js8_tel.h

class Js8Receiver {
public:
    Js8Receiver();
    ~Js8Receiver();

    void configure(double inputRateHz);
    void setSignalOffset(double hz);
    bool ready() const;
    void reset();                        // after a retune: keeps the decodes, messages and stations
    void feed(const cf32* x, size_t n);
    bool telemetry(Js8Telemetry& out, uint64_t lastSeq);
    void setLogCallback(std::function<void(const std::string&)> cb);

    void setEnabled(int submode, bool on);   // normal, fast, turbo (all on by default)
    void setStartTime(double utc);       // the slot clock of the first sample (< 0: the system clock, the default)
    void clearDecodes();
    void waitIdle();                     // tests: until every slot handed to the decoder is done
    void flush();                        // tests: decode the slot in progress now (the end of a recording)
    void setSilent(bool) {}              // JS8 has no sound of its own

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning js8Tuning();

struct Js8Preset { const char* name; double mhz; };
const Js8Preset* js8Presets(int& count);   // the JS8Call dial frequencies (upper sideband)

} // namespace dect2
