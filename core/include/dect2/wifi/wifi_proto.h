// Wi-Fi (IEEE 802.11) pieces of the network monitor: 2.4 GHz channels, the DSSS PHY's scrambler, PLCP header and CRCs, and the
// management frames access points announce themselves with (beacons and probe responses). Built from IEEE 802.11-2020 clause 15 (DSSS)
// and 9.3.3 / 9.4.2 (management frames and elements). Only beacons and probe responses are parsed; other frames are only classified.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dect2::wifi {

constexpr double kChipRate = 11e6;       // Barker-11 chips per second
constexpr double kSymRate = 1e6;         // 1 Mbit/s DBPSK: one symbol per microsecond
constexpr uint16_t kSfd = 0xF3A0;        // long preamble start frame delimiter (sent LSB first)
constexpr uint8_t kSignal1M = 0x0A;      // PLCP SIGNAL: 1 Mbit/s
constexpr int kSyncBits = 128;           // long preamble SYNC: scrambled ones
constexpr int kMaxMpdu = 2346;           // the longest DSSS MPDU, FCS included
extern const int kBarker[11];            // +1 -1 +1 +1 -1 +1 +1 +1 -1 -1 -1

double channelMhz(int ch);               // 1 .. 13: 2407 + 5 ch, 14: 2484; 0 otherwise
int channelOf(double mhz);               // the 2.4 GHz channel within 1 MHz of mhz, 0 none

uint32_t crc32(const uint8_t* p, size_t n);                    // the FCS (IEEE 802.3 CRC-32)
uint16_t crc16(const uint8_t* bits, size_t n);                 // PLCP header CRC-16 over bits in transmit order (preset ones, inverted)

// The whole long-preamble PPDU at 1 Mbit/s as the bits that DBPSK carries (scrambled, transmit order): SYNC, SFD, header, MPDU.
// mpdu includes its FCS.
std::vector<uint8_t> ppduBits(const std::vector<uint8_t>& mpdu, uint8_t seed = 0x1B);

enum Security { Open = 0, Wep, Wpa, WpaWpa2, Wpa2, Wpa2Wpa3, Wpa3, Owe };
const char* securityName(int s, bool enterprise = false);

// An access point's announcement, for the test signal
struct ApSpec {
    uint8_t bssid[6] = {};
    std::string ssid;
    bool hidden = false;                 // an empty SSID element
    int channel = 6;                     // DS parameter set
    int interval = 100;                  // TU (1.024 ms)
    int security = Wpa2;
    bool enterprise = false;
    bool gRates = true;                  // 6 - 54 Mbit/s besides 1, 2, 5.5 and 11
    uint64_t tsf = 0;
    uint16_t seq = 0;
};
std::vector<uint8_t> makeBeacon(const ApSpec& s, bool probeResponse = false, const uint8_t* client = nullptr);   // FCS appended
std::vector<uint8_t> makeDataFrame(const uint8_t bssid[6], const uint8_t sta[6], size_t payload);               // FCS appended, payload random-ish
void appendFcs(std::vector<uint8_t>& f);

// What a frame says. n excludes the FCS (checked by the caller).
struct Frame {
    int type = -1, subtype = -1;         // 0 management, 1 control, 2 data
    bool beacon = false, probeResp = false;
    uint8_t addr1[6] = {}, addr2[6] = {}, bssid[6] = {};
    std::string ssid;
    bool hidden = false;
    int channel = 0;                     // DS parameter set (0: not sent)
    int interval = 0;                    // TU
    uint16_t cap = 0;
    uint64_t tsf = 0;
    std::vector<int> rates;              // in 500 kbit/s units, | 0x80 for a basic rate
    int security = Open;
    bool enterprise = false;
    std::string ciphers;                 // "CCMP", "TKIP+CCMP" ...
    std::string summary;
};
bool parseFrame(const uint8_t* p, size_t n, Frame& f);   // false: too short to say anything
std::string ratesText(const std::vector<int>& r);        // "1 2 5.5 11 6 9 12 18 24 36 48 54"
std::string macText(const uint8_t a[6]);
const char* vendorOf(const uint8_t a[6]);                // a short OUI table; "" unknown, "local" for a locally administered address
const char* frameTypeName(int type, int subtype);

} // namespace dect2::wifi
