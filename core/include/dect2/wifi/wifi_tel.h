// Wi-Fi network monitor telemetry, shared by the receiver and the UI.
// The first members are copied into RxTelemetry (history plot, top bar): keep them and their meaning.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace dect2 {

constexpr size_t kWifiRssiHist = 24;   // the last levels of a network, oldest first
constexpr size_t kWifiApMax = 300;     // networks kept (the longest unheard go first)
constexpr size_t kWifiLogMax = 400;    // frames in the log

// One access point (a BSSID): what its beacons and probe responses announce
struct WifiAp {
    std::string bssid;                 // "00:04:0E:12:34:56"
    std::string ssid;                  // "" when hidden
    bool hidden = false;               // the beacons carry no name
    std::string vendor;                // from the OUI ("local": a locally administered, often virtual BSSID)
    int channel = 0;                   // the AP's own (DS parameter set), 0 not sent
    int heardOn = 0;                   // the channel the radio was on (0: a file)
    int security = 0;                  // wifi::Security
    bool enterprise = false;
    std::string ciphers, rates;
    int interval = 0;                  // beacon interval, TU (1.024 ms)
    uint16_t cap = 0;
    float rssi = -200;                 // dBFS of the last frame (the radio's level, not calibrated dBm)
    std::vector<float> rssiHist;
    double firstSec = 0, lastSec = 0;  // signal time
    int64_t firstWall = 0, lastWall = 0;
    uint64_t beacons = 0, probeResps = 0;
};

struct WifiFrameLog {
    int64_t wallTime = 0;
    double sec = 0;
    int channel = 0;                   // the tuned channel (0: a file)
    int type = 0, subtype = 0;
    std::string bssid;
    float rssi = -200;
    int len = 0;                       // MPDU bytes, FCS included
    std::string info;
};

struct WifiTelemetry {
    uint64_t seq = 0;
    int state = 0;                     // 0 nothing heard, 1 frames heard in the last seconds
    double cfoHz = 0;                  // carrier offset of the last good frame
    float snrDb = 0;                   // symbol SNR after despreading (from the DBPSK phase spread)
    float levelDb = -200;              // input level, dBFS
    bool dataValid = false;
    uint64_t blocksOk = 0;             // frames with a good FCS
    uint64_t blocksBad = 0;            // good PLCP header, bad FCS
    // details
    double timeSec = 0, inputRate = 0;
    int channel = 0;                   // the channel the radio is tuned to (0: not known, a file)
    uint64_t sfdHits = 0, headersOk = 0, headersBad = 0, otherRate = 0;   // otherRate: a good header for 2, 5.5 or 11 Mbit/s (not decoded)
    uint64_t perType[3] = {};          // good frames: management, control, data (data frames are only counted)
    uint64_t beacons = 0, probeResps = 0;
    std::vector<WifiAp> aps;           // most recently heard first
    std::vector<WifiFrameLog> log;     // beacons and probe responses, newest first
};

inline std::string wifiSummary(const WifiTelemetry& t) {
    char b[160];
    if (t.aps.empty()) {
        if (t.channel) { snprintf(b, sizeof b, "listening on channel %d", t.channel); return b; }
        return "listening for beacons";
    }
    snprintf(b, sizeof b, "%zu networks, %llu beacons", t.aps.size(), (unsigned long long)t.beacons);
    return b;
}

} // namespace dect2
