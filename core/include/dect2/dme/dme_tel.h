// DME telemetry. RxTelemetry::dme, engine standard 123 (family 121).
//
// The DME receiver reports the X or Y channel of the beacon, the pulse-pair rate, the envelope level and the Morse ident.
// Work in progress: the decoded ident is not checked against a published ident list.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace dect2 {

struct DmeTelemetry {
    uint64_t seq = 0;
    int state = 0;                 // 0 no pulse pairs yet, 1 pairs but no ident letter yet, 2 ident letters read
    double cfoHz = 0;              // not measured
    float snrDb = 0;               // not measured
    bool dataValid = false;        // a pair rate is measured
    uint64_t blocksOk = 0;         // ident letters decoded
    uint64_t blocksBad = 0;        // not used
    int channel = 0;               // 0 none, 1 X (12 us pair spacing), 2 Y (36 us pair spacing)
    double pairRatePps = 0;        // pulse pairs per second over the last second
    int pairsX = 0, pairsY = 0;    // pairs counted since the start
    float levelDb = -200;          // envelope peak level, dB
    bool identKeyed = false;       // the ident tone is on now
    std::string ident;             // decoded ident letters, a space between words
};

inline const char* dmeChannelName(int channel) {
    return channel == 1 ? "X" : channel == 2 ? "Y" : "none";
}

inline std::string dmeSummary(const DmeTelemetry& t) {
    char b[160];
    if (t.state == 0) return "DME: no pulse pairs";
    if (t.ident.empty()) {
        snprintf(b, sizeof b, "DME %s, %.0f pairs/s, no ident yet", dmeChannelName(t.channel), t.pairRatePps);
        return b;
    }
    snprintf(b, sizeof b, "DME %s, %.0f pairs/s, ident %s", dmeChannelName(t.channel), t.pairRatePps, t.ident.c_str());
    return b;
}

} // namespace dect2
