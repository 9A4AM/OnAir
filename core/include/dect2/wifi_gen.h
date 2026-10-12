// Wi-Fi beacon test signal. Nothing is transmitted: the samples only exist in memory or in a file.
//
// A schedule of 802.11 frames (wifi_proto.h builds them), each sent as a long-preamble 1 Mbit/s DSSS PPDU (DBPSK, Barker-11 at
// 11 Mchip/s, rectangular chips), separated by gaps of silence, over complex white noise.
//
// SynthConfig (makeWifiSynth): snrDb (signal to noise in 20 MHz), cfoHz.
#pragma once
#include "mode_synth.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace dect2 {

struct WifiGenFrame {
    std::vector<uint8_t> mpdu;   // FCS included (wifi::makeBeacon)
    double gapSec = 0.002;       // silence after it
    double ampScale = 1;
    double cfoHz = 0;            // this AP's own carrier offset, on top of the config's
    bool badFcs = false;         // tests: one bit of the FCS sent wrong
};

struct WifiGenConfig {
    double rate = 20e6;
    double snrDb = 30;           // signal to noise in 20 MHz; >= 99: no noise
    double cfoHz = 0;
    double clockPpm = 0;
    double amp = 0.25;
    double leadSec = 0.001;
    bool loop = true;
    uint32_t seed = 1;
    std::vector<WifiGenFrame> frames;
};

class WifiGenerator {
public:
    explicit WifiGenerator(const WifiGenConfig& c);
    ~WifiGenerator();
    void generate(cf32* out, size_t n);
    static double noiseSigma(const WifiGenConfig& c);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// Two APs on channel 6 ("OnAir-Home", WPA2, AVM; "OnAir-Cafe", WPA2/WPA3, Ubiquiti), a hidden open AP, a probe response and a data frame.
std::vector<WifiGenFrame> wifiTestSchedule();

std::unique_ptr<ModeSynth> makeWifiSynth(const SynthConfig& cfg, double sampleRate);

} // namespace dect2
