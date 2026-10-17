// Sound-card input for narrowband IF and audio-frequency signals (issue #137): a receiver's 12 kHz IF, SSB audio, an AF tap.
// The sound card gives real samples; decoders want complex IQ with the signal where the dial says. audioIfProcess() does the steps in order:
// gain, DC removal, channel choice, real -> analytic (Hilbert), shift the IF down to 0 Hz, optional sideband inversion.
#pragma once
#include "dect2/source/source.h"
#include <string>

namespace dect2 {

// TuneSettings::radio keys: audio_iq (AudioIqMode 0..3; 3 = Real (mono): analytic, like USB), audio_chan (0 left, 1 right, 2 mix), audio_if_hz (the carrier/centre's audio
// frequency, Hz), audio_invert (0/1), audio_gain_db, audio_dc (0/1, default 1 for the real modes), audio_rate (Hz, 8000..192000).
struct AudioIfConfig {
    AudioIqMode mode = AudioIqMode::StereoIq;
    int chan = 0;
    double ifHz = 0;
    bool invert = false;
    float gainDb = 0;
    bool dc = true;
    int rate = 48000;
};
AudioIfConfig audioIfConfigOf(const TuneSettings& s);

struct AudioIfState {
    AudioHilbert hil;
    double phase = 0;        // the IF shifter's oscillator, radians
    float dcL = 0, dcR = 0;  // the DC trackers
    float peak = 0;          // input level after gain, 0..1+, decays
};

// n stereo frames in (interleaved L,R), n complex samples out at cfg.rate
void audioIfProcess(const float* stereo, int n, const AudioIfConfig& cfg, cf32* out, AudioIfState& st);

// The input level of the running sound-card source, 0..1 (1 = full scale; >0.95 clips): for the meter. 0 when none runs.
float audioSourceLevel();
// The sample rates offered for a sound card (the card resamples to the one picked)
const int* audioRateChoices(int& n);

} // namespace dect2
