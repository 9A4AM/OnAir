// COSPAS-SARSAT 406 MHz test signal. Nothing is transmitted: the samples only exist in memory or in a file. The messages are made-up beacons
// (a test protocol identification, never a registered one).
//
// makeSarsatSynth(): an endless loop of bursts: 160 ms unmodulated carrier, then the message at 400 bit/s Manchester, phase modulated +-1.1 rad.
// The carrier sits sarsatTuning().tuneOffsetHz below the radio's centre plus SynthConfig::cfoHz. snrDb is the carrier power over the noise power in 2.4 kHz.
//
// SynthConfig::modeOpt:  [0] which test beacon to repeat (1..N, 0 = all in turn)   [1] 1 = phase polarity reversed   [3] noise seed, 0 = 1
// SynthConfig::modeVal:  [0] bit rate error in percent (0 = exact)   [1] seconds between bursts (0 = 3 s; real beacons repeat about every 50 s)
#pragma once
#include "dect2/engine/mode_synth.h"
#include "dect2/sarsat/sarsat_proto.h"
#include <memory>
#include <vector>

namespace dect2::sarsat {
// The messages of the test signal, in the order it plays them
std::vector<Spec> testBeacons();
// The phase waveform of one burst (carrier, then the message) as samples at `rate`: complex unit-amplitude values, used by the tests and the synth
std::vector<std::complex<float>> burstSamples(const std::vector<uint8_t>& messageBits, double rate, double carrierHz, double rateErrorPct = 0, bool inverted = false);
}

namespace dect2 {
std::unique_ptr<ModeSynth> makeSarsatSynth(const SynthConfig& cfg, double sampleRate);
}
