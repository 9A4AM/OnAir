// QO-100 (Es'hail-2) tools: the geostationary amateur satellite's downlink, 10489.5 - 10500 MHz, through a Ku-band LNB (IF 739.5 - 750
// MHz with the usual 9750 MHz oscillator). No decoder of its own:
//  - beacon lock: the two CW beacons of the narrowband transponder (10489.500 and 10490.000 MHz) are found in a +-600 kHz search, then
//    followed by a fine estimate (about 1 Hz bins, a new value every 0.4 s); they measure the LNB's frequency error and drift, which
//    corrects every frequency shown (state 3 "held" keeps the last correction while the beacons are out of the band);
//  - the narrowband transponder (10489.5 - 10490.0) as a high-resolution spectrum (about 75 Hz bins) with its signals listed, and SSB
//    listening: an upper sideband receiver, 300 - 3000 Hz, on the corrected dial frequency, through the sound card ("Listen");
//  - the wideband transponder (10491 - 10499): the radio's whole band as a spectrum, its DATV carriers measured (symbol rate) for a
//    hand-over to the DVB-S/S2 mode.
// Engine standard 55 (active 54). The radio sits on the IF; 10 Msps on 744.0 MHz (9750 LO) sees the whole downlink, 2 Msps on 740.1 MHz
// the narrowband transponder alone. Written to the engine's contract: configure() once the sample rate is known, feed() from the analysis
// thread (never blocks), telemetry() and the setters from any other thread.
#pragma once
#include "dect2/engine/mode_tuning.h"
#include "dect2/common/ring.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dect2 {

struct Qo100Telemetry;   // qo100_tel.h

class Qo100Receiver {
public:
    Qo100Receiver();
    ~Qo100Receiver();

    void configure(double inputRateHz);
    void setSignalOffset(double) {}
    void setCenterHz(double ifHz);       // the radio's frequency (0: unknown, a file: the default tuning for the rate is assumed)
    bool ready() const;
    void reset();                        // the lock and the averages start again
    void feed(const cf32* x, size_t n);
    bool telemetry(Qo100Telemetry& out, uint64_t lastSeq);
    void setLogCallback(std::function<void(const std::string&)> cb);

    void setLo(double loHz);             // the LNB's nominal oscillator (default 9750 MHz; above the downlink = inverted spectrum)
    void setSearchHz(double hz);         // how far from nominal the beacons are looked for (default 600 kHz)
    void setDialHz(double rfHz);         // SSB dial (USB suppressed carrier), corrected RF
    void setAutoCorrection(bool on);     // default on: the LNB error measured on the beacons corrects every frequency; off for a GPS-locked LNB
    void setListen(bool on);
    bool listening() const;
    void setVolume(float v);
    void setMuted(bool m);
    void setSilent(bool s);              // listen without opening the sound device (tests, command line)
    void setListenTap(std::function<void(const float* x, size_t n)> cb);   // tests: the 8 kHz audio Listen plays

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning qo100Tuning();
std::string qo100Summary(const Qo100Telemetry& t);

} // namespace dect2
