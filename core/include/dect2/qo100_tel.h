// What the QO-100 tool reports (RxTelemetry::qo100, standard 54). Frequencies are true downlink RF (the beacon correction applied when
// there is one), except where a field says IF.
#pragma once
#include <cstdint>
#include <vector>

namespace dect2 {

struct Qo100Beacon {
    double nominalHz = 0;
    bool found = false;
    double rfHz = 0;            // where it is, apparent RF (nominal LO, no correction): nominal - LNB error
    double snrDb = 0;           // CW beacons: the fine estimate's peak over the median; PSK beacon: level over the floor of the narrowband spectrum
};

struct Qo100Signal {            // a carrier or an SSB signal on the narrowband transponder
    double rfHz = 0;            // top of the signal (CW: the carrier), corrected
    double widthHz = 0;         // -6 dB width
    float levelDb = -200, snrDb = 0;
    int beacon = -1;            // 0 lower CW, 1 PSK, 2 upper CW, -1 a user signal
};

struct Qo100Datv {              // a carrier on the wideband transponder
    double rfHz = 0;            // centre, corrected
    double ifHz = 0;            // where it is at the radio (what the DVB-S/S2 receiver tunes to)
    double bw3Hz = 0, symRate = 0;
    float snrDb = 0;
    bool looksDvbs = false;     // flat-topped: a DVB-S / S2 carrier
    bool beacon = false;        // the DATV beacon (10491.5 MHz, 1.5 Msym/s)
};

struct Qo100Telemetry {
    uint64_t seq = 0;
    int state = 0;              // 0 waiting / the narrowband transponder is outside the radio's band, 1 searching for the beacons, 2 locked, 3 held (beacons lost: the last correction is kept)
    double rate = 0;            // input sample rate, Hz
    double centreIfHz = 0;      // the radio's frequency
    double loHz = 0;            // the nominal LNB oscillator
    bool inverted = false;      // the LO is above the downlink: the spectrum arrives mirrored (the receiver turns it back)
    bool nbOk = false, wbOk = false;   // the narrowband / wideband transponder lies inside the radio's band
    // the LNB's frequency error, measured on the CW beacons: actual LO = nominal + lnbErrHz
    bool corrValid = false;
    double lnbErrHz = 0, lnbErrPpm = 0;
    double driftHzPerMin = 0;   // over the last 30 s
    bool driftValid = false;
    double lockSec = 0;         // since the lock was taken
    uint64_t measurements = 0;
    std::vector<float> histT, histErr;   // seconds since the start, lnbErrHz: one point per measurement (the newest last)
    Qo100Beacon beacons[3];     // lower CW, PSK, upper CW
    // the narrowband transponder: full-resolution averaged spectrum (dBFS per bin), bin k at nbF0Hz + k * nbBinHz (corrected RF)
    std::vector<float> nb;
    double nbF0Hz = 0, nbBinHz = 1;
    float nbFloorDb = -200, nbPeakDb = -200;
    std::vector<Qo100Signal> signals;
    // the radio's whole band: averaged spectrum, bin k at wbF0Hz + k * wbBinHz (corrected RF, lowest first even when inverted)
    std::vector<float> wb;
    double wbF0Hz = 0, wbBinHz = 1;
    float wbFloorDb = -200, wbPeakDb = -200;
    std::vector<Qo100Datv> datv;
    // SSB listening
    bool listening = false;
    double dialHz = 0;          // USB suppressed carrier (corrected RF)
    float audioDb = -200;       // level in the 2.7 kHz passband
    double powerDb = -200;      // total input power, dBFS
};

} // namespace dect2
