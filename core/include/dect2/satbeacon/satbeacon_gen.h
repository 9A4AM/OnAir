// Satellite beacon test signal: a carrier (offset from the radio's centre like the IF of an LNB, slowly drifting), phase modulated by telemetry on a BPSK
// subcarrier (the PCM/PSK/PM of the Ku-band beacons: bi-phase data at 4800 bit/s on 48 kHz), and a second unmodulated reference carrier a few hundred kHz away.
// Frames of 256 bytes in the style of the recorded beacon: the sync word FAF320, 0xFE, a counter byte 0 to 31 (a major frame is 32 minor frames), a clock
// (byte 15, one count a second), a copy of the counter (byte 122), fixed words, idle fill, commutated and changing words and a checksum (the sum of the others) in the last byte. This layout only exists for the tests; the real beacon's meaning of the words is not known.
// Nothing is transmitted: the samples only exist in memory or a file.
//
// SynthConfig (makeSatbeaconSynth): snrDb (carrier over the noise in 1 kHz), cfoHz (carrier offset from the centre; 0: +236.1 kHz);
// modeOpt[0]: 0 carrier, reference carrier and telemetry, 1 carrier only (no modulation), 2 telemetry without the reference carrier.
#pragma once
#include "dect2/engine/mode_synth.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace dect2 {

struct SatbeaconGenConfig {
    double rate = 2e6;
    double snrDb = 30;                  // carrier over the noise in 1 kHz; >= 99: no noise
    double carrierHz = 236100;
    double driftHzPerSec = -0.4;        // the LNB's warm-up drift, carrier and reference together
    double amp = 0.25;                  // carrier amplitude
    bool telemetry = true;
    bool reference = true;              // second carrier
    double refOffsetHz = 545642;        // from the main carrier
    double refDb = -8;
    double subcarrierHz = 48000;
    double bitRate = 4800;
    bool biphase = true;                // bi-phase (Manchester) data; false: NRZ
    double clockPpm = 7;                // the data clock's error against the radio's
    double pmIndex = 1.2;               // rad peak
    bool ccsds = false;                 // frames are CCSDS codeblocks instead: the attached sync marker 1ACFFC1D, then a randomised Reed-Solomon (255,223) block of a TM transfer frame
    uint32_t seed = 1;
};

constexpr int kSbGenFrameBytes = 256;
constexpr int kSbGenCcsdsBytes = 4 + 255;
std::vector<uint8_t> satbeaconCcsdsFrame(uint32_t seed, uint32_t n);   // ASM + codeblock of transfer frame number n (spacecraft 0x2DF, virtual channel 1, counters from n)
// the bytes of frame number n of a generator with this seed (what the receiver should find)
std::vector<uint8_t> satbeaconFrameBytes(uint32_t seed, uint32_t n);

class SatbeaconGenerator {
public:
    explicit SatbeaconGenerator(const SatbeaconGenConfig& c);
    ~SatbeaconGenerator();
    void generate(cf32* out, size_t n);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

std::unique_ptr<ModeSynth> makeSatbeaconSynth(const SynthConfig& cfg, double sampleRate);

} // namespace dect2
