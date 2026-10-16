// TETRAPOL PAS 0001-2 UHF data frames (tetrapol_codec.h). Inner coding of the 20 ms / 160-bit GMSK frame:
// header 0x46, CRC-5, rate-1/2 K=5 convolutional code, 152-bit interleave, optional LFSR scramble.
#include "dect2/tetrapol/tetrapol_codec.h"
#include <algorithm>
#include <cstring>

namespace dect2 {
namespace tetrapol {

namespace {

constexpr uint8_t kCrcPoly = 0x12;   // x^5 + x^2
constexpr int kInfo = 68;            // 66 payload + 2 ASB
constexpr int kCoded = 73;           // + CRC-5
constexpr int kConvIn = 76;          // + 3 tail zeros → 152 coded bits
constexpr unsigned kG0 = 0x19, kG1 = 0x17;   // K=5

uint8_t crc5(const uint8_t* b, int n) {
    uint8_t r = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t m = (uint8_t)(b[i] ^ (r >> 4));
        r = (uint8_t)((r << 1) & 0x1f);
        if (m) r ^= kCrcPoly;
    }
    return (uint8_t)(r & 0x1f);
}

int permute(int i) { return (int)(((unsigned)i * 13u) % (unsigned)kDataBits); }

void interleave(const uint8_t in[kDataBits], uint8_t out[kDataBits]) {
    for (int i = 0; i < kDataBits; i++) out[permute(i)] = in[i];
}
void deinterleave(const uint8_t in[kDataBits], uint8_t out[kDataBits]) {
    for (int i = 0; i < kDataBits; i++) out[i] = in[permute(i)];
}

void scramble(uint8_t* bits, int n, int seed) {
    if (seed <= 0) return;
    unsigned s = (unsigned)seed & 127u;
    if (!s) return;
    for (int i = 0; i < n; i++) {
        const unsigned t = ((s >> 6) ^ s) & 1u;
        bits[i] = (uint8_t)(bits[i] ^ (uint8_t)t);
        s = ((s << 1) | t) & 127u;
    }
}

unsigned convOut(unsigned st, unsigned bit) {
    const unsigned x = ((st << 1) | bit) & 0x1f;
    unsigned p0 = 0, p1 = 0, a = x, b = x;
    for (int k = 0; k < 5; k++) { if ((kG0 >> k) & 1) p0 ^= (a & 1); a >>= 1; }
    for (int k = 0; k < 5; k++) { if ((kG1 >> k) & 1) p1 ^= (b & 1); b >>= 1; }
    return (p0 << 1) | p1;
}

void convEnc(const uint8_t in[kConvIn], uint8_t out[kDataBits]) {
    unsigned st = 0;
    for (int i = 0; i < kConvIn; i++) {
        const unsigned o = convOut(st, in[i]);
        out[2 * i] = (uint8_t)(o >> 1);
        out[2 * i + 1] = (uint8_t)(o & 1);
        st = ((st << 1) | in[i]) & 0x0f;
    }
}

// 16-state Viterbi for the K=5 code (input bit is the new LSB of the shift register after the step)
int convDec(const uint8_t in[kDataBits], uint8_t out[kConvIn]) {
    int met[16], nmet[16];
    int prev[kConvIn][16];
    for (int s = 0; s < 16; s++) met[s] = s ? -100000 : 0;
    for (int i = 0; i < kConvIn; i++) {
        for (int s = 0; s < 16; s++) nmet[s] = -100000;
        const int y0 = in[2 * i], y1 = in[2 * i + 1];
        for (int s = 0; s < 16; s++) {
            if (met[s] < -50000) continue;
            for (int b = 0; b < 2; b++) {
                const unsigned o = convOut((unsigned)s, (unsigned)b);
                const int ns = ((s << 1) | b) & 0x0f;
                const int m = met[s] + ((int)(o >> 1) == y0) + ((int)(o & 1) == y1);
                if (m > nmet[ns]) { nmet[ns] = m; prev[i][ns] = (s << 1) | b; }
            }
        }
        for (int s = 0; s < 16; s++) met[s] = nmet[s];
    }
    int st = 0;
    for (int s = 1; s < 16; s++) if (met[s] > met[st]) st = s;
    const int score = met[st];
    for (int i = kConvIn - 1; i >= 0; i--) {
        const int p = prev[i][st];
        out[i] = (uint8_t)(p & 1);
        st = p >> 1;
    }
    return score;
}

void putBits(uint8_t* dst, int& p, int v, int n) {
    for (int i = n - 1; i >= 0; i--) dst[p++] = (uint8_t)((v >> i) & 1);
}
int getBits(const uint8_t* src, int& p, int n) {
    int v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | src[p++];
    return v;
}

} // namespace

void packSysinfo(uint8_t payload[kPayloadBits], int country, int systemId, int bn, int cell, int la, int channel, int band) {
    memset(payload, 0, (size_t)kPayloadBits);
    int p = 0;
    putBits(payload, p, kCodopSysinfo, 8);
    putBits(payload, p, country & 255, 8);
    putBits(payload, p, systemId & 255, 8);
    putBits(payload, p, bn & 15, 4);
    putBits(payload, p, cell & 4095, 12);
    putBits(payload, p, la & 4095, 12);
    putBits(payload, p, channel & 255, 8);
    putBits(payload, p, band & 3, 2);
}

bool parseSysinfo(const uint8_t payload[kPayloadBits], int& country, int& systemId, int& bn, int& cell, int& la, int& channel, int& band) {
    int p = 0;
    if (getBits(payload, p, 8) != kCodopSysinfo) return false;
    country = getBits(payload, p, 8);
    systemId = getBits(payload, p, 8);
    bn = getBits(payload, p, 4);
    cell = getBits(payload, p, 12);
    la = getBits(payload, p, 12);
    channel = getBits(payload, p, 8);
    band = getBits(payload, p, 2);
    return true;
}

uint8_t undiffBits(uint8_t* bits, int n, uint8_t prev) {
    for (int i = 0; i < n; i++) {
        const uint8_t d = bits[i] & 1;
        bits[i] = (uint8_t)(d ^ prev);
        prev = bits[i];
    }
    return prev;
}
uint8_t diffBits(uint8_t* bits, int n, uint8_t prev) {
    for (int i = 0; i < n; i++) {
        const uint8_t o = bits[i] & 1;
        bits[i] = (uint8_t)(o ^ prev);
        prev = o;
    }
    return prev;
}

void encodeDataFrame(const DataFrame& in, uint8_t bits160[kFrameBits]) {
    uint8_t info[kConvIn] = {};
    memcpy(info, in.payload, (size_t)kPayloadBits);
    info[kPayloadBits] = in.asb[0] & 1;
    info[kPayloadBits + 1] = in.asb[1] & 1;
    const uint8_t c = crc5(info, kInfo);
    for (int i = 0; i < 5; i++) info[kInfo + i] = (uint8_t)((c >> (4 - i)) & 1);
    uint8_t coded[kDataBits] = {}, intl[kDataBits] = {};
    convEnc(info, coded);
    interleave(coded, intl);
    scramble(intl, kDataBits, in.scramble);
    for (int i = 0; i < 8; i++) bits160[i] = (uint8_t)((kFrameHdr >> (7 - i)) & 1);
    memcpy(bits160 + 8, intl, (size_t)kDataBits);
}

bool decodeDataFrame(const uint8_t bits152[kDataBits], int scr, DataFrame& out) {
    out = DataFrame();
    out.scramble = scr;
    uint8_t work[kDataBits];
    memcpy(work, bits152, (size_t)kDataBits);
    scramble(work, kDataBits, scr);
    uint8_t coded[kDataBits] = {}, info[kConvIn] = {};
    deinterleave(work, coded);
    out.metric = convDec(coded, info);
    const uint8_t got = crc5(info, kInfo);
    uint8_t want = 0;
    for (int i = 0; i < 5; i++) want = (uint8_t)((want << 1) | info[kInfo + i]);
    if (got != want) return false;
    memcpy(out.payload, info, (size_t)kPayloadBits);
    out.asb[0] = info[kPayloadBits];
    out.asb[1] = info[kPayloadBits + 1];
    out.ok = true;
    return true;
}

bool decodeFrame(const uint8_t bits160[kFrameBits], int scr, DataFrame& out) {
    uint8_t h = 0;
    for (int i = 0; i < 8; i++) h = (uint8_t)((h << 1) | (bits160[i] & 1));
    if (h != kFrameHdr) { out = DataFrame(); return false; }
    return decodeDataFrame(bits160 + 8, scr, out);
}

bool huntFrame(const uint8_t* bits, int n, int scrambleHint, int& off, DataFrame& out) {
    off = -1;
    if (n < kFrameBits) return false;
    const int s0 = scrambleHint >= 0 ? scrambleHint : 0;
    const int s1 = scrambleHint >= 0 ? scrambleHint : kScrMax;
    DataFrame best;
    int bestOff = -1;
    for (int o = 0; o <= n - kFrameBits; o++) {
        uint8_t h = 0;
        for (int i = 0; i < 8; i++) h = (uint8_t)((h << 1) | (bits[o + i] & 1));
        if (h != kFrameHdr) continue;
        for (int s = s0; s <= s1; s++) {
            DataFrame f;
            if (!decodeFrame(bits + o, s, f) || f.metric < 120) continue;
            if (bestOff < 0 || f.metric > best.metric) { best = f; bestOff = o; }
        }
        if (bestOff >= 0 && scrambleHint >= 0) break;   // known scramble: first header with a CRC is enough
    }
    if (bestOff < 0) return false;
    off = bestOff;
    out = best;
    return true;
}

} // namespace tetrapol
} // namespace dect2
