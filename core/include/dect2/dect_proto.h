// DECT (cordless telephones, 1880 - 1900 MHz in Europe): the parts every base station broadcasts in the clear. Receive only.
// Ten carriers 1.728 MHz apart (carrier n at 1897.344 - 1.728 n MHz), 1.152 Mbit/s GFSK, a frame of 10 ms with 24 slots. A full slot starts
// with the S-field (16 preamble bits then a 16-bit sync word: 0xE98A from a base station (RFP), 0x1675 from a handset (PP)), then the
// 64-bit A-field: an 8-bit header (tail type TA 3 bits, Q1, BA 3 bits, Q2), 40 tail bits and a 16-bit CRC over the first 48. The B-field
// (voice or data, scrambled, and encrypted once a call has keys) is not decoded. EN 300 175-3.
#pragma once
#include <cstdint>
#include <string>

namespace dect {

constexpr double kBitRate = 1152000.0;
constexpr double kCarrier0Mhz = 1897.344, kSpacingMhz = 1.728;
constexpr int kCarriers = 10, kSlots = 24;
constexpr double kFrameSec = 0.010;
constexpr uint32_t kSyncRfp = 0xAAAAE98Au;   // preamble and sync word, first bit sent = most significant
constexpr uint32_t kSyncPp = 0x55551675u;

inline double carrierMhz(int n) { return kCarrier0Mhz - kSpacingMhz * n; }
inline int carrierOf(double mhz) {
    const double x = (kCarrier0Mhz - mhz) / kSpacingMhz;
    const int n = (int)(x + (x < 0 ? -0.5 : 0.5));
    return n >= 0 && n < kCarriers && (x - n < 0.2 && n - x < 0.2) ? n : -1;
}

// the A-field CRC: x^16 + x^10 + x^8 + x^7 + x^3 + 1 over the 48 bits (first bit sent = bit 47 of v), register starting at zero
inline uint16_t rcrc(uint64_t v48) {
    uint16_t reg = 0;
    for (int i = 47; i >= 0; i--) {
        const bool fb = ((reg >> 15) & 1) ^ ((v48 >> i) & 1);
        reg = (uint16_t)(reg << 1);
        if (fb) reg ^= 0x0589;
    }
    return reg;
}

enum TailType { TaCt0 = 0, TaCt1 = 1, TaNt = 2, TaNtCl = 3, TaQt = 4, TaEscape = 5, TaMt = 6, TaPt = 7 };
inline const char* tailName(int ta) {
    static const char* const n[8] = {"Ct (data 0)", "Ct (data 1)", "Nt (identity)", "Nt (connectionless)", "Qt (system info)", "escape", "Mt (MAC control)", "Pt (paging)"};
    return n[ta & 7];
}

struct AField {
    uint64_t raw = 0;        // the 64 bits, first bit sent = bit 63
    int ta = 0, q1 = 0, ba = 0, q2 = 0;
    uint64_t tail = 0;       // 40 bits
    uint16_t crc = 0;
    bool crcOk = false;      // the CRC agrees (as sent or complemented)
    bool crcInverted = false;   // ...and it was the complemented one
};

inline AField parseAField(uint64_t raw64) {
    AField a;
    a.raw = raw64;
    const uint64_t head48 = raw64 >> 16;
    a.ta = (int)(raw64 >> 61) & 7; a.q1 = (int)(raw64 >> 60) & 1; a.ba = (int)(raw64 >> 57) & 7; a.q2 = (int)(raw64 >> 56) & 1;
    a.tail = (raw64 >> 16) & 0xFFFFFFFFFFULL;
    a.crc = (uint16_t)(raw64 & 0xFFFF);
    const uint16_t c = rcrc(head48);
    a.crcOk = c == a.crc || (uint16_t)~c == a.crc;
    a.crcInverted = a.crcOk && c != a.crc;
    return a;
}

// builds an A-field with a correct CRC (tests and the generator); inverted: the complemented CRC
inline uint64_t makeAField(int ta, int q1, int ba, int q2, uint64_t tail40, bool inverted = false) {
    const uint64_t head = ((uint64_t)(ta & 7) << 5 | (uint64_t)(q1 & 1) << 4 | (uint64_t)(ba & 7) << 1 | (uint64_t)(q2 & 1)) & 0xFF;
    const uint64_t v48 = head << 40 | (tail40 & 0xFFFFFFFFFFULL);
    uint16_t c = rcrc(v48);
    if (inverted) c = (uint16_t)~c;
    return v48 << 16 | c;
}

// The base station identity (RFPI, 40 bits) carried by Nt: an exclusive-bit, the 3-bit class, then for class A the manufacturer code (EMC,
// 16 bits) and the fixed part number (17 bits). This split is read from the standard, not checked on a real station.
struct Rfpi {
    uint64_t value = 0;
    int arc = 0;             // access rights class
    int emc = -1;            // manufacturer code (class A)
    int fpn = -1;
};
inline Rfpi rfpiOf(uint64_t tail40) {
    Rfpi r;
    r.value = tail40;
    r.arc = (int)((tail40 >> 35) & 7);
    if (r.arc == 0) { r.emc = (int)((tail40 >> 19) & 0xFFFF); r.fpn = (int)((tail40 >> 2) & 0x1FFFF); }
    return r;
}
inline std::string hex40(uint64_t v) { char b[16]; snprintf(b, sizeof b, "%010llX", (unsigned long long)(v & 0xFFFFFFFFFFULL)); return b; }

}  // namespace dect
