// What the ionospheric sounder mode reports (RxTelemetry::iono, standard 53).
#pragma once
#include <cstdint>
#include <vector>

namespace dect2 {

// A chirp sounder (ionosonde or oblique sounder) heard at least once: the sweep rate, and once two sweeps were seen the repetition period
// and the start offset in it (sweep start, the moment the sweep passes 0 MHz, modulo the period).
struct IonoSounder {
    int id = 0;
    double rateHz = 0;          // sweep rate, Hz/s (100e3 = 100 kHz/s)
    double periodSec = 0;       // 0 until two sweeps were seen
    double offsetSec = 0;       // lastT0 modulo the period (with a radio: seconds after the UTC epoch modulo the period)
    double lastT0 = 0;          // start of the last sweep (time base of IonoTelemetry::nowSec)
    double lastSeen = 0;        // when it was last in the captured span
    double nextT0 = 0;          // the next sweep start expected (0 = period unknown)
    double snrDb = 0;           // strength of the last sweep (chirp peak over the noise, per detector bin)
    double fLoHz = 0, fHiHz = 0;   // the part of 0 .. 30 MHz its sweeps were heard in
    int sweeps = 0;             // sweeps heard
    int missed = 0;             // expected sweeps that did not come
    bool active = false;        // a sweep of it is in the span now
};

// One sweep heard (the timeline)
struct IonoEvent {
    double t0 = 0;              // sweep start
    int sounder = 0;            // IonoSounder::id
    double rateHz = 0;
    double fLoHz = 0, fHiHz = 0;   // the part of the sweep that crossed the captured span
    double snrDb = 0;
    bool predicted = false;     // the sweep was expected from the period (its whole crossing went into the ionogram)
};

struct IonoTelemetry {
    uint64_t seq = 0;
    int state = 0;              // 0 waiting for samples, 1 searching
    double rate = 0;            // input sample rate, Hz
    double centerHz = 0;        // the radio's frequency
    double nowSec = 0;          // stream time (with a radio: UTC seconds, from the computer clock; a file or the test signal: from 0)
    bool locked = true;         // sweeps start on whole seconds (GPS-timed sounders): delays are absolute, else relative to the first echo
    double spanLoHz = 0, spanHiHz = 0;   // the usable captured span now
    std::vector<uint8_t> covered;        // kCols: the columns of 0 .. 30 MHz the radio has captured since the reset
    std::vector<IonoSounder> sounders;
    std::vector<IonoEvent> timeline;     // newest last
    int sel = -1;                        // index in sounders of the ionogram below (-1: none yet)
    std::vector<float> ionoDb;           // kRows x kCols (row = delay bin, row 0 = 0 ms), dB over each column's median; NAN where never swept
    std::vector<uint8_t> swept;          // kCols: columns of the selected sounder with data
    int activeSweeps = 0;
    uint64_t frames = 0, hits = 0;       // detector FFTs, chirp peaks found
    int fftSize = 0;                     // detector FFT
    double powerDb = -200;               // input power, dBFS
};

} // namespace dect2
