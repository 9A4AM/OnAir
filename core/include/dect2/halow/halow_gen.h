// Wi-Fi HaLow (802.11ah) test signal, sub-GHz S1G OFDM. Nothing is transmitted: the samples only exist in memory or in a file.
//
// A schedule of PPDUs (halow_proto.h builds them: STF, LTF1, SIG, data at BPSK 1/2) at 1, 2 and 4 MHz, separated by gaps of silence, over
// complex white noise. They are made at 4 Msps and brought to the radio's sample rate with a windowed sinc; the start of each PPDU and
// the sample clock (clockPpm) are not locked to the receiver's.
//
// SynthConfig (makeHalowSynth): snrDb (signal to noise in 1 MHz), cfoHz; modeOpt[0]: 0 the schedule.
#pragma once
#include "dect2/engine/mode_synth.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace dect2 {

struct HalowGenFrame {
    std::vector<uint8_t> psdu;   // MAC frame with its FCS (halow::makeS1gBeacon ...)
    int width = 0;               // 0 1 MHz, 1 2 MHz, 2 4 MHz
    int mcs = 0;                 // 0, or 10 (1 MHz); any other value sends the preamble and SIG field only (the data rate the monitor does not decode)
    double gapSec = 0.003;       // silence after it
    double ampScale = 1;
    double cfoHz = 0;            // this transmitter's own carrier offset, on top of the config's
    uint8_t seed = 0x5D;         // scrambler state
    bool badFcs = false;         // tests: one bit of the PSDU flipped after the FCS was computed
};

struct HalowGenConfig {
    double rate = 4e6;
    double dialOffsetHz = 0;
    double snrDb = 30;           // signal to noise in 1 MHz; >= 99: no noise
    double cfoHz = 0;
    double clockPpm = 0;         // the transmitter's sample clock against the receiver's
    double amp = 0.3;
    double leadSec = 0.0005;
    bool loop = true;
    uint32_t seed = 1;
    std::vector<HalowGenFrame> frames;
};

class HalowGenerator {
public:
    explicit HalowGenerator(const HalowGenConfig& c);
    ~HalowGenerator();
    void generate(cf32* out, size_t n);
    static double noiseSigma(const HalowGenConfig& c);   // per real component
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// The networks of the schedule: a 1 MHz WPA3 network, a 2 MHz WPA2 one, a 4 MHz open one (US plan), one that first announces only its compressed
// SSID and answers a probe later, a probe response, a data frame, one PPDU at a rate that is not decoded
std::vector<HalowGenFrame> halowTestSchedule();

std::unique_ptr<ModeSynth> makeHalowSynth(const SynthConfig& cfg, double sampleRate);

} // namespace dect2
