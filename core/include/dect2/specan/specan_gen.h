#pragma once
#include "dect2/engine/mode_synth.h"
#include <memory>

namespace dect2 {

struct SpecanTone { double hz; double dbfs; };
const SpecanTone* specanTestTones(int& count);   // the carriers of the test scene
std::unique_ptr<ModeSynth> makeSpecanSynth(const SynthConfig& cfg, double sampleRate);

} // namespace dect2
