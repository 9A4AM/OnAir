// Test signal of the ionospheric sounder mode (makeIonoSynth): two chirp sounders of different rates and timing, each heard on two
// ionospheric paths (delayed echoes, the delays constant over frequency), in Gaussian noise. Time starts at 0 with the first sample.
// The generator places the sweeps for a radio tuned to SynthConfig::modeVal[0] MHz (0: kIonoSynthDefMhz).
// SynthConfig::modeOpt[0] = 1: noise only.
#pragma once
#include "mode_synth.h"
#include <memory>

namespace dect2 {

constexpr double kIonoSynthDefMhz = 10.0;
struct IonoSynthSounder { double rateHz, periodSec, offsetSec; double delaySec[2]; double amp[2]; };
// A: 100 kHz/s every 30 s at +2 s; B: 500 kHz/s every 20 s at +7 s. Each sweep starts at 0 MHz and lasts 95 % of the period
// (a quick scene: real sounders repeat every few minutes and sweep the whole 0 .. 30 MHz).
constexpr IonoSynthSounder kIonoSynth[2] = {
    {100e3, 30, 2, {1.6e-3, 2.4e-3}, {1.0, 0.6}},
    {500e3, 20, 7, {1.8e-3, 2.9e-3}, {1.0, 0.5}},
};
constexpr double kIonoSynthAmp = 0.063;   // of the strongest path: 10 dB under the noise (rms 0.2) per sample
std::unique_ptr<ModeSynth> makeIonoSynth(const SynthConfig& cfg, double sampleRate);

} // namespace dect2
