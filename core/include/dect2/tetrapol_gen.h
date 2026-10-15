// TETRAPOL test signal (tetrapol_rx.h): one UHF control-channel carrier of GMSK 8 kbit/s data frames.
// Nothing is transmitted; the samples only exist in memory or in a file.
//
// SynthConfig (makeTetrapolSynth):
//   snrDb, cfoHz, sroPpm
//   modeOpt[0] scramble 0 .. 127 (default 5)
#pragma once
#include "mode_synth.h"
#include <cstdint>
#include <memory>

namespace dect2 {

constexpr double kTetrapolTestCentreHz = 392e6;
constexpr int kTetrapolTestCountry = 33;
constexpr int kTetrapolTestSystem = 7;
constexpr int kTetrapolTestBn = 2;
constexpr int kTetrapolTestCell = 18;
constexpr int kTetrapolTestLa = 101;
constexpr int kTetrapolTestChannel = 40;
constexpr int kTetrapolTestScramble = 5;

std::unique_ptr<ModeSynth> makeTetrapolSynth(const SynthConfig& cfg, double sampleRate);

} // namespace dect2
