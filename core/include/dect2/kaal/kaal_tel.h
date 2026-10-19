// KAAL receiver telemetry. RxTelemetry::kaal, engine standard 124, active standard 123.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace dect2 {

struct KaalTelemetry {
    uint64_t seq = 0;
    int state = 0;                 // 0 no input, 1 searching, 2 locked
    double cfoHz = 0;
    float snrDb = 0;
    float sweepDb = 0;             // level of the sweep signature
    float audioLevel = 0;          // 0 .. 1, peak of the decoded speech, decays
    bool dataValid = false;
    uint64_t blocksOk = 0, blocksBad = 0;   // good and bad frames
    double inputRate = 0, timeSec = 0;
    // the last valid beacon (station identity frame)
    bool beaconValid = false;
    uint64_t beacons = 0;          // valid beacon frames so far
    double beaconAgeSec = 0;       // seconds since the last valid frame
    std::string callsign, grid, text;
    double lat = 0, lon = 0, powerDbm = 0;
    int linkQuality = 0;
};

inline std::string kaalSummary(const KaalTelemetry& t) {
    char b[200];
    if (t.state == 0) return "KAAL: waiting for samples";
    if (t.state == 1) return "KAAL: searching";
    if (t.beaconValid) snprintf(b, sizeof b, "KAAL: locked, %s %s", t.callsign.c_str(), t.grid.c_str());
    else snprintf(b, sizeof b, "KAAL: locked, SNR %.0f dB", (double)t.snrDb);
    return b;
}

} // namespace dect2
