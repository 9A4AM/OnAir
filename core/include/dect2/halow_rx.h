// Wi-Fi HaLow (IEEE 802.11ah) network monitor: the networks around, from the S1G beacons they broadcast openly in the 863 - 868 MHz (EU)
// and 902 - 928 MHz (US) bands, as a Wi-Fi list shows them (SSID, BSSID and vendor, channel, bandwidth, MCS, security, level). Passive:
// it never transmits, probes, associates or joins; the security type is shown as information only and nothing is decrypted.
//
// PHY (IEEE 802.11ah, S1G OFDM; 1, 2 and 4 MHz channels, 31.25 kHz tones, 40 us symbols): the input is mixed to 0 Hz and brought to
// 4 Msps. The short training field repeats every 8 us, so a delay-and-correlate detector (lag 32 samples) finds a preamble at every
// width and estimates the carrier offset. For each candidate every width is tried: the long training symbol is matched for timing, the
// residual offset and the channel are estimated from it, the SIG field (BPSK, rate 1/2 convolutional code, CRC-4) is decoded and must
// agree with the width, and the data field is decoded (Viterbi, descrambler) when the rate is BPSK 1/2 (MCS 0, and MCS 10 at 1 MHz),
// which is how beacons are sent. The FCS (CRC-32) must pass. Frames sent at higher rates are counted from their SIG field only.
//
// Written to the engine's contract: configure() once the sample rate is known, feed() from the analysis thread (never blocks),
// telemetry() and the setters from the interface thread.
#pragma once
#include "mode_tuning.h"
#include "ring.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dect2 {

struct HalowTelemetry;   // halow_tel.h

class HalowReceiver {
public:
    HalowReceiver();
    ~HalowReceiver();

    void configure(double inputRateHz);
    void setSignalOffset(double hz);
    void setCenterHz(double hz);          // the radio's centre: the channel shown with each network (0: unknown)
    bool ready() const;
    void reset();                         // the demodulator only: the tables stay
    void clearResults();
    void feed(const cf32* x, size_t n);
    bool telemetry(HalowTelemetry& out, uint64_t lastSeq);
    // The tables and the frame log stay through reset(), configure() and retunes: only clearResults() empties them
    std::string saveResults() const;                     // for the results file (results_io.h)
    bool loadResults(const std::string& blob);           // false: not this build's format, nothing changes
    void setLogCallback(std::function<void(const std::string&)> cb);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning halowTuning();

} // namespace dect2
