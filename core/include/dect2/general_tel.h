// General receiver telemetry, shared by the receiver and the UI.
// The first members are copied into RxTelemetry (history plot, top bar): keep them and their meaning.
#pragma once
#include <cstdint>
#include <string>

namespace dect2 {

struct GeneralTelemetry {
    uint64_t seq = 0;
    int state = 0;                     // 0 no input (or the channel outside the band), 1 squelch closed, 2 squelch open
    double cfoHz = 0;                  // FM: the mean of the discriminator (how far the station is from the channel); else 0
    float snrDb = 0;                   // channel power over the noise in the channel (noise from the band beside it)
    bool dataValid = false;            // the noise estimate is in (snrDb means something)
    uint64_t blocksOk = 0;             // 10 ms blocks with the squelch open
    uint64_t blocksBad = 0;            // and closed

    int demod = 0;                     // GeneralDemod
    double rate = 0;                   // input sample rate
    double demodRate = 0;              // where the channel is demodulated (48 kHz, WFM 240 kHz)
    double chanOffsetHz = 0;           // the channel relative to the dial frequency
    double filterLoHz = 0, filterHiHz = 0;   // the channel filter, relative to the channel frequency
    float levelDb = -200;              // channel power, dBFS (after the channel filter)
    float noiseDb = -200;              // noise in the channel, dBFS (-200: not measured yet)
    float audioDb = -200;              // the audio that plays (after the squelch, before the volume), dBFS rms
    float squelchDb = 0;               // the threshold now in force, dBFS (auto: noise + margin)
    bool squelchOpen = false;
    bool recording = false;
    double recordSec = 0;
    std::string recordPath;            // the WAV being written (or the last one)
    std::string recordError;           // why recording could not start
};

} // namespace dect2
