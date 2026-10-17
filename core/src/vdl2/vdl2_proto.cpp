// VDL Mode 2 bit layer, AVLC and the protocols above it: see vdl2_proto.h.
#include "dect2/vdl2/vdl2_proto.h"
#include "vdl2_internal.h"
#include "dect2/acars/acars_proto.h"
#include "dect2/aero/aero_adsc.h"
#include "dect2/aero/aero_pos.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "dect2/common/str.h"

namespace dect2 {

const int kVdl2PreamblePhase[kVdl2PreambleSyms] = {0, 3, -3, 1, 1, 2, 0, 4, -3, 4, -2, 3, 1, -2, -3, 0};
const uint8_t kVdl2GrayBits[8] = {0, 1, 3, 2, 6, 7, 5, 4};

int vdl2StepForBits(int b) {
    for (int i = 0; i < 8; i++) if (kVdl2GrayBits[i] == (b & 7)) return i;
    return 0;
}

// ---------------------------------------------------------------- scrambler, header

// The C++ below up to the matching #endif is replaced by rust/onair_vdl2 (vdl2_proto_rs.cpp) when DECT2_RUST_VDL2 is defined; it stays for DECT2_RUST=OFF
// builds and as the reference of tests/vdl2/test_vdl2_proto_rs.cpp (rust/README.md).
#ifndef DECT2_RUST_VDL2
void vdl2Scramble(std::vector<uint8_t>& bits, size_t from) {
    uint16_t s = kVdl2LfsrSeed;
    for (size_t i = from; i < bits.size(); i++) {
        const int b = (s ^ (s >> 14)) & 1;
        s = (uint16_t)((s >> 1) | (b << 14));
        bits[i] ^= (uint8_t)b;
    }
}

namespace {
// parity rows of the header code over the 25-bit word (bit 24 = first sent); the low five bits are the parity bits
uint32_t hrow(int i) {
    static const char* rows[5] = {"0000000011111111111110000", "0011111100001111111101000", "1100011100110000111100100", "1101101101010011001100010", "0110100111100101010100001"};
    uint32_t v = 0;
    for (int k = 0; k < 25; k++) v = (v << 1) | (uint32_t)(rows[i][k] - '0');
    return v;
}
// error pattern for each syndrome (bit 4 of the syndrome = row 0)
uint32_t synd(int s) {
    static const char* t[32] = {
        "0000000000000000000000000", "0000000000000000000000001", "0000000000000000000000010", "0100000000000000000000100",
        "0000000000000000000000100", "0100000000000000000000010", "1000000000000000000000000", "0100000000000000000000000",
        "0000000000000000000001000", "0010000000000000000000000", "0001000000000000000000000", "0000100000000000000000000",
        "0000010000000000000000000", "1000100000000000000000000", "0000001000000000000000000", "0000000100000000000000000",
        "0000000000000000000010000", "0000000010000000000000000", "0100000000100000000000000", "0000000001000000000000000",
        "0100000001000000000000000", "0000000000100000000000000", "0000000000010000000000000", "1000000010000000000000000",
        "0000000000001000000000000", "0000000000000100000000000", "0000000000000010000000000", "0000000000000001000000000",
        "0000000000000000100000000", "0000000000000000010000000", "0000000000000000001000000", "0000000000000000000100000"};
    uint32_t v = 0;
    for (int k = 0; k < 25; k++) v = (v << 1) | (uint32_t)(t[s][k] - '0');
    return v;
}
uint32_t rev(uint32_t v, int n) { uint32_t r = 0; for (int i = 0; i < n; i++) { r = (r << 1) | (v & 1); v >>= 1; } return r; }
int par(uint32_t v) { return __builtin_popcount(v) & 1; }
int syndrome(uint32_t w) { int s = 0; for (int i = 0; i < 5; i++) s |= par(w & hrow(i)) << (4 - i); return s; }
} // namespace

uint32_t vdl2HeaderWord(uint32_t lenBits) {
    uint32_t w = rev(lenBits & 0x1FFFF, 17) << 5;
    for (int i = 0; i < 5; i++) w |= (uint32_t)par(w & hrow(i)) << (4 - i);
    return w;
}

bool vdl2HeaderDecode(uint32_t word, uint32_t& lenBits, bool& corrected) {
    word &= (1u << 22) - 1;                 // the reserved bits are not checked
    const int s = syndrome(word);
    corrected = s != 0;
    word ^= synd(s);
    if (word >> 22) return false;
    lenBits = rev((word >> 5) & 0x1FFFF, 17);
    if (lenBits == 0) return false;
    return lenBits <= (corrected ? 0x1FFFu : 0x3FFFu);
}

int vdl2LastFec(int n) { return n < 3 ? 0 : n <= 30 ? 2 : n <= 67 ? 4 : 6; }

bool vdl2Layout(uint32_t lenBits, Vdl2Layout& l) {
    l = {};
    l.dataOct = (int)((lenBits + 7) / 8);
    l.blocks = l.dataOct / kVdl2RsK;
    l.fecOct = l.blocks * (kVdl2RsN - kVdl2RsK);
    l.lastLen = l.dataOct % kVdl2RsK;
    l.lastFec = 6;
    if (l.lastLen) { l.blocks++; l.lastFec = vdl2LastFec(l.lastLen); l.fecOct += l.lastFec; }
    else l.lastLen = kVdl2RsK;
    return l.dataOct > 0 && l.fecOct > 0;
}
#endif // DECT2_RUST_VDL2

// ---------------------------------------------------------------- Reed-Solomon

#ifndef DECT2_RUST_VDL2
namespace {
struct Gf {
    uint8_t exp[512], log[256];
    Gf() {
        int x = 1;
        for (int i = 0; i < 255; i++) { exp[i] = (uint8_t)x; log[x] = (uint8_t)i; x <<= 1; if (x & 0x100) x ^= 0x187; }
        for (int i = 255; i < 512; i++) exp[i] = exp[i - 255];
        log[0] = 0;
    }
    uint8_t mul(uint8_t a, uint8_t b) const { return (a && b) ? exp[log[a] + log[b]] : 0; }
    uint8_t div(uint8_t a, uint8_t b) const { return a ? exp[(log[a] + 255 - log[b]) % 255] : 0; }
    uint8_t pw(int e) const { e %= 255; if (e < 0) e += 255; return exp[e]; }
};
const Gf& gf() { static const Gf g; return g; }
constexpr int kFcr = 120, kRoots = 6;

// generator polynomial, highest power first: g[0] = 1
const uint8_t* genPoly() {
    static uint8_t g[kRoots + 1];
    static bool done = false;
    if (!done) {
        const Gf& G = gf();
        uint8_t p[kRoots + 1] = {1};
        int deg = 0;
        for (int i = 0; i < kRoots; i++) {      // p(x) *= (x + a^(fcr+i))
            const uint8_t r = G.pw(kFcr + i);
            uint8_t q[kRoots + 1] = {};
            for (int k = 0; k <= deg; k++) { q[k] ^= p[k]; q[k + 1] ^= G.mul(p[k], r); }
            deg++;
            memcpy(p, q, sizeof p);
        }
        memcpy(g, p, sizeof g);
        done = true;
    }
    return g;
}
} // namespace

void vdl2RsEncode(const uint8_t data[kVdl2RsK], uint8_t parity[6]) {
    const Gf& G = gf();
    const uint8_t* g = genPoly();
    uint8_t r[kRoots] = {};
    for (int i = 0; i < kVdl2RsK; i++) {
        const uint8_t fb = data[i] ^ r[0];
        for (int k = 0; k < kRoots - 1; k++) r[k] = r[k + 1] ^ G.mul(fb, g[k + 1]);
        r[kRoots - 1] = G.mul(fb, g[kRoots]);
    }
    memcpy(parity, r, kRoots);
}

int vdl2RsDecode(uint8_t cw[kVdl2RsN], int nParity) {
    const Gf& G = gf();
    const int N = kVdl2RsN;
    // syndromes: S_i = c(a^(fcr+i)), c[0] the highest power
    uint8_t S[kRoots];
    bool zero = true;
    for (int i = 0; i < kRoots; i++) {
        uint8_t s = 0;
        const uint8_t a = G.pw(kFcr + i);
        for (int j = 0; j < N; j++) s = G.mul(s, a) ^ cw[j];
        S[i] = s;
        zero &= s == 0;
    }
    const int nEr = kRoots - nParity;
    if (zero && nEr == 0) return 0;
    // erasure locator: positions kVdl2RsK + nParity .. N - 1, locator a^(N-1-j)
    uint8_t lam[kRoots + 1] = {1};
    for (int e = 0; e < nEr; e++) {
        const int j = kVdl2RsK + nParity + e;
        const uint8_t X = G.pw(N - 1 - j);
        for (int k = kRoots; k > 0; k--) lam[k] ^= G.mul(X, lam[k - 1]);
    }
    uint8_t B[kRoots + 1];
    memcpy(B, lam, sizeof B);
    int L = nEr;
    for (int r = nEr + 1; r <= kRoots; r++) {
        uint8_t d = 0;
        for (int i = 0; i < r; i++) d ^= G.mul(lam[i], S[r - 1 - i]);
        uint8_t xB[kRoots + 1] = {};
        for (int k = kRoots; k > 0; k--) xB[k] = B[k - 1];
        if (d == 0) { memcpy(B, xB, sizeof B); continue; }
        uint8_t T[kRoots + 1];
        for (int k = 0; k <= kRoots; k++) T[k] = lam[k] ^ G.mul(d, xB[k]);
        if (2 * L <= r + nEr - 1) {
            L = r + nEr - L;
            for (int k = 0; k <= kRoots; k++) B[k] = G.div(lam[k], d);
        } else memcpy(B, xB, sizeof B);
        memcpy(lam, T, sizeof lam);
    }
    int deg = 0;
    for (int k = 0; k <= kRoots; k++) if (lam[k]) deg = k;
    if (deg == 0) return zero ? 0 : -1;
    // Chien search over the 255 positions
    int pos[kRoots], nr = 0;
    for (int j = 0; j < N; j++) {
        const int e = N - 1 - j;                 // locator exponent of position j
        uint8_t v = 0;
        for (int k = 0; k <= deg; k++) v ^= G.mul(lam[k], G.pw(-e * k));
        if (v == 0) { if (nr == kRoots) return -1; pos[nr++] = j; }
    }
    if (nr != deg) return -1;
    // evaluator omega = S(x) lambda(x) mod x^roots
    uint8_t om[kRoots] = {};
    for (int i = 0; i < kRoots; i++) for (int k = 0; k <= i && k <= deg; k++) om[i] ^= G.mul(lam[k], S[i - k]);
    int fixed = 0;
    for (int q = 0; q < nr; q++) {
        const int j = pos[q], e = N - 1 - j;
        const uint8_t xinv = G.pw(-e);
        uint8_t o = 0, xp = 1;
        for (int i = 0; i < kRoots; i++) { o ^= G.mul(om[i], xp); xp = G.mul(xp, xinv); }
        uint8_t dl = 0;
        for (int k = 1; k <= deg; k += 2) dl ^= G.mul(lam[k], G.pw(-e * (k - 1)));
        if (!dl) return -1;
        const uint8_t val = G.mul(G.div(o, dl), G.pw(e * (1 - kFcr)));
        if (val && j < kVdl2RsK + nParity) fixed++;
        cw[j] ^= val;
    }
    if (2 * fixed + nEr > kRoots) return -1;
    return fixed;
}
#endif // DECT2_RUST_VDL2

// ---------------------------------------------------------------- burst body

namespace {
void putOctet(std::vector<uint8_t>& bits, uint8_t v) { for (int b = 0; b < 8; b++) bits.push_back((v >> b) & 1); }
#ifndef DECT2_RUST_VDL2
uint8_t getOctet(const uint8_t* bits) { uint8_t v = 0; for (int b = 0; b < 8; b++) v |= (uint8_t)((bits[b] & 1) << b); return v; }
#endif // DECT2_RUST_VDL2
} // namespace

std::vector<uint8_t> vdl2BurstBits(const std::vector<uint8_t>& dataBits) {
    const uint32_t len = (uint32_t)dataBits.size();
    Vdl2Layout l;
    std::vector<uint8_t> out;
    if (!vdl2Layout(len, l)) return out;
    std::vector<uint8_t> data(l.dataOct, 0);
    for (size_t i = 0; i < dataBits.size(); i++) data[i / 8] |= (uint8_t)((dataBits[i] & 1) << (i % 8));
    // blocks (rows), data row by row
    std::vector<std::vector<uint8_t>> rows(l.blocks, std::vector<uint8_t>(kVdl2RsN, 0));
    for (int i = 0; i < l.dataOct; i++) rows[i / kVdl2RsK][i % kVdl2RsK] = data[i];
    for (int r = 0; r < l.blocks; r++) vdl2RsEncode(rows[r].data(), &rows[r][kVdl2RsK]);
    const uint32_t hw = vdl2HeaderWord(len);
    for (int b = 24; b >= 0; b--) out.push_back((hw >> b) & 1);
    for (int c = 0; c < kVdl2RsK; c++)
        for (int r = 0; r < l.blocks; r++) {
            if (r == l.blocks - 1 && c >= l.lastLen) continue;
            putOctet(out, rows[r][c]);
        }
    const int fecRows = l.lastFec ? l.blocks : l.blocks - 1;
    for (int c = 0; c < kVdl2RsN - kVdl2RsK; c++)
        for (int r = 0; r < fecRows; r++) {
            if (r == l.blocks - 1 && c >= l.lastFec) continue;
            putOctet(out, rows[r][kVdl2RsK + c]);
        }
    vdl2Scramble(out, 0);
    return out;
}

#ifndef DECT2_RUST_VDL2
bool vdl2BurstData(const uint8_t* bits, const Vdl2Layout& l, uint32_t lenBits, std::vector<uint8_t>& dataBits, int& rsFixed) {
    rsFixed = 0;
    std::vector<std::vector<uint8_t>> rows(l.blocks, std::vector<uint8_t>(kVdl2RsN, 0));
    size_t k = 0;
    for (int c = 0; c < kVdl2RsK; c++)
        for (int r = 0; r < l.blocks; r++) {
            if (r == l.blocks - 1 && c >= l.lastLen) continue;
            rows[r][c] = getOctet(bits + 8 * k++);
        }
    const int fecRows = l.lastFec ? l.blocks : l.blocks - 1;
    for (int c = 0; c < kVdl2RsN - kVdl2RsK; c++)
        for (int r = 0; r < fecRows; r++) {
            if (r == l.blocks - 1 && c >= l.lastFec) continue;
            rows[r][kVdl2RsK + c] = getOctet(bits + 8 * k++);
        }
    for (int r = 0; r < l.blocks; r++) {
        const int np = r == l.blocks - 1 ? l.lastFec : 6;
        if (!np) continue;
        const int f = vdl2RsDecode(rows[r].data(), np);
        if (f < 0) return false;
        rsFixed += f;
    }
    dataBits.clear();
    dataBits.reserve(lenBits);
    for (int r = 0; r < l.blocks; r++) {
        const int n = r == l.blocks - 1 ? l.lastLen : kVdl2RsK;
        for (int c = 0; c < n; c++) for (int b = 0; b < 8 && dataBits.size() < lenBits; b++) dataBits.push_back((rows[r][c] >> b) & 1);
    }
    return true;
}
#endif // DECT2_RUST_VDL2

// ---------------------------------------------------------------- HDLC

#ifndef DECT2_RUST_VDL2
namespace {
uint16_t crcReg(const uint8_t* p, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) { c ^= p[i]; for (int b = 0; b < 8; b++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0x8408) : (uint16_t)(c >> 1); }
    return c;
}
} // namespace

uint16_t vdl2Fcs(const uint8_t* p, size_t n) { return (uint16_t)~crcReg(p, n); }
bool vdl2FcsOk(const uint8_t* p, size_t n) { return n >= 2 && crcReg(p, n) == 0xF0B8; }
#endif // DECT2_RUST_VDL2

std::vector<uint8_t> vdl2HdlcBits(const std::vector<std::vector<uint8_t>>& frames) {
    std::vector<uint8_t> out;
    auto flag = [&] { putOctet(out, 0x7E); };
    flag();
    for (const auto& f : frames) {
        int ones = 0;
        for (uint8_t v : f)
            for (int b = 0; b < 8; b++) {
                const int bit = (v >> b) & 1;
                out.push_back((uint8_t)bit);
                if (bit) { if (++ones == 5) { out.push_back(0); ones = 0; } } else ones = 0;
            }
        flag();
    }
    return out;
}

#ifndef DECT2_RUST_VDL2
std::vector<std::vector<uint8_t>> vdl2HdlcFrames(const std::vector<uint8_t>& bits) {
    std::vector<std::vector<uint8_t>> out;
    std::vector<uint8_t> cur;
    int ones = 0;
    bool sync = false, abort = false;
    auto finish = [&] {
        if (sync && !abort && cur.size() >= 16 && cur.size() % 8 == 0) {
            std::vector<uint8_t> f(cur.size() / 8);
            for (size_t i = 0; i < f.size(); i++) f[i] = getOctet(&cur[8 * i]);
            out.push_back(std::move(f));
        }
        cur.clear(); abort = false;
    };
    for (uint8_t b : bits) {
        if (b) {
            if (++ones > 6) { abort = true; cur.clear(); }
            else cur.push_back(1);
            continue;
        }
        if (ones == 5) { ones = 0; continue; }       // inserted zero
        if (ones == 6) {                             // flag: drop its leading zero and six ones
            cur.resize(cur.size() >= 7 ? cur.size() - 7 : 0);
            finish();
            sync = true; ones = 0;
            continue;
        }
        cur.push_back(0); ones = 0;
    }
    return out;
}
#endif // DECT2_RUST_VDL2

// ---------------------------------------------------------------- AVLC

#ifndef DECT2_RUST_VDL2
namespace {
uint32_t rev28(uint32_t v) { return rev(v, 28); }
} // namespace

void vdl2PutAddr(uint8_t* o, const Vdl2Addr& a, bool last) {
    const uint32_t V = ((uint32_t)a.status << 27) | ((uint32_t)(a.type & 7) << 24) | (a.addr & 0xFFFFFF);
    const uint32_t t = rev28(V);
    for (int i = 0; i < 4; i++) o[i] = (uint8_t)(((t >> (7 * i)) & 0x7F) << 1);
    if (last) o[3] |= 1;
}

Vdl2Addr vdl2GetAddr(const uint8_t* o) {
    uint32_t t = 0;
    for (int i = 0; i < 4; i++) t |= (uint32_t)((o[i] >> 1) & 0x7F) << (7 * i);
    const uint32_t V = rev28(t);
    Vdl2Addr a;
    a.addr = V & 0xFFFFFF; a.type = (V >> 24) & 7; a.status = (V >> 27) & 1;
    return a;
}

const char* vdl2AddrTypeName(int t) {
    switch (t) { case 1: return "aircraft"; case 4: return "ground station (ADM)"; case 5: return "ground station (DEL)"; case 7: return "all stations"; default: return "reserved"; }
}
#endif // DECT2_RUST_VDL2

uint8_t vdl2ControlI(int ns, int nr, bool p) { return (uint8_t)(((ns & 7) << 1) | (p ? 0x10 : 0) | ((nr & 7) << 5)); }

std::vector<uint8_t> vdl2Frame(const Vdl2Addr& dst, const Vdl2Addr& src, uint8_t control, const std::vector<uint8_t>& info) {
    std::vector<uint8_t> f(9);
    vdl2PutAddr(&f[0], dst, false);
    vdl2PutAddr(&f[4], src, true);
    f[8] = control;
    f.insert(f.end(), info.begin(), info.end());
    const uint16_t c = vdl2Fcs(f.data(), f.size());
    f.push_back((uint8_t)(c & 0xFF)); f.push_back((uint8_t)(c >> 8));
    return f;
}

// ---- protocol readers ----
namespace {

#ifndef DECT2_RUST_VDL2
std::string hexs(const uint8_t* p, size_t n, size_t max = 24) { std::string s = dect2::str::toHex(p, n, true, 0, max); if (n > max) s += "..."; return s; }

std::string bcd(const uint8_t* p, int digits, int startNibble) {
    std::string s;
    for (int i = 0; i < digits; i++) {
        const int nib = startNibble + i;
        const uint8_t v = (p[nib / 2] >> (nib % 2 ? 0 : 4)) & 0xF;
        s += (char)(v < 10 ? '0' + v : '?');
    }
    return s;
}
#endif // DECT2_RUST_VDL2

void addLine(std::string& d, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void addLine(std::string& d, const char* fmt, ...) {
    char b[400];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    d += b; d += '\n';
}

#ifndef DECT2_RUST_VDL2
double signed12(int v) { return (v & 0x800) ? v - 0x1000 : v; }

const char* cotpName(uint8_t c) {
    switch (c & 0xF0) {
    case 0xE0: return "CR (connection request)"; case 0xD0: return "CC (connection confirm)"; case 0xF0: return "DT (data)";
    case 0x80: return "DR (disconnect request)"; case 0xC0: return "DC (disconnect confirm)"; case 0x60: return "AK (acknowledgement)";
    case 0x10: return "ED (expedited data)"; case 0x20: return "EA (expedited ack)"; case 0x70: return "ER (error)"; default: return "unknown TPDU";
    }
}

std::string nsap(const uint8_t* p, int n) { return hexs(p, (size_t)n, 20); }

// CLNP (ISO 8473) header and what it carries
void readClnp(const uint8_t* p, size_t n, Vdl2Message& m) {
    if (n < 9) { addLine(m.detail, "CLNP: cut short (%zu octets)", n); return; }
    const int li = p[1], ver = p[2], life = p[3], type = p[4] & 0x1F;
    const int segLen = (p[5] << 8) | p[6];
    const char* tn = type == 0x1C ? "DT (data)" : type == 0x01 ? "ER (error report)" : type == 0x1E ? "ERQ (echo request)" : type == 0x1F ? "ERP (echo reply)" : "unknown";
    m.proto = "CLNP";
    addLine(m.detail, "CLNP: %s, version %d, lifetime %.1f s, segment %d octets, header %d octets%s%s", tn, ver, life * 0.5, segLen, li,
            (p[4] & 0x80) ? ", segmented" : "", (p[4] & 0x20) ? ", error reports wanted" : "");
    size_t o = 9;
    std::string dst, src;
    if (o < n) { const int dl = p[o++]; if (o + dl <= n) { dst = nsap(p + o, dl); o += dl; } }
    if (o < n) { const int sl = p[o++]; if (o + sl <= n) { src = nsap(p + o, sl); o += sl; } }
    addLine(m.detail, "  destination NSAP %s", dst.empty() ? "-" : dst.c_str());
    addLine(m.detail, "  source NSAP      %s", src.empty() ? "-" : src.c_str());
    m.summary = std::string("CLNP ") + (type == 0x1C ? "DT" : tn);
    if (type == 0x1C && (size_t)li < n) {
        const uint8_t* q = p + li;
        const size_t qn = n - (size_t)li;
        if (qn >= 2 && q[0] + 1u <= qn) {
            m.proto = "COTP";
            addLine(m.detail, "COTP (ISO 8073): %s, %zu octets of user data", cotpName(q[1]), qn - q[0] - 1);
            m.summary += std::string(", COTP ") + std::string(cotpName(q[1])).substr(0, 2);
            if ((q[1] & 0xF0) == 0xF0 && qn > q[0] + 1u) {
                addLine(m.detail, "  ATN application data (CPDLC, ADS-C, CM over ATN: ASN.1 PER, not decoded): %s", hexs(q + q[0] + 1, qn - q[0] - 1).c_str());
                m.summary += ", ATN application data";
            }
        }
    }
}

void readX25(const uint8_t* p, size_t n, Vdl2Message& m) {
    m.proto = "X.25";
    if (n < 3) { m.summary = "X.25: cut short"; addLine(m.detail, "X.25 (ISO 8208): %zu octets, too short", n); return; }
    const int gfi = p[0] >> 4, lcn = ((p[0] & 0xF) << 8) | p[1];
    const uint8_t pt = p[2];
    char b[160];
    if (!(pt & 1)) {
        const int pr = pt >> 5, ps = (pt >> 1) & 7, more = (pt >> 4) & 1;
        snprintf(b, sizeof b, "X.25 data, LCN %d, P(S) %d P(R) %d%s", lcn, ps, pr, more ? ", more follows" : "");
        m.summary = b;
        addLine(m.detail, "X.25 (ISO 8208): data packet, GFI %X, logical channel %d, P(S) %d, P(R) %d%s, %zu octets of user data", gfi, lcn, ps, pr, more ? ", M bit set" : "", n - 3);
        const uint8_t* u = p + 3;
        const size_t un = n - 3;
        if (!un) return;
        if (u[0] == 0x81) { const std::string s = m.summary; readClnp(u, un, m); m.summary = s + ": " + m.summary; }
        else if (u[0] == 0x82) { m.proto = "ES-IS"; m.summary += ": ES-IS"; addLine(m.detail, "ES-IS (ISO 9542) routeing exchange, %zu octets", un); }
        else if (u[0] == 0x85) { m.proto = "IDRP"; m.summary += ": IDRP"; addLine(m.detail, "IDRP (ISO 10747) inter-domain routeing, %zu octets", un); }
        else { m.summary += ": compressed CLNP header"; addLine(m.detail, "ICAO SNDCF compressed (LREF) CLNP header: %s", hexs(u, un).c_str()); }
        return;
    }
    switch (pt) {
    case 0x0B: case 0x0F: {
        const bool req = pt == 0x0B;
        std::string called, calling;
        size_t o = 3;
        if (n > 3) {
            const int lcalling = p[3] >> 4, lcalled = p[3] & 0xF;
            o = 4;
            const size_t bytes = (size_t)(lcalling + lcalled + 1) / 2;
            if (o + bytes <= n) { called = bcd(p + o, lcalled, 0); calling = bcd(p + o, lcalling, lcalled); o += bytes; }
        }
        int facLen = -1;
        if (o < n) { facLen = p[o++]; o += (size_t)facLen; }
        snprintf(b, sizeof b, "X.25 %s, LCN %d", req ? "call request" : "call accepted", lcn);
        m.summary = b;
        addLine(m.detail, "X.25 (ISO 8208): %s, logical channel %d", req ? "call request" : "call accepted", lcn);
        addLine(m.detail, "  called DTE %s, calling DTE %s, facilities %d octets", called.empty() ? "-" : called.c_str(), calling.empty() ? "-" : calling.c_str(), std::max(0, facLen));
        if (o < n) addLine(m.detail, "  call user data: %s%s", hexs(p + o, n - o).c_str(), p[o] == 0xC1 ? " (ICAO SNDCF)" : "");
        return;
    }
    case 0x13: case 0x17: {
        snprintf(b, sizeof b, "X.25 clear %s, LCN %d", pt == 0x13 ? "request" : "confirm", lcn);
        m.summary = b;
        addLine(m.detail, "X.25 (ISO 8208): clear %s, logical channel %d%s", pt == 0x13 ? "request" : "confirmation", lcn, n > 3 ? "" : "");
        if (pt == 0x13 && n >= 5) addLine(m.detail, "  cause %d, diagnostic %d", p[3], p[4]);
        return;
    }
    case 0x1B: case 0x1F: case 0xFB: case 0xFF: case 0x23: case 0x27: case 0xF1: {
        const char* nm = pt == 0x1B ? "reset request" : pt == 0x1F ? "reset confirmation" : pt == 0xFB ? "restart request" : pt == 0xFF ? "restart confirmation" : pt == 0x23 ? "interrupt" : pt == 0x27 ? "interrupt confirmation" : "diagnostic";
        snprintf(b, sizeof b, "X.25 %s, LCN %d", nm, lcn);
        m.summary = b;
        addLine(m.detail, "X.25 (ISO 8208): %s, logical channel %d", nm, lcn);
        return;
    }
    default:
        if ((pt & 0x1F) == 0x01 || (pt & 0x1F) == 0x05 || (pt & 0x1F) == 0x09) {
            const char* nm = (pt & 0x1F) == 0x01 ? "RR" : (pt & 0x1F) == 0x05 ? "RNR" : "REJ";
            snprintf(b, sizeof b, "X.25 %s, LCN %d, P(R) %d", nm, lcn, pt >> 5);
            m.summary = b;
            addLine(m.detail, "X.25 (ISO 8208): %s, logical channel %d, P(R) %d", nm, lcn, pt >> 5);
            return;
        }
        snprintf(b, sizeof b, "X.25 packet type %02X, LCN %d", pt, lcn);
        m.summary = b;
        addLine(m.detail, "X.25 (ISO 8208): packet type %02X, logical channel %d, %zu octets", pt, lcn, n);
    }
}

void readXid(const uint8_t* p, size_t n, Vdl2Message& m) {
    m.proto = "XID";
    int h = 1, r = 1;
    std::string lines;
    bool any = false;
    if (n >= 3 && p[0] == 0x82) {
        size_t o = 1;
        while (o + 3 <= n) {
            const uint8_t gid = p[o];
            const size_t glen = ((size_t)p[o + 1] << 8) | p[o + 2];
            o += 3;
            if (o + glen > n) break;
            const uint8_t* g = p + o;
            size_t k = 0;
            while (k + 2 <= glen) {
                const uint8_t id = g[k], len = g[k + 1];
                const uint8_t* v = g + k + 2;
                if (k + 2 + len > glen) break;
                any = true;
                if (gid == 0xF0) {
                    switch (id) {
                    case 0x00: addLine(lines, "  parameter set %.*s", (int)len, (const char*)v); break;
                    case 0x01: if (len >= 1) { r = v[0] & 1; h = (v[0] >> 1) & 1; addLine(lines, "  connection management: h %d, r %d", h, r); } break;
                    case 0x03: if (len >= 1) addLine(lines, "  XID sequencing: seq %d, retry %d", v[0] & 7, v[0] >> 4); break;
                    case 0x83: addLine(lines, "  destination airport %.*s", (int)len, (const char*)v); break;
                    case 0xC1: addLine(lines, "  airport coverage %.*s", (int)len, (const char*)v); break;
                    case 0xC3: addLine(lines, "  nearest airport %.*s", (int)len, (const char*)v); break;
                    case 0x84: case 0xC8:
                        if (len >= 3) {
                            const double la = signed12((v[0] << 4) | (v[1] >> 4)) / 10.0, lo = signed12(((v[1] & 0xF) << 8) | v[2]) / 10.0;
                            m.hasPos = true; m.lat = la; m.lon = lo;
                            if (id == 0x84 && len >= 4) { m.hasAlt = true; m.altFt = v[3] * 1000; }
                            m.posKind = id == 0x84 ? "XID aircraft location" : "XID ground station location";
                            addLine(lines, "  %s %.1f%c %.1f%c%s", id == 0x84 ? "aircraft location" : "ground station location", std::fabs(la), la < 0 ? 'S' : 'N',
                                    std::fabs(lo), lo < 0 ? 'W' : 'E', id == 0x84 && len >= 4 ? (" " + std::to_string(v[3] * 1000) + " ft").c_str() : "");
                        }
                        break;
                    case 0xC0: addLine(lines, "  frequency support list, %d octets", len); break;
                    case 0x82: addLine(lines, "  alternate ground stations, %d octets", len); break;
                    case 0x81: addLine(lines, "  modulation support %s", len ? hexs(v, len).c_str() : "-"); break;
                    default: addLine(lines, "  VDL parameter %02X: %s", id, hexs(v, len).c_str()); break;
                    }
                } else if (gid == 0x80) {
                    addLine(lines, "  public parameter %d: %s", id, hexs(v, len).c_str());
                }
                k += 2u + len;
            }
            o += glen;
        }
    }
    const int type = ((int)m.response << 3) | ((int)m.pf << 2) | (h << 1) | r;
    static const char* names[16] = {"XID", "XID_CMD_LCR", "XID_CMD_HO", "GSIF", "XID_CMD_LE", "XID", "XID_CMD_HO", "XID_CMD_LPM",
                                    "XID", "XID", "XID", "XID", "XID_RSP_LE", "XID_RSP_LCR", "XID_RSP_HO", "XID_RSP_LPM"};
    m.frameName = names[type & 15];
    const char* desc = m.frameName == "GSIF" ? "ground station information" : m.frameName == "XID_CMD_LE" ? "link establishment" : m.frameName == "XID_RSP_LE" ? "link establishment response"
                     : m.frameName == "XID_CMD_HO" ? "handoff" : m.frameName == "XID_RSP_HO" ? "handoff response" : m.frameName == "XID_CMD_LCR" ? "link connection refused" : "link parameters";
    m.summary = m.frameName + " (" + desc + ")";
    addLine(m.detail, "XID: %s%s", desc, any ? "" : ", no parameters read");
    m.detail += lines;
}
#endif // DECT2_RUST_VDL2

void readAcars(const uint8_t* p, size_t n, Vdl2Message& m) {
    // p: from the SOH; mode .. suffix, check bytes, an optional DEL
    m.proto = "ACARS";
    m.acars = true;
    size_t end = n;
    if (end && p[end - 1] == 0x7F) end--;
    if (end < 1 + 13 + 2) { m.summary = "ACARS: cut short"; addLine(m.detail, "ACARS: %zu octets, too short", n); return; }
    const uint8_t* blk = p + 1;
    const size_t bl = end - 1 - 2;                     // mode .. suffix
    m.acarsCrcOk = acarsCrc(blk, bl + 2) == 0;
    std::vector<uint8_t> t7(bl);
    for (size_t i = 0; i < bl; i++) t7[i] = blk[i] & 0x7F;
    AcarsMessage a;
    if (!acarsParseBlock(t7.data(), (int)bl, a)) { m.summary = "ACARS: not a valid block"; addLine(m.detail, "ACARS: the block layout is not valid"); return; }
    m.reg = a.reg; m.flight = a.flightId; m.label = a.label; m.labelText = a.labelText; m.msgNum = a.msgNum; m.acarsText = a.text; m.blockId = a.blockId;
    std::string s = "ACARS " + a.label;
    if (!a.labelText.empty()) s += " (" + a.labelText + ")";
    if (!a.flightId.empty()) s += " " + a.flightId;
    std::string one;
    for (char c : a.text) one += (c == '\n' || c == '\r') ? ' ' : c;
    if (!one.empty()) s += ": " + (one.size() > 80 ? one.substr(0, 80) + "..." : one);
    m.summary = s;
    addLine(m.detail, "ACARS: mode %c, registration %s, label %s%s%s, block id %c%s", a.mode ? a.mode : '-', a.reg.empty() ? "-" : a.reg.c_str(), a.label.c_str(),
            a.labelText.empty() ? "" : " ", a.labelText.c_str(), a.blockId ? a.blockId : '-', m.acarsCrcOk ? "" : ", check sequence FAILED");
    if (!a.msgNum.empty() || !a.flightId.empty()) addLine(m.detail, "  message %s, flight %s", a.msgNum.empty() ? "-" : a.msgNum.c_str(), a.flightId.empty() ? "-" : a.flightId.c_str());
    if (!a.decoded.empty()) addLine(m.detail, "  reading: %s", a.decoded.c_str());
    const bool uplink = !m.fromAir;
    AeroAdscMessage adsc;
    if (aeroDecodeAdsc(a.text, uplink, adsc)) {
        const std::string adscText = aeroDescribeAdsc(adsc);
        if (!adscText.empty()) {
            addLine(m.detail, "ADS-C (FANS-1/A):");
            m.detail += adscText;
            if (m.detail.back() != '\n') m.detail += '\n';
        }
        if (adsc.crcOk) {
            if (adsc.hasEarth && adsc.trackValid) {
                m.hasKin = true;
                m.kinTrackDeg = adsc.trackDeg;
                m.groundKt = adsc.groundKt;
                m.vsFpm = adsc.vsFpm;
            }
            if (adsc.hasPos) {
                m.hasPos = true;
                m.lat = adsc.lat;
                m.lon = adsc.lon;
                m.hasAlt = true;
                m.altFt = adsc.altFt;
                m.posKind = std::string("ADS-C ") + aeroAdscTagName(adsc.posTag);
            }
            if (!adsc.flightId.empty() && m.flight.empty()) m.flight = adsc.flightId;
        }
        if (adsc.found) m.summary = "ACARS " + a.label + " ADS-C" + (m.hasPos ? " position report" : m.hasKin ? " earth reference" : "");
    }
    if (!m.hasPos) {
        AeroPosition pos;
        if (aeroParseTextPosition(a.label, a.text, pos) && pos.valid) {
            m.hasPos = true;
            m.lat = pos.lat;
            m.lon = pos.lon;
            m.hasAlt = pos.hasAlt;
            m.altFt = pos.altFt;
            m.posKind = pos.kind;
            if (!pos.flightId.empty() && m.flight.empty()) m.flight = pos.flightId;
            if (pos.hasTrack && pos.hasSpeed) {
                m.hasKin = true;
                m.kinTrackDeg = pos.trackDeg;
                m.groundKt = pos.speedKt;
            }
        }
    }
    // FANS-1/A CPDLC (ARINC 622 IMI AT1 / CR1 / CC1 / DR1): found and named, the ASN.1 PER body is not decoded
    for (const char* imi : {".AT1", ".CR1", ".CC1", ".DR1"}) {
        const size_t at = a.text.find(imi);
        if (at != std::string::npos) {
            const std::string what = imi[1] == 'A' ? "CPDLC message" : imi[1] == 'C' && imi[2] == 'R' ? "CPDLC connect request" : imi[2] == 'C' ? "CPDLC connect confirm" : "CPDLC disconnect";
            addLine(m.detail, "FANS-1/A %s (IMI %s), ground facility %s, %zu hex digits of ASN.1 PER (not decoded)", what.c_str(), imi + 1, a.text.substr(at > 7 ? at - 7 : 0, at > 7 ? 7 : at).c_str(),
                    a.text.size() - at - 4);
            m.summary = "ACARS " + a.label + " FANS-1/A " + what;
            break;
        }
    }
    if (m.hasPos) addLine(m.detail, "  position %.4f %.4f%s (%s)", m.lat, m.lon, m.hasAlt ? (" " + std::to_string(m.altFt) + " ft").c_str() : "", m.posKind.c_str());
}

} // namespace

// The ACARS reader (ARINC 618 block, ADS-C, position text) is not part of the Rust crate; the Rust-backed vdl2ParseFrame calls it through this.
void vdl2ReadAcars(const uint8_t* p, size_t n, Vdl2Message& m) { readAcars(p, n, m); }

#ifndef DECT2_RUST_VDL2
bool vdl2ParseFrame(const uint8_t* f, size_t n, Vdl2Message& m) {
    if (n < 9) return false;
    const Vdl2Addr d = vdl2GetAddr(f), s = vdl2GetAddr(f + 4);
    m.dst = d.addr; m.dstType = d.type; m.src = s.addr; m.srcType = s.type;
    m.onGround = d.status; m.response = s.status;
    m.fromAir = s.type == 1; m.fromGround = s.type == 4 || s.type == 5;
    const uint8_t c = f[8];
    const uint8_t* info = f + 9;
    const size_t in = n - 9;
    m.detail.clear();
    addLine(m.detail, "AVLC: %06X (%s) to %06X (%s), %s, %s", s.addr, vdl2AddrTypeName(s.type), d.addr, vdl2AddrTypeName(d.type), m.onGround ? "on ground" : "airborne", m.response ? "response" : "command");
    if (!(c & 1)) {
        m.frameKind = 'I'; m.frameName = "I"; m.ns = (c >> 1) & 7; m.nr = (c >> 5) & 7; m.pf = (c >> 4) & 1;
        addLine(m.detail, "  I frame, N(S) %d, N(R) %d%s, %zu octets of information", m.ns, m.nr, m.pf ? ", poll" : "", in);
        if (in > 3 && info[0] == 0xFF && info[1] == 0xFF && info[2] == 0x01) readAcars(info + 2, in - 2, m);
        else if (in) readX25(info, in, m);
        else m.summary = "I frame, no information";
    } else if ((c & 3) == 1) {
        static const char* sn[4] = {"RR", "RNR", "REJ", "SREJ"};
        m.frameKind = 'S'; m.frameName = sn[(c >> 2) & 3]; m.nr = (c >> 5) & 7; m.pf = (c >> 4) & 1;
        m.summary = m.frameName + " N(R) " + std::to_string(m.nr);
        addLine(m.detail, "  S frame %s, N(R) %d%s", m.frameName.c_str(), m.nr, m.pf ? ", poll/final" : "");
    } else {
        m.frameKind = 'U'; m.pf = (c >> 4) & 1;
        const int mf = (c >> 2) & 0x3B;
        switch (mf) {
        case 0x00: m.frameName = "UI"; break; case 0x03: m.frameName = "DM"; break; case 0x10: m.frameName = "DISC"; break;
        case 0x18: m.frameName = "UA"; break; case 0x21: m.frameName = "FRMR"; break; case 0x2B: m.frameName = "XID"; break;
        case 0x38: m.frameName = "TEST"; break; default: m.frameName = "U?"; break;
        }
        addLine(m.detail, "  U frame %s%s, %zu octets of information", m.frameName.c_str(), m.pf ? ", poll/final" : "", in);
        if (mf == 0x2B) readXid(info, in, m);
        else if (mf == 0x00 && in > 3 && info[0] == 0xFF && info[1] == 0xFF && info[2] == 0x01) readAcars(info + 2, in - 2, m);
        else m.summary = m.frameName + (in ? " (" + std::to_string(in) + " octets)" : "");
    }
    return true;
}
#endif // DECT2_RUST_VDL2

// ---------------------------------------------------------------- builders

std::vector<uint8_t> vdl2AcarsInfo(const std::string& reg, const std::string& label, char blockId, const std::string& text, char ack, char mode) {
    AcarsBlockSpec s;
    s.mode = mode;
    s.reg = reg;
    s.ack = (uint8_t)ack;
    s.label[0] = label.size() > 0 ? label[0] : 'H';
    s.label[1] = label.size() > 1 ? label[1] : '1';
    s.blockId = blockId;
    s.text = text;
    s.hasText = !text.empty();
    std::vector<uint8_t> f = acarsBuildFrame(s, 0);    // SYN SYN SOH ... DEL
    std::vector<uint8_t> out = {0xFF, 0xFF};
    out.insert(out.end(), f.begin() + 2, f.end());
    return out;
}

std::vector<uint8_t> vdl2X25ClnpData(int lcn, int ps, int pr, const std::vector<uint8_t>& src, const std::vector<uint8_t>& dst, const std::vector<uint8_t>& user) {
    std::vector<uint8_t> o = {(uint8_t)(0x10 | ((lcn >> 8) & 0xF)), (uint8_t)(lcn & 0xFF), (uint8_t)(((pr & 7) << 5) | ((ps & 7) << 1))};
    std::vector<uint8_t> h = {0x81, 0, 0x01, 60, 0x1C, 0, 0, 0, 0};
    h.push_back((uint8_t)dst.size()); h.insert(h.end(), dst.begin(), dst.end());
    h.push_back((uint8_t)src.size()); h.insert(h.end(), src.begin(), src.end());
    h[1] = (uint8_t)h.size();
    std::vector<uint8_t> tp = {2, 0xF0, 0x80};          // COTP DT, end of TSDU, sequence 0
    tp.insert(tp.end(), user.begin(), user.end());
    const size_t seg = h.size() + tp.size();
    h[5] = (uint8_t)(seg >> 8); h[6] = (uint8_t)seg;
    o.insert(o.end(), h.begin(), h.end());
    o.insert(o.end(), tp.begin(), tp.end());
    return o;
}

std::vector<uint8_t> vdl2X25CallRequest(int lcn, const std::string& called, const std::string& calling) {
    std::vector<uint8_t> o = {(uint8_t)(0x10 | ((lcn >> 8) & 0xF)), (uint8_t)(lcn & 0xFF), 0x0B, (uint8_t)((calling.size() << 4) | (called.size() & 0xF))};
    const std::string all = called + calling;
    for (size_t i = 0; i < all.size(); i += 2) {
        const int hi = all[i] - '0', lo = i + 1 < all.size() ? all[i + 1] - '0' : 0;
        o.push_back((uint8_t)((hi << 4) | lo));
    }
    o.push_back(0);                                     // no facilities
    o.push_back(0xC1); o.push_back(0x00);               // ICAO SNDCF, no compression options
    return o;
}

namespace {
void putLoc(std::vector<uint8_t>& g, double lat, double lon) {
    const int la = (int)std::lround(lat * 10) & 0xFFF, lo = (int)std::lround(lon * 10) & 0xFFF;
    g.push_back((uint8_t)(la >> 4)); g.push_back((uint8_t)(((la & 0xF) << 4) | (lo >> 8))); g.push_back((uint8_t)(lo & 0xFF));
}
std::vector<uint8_t> xid(const std::vector<uint8_t>& vdlParams) {
    std::vector<uint8_t> o = {0x82, 0x80, 0x00, 0x03, 0x01, 0x01, 0x00};   // public group: parameter set id
    o.push_back(0xF0); o.push_back((uint8_t)(vdlParams.size() >> 8)); o.push_back((uint8_t)vdlParams.size());
    o.insert(o.end(), vdlParams.begin(), vdlParams.end());
    return o;
}
void param(std::vector<uint8_t>& g, uint8_t id, const std::vector<uint8_t>& v) { g.push_back(id); g.push_back((uint8_t)v.size()); g.insert(g.end(), v.begin(), v.end()); }
} // namespace

std::vector<uint8_t> vdl2XidGsif(double lat, double lon, const std::string& airports) {
    std::vector<uint8_t> g;
    param(g, 0x00, {'V', '2'});
    std::vector<uint8_t> loc; putLoc(loc, lat, lon);
    param(g, 0xC8, loc);
    param(g, 0xC1, std::vector<uint8_t>(airports.begin(), airports.end()));
    return xid(g);
}

std::vector<uint8_t> vdl2XidAircraftLe(double lat, double lon, int altFt, const std::string& dest) {
    std::vector<uint8_t> g;
    param(g, 0x00, {'V', '2'});
    param(g, 0x01, {0x00});
    std::vector<uint8_t> loc; putLoc(loc, lat, lon); loc.push_back((uint8_t)std::max(0, std::min(255, (altFt + 500) / 1000)));
    param(g, 0x84, loc);
    param(g, 0x83, std::vector<uint8_t>(dest.begin(), dest.end()));
    return xid(g);
}

} // namespace dect2
