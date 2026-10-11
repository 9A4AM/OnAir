// ISM sensors: pulse slicers and device decoders. See ism_proto.h.
//
// Written from rtl_433 (GPL-2.0, https://github.com/merbanan/rtl_433): the slicers follow src/pulse_slicer.c, the bit helpers
// src/bitbuffer.c and src/bit_util.c, and every decoder names the src/devices/ file it was written from. The timings in the protocol
// table are the r_device values of those files.
#include "dect2/ism_proto.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace dect2 {

// ---------------------------------------------------------------- bit_util.c

uint8_t ismCrc8(const uint8_t* m, unsigned n, uint8_t poly, uint8_t init) {
    uint8_t r = init;
    for (unsigned i = 0; i < n; i++) {
        r ^= m[i];
        for (int b = 0; b < 8; b++) r = (r & 0x80) ? (uint8_t)((r << 1) ^ poly) : (uint8_t)(r << 1);
    }
    return r;
}

uint16_t ismCrc16(const uint8_t* m, unsigned n, uint16_t poly, uint16_t init) {
    uint16_t r = init;
    for (unsigned i = 0; i < n; i++) {
        r ^= (uint16_t)(m[i] << 8);
        for (int b = 0; b < 8; b++) r = (r & 0x8000) ? (uint16_t)((r << 1) ^ poly) : (uint16_t)(r << 1);
    }
    return r;
}

uint8_t ismLfsrDigest8(const uint8_t* m, unsigned n, uint8_t gen, uint8_t key) {
    uint8_t sum = 0;
    for (unsigned k = 0; k < n; k++)
        for (int i = 7; i >= 0; i--) {
            if ((m[k] >> i) & 1) sum ^= key;
            key = (key & 1) ? (uint8_t)((key >> 1) ^ gen) : (uint8_t)(key >> 1);
        }
    return sum;
}

uint8_t ismLfsrDigest8Reflect(const uint8_t* m, int n, uint8_t gen, uint8_t key) {
    uint8_t sum = 0;
    for (int k = n - 1; k >= 0; k--)
        for (int i = 0; i < 8; i++) {
            if ((m[k] >> i) & 1) sum ^= key;
            key = (key & 0x80) ? (uint8_t)((key << 1) ^ gen) : (uint8_t)(key << 1);
        }
    return sum;
}

uint16_t ismLfsrDigest16(const uint8_t* m, unsigned n, uint16_t gen, uint16_t key) {
    uint16_t sum = 0;
    for (unsigned k = 0; k < n; k++)
        for (int i = 7; i >= 0; i--) {
            if ((m[k] >> i) & 1) sum ^= key;
            key = (key & 1) ? (uint16_t)((key >> 1) ^ gen) : (uint16_t)(key >> 1);
        }
    return sum;
}

namespace {

int addBytes(const uint8_t* m, unsigned n) { int r = 0; for (unsigned i = 0; i < n; i++) r += m[i]; return r; }
uint8_t xorBytes(const uint8_t* m, unsigned n) { uint8_t r = 0; for (unsigned i = 0; i < n; i++) r ^= m[i]; return r; }
int parity8(uint8_t b) { b ^= b >> 4; b ^= b >> 2; b ^= b >> 1; return b & 1; }
int parityBytes(const uint8_t* m, unsigned n) { int r = 0; for (unsigned i = 0; i < n; i++) r ^= parity8(m[i]); return r; }
uint8_t reflect4(uint8_t x) {
    x = (uint8_t)(((x & 0xCC) >> 2) | ((x & 0x33) << 2));
    x = (uint8_t)(((x & 0xAA) >> 1) | ((x & 0x55) << 1));
    return x;
}
std::string hexs(unsigned v, int digits) { char b[16]; snprintf(b, sizeof b, "%0*x", digits, v); return b; }
std::string hexU(unsigned v, int digits) { char b[16]; snprintf(b, sizeof b, "%0*X", digits, v); return b; }
std::string decs(long v) { char b[24]; snprintf(b, sizeof b, "%ld", v); return b; }

// ---------------------------------------------------------------- bitbuffer.c

constexpr int kMaxRows = 50;
constexpr unsigned kMaxBits = 4096;

struct Bits {
    std::vector<std::vector<uint8_t>> bb;
    std::vector<unsigned> n;
    Bits() { clear(); }
    int rows() const { return (int)bb.size(); }
    void clear() { bb.assign(1, {}); n.assign(1, 0); }
    bool hasData() const { return n[0] > 0 || rows() > 1; }
    void addBit(int b) {
        unsigned& k = n.back();
        if (k >= kMaxBits) return;
        auto& r = bb.back();
        if ((k >> 3) >= r.size()) r.push_back(0);
        if (b) r[k >> 3] |= (uint8_t)(0x80 >> (k & 7));
        k++;
    }
    void addRow() {                                   // as bitbuffer_add_row: empty rows count
        if (rows() < kMaxRows) { bb.emplace_back(); n.push_back(0); }
        else { bb.back().clear(); n.back() = 0; }
    }
    void addSync() { if (n.back()) addRow(); }
    int bit(int row, unsigned pos) const {
        if (row < 0 || row >= rows() || pos >= n[row]) return 0;
        return (bb[row][pos >> 3] >> (7 - (pos & 7))) & 1;
    }
    uint8_t byteAt(int row, unsigned i) const { return row < rows() && i < bb[row].size() ? bb[row][i] : 0; }
    const uint8_t* row(int r) { bb[r].resize(std::max<size_t>(bb[r].size(), 64), 0); return bb[r].data(); }   // padded with zeros, as rtl_433's fixed rows
    void invert() {
        for (int r = 0; r < rows(); r++) {
            if (!n[r]) continue;
            const unsigned lastCol = (n[r] - 1) / 8, lastBits = (n[r] - 1) % 8 + 1;
            for (unsigned c = 0; c <= lastCol && c < bb[r].size(); c++) bb[r][c] = (uint8_t)~bb[r][c];
            if (lastCol < bb[r].size()) bb[r][lastCol] ^= (uint8_t)(0xFF >> lastBits);
        }
    }
    // bitbuffer_search: position of the pattern in the row, or the row length when absent
    unsigned search(int r, unsigned start, const uint8_t* pat, unsigned plen) const {
        const unsigned len = n[r];
        unsigned ipos = start, ppos = 0;
        while (ipos < len && ppos < plen) {
            if (bit(r, ipos) == ((pat[ppos >> 3] >> (7 - (ppos & 7))) & 1)) {
                ppos++; ipos++;
                if (ppos == plen) return ipos - plen;
            } else { ipos -= ppos; ipos++; ppos = 0; }
        }
        return len;
    }
    void extract(int r, unsigned pos, uint8_t* out, unsigned nbits) const {
        for (unsigned i = 0; i < (nbits + 7) / 8; i++) out[i] = 0;
        for (unsigned i = 0; i < nbits; i++) if (bit(r, pos + i)) out[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
    }
    int repeats(int r) const {
        int c = 0;
        for (int j = 0; j < rows(); j++) {
            if (n[j] != n[r]) continue;
            bool same = true;
            for (unsigned i = 0; i < (n[r] + 7) / 8 && same; i++) same = byteAt(j, i) == byteAt(r, i);
            c += same;
        }
        return c;
    }
    int findRepeatedRow(int minRepeats, unsigned minBits) const {
        for (int i = 0; i < rows(); i++) if (n[i] >= minBits && repeats(i) >= minRepeats) return i;
        return -1;
    }
};

// bitbuffer_manchester_decode: pairs 01 -> 1, 10 -> 0 (the second bit), stops at the first pair without a transition
unsigned manchester(const Bits& in, int row, unsigned start, Bits& out, unsigned max) {
    unsigned len = in.n[row], ipos = start;
    if (max && len > start + max * 2) len = start + max * 2;
    while (ipos < len) {
        const int b1 = in.bit(row, ipos++), b2 = in.bit(row, ipos++);
        if (b1 == b2) break;
        out.addBit(b2);
    }
    return ipos;
}

// bitbuffer_differential_manchester_decode
unsigned diffManchester(const Bits& in, int row, unsigned start, Bits& out, unsigned max) {
    unsigned len = in.n[row], ipos = start;
    int b1, b2 = 0;
    if (max && len > start + max * 2) len = start + max * 2;
    while (ipos < len) {
        b1 = in.bit(row, ipos++); b2 = in.bit(row, ipos++);
        const int b3 = in.bit(row, ipos);
        if (b1 != b2) {
            if (b2 != b3) out.addBit(0);
            else { b2 = b1; ipos -= 1; break; }
        } else { b2 = 1 - b1; ipos -= 2; break; }
    }
    while (ipos < len) {
        b1 = in.bit(row, ipos++);
        if (b1 == b2) break;
        b2 = in.bit(row, ipos++);
        out.addBit(b1 == b2 ? 1 : 0);
    }
    return ipos;
}

// ---------------------------------------------------------------- pulse_slicer.c

thread_local float tlPcmBitUs = 0;   // the bit width slicePcm measured for the rows it hands out

enum Mod { OokPwm, OokPpm, OokPcm, OokMcZero, FskPcm, FskPwm };
struct Timing { float s = 0, l = 0, reset = 0, gap = 0, sync = 0, tol = 0; };
using Emit = std::function<void(Bits&)>;

void slicePcm(const IsmTrain& t, const Timing& d, const Emit& emit) {   // NRZ only (short == long), as every protocol here uses
    const auto& P = t.p;
    const float sl = d.s, tol = d.tol > 0 ? d.tol : d.l / 4;
    float T = sl;
    int minCount = 12, pre = 0;
    for (size_t n = 0; n < P.size(); n++) {   // the exact bit width from a preamble of single bits
        float w = 0; int c = 0;
        while (n < P.size() && (int)(P[n].pulseUs / T + 0.5f) == 1 && (int)(P[n].gapUs / T + 0.5f) == 1) { w += P[n].pulseUs + P[n].gapUs; c += 2; n++; }
        if (c >= minCount) { T = w / (float)c; minCount = c; pre = c; }
    }
    if (!pre) {                               // or measured over all single and double bits
        float w = 0; int c = 0;
        for (const auto& q : P) {
            if (std::fabs(q.pulseUs - sl) <= tol) { w += q.pulseUs; c += 1; }
            if (std::fabs(q.pulseUs - 2 * sl) <= tol) { w += q.pulseUs; c += 2; }
            if (std::fabs(q.gapUs - sl) <= tol) { w += q.gapUs; c += 1; }
            if (std::fabs(q.gapUs - 2 * sl) <= tol) { w += q.gapUs; c += 2; }
        }
        if (c > 20) T = w / (float)c;
    }
    tlPcmBitUs = T;
    const float gapLimit = d.gap > 0 ? d.gap : d.reset;
    const int maxZeros = (int)(gapLimit / T);
    Bits bits;
    for (size_t n = 0; n < P.size(); n++) {
        const int highs = (int)(P[n].pulseUs / T + 0.5f);
        int lows = std::min((int)(P[n].gapUs / T + 0.5f), maxZeros);
        for (int i = 0; i < highs; i++) bits.addBit(1);
        for (int i = 0; i < lows; i++) bits.addBit(0);
        if (P[n].gapUs > gapLimit && P[n].gapUs <= d.reset) bits.addRow();
        if ((n == P.size() - 1 || P[n].gapUs > d.reset) && bits.hasData()) { emit(bits); bits.clear(); }
    }
}

void slicePpm(const IsmTrain& t, const Timing& d, const Emit& emit) {
    float zl, zu, ol, ou, syl = 0, syu = 0;
    if (d.tol > 0) {
        zl = d.s - d.tol; zu = d.s + d.tol; ol = d.l - d.tol; ou = d.l + d.tol;
        if (d.sync > 0) { syl = d.sync - d.tol; syu = d.sync + d.tol; }
    } else { zl = 0; zu = (d.s + d.l) / 2 + 1; ol = zu - 1; ou = d.gap > 0 ? d.gap : d.reset; }
    Bits bits;
    const auto& P = t.p;
    for (size_t n = 0; n < P.size(); n++) {
        const float g = P[n].gapUs;
        if (g > zl && g < zu) bits.addBit(0);
        else if (g > ol && g < ou) bits.addBit(1);
        else if (g > syl && g < syu) bits.addSync();
        else if (g < d.reset) bits.addRow();
        if ((n == P.size() - 1 || g >= d.reset) && bits.hasData()) { emit(bits); bits.clear(); }
    }
}

void slicePwm(const IsmTrain& t, const Timing& d, const Emit& emit) {
    float ol, ou, zl, zu, syl = 0, syu = 0;
    const float s = d.s, l = d.l, sy = d.sync;
    if (d.tol > 0) {
        ol = s - d.tol; ou = s + d.tol; zl = l - d.tol; zu = l + d.tol;
        if (sy > 0) { syl = sy - d.tol; syu = sy + d.tol; }
    } else if (sy <= 0) { ol = 0; ou = (s + l) / 2 + 1; zl = ou - 1; zu = 1e9f; }
    else if (sy < s) { syl = 0; syu = (sy + s) / 2 + 1; ol = syu - 1; ou = (s + l) / 2 + 1; zl = ou - 1; zu = 1e9f; }
    else if (sy < l) { ol = 0; ou = (s + sy) / 2 + 1; syl = ou - 1; syu = (sy + l) / 2 + 1; zl = syu - 1; zu = 1e9f; }
    else { ol = 0; ou = (s + l) / 2 + 1; zl = ou - 1; zu = (l + sy) / 2 + 1; syl = zu - 1; syu = 1e9f; }
    Bits bits;
    const auto& P = t.p;
    for (size_t n = 0; n < P.size(); n++) {
        const float p = P[n].pulseUs;
        if (p > ol && p < ou) bits.addBit(1);
        else if (p > zl && p < zu) bits.addBit(0);
        else if (p > syl && p < syu) bits.addSync();
        else if (p <= ol) {}
        else bits.addRow();
        if (n == P.size() - 1 || P[n].gapUs > d.reset) { if (bits.hasData()) emit(bits); bits.clear(); }
        else if (d.gap > 0 && P[n].gapUs > d.gap && bits.n.back() > 0) bits.addRow();
    }
}

void sliceMcZero(const IsmTrain& t, const Timing& d, const Emit& emit) {
    const float s = d.s;
    float since = 0;
    Bits bits;
    bits.addBit(0);
    const auto& P = t.p;
    for (size_t n = 0; n < P.size(); n++) {
        if (P[n].pulseUs + since > s * 1.5f) { bits.addBit(1); since = 0; }
        else since += P[n].pulseUs;
        if (n == P.size() - 1 || P[n].gapUs > d.reset) {
            emit(bits); bits.clear(); bits.addBit(0); since = 0;
        } else if (P[n].gapUs + since > s * 1.5f) { bits.addBit(0); since = 0; }
        else since += P[n].gapUs;
    }
}

// ---------------------------------------------------------------- decoders

using Out = std::vector<IsmReading>;

IsmReading mk(const char* model, const char* kind, const std::string& id, const char* mic) {
    IsmReading r; r.model = model; r.kind = kind; r.id = id; r.mic = mic; return r;
}

// nexus.c: Nexus, FreeTec NC-7345, NX-3980, Solight TE82S, TFA 30.3209 (36 bits PPM, no check: a row repeated 3 times)
void decNexus(Bits& B, Out& out) {
    const int r = B.findRepeatedRow(3, 36);
    if (r < 0 || B.n[r] > 37) return;
    const uint8_t* b = B.row(r);
    if ((b[3] & 0xf0) != 0xf0) return;
    if ((b[0] == 0 && b[2] == 0 && b[3] == 0) || (b[0] == 0xff && b[2] == 0xff && b[3] == 0xff)) return;
    if ((b[1] & 0x30) == 0x30) return;
    uint8_t c[5] = {b[0], b[1], b[2], (uint8_t)(b[3] & 0xf0), (uint8_t)((b[3] & 0x0f) << 4 | (b[4] & 0xf0) >> 4)};
    if (ismCrc8(c, 5, 0x31, 0x6c) == 0) return;   // a Rubicson frame
    const int hum = ((b[3] & 0x0F) << 4) | (b[4] >> 4);
    if (hum != 0 && hum > 100) return;
    const int traw = (int16_t)((b[1] << 12) | (b[2] << 4));
    IsmReading R = mk(hum ? "Nexus-TH" : "Nexus-T", "Weather", decs(b[0]), "REPEAT");
    R.channel = ((b[1] & 0x30) >> 4) + 1;
    R.battery = (b[1] & 0x80) ? 1 : 0;
    R.add("Temperature", (traw >> 4) * 0.1, "C");
    if (hum) R.add("Humidity", hum, "%", 0);
    out.push_back(R);
}

// rubicson.c: Rubicson, TFA 30.3197, InFactory PT-310 (36 bits PPM, CRC-8 poly 0x31 init 0x6c)
void decRubicson(Bits& B, Out& out) {
    const int r = B.findRepeatedRow(3, 36);
    if (r < 0 || B.n[r] < 36 || B.n[r] > 38) return;
    const uint8_t* b = B.row(r);
    if ((b[3] & 0xf0) != 0xf0) return;
    uint8_t c[5] = {b[0], b[1], b[2], (uint8_t)(b[3] & 0xf0), (uint8_t)((b[3] & 0x0f) << 4 | (b[4] & 0xf0) >> 4)};
    if (ismCrc8(c, 5, 0x31, 0x6c)) return;
    const int traw = (int16_t)((b[1] << 12) | (b[2] << 4));
    IsmReading R = mk("Rubicson-Temperature", "Weather", decs(b[0]), "CRC");
    R.channel = ((b[1] & 0x30) >> 4) + 1;
    R.battery = (b[1] & 0x80) ? 1 : 0;
    R.add("Temperature", (traw >> 4) * 0.1, "C");
    out.push_back(R);
}

// generic_temperature_sensor.c: Generic temperature sensor 1 (10 frames of 24 bits PPM, no check: 9 rows of exactly 24 bits)
void decGenericTemp(Bits& B, Out& out) {
    if (B.rows() < 10) return;
    for (int i = 1; i < 10; i++) if (B.n[i] != 24) return;
    const uint8_t* b = B.row(1);
    if ((b[0] == 0 && b[1] == 0 && b[2] == 0) || (b[0] == 0xff && b[1] == 0xff && b[2] == 0xff)) return;
    if (B.findRepeatedRow(3, 24) < 0) return;   // stricter than rtl_433: the frame must repeat (there is no check)
    const int traw = (int16_t)(((b[1] & 0x3f) << 10) | (b[2] << 2));
    IsmReading R = mk("Generic-Temperature", "Weather", decs(b[0]), "REPEAT");
    R.add("Temperature", (traw >> 4) * 0.1, "C", 2);
    R.add("Battery?", (b[1] & 0xC0) >> 6, "", 0);
    out.push_back(R);
}

// lacrosse_tx141x.c: LaCrosse TX141-Bv2, TX141TH-Bv2, TX141-Bv3, TX141W, TFA 30.3221.02 (PWM)
void decTx141(Bits& B, Out& out) {
    B.invert();
    int r = B.findRepeatedRow(B.rows() > 5 ? 5 : 3, 32);
    if (r < 0) r = B.findRepeatedRow(2, 64);
    if (r < 0 && B.rows() <= 4)
        for (int row = 0; row < B.rows(); row++)
            if ((B.n[row] == 40 || B.n[row] == 41) && ismLfsrDigest8Reflect(B.row(row), 4, 0x31, 0xf4) == B.row(row)[4]) { r = row; break; }
    if (r < 0) return;
    const unsigned nb = B.n[r];
    enum { B32, B37, TH, BV3, W } dev;
    if (nb >= 64) dev = W;
    else if (nb > 41) return;
    else if (nb >= 41) { if (B.rows() > 12) return; dev = TH; }
    else if (nb >= 40) dev = TH;
    else if (nb >= 37) dev = B37;
    else if (nb == 32) dev = B32;
    else dev = BV3;
    const uint8_t* b = B.row(r);
    if (dev == W) {
        if ((b[0] >> 3) != 0x01 || ismCrc8(b, 8, 0x31, 0x00)) return;
        const int id = ((b[0] & 0x07) << 16) | (b[1] << 8) | b[2], type = b[3] & 0x0f;
        const int traw = (b[4] << 4) | (b[5] >> 4), hum = ((b[5] & 0x0f) << 8) | b[6];
        IsmReading R = mk("LaCrosse-TX141W", "Weather", hexs(id, 5), "CRC");
        R.channel = (b[3] & 0x30) >> 4; R.battery = !(b[3] >> 7);
        if (type == 1) { R.add("Temperature", (traw - 500) * 0.1, "C"); R.add("Humidity", hum, "%", 0); }
        else if (type == 2) { R.add("Wind speed", traw * 0.1, "km/h"); R.add("Wind direction", hum, "deg", 0); }
        else return;
        out.push_back(R);
        return;
    }
    const int id = b[0];
    const int batLow = dev == TH ? (b[1] >> 7) : !(b[1] >> 7);
    const int traw = ((b[1] & 0x0F) << 8) | b[2];
    const double tc = (traw - 500) * 0.1;
    const int hum = dev == TH ? b[3] : 0;
    if (id == 0 || (dev == TH && (hum == 0 || hum > 100)) || tc < -40.0 || tc > 140.0) return;
    if (dev == TH && ismLfsrDigest8Reflect(b, 4, 0x31, 0xf4) != b[4]) return;
    IsmReading R = mk(dev == B32 ? "LaCrosse-TX141B" : dev == B37 ? "LaCrosse-TX141Bv2" : dev == BV3 ? "LaCrosse-TX141Bv3" : "LaCrosse-TX141THBv2",
                      "Weather", hexs(id, 2), dev == TH ? "CRC" : "REPEAT");
    if (dev != B32) R.channel = (b[1] & 0x30) >> 4;
    R.battery = !batLow;
    R.add("Temperature", tc, "C");
    if (dev == TH) R.add("Humidity", hum, "%", 0);
    out.push_back(R);
}

// acurite.c: Acurite 592TXR tower sensor and 5-in-1 (Iris) weather station (PWM, add checksum and byte parity)
const int kAcurite5n1Dir[16] = {14, 11, 13, 12, 15, 10, 0, 9, 3, 6, 4, 5, 2, 7, 1, 8};
bool acuriteCheck(const uint8_t* bb, int browlen, int explen) {
    if (browlen < 6 || browlen < explen) return false;
    if ((addBytes(bb, explen - 1) & 0xff) != bb[explen - 1]) return false;
    if (parityBytes(&bb[2], explen - 3)) return false;
    return ((bb[0] & 0xC0) >> 6) != 1;
}
void decAcurite(Bits& B, Out& out) {
    B.invert();
    static const char* const chan[4] = {"C", "E", "B", "A"};
    for (int r = 0; r < B.rows(); r++) {
        const int browlen = (int)(B.n[r] / 8);
        if (browlen < 6 || browlen > 10) continue;
        const uint8_t* bb = B.row(r);
        if (bb[0] == 0 && bb[1] == 0 && bb[2] == 0 && bb[browlen - 1] == 0) continue;
        const int mt = bb[2] & 0x3f;
        if (mt == 0x04) {
            if (!acuriteCheck(bb, browlen, 7)) continue;
            const int hum = bb[3] & 0x7f;
            if (hum > 100 && hum != 127) continue;
            const double tc = ((((bb[4] & 0x7F) << 7) | (bb[5] & 0x7F)) - 1000) * 0.1;
            if (tc < -40 || tc > 70) continue;
            IsmReading R = mk("Acurite-Tower", "Weather", decs(((bb[0] & 0x3f) << 8) | bb[1]), "CHECKSUM");
            R.channelName = chan[(bb[0] & 0xC0) >> 6];
            R.battery = (bb[2] & 0x40) != 0;
            R.add("Temperature", tc, "C");
            if (hum != 127) R.add("Humidity", hum, "%", 0);
            out.push_back(R);
        } else if (mt == 0x31 || mt == 0x38) {
            if (!acuriteCheck(bb, browlen, 8)) continue;
            IsmReading R = mk("Acurite-5n1", "Weather", decs(((bb[0] & 0x0f) << 8) | bb[1]), "CHECKSUM");
            R.channelName = chan[(bb[0] & 0xC0) >> 6];
            R.battery = (bb[2] & 0x40) != 0;
            const int wraw = ((bb[3] & 0x1F) << 3) | ((bb[4] & 0x70) >> 4);
            const double wkph = wraw > 0 ? wraw * 0.8278 + 1.0 : 0.0;
            R.add("Wind speed", wkph, "km/h");
            if (mt == 0x31) {
                R.add("Wind direction", kAcurite5n1Dir[bb[4] & 0x0f] * 22.5, "deg");
                R.add("Rain", (((bb[5] & 0x7f) << 7) | (bb[6] & 0x7F)) * 0.01, "in", 2);
            } else {
                const double tf = ((((bb[4] & 0x0F) << 7) | (bb[5] & 0x7F)) - 400) * 0.1;
                const int hum = bb[6] & 0x7f;
                if (tf < -40.0 || tf > 158.0 || hum > 100) continue;
                R.add("Temperature", tf, "F");
                R.add("Humidity", hum, "%", 0);
            }
            out.push_back(R);
        }
    }
}

// fineoffset_wh1080.c: Fine Offset WH1080 / WH3080 weather message (OOK PWM, CRC-8 poly 0x31 init 0xff over the preamble byte too)
const int kWh1080Dir[16] = {0, 23, 45, 68, 90, 113, 135, 158, 180, 203, 225, 248, 270, 293, 315, 338};
void decWh1080(Bits& B, Out& out) {
    if (B.rows() != 1) return;
    uint8_t buf[16] = {};
    const uint8_t* br;
    if (B.n[0] >= 88 && B.n[0] < 100) br = B.row(0);
    else if (B.n[0] == 87) { B.extract(0, 7, buf + 1, 80); buf[0] = (uint8_t)((B.row(0)[0] >> 1) | 0x80); br = buf; }
    else return;
    if (br[0] != 0xff || ismCrc8(br, 11, 0x31, 0xff)) return;
    if ((br[1] >> 4) != 0x0a) return;   // the weather message; time and UV messages are not shown
    IsmReading R = mk("Fineoffset-WHx080", "Weather", decs((br[1] << 4 & 0xf0) | (br[2] >> 4)), "CRC");
    R.battery = (br[9] >> 4) != 1;
    R.add("Temperature", ((((br[2] & 0x03) << 8) | br[3]) - 400) * 0.1, "C");
    R.add("Humidity", br[4], "%", 0);
    R.add("Wind direction", kWh1080Dir[br[9] & 0x0f], "deg", 0);
    R.add("Wind speed", br[5] * 0.34 * 3.6, "km/h", 2);
    R.add("Wind gust", br[6] * 0.34 * 3.6, "km/h", 2);
    R.add("Rain", (((br[7] & 0x0f) << 8) | br[8]) * 0.3, "mm");
    out.push_back(R);
}

// ambient_weather.c: Ambient Weather F007TH, F012TH, TFA 30.3208.02, SwitchDoc F016TH (Manchester, LFSR digest)
void decF007th(Bits& B, Out& out) {
    static const uint8_t pre[2] = {0x01, 0x45}, inv[2] = {0xfd, 0x45};
    auto one = [&](int row, unsigned pos) -> bool {
        uint8_t b[6];
        B.extract(row, pos, b, 48);
        if ((uint8_t)(ismLfsrDigest8(b, 5, 0x98, 0x3e) ^ 0x64) != b[5]) return false;
        const int hum = b[4], traw = ((b[2] & 0x0f) << 8) | b[3];
        const double tf = (traw - 400) * 0.1;
        if (hum > 100 || tf < -40.0 || tf >= 344.0) return false;
        IsmReading R = mk("Ambientweather-F007TH", "Weather", decs(b[1]), "CRC");
        R.channel = ((b[2] & 0x70) >> 4) + 1;
        R.battery = (b[2] & 0x80) == 0;
        R.add("Temperature", tf, "F");
        R.add("Humidity", hum, "%", 0);
        out.push_back(R);
        return true;
    };
    for (int row = 0; row < B.rows(); row++) {
        unsigned pos = 0;
        while ((pos = B.search(row, pos, pre, 12)) + 8 + 48 <= B.n[row]) { if (one(row, pos + 8)) return; pos += 16; }
        pos = 0;
        while ((pos = B.search(row, pos, inv, 12)) + 8 + 48 <= B.n[row]) { if (one(row, pos + 8)) return; pos += 15; }
    }
}

// oregon_scientific.c: v2.1 THGR122N / THGR228N / THGR968 and v3 THGR810 (Manchester, sum of nibbles)
double osTemp(const uint8_t* m) {
    double t = (((m[5] >> 4) * 100) + ((m[4] & 0x0f) * 10) + ((m[4] >> 4) & 0x0f)) / 10.0;
    t += (m[5] & 0x07) * 100.0;
    return (m[5] & 0x08) ? -t : t;
}
int osHum(const uint8_t* m) { return ((m[6] & 0x0f) * 10) + (m[6] >> 4); }
bool osChecksum(const uint8_t* m, int idx) {
    unsigned sum = 0, chk;
    for (int i = 0; i < idx - 1; i += 2) sum += (m[i >> 1] >> 4) + (m[i >> 1] & 0x0f);
    if (idx & 1) { sum += m[idx >> 1] >> 4; chk = (m[idx >> 1] & 0x0f) | (m[(idx + 1) >> 1] & 0xf0); }
    else chk = (m[idx >> 1] >> 4) | ((m[idx >> 1] & 0x0f) << 4);
    return (sum & 0xff) == chk;
}
void decOregon(Bits& B, Out& out) {
    const uint8_t* b = B.row(0);
    // v2.1
    if (((b[1] == 0x55 && b[2] == 0x55) || (b[1] == 0xAA && b[2] == 0xAA))) {
        Bits db;
        const unsigned sync = ((unsigned)b[3] << 24) | (b[4] << 16) | (b[5] << 8) | b[6];
        bool found = false;
        for (int k = 0; k < 8; k++) {
            const unsigned mask = 0xffff0000u >> k, p1 = 0x55990000u >> k, p2 = 0xaa990000u >> k;
            if ((sync & mask) != p1 && (sync & mask) != p2) continue;
            manchester(B, 0, k + 40, db, 173);
            found = true;
            break;
        }
        if (!found) return;
        const int bits = (int)db.n[0];
        uint8_t* m = const_cast<uint8_t*>(db.row(0));
        for (int i = 0; i < (bits + 7) / 8; i++) m[i] = reflect4(m[i]);
        const int sid = (m[0] << 8) | m[1];
        if (sid == 0x1d20 || sid == 0x1d30) {
            if (!((bits == 68 || bits == 76) && osChecksum(m, 15))) return;
            IsmReading R = mk(sid == 0x1d30 ? "Oregon-THGR968" : bits == 76 ? "Oregon-THGR122N" : "Oregon-THGR228N", "Weather",
                              decs((m[2] & 0x0f) | (m[3] & 0xf0)), "CHECKSUM");
            R.channel = (m[2] >> 4) & 0x0f;
            R.battery = !((m[3] >> 2) & 1);
            R.add("Temperature", osTemp(m), "C", 2);
            R.add("Humidity", osHum(m), "%", 0);
            out.push_back(R);
        }
        return;
    }
    // v3
    if ((((b[0] & 0xf) != 0x0f) || (b[1] != 0xff) || ((b[2] & 0xc0) != 0xc0)) && (((b[0] & 0xf) != 0x00) || (b[1] != 0x00) || ((b[2] & 0xc0) != 0x00))) return;
    static const uint8_t pat[2] = {0x00, 0x05};
    const unsigned pos = B.search(0, 0, pat, 16) + 16;
    if (pos > B.n[0] || B.n[0] - pos < 56) return;
    const unsigned len = B.n[0] - pos;
    if (len > 44 * 8) return;
    uint8_t m[44] = {};
    B.extract(0, pos, m, len);
    for (unsigned i = 0; i < (len + 7) / 8; i++) m[i] = reflect4(m[i]);
    const int sid = (m[0] << 8) | m[1];
    if ((sid & 0xf0ff) == 0xf024 || sid == 0xf8b4) {
        if (!osChecksum(m, 15)) return;
        if (((m[5] >> 4) & 0x0F) > 9 || (m[4] & 0x0F) > 9 || ((m[4] >> 4) & 0x0F) > 9 || (m[6] & 0x0F) > 9 || ((m[6] >> 4) & 0x0F) > 9) return;
        const double tc = osTemp(m);
        if (tc > 70 || tc < -50) return;
        IsmReading R = mk("Oregon-THGR810", "Weather", decs((m[2] & 0x0f) | (m[3] & 0xf0)), "CHECKSUM");
        R.channel = (m[2] >> 4) & 0x0f;
        R.battery = !((m[3] >> 2) & 1);
        R.add("Temperature", tc, "C", 2);
        R.add("Humidity", osHum(m), "%", 0);
        out.push_back(R);
    }
}

// schraeder.c: Schrader TPMS (FCC MRXGG4, 68 bits Manchester, CRC-8 poly 0x07 init 0xf0)
void decSchrader(Bits& B, Out& out) {
    if (B.n[0] != 68) return;
    uint8_t b[8];
    B.extract(0, 4, b, 64);
    if (b[7] != ismCrc8(b, 7, 0x07, 0xf0)) return;
    IsmReading R = mk("Schrader", "TPMS", hexU((b[1] & 0x0F) << 24 | b[2] << 16 | b[3] << 8 | b[4], 7), "CRC");
    R.add("Pressure", b[5] * 25 * 0.1, "kPa");
    R.add("Temperature", b[6] - 50, "C", 0);
    R.addText("Flags", hexs((b[0] & 0x0F) << 4 | b[1] >> 4, 2));
    out.push_back(R);
}

// schraeder.c: Schrader EG53MA4 (Opel, Saab, Chevrolet; 120 bits Manchester, add checksum)
void decSchraderEg53(Bits& B, Out& out) {
    if (B.n[0] != 120) return;
    uint8_t b[10];
    B.extract(0, 40, b, 80);
    if (!b[1] && !b[2] && !b[4] && !b[5] && !b[7] && !b[8]) return;
    if ((addBytes(b, 9) & 0xff) != b[9]) return;
    IsmReading R = mk("Schrader-EG53MA4", "TPMS", hexU((b[4] << 16) | (b[5] << 8) | b[6], 6), "CHECKSUM");
    R.add("Pressure", b[7] * 25 * 0.1, "kPa");
    R.add("Temperature", b[8], "F", 0);
    out.push_back(R);
}

// generic_remote.c: PT2260 / PT2262 / SC2260 / EV1527 remotes and sensors (25 bits PWM; no check, so the code must come twice)
void decGenericRemote(Bits& B, Out& out, std::vector<uint32_t>& seen) {
    if (B.n[0] != 25) return;
    uint8_t b[4] = {(uint8_t)~B.row(0)[0], (uint8_t)~B.row(0)[1], (uint8_t)~B.row(0)[2], B.row(0)[3]};
    if ((b[3] & 0x80) == 0 || (b[0] == 0 && b[1] == 0) || b[2] == 0) return;
    const uint32_t full = (uint32_t)b[0] << 16 | b[1] << 8 | b[2];
    if (std::find(seen.begin(), seen.end(), full) == seen.end()) { seen.push_back(full); return; }   // the first frame of a burst
    for (const auto& r : out) if (r.model == "Generic-Remote" && r.id == decs(b[0] << 8 | b[1])) return;
    char tri[13];
    for (int i = 22, k = 0; i >= 0; i -= 2, k++) tri[k] = "0ZX1"[(full >> i) & 3];
    tri[12] = 0;
    IsmReading R = mk("Generic-Remote", "Remote", decs(b[0] << 8 | b[1]), "REPEAT");
    R.addText("Code", hexs(full, 6));
    R.add("Command", b[2], "", 0);
    R.addText("Tri-state", tri);
    out.push_back(R);
}

// bresser_5in1.c: Bresser Weather Center 5-in-1 (FSK, 26 bytes: the first 13 inverted copies of the last 13)
int bcd(int x) { return (x >> 4) * 10 + (x & 0xf); }
void decBresser5in1(Bits& B, Out& out) {
    static const uint8_t pre[5] = {0xaa, 0xaa, 0xaa, 0x2d, 0xd4};
    if (B.rows() != 1 || B.n[0] < 248 || B.n[0] > 440) return;
    unsigned sp = B.search(0, 0, pre, 40);
    if (sp == B.n[0]) return;
    sp += 40;
    unsigned len = B.n[0] - sp;
    uint8_t msg[26];
    if ((len + 7) / 8 < sizeof msg) return;
    len = std::min<unsigned>(len, sizeof msg * 8);
    B.extract(0, sp, msg, len);
    for (int c = 0; c < 13; c++) if ((msg[c] ^ msg[c + 13]) != 0xff) return;
    // uu: the number of set bits in bytes 14 to 25, as the file documents (its examples agree; rtl_433 does not test it). Without it a
    // message shifted by a bit after the sync word still passes the inverted copy about half the time.
    int ones = 0;
    for (int i = 14; i < 26; i++) for (int k = 0; k < 8; k++) ones += (msg[i] >> k) & 1;
    if (ones != msg[13]) return;
    const bool tok = (msg[20] & 0x0f) <= 9, hok = (msg[22] & 0x0f) <= 9;
    int traw = (msg[20] & 0x0f) + ((msg[20] & 0xf0) >> 4) * 10 + (msg[21] & 0x0f) * 100;
    if (msg[25] & 0x0f) traw = -traw;
    const int hum = (msg[22] & 0x0f) + ((msg[22] & 0xf0) >> 4) * 10;
    const int stype = msg[15] & 0x7f;
    double rain = ((msg[23] & 0x0f) + ((msg[23] & 0xf0) >> 4) * 10 + (msg[24] & 0x0f) * 100 + ((msg[24] & 0xf0) >> 4) * 1000) * 0.1;
    const bool gauge = stype >= 0x39 && stype <= 0x3b;
    IsmReading R = mk(gauge ? "Bresser-ProRainGauge" : "Bresser-5in1", "Weather", decs(msg[14]), "CHECKSUM");
    R.battery = !(msg[25] & 0x80);
    if (tok) R.add("Temperature", traw * 0.1, "C");
    if (gauge) { R.add("Rain", rain * 2.5, "mm"); out.push_back(R); return; }
    if (hok) R.add("Humidity", hum, "%", 0);
    R.add("Wind gust", (((msg[17] & 0x0f) << 8) + msg[16]) * 0.1, "m/s");
    R.add("Wind speed", ((msg[18] & 0x0f) + ((msg[18] & 0xf0) >> 4) * 10 + (msg[19] & 0x0f) * 100) * 0.1, "m/s");
    R.add("Wind direction", ((msg[17] & 0xf0) >> 4) * 22.5, "deg");
    R.add("Rain", rain, "mm");
    out.push_back(R);
}

// bresser_6in1.c: Bresser 6-in-1, 7-in-1 indoor, new 5-in-1, 3-in-1 wind, soil, thermo-hygro, pool (FSK, LFSR-16 digest and add checksum)
void decBresser6in1(Bits& B, Out& out) {
    static const uint8_t pre[4] = {0xaa, 0xaa, 0x2d, 0xd4};
    static const int moist[16] = {0, 7, 13, 20, 27, 33, 40, 47, 53, 60, 67, 73, 80, 87, 93, 99};
    if (B.rows() != 1 || B.n[0] < 160 || B.n[0] > 440) return;
    const unsigned sp = B.search(0, 0, pre, 32) + 32;
    if (sp >= B.n[0] || B.n[0] - sp < 18 * 8) return;
    uint8_t msg[18];
    B.extract(0, sp, msg, 144);
    if (((msg[0] << 8) | msg[1]) != ismLfsrDigest16(&msg[2], 15, 0x8810, 0x5412)) return;
    if ((addBytes(&msg[2], 16) & 0xff) != 0xff) return;
    const uint32_t id = ((uint32_t)msg[2] << 24) | (msg[3] << 16) | (msg[4] << 8) | msg[5];
    const int stype = msg[6] >> 4, chan = msg[6] & 7, bat = (msg[13] >> 1) & 1;
    const bool tok = msg[12] <= 0x99 && (msg[13] & 0xf0) <= 0x90;
    const int traw = (msg[12] >> 4) * 100 + (msg[12] & 0x0f) * 10 + (msg[13] >> 4);
    double tc = ((msg[13] >> 3) & 1) ? (traw - 1000) * 0.1 : traw * 0.1;
    if (tc < -50.0) tc = -traw * 0.1;
    const int hum = (msg[14] >> 4) * 10 + (msg[14] & 0x0f);
    bool uvok = (msg[16] & 0x0f) == 0 && (~msg[15] & 0xff) <= 0x99 && (~msg[16] & 0xf0) <= 0x90;
    const int uvraw = ((~msg[15] & 0xf0) >> 4) * 100 + (~msg[15] & 0x0f) * 10 + ((~msg[16] & 0xf0) >> 4);
    uint8_t w7 = (uint8_t)~msg[7], w8 = (uint8_t)~msg[8], w9 = (uint8_t)~msg[9];
    bool wok = w7 <= 0x99 && w8 <= 0x99 && w9 <= 0x99;
    const int gust = (w7 >> 4) * 100 + (w7 & 0x0f) * 10 + (w8 >> 4), wavg = (w9 >> 4) * 100 + (w9 & 0x0f) * 10 + (w8 & 0x0f);
    const int wdir = ((msg[10] & 0xf0) >> 4) * 100 + (msg[10] & 0x0f) * 10 + ((msg[11] & 0xf0) >> 4);
    const uint8_t r12 = (uint8_t)~msg[12], r13 = (uint8_t)~msg[13], r14 = (uint8_t)~msg[14];
    const bool rok = msg[16] & 1;
    const int rraw = (r12 >> 4) * 100000 + (r12 & 0x0f) * 10000 + (r13 >> 4) * 1000 + (r13 & 0x0f) * 100 + (r14 >> 4) * 10 + (r14 & 0x0f);
    if (stype == 2 || stype == 4) { wok = false; uvok = false; }
    int mo = -1;
    if (stype == 4 && tok && hum >= 1 && hum <= 16) mo = moist[hum - 1];
    IsmReading R = mk("Bresser-6in1", "Weather", hexs(id, 8), "CRC");
    R.channel = chan;
    if (!rok) R.battery = bat;
    if (tok) R.add("Temperature", tc, "C");
    if (tok && mo < 0) R.add("Humidity", hum, "%", 0);
    if (mo >= 0) R.add("Moisture", mo, "%", 0);
    if (wok) { R.add("Wind gust", gust * 0.1, "m/s"); R.add("Wind speed", wavg * 0.1, "m/s"); R.add("Wind direction", wdir, "deg", 0); }
    if (rok) R.add("Rain", rraw * 0.1, "mm");
    if (uvok) R.add("UV index", uvraw * 0.1, "");
    R.add("Sensor type", stype, "", 0);
    out.push_back(R);
}

// bresser_7in1.c: Bresser 7-in-1 / 3-in-1 / 8-in-1 weather, PM2.5/PM10, CO2, HCHO/VOC (FSK, whitened 0xaa, LFSR-16 digest)
void decBresser7in1(Bits& B, Out& out) {
    static const uint8_t pre[5] = {0xaa, 0xaa, 0xaa, 0x2d, 0xd4};
    if (B.rows() != 1 || B.n[0] < 160) return;
    const unsigned sp = B.search(0, 0, pre, 40) + 40;
    if (sp >= B.n[0] || sp + 21 * 8 >= B.n[0]) return;
    uint8_t msg[25];
    B.extract(0, sp, msg, 200);
    if (msg[21] == 0x00) return;
    const int stype = msg[6] >> 4, nstartup = (msg[6] & 0x08) >> 3, chan = msg[6] & 0x07;
    (void)nstartup;
    for (auto& v : msg) v ^= 0xaa;
    if ((((msg[0] << 8) | msg[1]) ^ ismLfsrDigest16(&msg[2], 23, 0x8810, 0xba95)) != 0x6df1) return;
    const int id = (msg[2] << 8) | msg[3];
    const int batLow = (msg[15] & 0x06) == 0x06;
    if (stype == 1 || stype == 12 || stype == 13) {
        const int wdir = (msg[4] >> 4) * 100 + (msg[4] & 0x0f) * 10 + (msg[5] >> 4);
        const int wg = (msg[7] >> 4) * 100 + (msg[7] & 0x0f) * 10 + (msg[8] >> 4), wa = (msg[8] & 0x0f) * 100 + (msg[9] >> 4) * 10 + (msg[9] & 0x0f);
        const int rr = (msg[10] >> 4) * 100000 + (msg[10] & 0x0f) * 10000 + (msg[11] >> 4) * 1000 + (msg[11] & 0x0f) * 100 + (msg[12] >> 4) * 10 + (msg[12] & 0x0f);
        const int traw = (msg[14] >> 4) * 100 + (msg[14] & 0x0f) * 10 + (msg[15] >> 4);
        const double tc = traw > 600 ? (traw - 1000) * 0.1 : traw * 0.1;
        const int hum = (msg[16] >> 4) * 10 + (msg[16] & 0x0f);
        const int lux = (msg[17] >> 4) * 100000 + (msg[17] & 0x0f) * 10000 + (msg[18] >> 4) * 1000 + (msg[18] & 0x0f) * 100 + (msg[19] >> 4) * 10 + (msg[19] & 0x0f);
        const int uv = (msg[20] >> 4) * 100 + (msg[20] & 0x0f) * 10 + (msg[21] >> 4);
        IsmReading R = mk("Bresser-7in1", "Weather", decs(id), "CRC");
        R.battery = !batLow;
        R.add("Temperature", tc, "C");
        R.add("Humidity", hum, "%", 0);
        if (stype != 12) { R.add("Wind gust", wg * 0.1, "m/s"); R.add("Wind speed", wa * 0.1, "m/s"); R.add("Wind direction", wdir, "deg", 0); }
        R.add("Rain", rr * 0.1, "mm");
        if (stype != 12) { R.add("Light", lux, "lux", 0); R.add("UV index", uv * 0.1, ""); }
        if (stype == 13 && (msg[23] >> 4) < 10) R.add("Globe temperature", (msg[22] >> 4) * 10 + (msg[22] & 0x0f) + (msg[23] >> 4) * 0.1, "C");
        out.push_back(R);
    } else if (stype == 8) {
        IsmReading R = mk("Bresser-7in1", "Sensor", decs(id), "CRC");
        R.channel = chan; R.battery = !batLow;
        if ((msg[10] & 0x0f) != 0x0f) R.add("PM2.5", (msg[10] & 0x0f) * 1000 + (msg[11] >> 4) * 100 + (msg[11] & 0x0f) * 10 + (msg[12] >> 4), "ug/m3", 0);
        if ((msg[12] & 0x0f) != 0x0f) R.add("PM10", (msg[12] & 0x0f) * 1000 + (msg[13] >> 4) * 100 + (msg[13] & 0x0f) * 10 + (msg[14] >> 4), "ug/m3", 0);
        out.push_back(R);
    } else if (stype == 10 || stype == 11) {
        IsmReading R = mk(stype == 10 ? "Bresser-CO2" : "Bresser-HCHOVOC", "Sensor", decs(id), "CRC");
        R.channel = chan; R.battery = !batLow;
        const int v = ((msg[4] & 0xf0) >> 4) * 1000 + (msg[4] & 0x0f) * 100 + ((msg[5] & 0xf0) >> 4) * 10 + (msg[5] & 0x0f);
        if ((msg[5] & 0x0f) != 0x0f) R.add(stype == 10 ? "CO2" : "Formaldehyde", v, stype == 10 ? "ppm" : "ppb", 0);
        if (stype == 11 && (msg[22] & 0x0f) != 0x0f) R.add("VOC level", msg[22] & 0x0f, "", 0);
        out.push_back(R);
    }
}

// lacrosse_tx35.c: LaCrosse TX29-IT / TX35DTH-IT, TFA 30.3159.IT / 30.3155 (FSK, CRC-8 poly 0x31)
void decTx29(Bits& B, Out& out, bool tx35) {
    static const uint8_t pre[3] = {0xa2, 0xdd, 0x49};
    // the two share the frame and differ in the bit width (55 / 105 us); the slicer's measured width tells them apart, so one burst is
    // not reported as both
    if (std::fabs(tlPcmBitUs - (tx35 ? 105.f : 55.f)) > (tx35 ? 105.f : 55.f) * 0.2f) return;
    for (int r = 0; r < B.rows(); r++) {
        const unsigned sp = B.search(r, 0, pre, 24);
        if (sp >= B.n[r]) continue;
        uint8_t b[5];
        B.extract(r, sp + 20, b, 40);
        if (b[4] != ismCrc8(b, 4, 0x31, 0x00)) continue;
        int id = ((b[0] & 0x0f) << 2) | (b[1] >> 6);
        const double tc = 10 * (b[1] & 0x0f) + 1 * ((b[2] >> 4) & 0x0f) + 0.1 * (b[2] & 0x0f) - 40.0;
        const int hum = b[3] & 0x7f;
        if (hum == 0x7d) id += 0x40;
        IsmReading R = mk(tx35 ? "LaCrosse-TX35DTHIT" : "LaCrosse-TX29IT", "Weather", decs(id), "CRC");
        R.battery = !(b[3] >> 7);
        R.add("Temperature", tc, "C");
        if (hum != 0x6a && hum != 0x7d) R.add("Humidity", hum, "%", 0);
        if ((b[1] >> 5) & 1) R.add("New battery", 1, "", 0);
        out.push_back(R);
    }
}

// fineoffset.c (fineoffset_WH24_callback): Fine Offset / Ecowitt WH24, WH65, WS69, HP1000, Misol WS2320 (FSK, CRC-8 and add checksum)
void decWh24(Bits& B, Out& out) {
    static const uint8_t pre[3] = {0xAA, 0x2D, 0xD4};
    const unsigned nb = B.n[0];
    if (nb < 190 || nb > 268) return;
    const unsigned off = B.search(0, 0, pre, 24) + 24;
    if (off + 17 * 8 > nb) return;
    int type = (nb - off - 17 * 8 < 8) ? (off < 61 ? 24 : 65) : 65;
    if (nb > 215) type = 69;
    uint8_t b[25];
    B.extract(0, off, b, 200);
    if (b[0] != 0x24) return;
    if (ismCrc8(b, 16, 0x31, 0x00) != 0 || (uint8_t)addBytes(b, 16) != b[16]) return;
    const int wdir = b[2] | (b[3] & 0x80) << 1, traw = (b[3] & 0x07) << 8 | b[4], hum = b[5];
    const int wsraw = b[6] | (b[3] & 0x10) << 4, gust = b[7], rain = b[8] << 8 | b[9], uv = b[10] << 8 | b[11];
    const int light = b[12] << 16 | b[13] << 8 | b[14];
    const double wf = type == 24 ? 1.12 : 0.51, rf = type == 24 ? 0.3 : 0.254;
    IsmReading R = mk(type == 24 ? "Fineoffset-WH24" : type == 65 ? "Fineoffset-WH65B" : "Fineoffset-WS69", "Weather", decs(b[1]), "CRC");
    R.battery = !((b[3] & 0x08) >> 3);
    if (traw != 0x7ff) R.add("Temperature", (traw - 400) * 0.1, "C");
    if (hum != 0xff) R.add("Humidity", hum, "%", 0);
    if (wdir != 0x1ff) R.add("Wind direction", wdir, "deg", 0);
    if (wsraw != 0x1ff) R.add("Wind speed", wsraw * 0.125 * wf, "m/s");
    if (gust != 0xff) R.add("Wind gust", gust * wf, "m/s");
    R.add("Rain", rain * rf, "mm");
    if (uv != 0xffff) {
        static const int up[13] = {432, 851, 1210, 1570, 2017, 2450, 2761, 3100, 3512, 3918, 4277, 4650, 5029};
        int i = 0;
        while (i < 13 && up[i] < uv) ++i;
        R.add("UV index", i, "", 0);
    }
    if (light != 0xffffff) R.add("Light", light * 0.1, "lux");
    out.push_back(R);
}

// tpms_toyota.c: Toyota / Pacific PMV-C210 (FSK, differential Manchester, CRC-8 poly 0x07 init 0x80, inverted pressure copy)
void decToyota(Bits& B, Out& out) {
    static const uint8_t pre[2] = {0xa9, 0xe0};
    unsigned pos = 0;
    while ((pos = B.search(0, pos, pre, 12)) + 156 <= B.n[0]) {
        Bits pk;
        const unsigned sp = diffManchester(B, 0, pos + 11, pk, 80);
        pos += 2;
        if (sp - (pos - 2 + 11) < 144) continue;
        const uint8_t* b = pk.row(0);
        if (ismCrc8(b, 8, 0x07, 0x80) != b[8]) continue;
        const unsigned p1 = (b[4] & 0x7f) << 1 | b[5] >> 7, t = (b[5] & 0x7f) << 1 | b[6] >> 7, p2 = b[7] ^ 0xff;
        if (p1 != p2) continue;
        IsmReading R = mk("Toyota", "TPMS", hexs((unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3], 8), "CRC");
        R.add("Pressure", p1 * 0.25 - 7.0, "PSI", 2);
        R.add("Temperature", t - 40.0, "C", 0);
        R.add("Status", (b[4] & 0x80) | (b[6] & 0x7f), "", 0);
        out.push_back(R);
        return;
    }
}

// tpms_pmv107j.c: Pacific PMV-107J (Toyota, 315 MHz; FSK differential Manchester, CRC-8 poly 0x13)
void decPmv107j(Bits& B, Out& out) {
    static const uint8_t pre[1] = {0xf8};
    unsigned pos = 0;
    while ((pos = B.search(0, pos, pre, 6)) + 67 * 2 <= B.n[0]) {
        Bits pk;
        const unsigned start = pos + 6;
        const unsigned sp = diffManchester(B, 0, start, pk, 70);
        pos += 2;
        if (sp - start < 67 * 2) continue;
        uint8_t b[9];
        b[0] = pk.row(0)[0] >> 6;
        pk.extract(0, 2, b + 1, 64);
        if (ismCrc8(b, 8, 0x13, 0x00) != b[8]) continue;
        if (b[5] != (b[6] ^ 0xff)) continue;
        IsmReading R = mk("PMV-107J", "TPMS", hexs((unsigned)b[0] << 26 | b[1] << 18 | b[2] << 10 | b[3] << 2 | b[4] >> 6, 8), "CRC");
        R.battery = !((b[4] & 0x20) >> 5);
        R.add("Pressure", (b[5] - 40.0) * 2.48, "kPa");
        R.add("Temperature", b[7] - 40.0, "C", 0);
        out.push_back(R);
        return;
    }
}

// The VDO / Continental family: inverted bits, preamble ...aaa9, Manchester. lenBits: Manchester data bits wanted; need: raw bits after
// the preamble position the callback asks for.
template <typename F>
void vdoFamily(Bits& B, const uint8_t* pre, unsigned plen, unsigned need, unsigned step, unsigned maxBits, bool allRows, F&& fn) {
    B.invert();
    for (int r = 0; r < (allRows ? B.rows() : 1); r++) {
        unsigned pos = 0;
        while ((pos = B.search(r, pos, pre, plen)) + need <= B.n[r]) {
            Bits pk;
            manchester(B, r, pos + plen, pk, maxBits);
            if (fn(pk)) return;
            pos += step;
        }
    }
}

// tpms_citroen.c: Citroen / Peugeot / Fiat / Mitsubishi VDO (FSK Manchester, XOR of bytes 1 to 9)
void decCitroen(Bits& B, Out& out) {
    static const uint8_t pre[2] = {0xaa, 0xa9};
    vdoFamily(B, pre, 16, 178, 2, 88, false, [&](Bits& pk) {
        if (pk.n[0] < 80) return false;
        const uint8_t* b = pk.row(0);
        if (b[6] == 0 || b[7] == 0) return false;
        if (b[1] ^ b[2] ^ b[3] ^ b[4] ^ b[5] ^ b[6] ^ b[7] ^ b[8] ^ b[9]) return false;
        IsmReading R = mk("Citroen", "TPMS", hexs((unsigned)b[1] << 24 | b[2] << 16 | b[3] << 8 | b[4], 8), "CHECKSUM");
        R.add("Pressure", b[6] * 1.364, "kPa", 0);
        R.add("Temperature", b[7] - 50.0, "C", 0);
        R.add("Repeat", b[5] & 0x0f, "", 0);
        R.add("Flags", b[5] >> 4, "", 0);
        out.push_back(R);
        return true;
    });
}

// tpms_renault.c: Renault Clio / Captur / Zoe, Dacia (FSK Manchester, CRC-8 poly 0x07)
void decRenault(Bits& B, Out& out) {
    static const uint8_t pre[2] = {0xaa, 0xa9};
    vdoFamily(B, pre, 16, 160, 15, 160, true, [&](Bits& pk) {
        if (pk.n[0] < 72) return false;
        const uint8_t* b = pk.row(0);
        if (ismCrc8(b, 8, 0x07, 0x00) != b[8]) return false;
        IsmReading R = mk("Renault", "TPMS", hexs(b[5] << 16 | b[4] << 8 | b[3], 6), "CRC");
        R.add("Pressure", (((b[0] & 0x03) << 8) | b[1]) * 0.75, "kPa");
        R.add("Temperature", b[2] - 30, "C", 0);
        R.addText("Flags", hexs(b[0] >> 2, 2));
        out.push_back(R);
        return true;
    });
}

// tpms_ford.c: Ford Fiesta / Focus / Kuga / Transit, VDO S180084730Z (FSK Manchester, add checksum, known flag patterns only)
void decFord(Bits& B, Out& out) {
    static const uint8_t pre[2] = {0xaa, 0xa9};
    vdoFamily(B, pre, 16, 144, 15, 160, true, [&](Bits& pk) {
        if (pk.n[0] < 64) return false;
        const uint8_t* b = pk.row(0);
        if (((b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6]) & 0xff) != b[7]) return false;
        int learn = 0, moving = 0, unknown = 0;
        switch (b[6] & 0x4c) {
        case 0x8: learn = 1; break;
        case 0x4: break;
        case 0x44: moving = 1; break;
        default: unknown = b[6] & 0x4c; break;
        }
        unknown |= b[6] & 0x90;
        if (unknown) return false;
        IsmReading R = mk("Ford", "TPMS", hexs((unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3], 8), "CHECKSUM");
        R.add("Pressure", (((b[6] & 0x20) << 3) | b[4]) * 0.25, "PSI", 2);
        if ((b[5] & 0x80) == 0) R.add("Temperature", (b[5] & 0x7f) - 56, "C", 0);
        R.add("Moving", moving, "", 0);
        R.add("Learn", learn, "", 0);
        out.push_back(R);
        return true;
    });
}

// tpms_hyundai_vdo.c: Hyundai / Kia VDO TG1C (FSK Manchester, CRC-8 poly 0x07 init 0xaa)
void decHyundai(Bits& B, Out& out) {
    static const uint8_t pre[4] = {0xaa, 0xaa, 0xaa, 0xa9};
    vdoFamily(B, pre, 32, 80, 2, 80, false, [&](Bits& pk) {
        if (pk.n[0] < 80) return false;
        const uint8_t* b = pk.row(0);
        if (ismCrc8(b, 9, 0x07, 0xaa) != b[9]) return false;
        IsmReading R = mk("Hyundai-VDO", "TPMS", hexs((unsigned)b[1] << 24 | b[2] << 16 | b[3] << 8 | b[4], 8), "CRC");
        R.add("Pressure", b[6] * 1.375, "kPa", 0);
        R.add("Temperature", b[7] - 50.0, "C", 0);
        R.add("Repeat", b[5] & 0x0f, "", 0);
        R.add("State", b[0], "", 0);
        out.push_back(R);
        return true;
    });
}

// tpms_abarth124.c: Abarth 124 Spider / Fiat 124 / Mazda MX-5 ND, VDO TG1C (FSK Manchester, XOR of 9 bytes, temperature range)
void decAbarth(Bits& B, Out& out) {
    static const uint8_t pre[3] = {0xaa, 0xaa, 0xa9};
    const unsigned nb = B.n[0];
    if (!(nb > 150 && nb < 210)) return;
    vdoFamily(B, pre, 24, 80, 2, 72, false, [&](Bits& pk) {
        if (pk.n[0] < 72) return false;
        const uint8_t* b = pk.row(0);
        if (xorBytes(b, 9) != 0) return false;
        const double tc = b[6] - 50.0;
        if (tc < -50.0 || tc > 125.0) return false;
        IsmReading R = mk("Abarth-124Spider", "TPMS", hexs((unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3], 8), "CHECKSUM");
        R.add("Pressure", b[5] * 1.38, "kPa", 0);
        R.add("Temperature", tc, "C", 0);
        R.add("Status", b[7], "", 0);
        out.push_back(R);
        return true;
    });
}

// efergy_e2_classic.c: Efergy e2 classic current clamp (FSK PWM, add checksum)
void decEfergy(Bits& B, Out& out) {
    if (B.rows() != 1) return;
    const unsigned nb = B.n[0];
    if (nb < 64 || nb > 65) return;
    uint8_t by[8];
    for (int i = 0; i < 8; i++) by[i] = B.byteAt(0, i);
    if ((by[0] & 0xf0) != 0xf0 && (by[0] & 0xf0) != 0x00) return;
    if (by[0] & 0xf0) for (auto& v : by) v = (uint8_t)~v;
    int zeros = 0;
    for (auto v : by) zeros += v == 0;
    if (zeros > 5) return;
    const unsigned sum = (unsigned)addBytes(by, 7);
    if (sum == 0 || (sum & 0xff) != by[7]) return;
    const uint8_t fact = (uint8_t)(-(int8_t)by[6] + 15);
    if (fact < 7 || fact > 23) return;
    IsmReading R = mk("Efergy-e2CT", "Energy", decs(by[2] << 8 | by[1]), "CHECKSUM");
    R.battery = (by[3] & 0x40) ? 1 : 0;
    R.add("Current", (double)(by[4] << 8 | by[5]) / (double)(1 << fact), "A", 2);
    R.add("Interval", (((by[3] & 0x30) >> 4) + 1) * 6, "s", 0);
    if (by[3] & 0x80) R.add("Learning", 1, "", 0);
    out.push_back(R);
}

// current_cost.c: CurrentCost TX and EnviR (FSK NRZ, Manchester; no checksum: the Manchester code must hold for all 64 bits)
void decCurrentCost(Bits& B, Out& out) {
    B.invert();
    static const uint8_t classic[6] = {0xcc, 0xcc, 0xcc, 0xce, 0x91, 0x5d};
    static const uint8_t envir[6] = {0x55, 0x55, 0x55, 0x55, 0xa4, 0x57};
    bool isEnvir = false;
    unsigned sp = B.search(0, 0, envir, 48);
    if (sp + 47 + 112 <= B.n[0]) { isEnvir = true; sp += 47; }
    else {
        sp = B.search(0, 0, classic, 45);
        if (sp + 45 + 112 > B.n[0]) return;
        sp += 45;
    }
    Bits pk;
    manchester(B, 0, sp, pk, 0);
    if (pk.n[0] < 64) return;
    const uint8_t* b = pk.row(0);
    const int dev = (b[0] & 0x0f) << 8 | b[1];
    if ((b[0] & 0xf0) == 0) {
        IsmReading R = mk(isEnvir ? "CurrentCost-EnviR" : "CurrentCost-TX", "Energy", decs(dev), "MANCHESTER");
        R.add("Power 0", (b[2] & 0x80) ? ((b[2] & 0x7F) << 8 | b[3]) : 0, "W", 0);
        R.add("Power 1", (b[4] & 0x80) ? ((b[4] & 0x7F) << 8 | b[5]) : 0, "W", 0);
        R.add("Power 2", (b[6] & 0x80) ? ((b[6] & 0x7F) << 8 | b[7]) : 0, "W", 0);
        out.push_back(R);
    } else if ((b[0] & 0xf0) == 64) {
        IsmReading R = mk(isEnvir ? "CurrentCost-EnviRCounter" : "CurrentCost-Counter", "Energy", decs(dev), "MANCHESTER");
        R.add("Sensor type", b[3], "", 0);
        R.add("Counter", (double)((uint32_t)b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7]), "", 0);
        out.push_back(R);
    }
}

// m_bus.c: wireless M-Bus (EN 13757-4) mode T (3-of-6) and C (NRZ), the link-layer header only: manufacturer, address, version, type.
// The header block's CRC (poly 0x3D65) is checked; the application data is neither decoded nor decrypted.
uint8_t dec3of6(uint8_t v) {
    switch (v) {
    case 22: return 0x0; case 13: return 0x1; case 14: return 0x2; case 11: return 0x3;
    case 28: return 0x4; case 25: return 0x5; case 26: return 0x6; case 19: return 0x7;
    case 44: return 0x8; case 37: return 0x9; case 38: return 0xA; case 35: return 0xB;
    case 52: return 0xC; case 49: return 0xD; case 50: return 0xE; case 41: return 0xF;
    default: return 0xF0;
    }
}
bool mbusCrc(const uint8_t* b, unsigned off) {
    const uint16_t c = (uint16_t)~ismCrc16(b, off, 0x3D65, 0);
    return c == (uint16_t)((b[off] << 8) | b[off + 1]);
}
const char* mbusType(uint8_t t) {
    switch (t) {
    case 0x00: return "Other"; case 0x01: return "Oil"; case 0x02: return "Electricity"; case 0x03: return "Gas"; case 0x04: return "Heat";
    case 0x05: return "Steam"; case 0x06: return "Warm Water"; case 0x07: return "Water"; case 0x08: return "Heat Cost Allocator";
    case 0x09: return "Compressed Air"; case 0x0A: case 0x0B: return "Cooling load meter"; case 0x0C: return "Heat";
    case 0x0D: return "Heat/Cooling load meter"; case 0x0E: return "Bus/System component"; case 0x0F: return "Unknown";
    case 0x15: return "Hot Water"; case 0x16: return "Cold Water"; case 0x17: return "Hot/Cold Water meter"; case 0x18: return "Pressure";
    case 0x19: return "A/D Converter"; case 0x1A: return "Smoke detector"; case 0x1B: return "Room sensor"; case 0x1C: return "Gas detector";
    case 0x20: return "Breaker (electricity)"; case 0x21: return "Valve (gas or water)"; case 0x25: return "Customer unit (display device)";
    case 0x28: return "Waste water meter"; case 0x29: return "Garbage"; case 0x2A: return "Carbon dioxide";
    case 0x31: return "Communication controller"; case 0x32: return "Unidirectional repeater"; case 0x33: return "Bidirectional repeater";
    case 0x36: return "Radio converter (system side)"; case 0x37: return "Radio converter (meter side)";
    default: return "";
    }
}
void decMbus(Bits& B, Out& out) {
    static const uint8_t preT[2] = {0x54, 0x3D};
    const unsigned nb = B.n[0];
    if (nb < 32 + 13 * 8 || nb > 64 + 256 * 12) return;
    unsigned off = B.search(0, 0, preT, 16);
    if (off + 13 * 8 >= nb) return;
    off += 16;
    uint8_t nx;
    B.extract(0, off, &nx, 8);
    off += 8;
    std::vector<uint8_t> d;
    const char* mode;
    bool formatB = false;
    if (nx == 0x54) {
        mode = "C";
        B.extract(0, off, &nx, 8);
        off += 8;
        if (nx != 0xCD && nx != 0x3D) return;
        formatB = nx == 0x3D;
        d.resize((nb - off) / 8);
        if (d.empty()) return;
        B.extract(0, off, d.data(), (unsigned)d.size() * 8);
    } else {
        mode = "T";
        off -= 8;
        const unsigned n = (nb - off) / 12;
        d.resize(n);
        for (unsigned i = 0; i < n; i++) {
            uint8_t h, l;
            B.extract(0, off + i * 12, &h, 6);
            B.extract(0, off + i * 12 + 6, &l, 6);
            h = dec3of6(h >> 2); l = dec3of6(l >> 2);
            if (h > 0xf) { d.resize(i); break; }
            if (l > 0xf) { d.resize(i); break; }   // the frame ends at the first code that is not 3-of-6
            d[i] = (uint8_t)(h << 4 | l);
        }
    }
    if (d.size() < 12) return;
    if (!formatB) {
        if (!mbusCrc(d.data(), 10)) return;
    } else {
        const unsigned L = d[0];
        if (L < 12 || L + 1 > d.size()) return;
        if (!mbusCrc(d.data(), std::min<unsigned>(L - 1, 126))) return;
    }
    const uint16_t mf = (uint16_t)(d[3] << 8 | d[2]);
    if (d[2] == 0xFF && d[3] == 0x03) return;   // KNX RF, not shown
    const std::string man{(char)(((mf >> 10) & 0x1F) + 0x40), (char)(((mf >> 5) & 0x1F) + 0x40), (char)((mf & 0x1F) + 0x40)};
    const unsigned id = (unsigned)(bcd(d[7]) * 1000000 + bcd(d[6]) * 10000 + bcd(d[5]) * 100 + bcd(d[4]));
    char ids[16];
    snprintf(ids, sizeof ids, "%08u", id);
    IsmReading R = mk("Wireless-MBus", "Meter", ids, "CRC");
    R.addText("Mode", std::string(mode) + (formatB ? " (format B)" : ""));
    R.addText("Manufacturer", man);
    const char* ty = mbusType(d[9]);
    R.addText("Type", *ty ? std::string(ty) : hexs(d[9], 2));
    R.add("Version", d[8], "", 0);
    R.addText("C field", hexs(d[1], 2));
    R.add("Length", d[0], "bytes", 0);
    out.push_back(R);
}

// ---------------------------------------------------------------- the table

thread_local std::vector<uint32_t> tlRemoteSeen;   // the remote codes seen in the current train (ismDecode clears it)

struct Proto {
    IsmProtoInfo info;
    Mod mod;
    Timing t;
    std::function<void(Bits&, Out&)> fn;
};

const std::vector<Proto>& protos() {
    static const std::vector<Proto> p = {
        {{"Nexus / FreeTec / TFA 30.3209", "nexus.c", "Weather", false}, OokPpm, {1000, 2000, 5000, 3000, 0, 0}, decNexus},
        {{"Rubicson / TFA 30.3197", "rubicson.c", "Weather", false}, OokPpm, {1000, 2000, 4800, 3000, 0, 0}, decRubicson},
        {{"Generic temperature sensor 1", "generic_temperature_sensor.c", "Weather", false}, OokPpm, {2000, 4000, 10000, 4800, 0, 0}, decGenericTemp},
        {{"LaCrosse TX141 / TX141TH-Bv2 / TFA 30.3221", "lacrosse_tx141x.c", "Weather", false}, OokPwm, {208, 417, 1700, 625, 833, 0}, decTx141},
        {{"Acurite 592TXR tower, 5-in-1", "acurite.c", "Weather", false}, OokPwm, {220, 408, 4000, 500, 620, 0}, decAcurite},
        {{"Fine Offset WH1080 / WH3080", "fineoffset_wh1080.c", "Weather", false}, OokPwm, {544, 1524, 2800, 0, 0, 0}, decWh1080},
        {{"Ambient Weather F007TH / TFA 30.3208", "ambient_weather.c", "Weather", false}, OokMcZero, {500, 0, 2400, 0, 0, 0}, decF007th},
        {{"Oregon Scientific v2.1 / v3", "oregon_scientific.c", "Weather", false}, OokMcZero, {440, 0, 2400, 0, 0, 0}, decOregon},
        {{"Schrader TPMS", "schraeder.c", "TPMS", false}, OokMcZero, {120, 0, 480, 0, 0, 0}, decSchrader},
        {{"Schrader EG53MA4 TPMS", "schraeder.c", "TPMS", false}, OokMcZero, {123, 0, 300, 0, 0, 0}, decSchraderEg53},
        {{"EV1527 / PT2262 remotes", "generic_remote.c", "Remote", false}, OokPwm, {464, 1404, 1800, 0, 0, 200},
         [](Bits& b, Out& o) { decGenericRemote(b, o, tlRemoteSeen); }},
        {{"Bresser 5-in-1", "bresser_5in1.c", "Weather", true}, FskPcm, {124, 124, 25000, 0, 0, 0}, decBresser5in1},
        {{"Bresser 6-in-1", "bresser_6in1.c", "Weather", true}, FskPcm, {124, 124, 25000, 0, 0, 0}, decBresser6in1},
        {{"Bresser 7-in-1 / air quality", "bresser_7in1.c", "Weather", true}, FskPcm, {124, 124, 25000, 0, 0, 0}, decBresser7in1},
        {{"LaCrosse TX29-IT / TFA 30.3159", "lacrosse_tx35.c", "Weather", true}, FskPcm, {55, 55, 4000, 0, 0, 0}, [](Bits& b, Out& o) { decTx29(b, o, false); }},
        {{"LaCrosse TX35DTH-IT / TFA 30.3155", "lacrosse_tx35.c", "Weather", true}, FskPcm, {105, 105, 4000, 0, 0, 0}, [](Bits& b, Out& o) { decTx29(b, o, true); }},
        {{"Fine Offset / Ecowitt WH24 / WH65", "fineoffset.c", "Weather", true}, FskPcm, {58, 58, 20000, 0, 0, 0}, decWh24},
        {{"Toyota TPMS (PMV-C210)", "tpms_toyota.c", "TPMS", true}, FskPcm, {52, 52, 150, 0, 0, 0}, decToyota},
        {{"PMV-107J TPMS (Toyota)", "tpms_pmv107j.c", "TPMS", true}, FskPcm, {100, 100, 250, 0, 0, 0}, decPmv107j},
        {{"Citroen / Peugeot TPMS", "tpms_citroen.c", "TPMS", true}, FskPcm, {52, 52, 150, 0, 0, 0}, decCitroen},
        {{"Renault TPMS", "tpms_renault.c", "TPMS", true}, FskPcm, {52, 52, 150, 0, 0, 0}, decRenault},
        {{"Ford TPMS", "tpms_ford.c", "TPMS", true}, FskPcm, {52, 52, 150, 0, 0, 0}, decFord},
        {{"Hyundai / Kia TPMS (VDO)", "tpms_hyundai_vdo.c", "TPMS", true}, FskPcm, {52, 52, 150, 0, 0, 0}, decHyundai},
        {{"Abarth 124 TPMS (VDO TG1C)", "tpms_abarth124.c", "TPMS", true}, FskPcm, {52, 52, 150, 0, 0, 0}, decAbarth},
        {{"Efergy e2 classic", "efergy_e2_classic.c", "Energy", true}, FskPwm, {64, 136, 400, 200, 500, 0}, decEfergy},
        {{"CurrentCost TX / EnviR", "current_cost.c", "Energy", true}, FskPcm, {250, 250, 8000, 0, 0, 0}, decCurrentCost},
        {{"Wireless M-Bus T / C (header)", "m_bus.c", "Meter", true}, FskPcm, {10, 10, 500, 0, 0, 0}, decMbus},
    };
    return p;
}

} // namespace

const std::vector<IsmProtoInfo>& ismProtocols() {
    static const std::vector<IsmProtoInfo> v = [] { std::vector<IsmProtoInfo> r; for (const auto& p : protos()) r.push_back(p.info); return r; }();
    return v;
}

std::vector<IsmReading> ismDecode(const IsmTrain& t) {
    Out out;
    if (t.p.empty()) return out;
    for (const auto& q : t.p) if (!(q.pulseUs >= 0 && q.gapUs >= 0 && std::isfinite(q.pulseUs) && std::isfinite(q.gapUs))) return out;
    const auto& P = protos();
    tlRemoteSeen.clear();
    for (const auto& pr : P) {
        if (pr.info.fsk != t.fsk) continue;
        Emit emit = [&](Bits& b) { Bits c = b; pr.fn(c, out); };
        switch (pr.mod) {
        case OokPwm: case FskPwm: slicePwm(t, pr.t, emit); break;
        case OokPpm: slicePpm(t, pr.t, emit); break;
        case OokPcm: case FskPcm: slicePcm(t, pr.t, emit); break;
        case OokMcZero: sliceMcZero(t, pr.t, emit); break;
        }
    }
    // a device repeats its packet within a burst: keep one of each
    Out u;
    for (auto& r : out) {
        bool dup = false;
        for (const auto& k : u) {
            if (k.model != r.model || k.id != r.id || k.channel != r.channel || k.fields.size() != r.fields.size()) continue;
            bool same = true;
            for (size_t i = 0; i < k.fields.size() && same; i++) same = k.fields[i].value == r.fields[i].value && k.fields[i].text == r.fields[i].text;
            if (same) { dup = true; break; }
        }
        if (!dup) u.push_back(std::move(r));
    }
    return u;
}

// ---------------------------------------------------------------- unknown signals

namespace {
std::vector<float> clusters(std::vector<float> v) {
    std::sort(v.begin(), v.end());
    std::vector<float> c;
    std::vector<int> cnt;
    for (float x : v) {
        if (!c.empty() && x <= c.back() * 1.3f + 2) { c.back() = (c.back() * (float)cnt.back() + x) / (float)(cnt.back() + 1); cnt.back()++; }
        else { c.push_back(x); cnt.push_back(1); }
    }
    std::vector<float> r;   // drop clusters under 10 % of the values (glitches, sync gaps, the end gap)
    for (size_t i = 0; i < c.size(); i++) if (cnt[i] * 10 >= (int)v.size()) r.push_back(c[i]);
    return r;
}
} // namespace

IsmGuess ismClassify(const IsmTrain& t) {
    IsmGuess g;
    if (t.p.size() < 4) { g.code = "?"; return g; }
    std::vector<float> pw, gw;
    for (size_t i = 0; i + 1 < t.p.size(); i++) { pw.push_back(t.p[i].pulseUs); gw.push_back(t.p[i].gapUs); }
    pw.push_back(t.p.back().pulseUs);
    std::vector<float> sg = gw;
    std::sort(sg.begin(), sg.end());
    const float med = sg.empty() ? 0 : sg[sg.size() / 2];
    std::vector<float> gIn;
    for (float x : gw) if (x < med * 6 + 1) gIn.push_back(x); else g.gapUs = std::max(g.gapUs, x);
    const auto pc = clusters(pw), gc = clusters(gIn);
    auto near = [](float a, float b) { return std::fabs(a - b) <= 0.25f * std::max(a, b) + 2; };
    if (pc.size() == 1 && gc.size() == 2) { g.code = "PPM"; g.shortUs = gc[0]; g.longUs = gc[1]; g.bits = (int)gIn.size(); }
    else if (pc.size() == 2 && gc.size() <= 2 && !near(pc[1], 2 * pc[0]) ) { g.code = "PWM"; g.shortUs = pc[0]; g.longUs = pc[1]; g.bits = (int)pw.size(); }
    else if (pc.size() <= 2 && gc.size() <= 2 && near(pc[0], gc.empty() ? pc[0] : gc[0]) &&
             (pc.size() < 2 || near(pc[1], 2 * pc[0])) && (gc.size() < 2 || near(gc[1], 2 * gc[0]))) {
        g.code = "Manchester"; g.shortUs = pc[0]; g.longUs = 2 * pc[0];
        float total = 0;
        for (float x : pw) total += x;
        for (float x : gIn) total += x;
        g.bits = (int)(total / (2 * pc[0]) + 0.5f);
    } else if (pc.size() == 2 && gc.size() <= 2) { g.code = "PWM"; g.shortUs = pc[0]; g.longUs = pc[1]; g.bits = (int)pw.size(); }
    else {
        const float base = std::min(pc.empty() ? 1e9f : pc[0], gc.empty() ? 1e9f : gc[0]);
        bool mult = base > 0 && base < 1e8f;
        for (float c : pc) mult = mult && std::fabs(c / base - std::round(c / base)) < 0.2f;
        for (float c : gc) mult = mult && std::fabs(c / base - std::round(c / base)) < 0.2f;
        if (mult) {
            g.code = "NRZ"; g.shortUs = base; g.longUs = base;
            float total = 0;
            for (float x : pw) total += x;
            for (float x : gIn) total += x;
            g.bits = (int)(total / base + 0.5f);
        } else g.code = "?";
    }
    return g;
}

} // namespace dect2
