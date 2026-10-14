// DJI DroneID (the OcuSync 2 broadcast burst), the parts a receiver needs. Written from the public description in
//   Schiller et al., "Drone Security and the Mysterious Case of DJI's DroneID", NDSS 2023 (the OFDM parameters, the cyclic prefixes, the ZC roots 600 and 147,
//   the LTE turbo code and Gold sequence, the packet structure of the appendix), and
//   the MIT-licensed notes and scripts of proto17/dji_droneid (the 7200-bit frame, K = 1408 with a CRC-24, the scrambler seed, the carrier mapping).
// No code of the AGPL-licensed RUB-SysSec/DroneSecurity receiver was used.
//
// A burst: 9 OFDM symbols at 15.36 Msps (FFT 1024, 15 kHz spacing, 600 carriers around an empty DC bin), cyclic prefix 80 samples for the first and last
// symbol and 72 for the others (9880 samples, 643 us); symbols 4 and 6 (1-based) are Zadoff-Chu sequences (roots 600 and 147), the other seven carry QPSK. Some
// drones leave out the first symbol. The six data symbols after the first carry 6 x 600 x 2 = 7200 bits: Gold-scrambled (LTE sequence, seed 0x12345678),
// the rate-matched output (rv 0) of the LTE turbo code (K = 1408, QPP interleaver) of 176 bytes: the DroneID packet (91 bytes), padding and a CRC-24A.
//
// What has been checked against the published material and what only against our own encoder is listed in tests/test_dji_proto.cpp.
#pragma once
#include "ring.h"
#include <cstdint>
#include <string>
#include <vector>

namespace dect2::dji {

constexpr int kFft = 1024;
constexpr int kCarriers = 600;                 // data carriers (300 below and 300 above the empty DC bin)
constexpr int kSyms = 9;
constexpr double kFs = 15.36e6;                // sample rate of the 1024-point FFT at 15 kHz spacing
constexpr double kSpacingHz = 15000;
constexpr int kBurstBits = 7200;               // coded bits of a burst
constexpr int kTurboK = 1408;                  // information bits of the turbo code (176 bytes)
constexpr int kFrameBytes = 176;
constexpr int kInfoBytes = 91;                 // the DroneID packet at the start of the frame
constexpr int kZcSym[2] = {3, 5};              // 0-based symbol indices of the Zadoff-Chu symbols
constexpr int kZcRoot[2] = {600, 147};
constexpr int kDataSym[6] = {1, 2, 4, 6, 7, 8};
constexpr int kBurstSamples = 9 * kFft + 2 * 80 + 7 * 72;   // 9880
constexpr double kBurstUs = kBurstSamples / kFs * 1e6;      // 643.2

inline int cpLen(int sym) { return sym == 0 || sym == 8 ? 80 : 72; }
// Where the useful part (after the cyclic prefix) of a symbol starts, counted from the start of the burst (the nine-symbol form)
inline int bodyStart(int sym) { int s = 0; for (int i = 0; i < sym; i++) s += cpLen(i) + kFft; return s + cpLen(sym); }
// The FFT bin of data carrier 0..599: 0..299 are the 300 below DC (lowest first), 300..599 the 300 above
inline int carrierBin(int idx) { return idx < 300 ? kFft - 300 + idx : idx - 300 + 1; }

std::vector<cf32> zcFreq(int root);            // 600 values in carrier order, e^(-j pi root n (n + 1) / 601) with n = 0..600 without n = 300
std::vector<cf32> zcTime(int root);            // the 1024-sample symbol (no cyclic prefix)

// LTE Gold sequence (36.211 7.2) with x2 seeded from the seed: variant 0 the bits of 0x12345678 as LTE's c_init (x2(i) = bit i), variant 1 the 31-bit reversal
std::vector<uint8_t> scramblerBits(int n, int variant);

uint32_t crc24a(const uint8_t* d, size_t n);   // LTE CRC-24A, init 0, no reflection (a block followed by its CRC gives 0)
uint16_t crc16Duml(const uint8_t* d, size_t n);   // DJI DUML packet checksum: reflected 0x1021, init 0x3692

// LTE turbo code at K = 1408 with the circular-buffer rate matching of one transmission (rv 0) to kBurstBits bits
std::vector<int> qppInterleaver();                                  // pi(i) = (43 i + 88 i^2) mod 1408
std::vector<uint8_t> turboEncode(const std::vector<uint8_t>& bytes176);   // 7200 bits, one per byte
// llr: 7200 soft bits (positive: the bit is 0). true when the CRC-24 of the decoded 176 bytes is 0.
bool turboDecode(const std::vector<float>& llr, std::vector<uint8_t>& bytes176, int iterations = 8);

struct DroneId {
    int pktLen = 88, zeroByte = 0, version = 2, seq = 0, state = 0;
    std::string serial;                        // up to 16 characters
    int32_t lonRaw = 0, latRaw = 0;            // radians x 1e7
    int altRaw = 0, heightRaw = 0;             // 0.01 m (from the published examples; not stated in the paper)
    int vNorth = 0, vEast = 0, vUp = 0, yaw = 0;   // raw values (units not published)
    uint64_t gpsMs = 0;                        // milliseconds since 1970
    int32_t appLatRaw = 0, appLonRaw = 0;      // the pilot's phone or controller
    int32_t homeLonRaw = 0, homeLatRaw = 0;
    int deviceType = 0, uuidLen = 0;
    std::string uuid;
    bool crc16Ok = false;                      // the DUML checksum inside the packet
    double lon() const { return lonRaw / 1e7 * 57.29577951308232; }
    double lat() const { return latRaw / 1e7 * 57.29577951308232; }
    double appLon() const { return appLonRaw / 1e7 * 57.29577951308232; }
    double appLat() const { return appLatRaw / 1e7 * 57.29577951308232; }
    double homeLon() const { return homeLonRaw / 1e7 * 57.29577951308232; }
    double homeLat() const { return homeLatRaw / 1e7 * 57.29577951308232; }
    double altitudeM() const { return altRaw * 0.01; }
    double heightM() const { return heightRaw * 0.01; }
};
std::vector<uint8_t> packDroneId(const DroneId& d);        // the 176-byte frame: the packet, zero padding, CRC-24A
bool parseDroneId(const std::vector<uint8_t>& frame176, DroneId& d);   // false when the frame is shorter or the packet length is impossible

// ---- the burst on the air: OFDM
// The 9 (or 8: no first symbol) symbols of a burst at 15.36 Msps with cyclic prefixes, from 7200 coded bits (already scrambled), unit-rms carriers
std::vector<cf32> buildBurst(const std::vector<uint8_t>& scrambledBits, bool withFirstSymbol);
// The bits of the QPSK carriers of one symbol (carrier order, 2 per carrier, bit 0 the sign of the real part): the constellation is (1 - 2 b0, 1 - 2 b1) / sqrt 2
inline cf32 qpsk(int b0, int b1) { return cf32((1 - 2 * b0) * 0.70710678f, (1 - 2 * b1) * 0.70710678f); }

// ---- receiving one burst
struct BurstResult {
    bool zc = false;               // both Zadoff-Chu symbols found
    float zc1 = 0, zc2 = 0;        // their normalised correlation (noise about 0.04, 1 for a clean burst)
    int timing = -1;               // burst start in the input, samples at 15.36 Msps (the first symbol's cyclic prefix; for an 8-symbol burst where it would be)
    double cfoHz = 0;
    float snrDb = 0;               // from the ZC symbols
    bool crcOk = false;
    int variant = -1, rotation = -1;   // which scrambler seed and QPSK orientation matched
    DroneId id;
};
// x: the burst at 15.36 Msps, centred on DC (frequency offset up to about +-100 kHz), with a margin of a few hundred samples on both sides
BurstResult receiveBurst(const std::vector<cf32>& x);

} // namespace dect2::dji
