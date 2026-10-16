// Elektro-L HRIT test signal. Nothing is transmitted: the samples only exist in memory or in a file.
// The framing is the GOES / CGMS xRIT test products (goes_gen.h) at 665.4 ksym/s, the published Elektro-L HRIT rate.
#pragma once
#include "dect2/engine/mode_synth.h"
#include <memory>

namespace dect2 {

std::unique_ptr<ModeSynth> makeElektroSynth(const SynthConfig& cfg, double sampleRate);

} // namespace dect2
