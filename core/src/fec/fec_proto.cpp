// Shared forward error correction (the C++ implementation; the Rust crate onair_fec replaces it when DECT2_RUST_FEC is defined, see fec_proto_rs.cpp):
// byte-wise Reed-Solomon (204,188) of DVB-T / ISDB-T, (207,187) of ATSC, (120,110) of DAB, and the DVB-T2 binary BCH codes. The code is the one that used to sit in
// dvbt.cpp, atsc.cpp, dab_fec.cpp and t2fec.cpp, moved here unchanged (each Reed-Solomon variant in its own namespace: they all have a Gf, RsRows, ...).
#include "dect2/fec/fec_proto.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <vector>
#ifndef DECT2_RUST_FEC
#include "dect2/t2/t2fec_tables.h"

namespace dect2::fecp {

// ---------------------------------------------------------------- DVB-T RS(204,188), g(x) = prod_{i=0}^{15} (x - alpha^i), EN 300 744 section 4.4
namespace rs204 {
namespace {
struct Gf {
    uint8_t exp[512], log[256];
    Gf() {
        unsigned x = 1;
        for (int i = 0; i < 255; i++) { exp[i] = (uint8_t)x; log[x] = (uint8_t)i; x <<= 1; if (x & 0x100) x ^= 0x11D; }
        for (int i = 255; i < 512; i++) exp[i] = exp[i - 255];
        log[0] = 0;
    }
    uint8_t mul(uint8_t a, uint8_t b) const { return (a && b) ? exp[log[a] + log[b]] : 0; }
    uint8_t inv(uint8_t a) const { return exp[255 - log[a]]; }
};
const Gf& gf() { static Gf g; return g; }
// g(x) = prod_{i=0}^{15} (x - alpha^i): coefficients g[0..15] (x^16 implicit)
const std::array<uint8_t, 16>& rsGen() {
    static std::array<uint8_t, 16> g = [] {
        std::array<uint8_t, 17> p{};
        p[0] = 1;
        const Gf& f = gf();
        for (int i = 0; i < 16; i++) {          // multiply by (x + alpha^i)
            std::array<uint8_t, 17> n{};
            for (int j = 0; j <= i; j++) { n[j + 1] ^= p[j]; n[j] ^= f.mul(p[j], f.exp[i]); }
            p = n;
        }
        std::array<uint8_t, 16> r;
        for (int i = 0; i < 16; i++) r[i] = p[i];
        return r;
    }();
    return g;
}
} // namespace

namespace {
// fb * generator for every feedback byte: one row of 16 bytes per value, so that one division step is two 64-bit exclusive ors
struct RsRows {
    alignas(8) uint8_t row[256][16];
    RsRows() {
        const Gf& f = gf();
        const auto& G = rsGen();
        for (int v = 0; v < 256; v++) {
            for (int j = 0; j < 15; j++) row[v][j] = f.mul((uint8_t)v, G[15 - j]);
            row[v][15] = f.mul((uint8_t)v, G[0]);
        }
    }
};
const RsRows& rsRows() { static const RsRows r; return r; }
}

void rsEncode(const uint8_t* in, uint8_t* out) {
    const RsRows& R = rsRows();
    uint8_t b[188 + 16 + 8] = {};   // the message, then the remainder builds up in the bytes behind it
    memcpy(b, in, 188);
    for (int i = 0; i < 188; i++) {
        const uint8_t fb = b[i];
        if (!fb) continue;
        uint64_t x[2], y[2];
        memcpy(x, b + i + 1, 16); memcpy(y, R.row[fb], 16);
        x[0] ^= y[0]; x[1] ^= y[1];
        memcpy(b + i + 1, x, 16);
    }
    memcpy(out, in, 188);
    memcpy(out + 188, b + 188, 16);
}

namespace {
// products with alpha^i for every byte, so that a syndrome is one table lookup and one exclusive or per byte
struct RsMulTab {
    uint8_t t[16][256];
    RsMulTab() { const Gf& f = gf(); for (int i = 0; i < 16; i++) for (int v = 0; v < 256; v++) t[i][v] = f.mul((uint8_t)v, f.exp[i]); }
};
const RsMulTab& rsMulTab() { static RsMulTab m; return m; }
inline bool rsSyndromes(const uint8_t* r, uint8_t* S) {
    // the remainder of r(x) modulo g(x) is zero exactly when every syndrome is, and S_i = rem(alpha^i)
    const RsRows& R = rsRows();
    const RsMulTab& m = rsMulTab();
    uint8_t b[204 + 16 + 8] = {};
    memcpy(b, r, 204);
    for (int i = 0; i < 188; i++) {
        const uint8_t fb = b[i];
        if (!fb) continue;
        uint64_t x[2], y[2];
        memcpy(x, b + i + 1, 16); memcpy(y, R.row[fb], 16);
        x[0] ^= y[0]; x[1] ^= y[1];
        memcpy(b + i + 1, x, 16);
    }
    const uint8_t* rem = b + 188;
    uint8_t any = 0;
    for (int j = 0; j < 16; j++) any |= rem[j];
    if (!any) { memset(S, 0, 16); return false; }
    for (int i = 0; i < 16; i++) { uint8_t s = 0; for (int j = 0; j < 16; j++) s = m.t[i][s] ^ rem[j]; S[i] = s; }
    return true;
}
}

int rsDecode(uint8_t* r) {
    const Gf& f = gf();
    // syndromes S_i = r(alpha^i), r as polynomial with byte 0 the highest degree (203)
    uint8_t S[16];
    if (!rsSyndromes(r, S)) return 0;
    // Berlekamp-Massey
    uint8_t C[17] = {1}, B[17] = {1};
    int L = 0, m = 1;
    uint8_t b = 1;
    for (int n = 0; n < 16; n++) {
        uint8_t d = S[n];
        for (int i = 1; i <= L; i++) d ^= f.mul(C[i], S[n - i]);
        if (d == 0) { m++; continue; }
        uint8_t T[17];
        memcpy(T, C, 17);
        const uint8_t coef = f.mul(d, f.inv(b));
        for (int i = 0; i + m < 17; i++) C[i + m] ^= f.mul(coef, B[i]);
        if (2 * L <= n) { L = n + 1 - L; memcpy(B, T, 17); b = d; m = 1; } else m++;
    }
    if (L > 8) return -1;
    // Chien search: error at degree p (position j = 203 - p) when C(alpha^{-p}) = 0
    int pos[8], cnt = 0;
    for (int p = 0; p < 204; p++) {
        const uint8_t x = f.exp[(255 - p) % 255];       // alpha^{-p}
        uint8_t v = 0;
        for (int i = L; i >= 0; i--) v = f.mul(v, x) ^ C[i];
        if (v == 0) { if (cnt >= 8) return -1; pos[cnt++] = p; }
    }
    if (cnt != L) return -1;
    // error evaluator Omega(x) = S(x) C(x) mod x^16, Forney with fcr = 0: e = X * Omega(X^-1) / C'(X^-1)
    uint8_t om[16] = {};
    for (int i = 0; i < 16; i++) { uint8_t s = 0; for (int j = 0; j <= std::min(i, L); j++) s ^= f.mul(C[j], S[i - j]); om[i] = s; }
    for (int k = 0; k < cnt; k++) {
        const int p = pos[k];
        const uint8_t X = f.exp[p % 255], xi = f.exp[(255 - p) % 255];
        uint8_t num = 0;
        for (int i = 15; i >= 0; i--) num = f.mul(num, xi) ^ om[i];
        uint8_t den = 0;                                  // C'(x) = sum over odd i of C[i] x^{i-1}
        for (int i = 1; i <= L; i += 2) den ^= f.mul(C[i], f.exp[((255 - p) * (i - 1)) % 255]);
        if (den == 0) return -1;
        const uint8_t e = f.mul(f.mul(num, X), f.inv(den));
        r[203 - p] ^= e;
    }
    // verify
    uint8_t S2[16];
    if (rsSyndromes(r, S2)) return -1;
    return cnt;
}

} // namespace rs204
void rs204Encode(const uint8_t* in, uint8_t* out) { rs204::rsEncode(in, out); }
int rs204Decode(uint8_t* r) { return rs204::rsDecode(r); }

// ---------------------------------------------------------------- ATSC RS(207,187), A/53 part 2 section 6.4.1
namespace rs207 {
// ---------------------------------------------------------------- Reed-Solomon (207,187)
namespace {
struct Gf {
    uint8_t exp[512], log[256];
    Gf() {
        unsigned x = 1;
        for (int i = 0; i < 255; i++) { exp[i] = (uint8_t)x; log[x] = (uint8_t)i; x <<= 1; if (x & 0x100) x ^= 0x11d; }
        for (int i = 255; i < 512; i++) exp[i] = exp[i - 255];
        log[0] = 0;
    }
    uint8_t mul(uint8_t a, uint8_t b) const { return (a && b) ? exp[log[a] + log[b]] : 0; }
    uint8_t inv(uint8_t a) const { return exp[255 - log[a]]; }
};
const Gf& gf() { static const Gf g; return g; }
constexpr int kRoots = 20;
constexpr int kN = 207;
struct RsGen {
    uint8_t g[kRoots + 1];   // g[0] x^20 + ... (highest degree first)
    RsGen() {
        const Gf& f = gf();
        uint8_t p[kRoots + 1] = {1};
        int deg = 0;
        for (int i = 0; i < kRoots; i++) {   // multiply by (x + alpha^i)
            uint8_t q[kRoots + 1] = {};
            for (int j = 0; j <= deg; j++) { q[j] ^= p[j]; q[j + 1] ^= f.mul(p[j], f.exp[i]); }
            deg++;
            memcpy(p, q, sizeof p);
        }
        memcpy(g, p, sizeof g);
    }
};
}

namespace {
// fb * g[j + 1] for every feedback byte fb: one row of 24 bytes (20 used, zero padded) per value, so that one division step is three
// 64-bit exclusive ors
struct RsRows {
    alignas(8) uint8_t row[256][24];
    RsRows() {
        const Gf& f = gf();
        const RsGen gen;
        memset(row, 0, sizeof row);
        for (int v = 0; v < 256; v++) for (int j = 0; j < kRoots; j++) row[v][j] = f.mul((uint8_t)v, gen.g[j + 1]);
    }
};
const RsRows& rsRows() { static const RsRows r; return r; }

// remainder of the 207 bytes in `b` (highest degree first) divided by the generator polynomial, written to rem[20]; `b` must have 24 bytes of
// room behind it (it is the working copy: its first 187 bytes are consumed)
inline void rsRemainder(uint8_t* b, uint8_t* rem) {
    const RsRows& R = rsRows();
    for (int i = 0; i < 187; i++) {
        const uint8_t fb = b[i];
        if (!fb) continue;
        uint64_t x[3], y[3];
        memcpy(x, b + i + 1, 24); memcpy(y, R.row[fb], 24);
        x[0] ^= y[0]; x[1] ^= y[1]; x[2] ^= y[2];
        memcpy(b + i + 1, x, 24);
    }
    memcpy(rem, b + 187, kRoots);
}
}

void rsEncode(const uint8_t* in, uint8_t* out) {
    uint8_t b[187 + 24 + 8] = {};
    memcpy(b, in, 187);
    uint8_t rem[kRoots];
    rsRemainder(b, rem);
    memcpy(out, in, 187);
    memcpy(out + 187, rem, kRoots);
}

int rsDecode(uint8_t* r) {
    const Gf& f = gf();
    uint8_t S[kRoots];
    bool any = false;
    {   // syndromes: the remainder of r(x) modulo g(x) is zero exactly when all of them are, and S_i = rem(alpha^i)
        uint8_t w[kN + 24 + 8] = {}, rem[kRoots];
        memcpy(w, r, kN);
        rsRemainder(w, rem);
        for (int j = 0; j < kRoots; j++) any |= rem[j] != 0;
        if (!any) return 0;
        for (int i = 0; i < kRoots; i++) {
            uint8_t s = 0;
            for (int j = 0; j < kRoots; j++) s = f.mul(s, f.exp[i]) ^ rem[j];
            S[i] = s;
        }
    }
    uint8_t C[kRoots + 1] = {1}, B[kRoots + 1] = {1};
    int L = 0, m = 1;
    uint8_t b = 1;
    for (int n = 0; n < kRoots; n++) {
        uint8_t d = S[n];
        for (int i = 1; i <= L; i++) d ^= f.mul(C[i], S[n - i]);
        if (d == 0) { m++; continue; }
        uint8_t T[kRoots + 1];
        memcpy(T, C, sizeof T);
        const uint8_t coef = f.mul(d, f.inv(b));
        for (int i = 0; i + m <= kRoots; i++) C[i + m] ^= f.mul(coef, B[i]);
        if (2 * L <= n) { L = n + 1 - L; memcpy(B, T, sizeof B); b = d; m = 1; } else m++;
    }
    if (L > kRoots / 2) return -1;
    int pos[kRoots / 2], cnt = 0;
    for (int p = 0; p < kN; p++) {
        const uint8_t x = f.exp[(255 - p) % 255];
        uint8_t v = 0;
        for (int i = L; i >= 0; i--) v = f.mul(v, x) ^ C[i];
        if (v == 0) { if (cnt >= kRoots / 2) return -1; pos[cnt++] = p; }
    }
    if (cnt != L) return -1;
    uint8_t om[kRoots] = {};
    for (int i = 0; i < kRoots; i++) { uint8_t s = 0; for (int j = 0; j <= std::min(i, L); j++) s ^= f.mul(C[j], S[i - j]); om[i] = s; }
    for (int k = 0; k < cnt; k++) {
        const int p = pos[k];
        const uint8_t X = f.exp[p % 255], xi = f.exp[(255 - p) % 255];
        uint8_t num = 0;
        for (int i = kRoots - 1; i >= 0; i--) num = f.mul(num, xi) ^ om[i];
        uint8_t den = 0;
        for (int i = 1; i <= L; i += 2) den ^= f.mul(C[i], f.exp[((255 - p) * (i - 1)) % 255]);
        if (den == 0) return -1;
        r[kN - 1 - p] ^= f.mul(f.mul(num, X), f.inv(den));
    }
    {
        uint8_t w[kN + 24 + 8] = {}, rem[kRoots];
        memcpy(w, r, kN);
        rsRemainder(w, rem);
        for (int j = 0; j < kRoots; j++) if (rem[j]) return -1;
    }
    return cnt;
}

} // namespace rs207
void rs207Encode(const uint8_t* in, uint8_t* out) { rs207::rsEncode(in, out); }
int rs207Decode(uint8_t* r) { return rs207::rsDecode(r); }

// ---------------------------------------------------------------- DAB RS(120,110) over GF(256), generator roots alpha^0 .. alpha^9, EN 300 401 section 5.3.3
namespace rs120 {

namespace {
struct Gf {
    uint8_t exp[512];
    int log[256];
    uint8_t gen[11];   // generator polynomial, highest power first
    Gf() {
        int x = 1;
        for (int i = 0; i < 255; i++) { exp[i] = (uint8_t)x; log[x] = i; x <<= 1; if (x & 0x100) x ^= 0x11d; }
        for (int i = 255; i < 512; i++) exp[i] = exp[i - 255];
        log[0] = 0;
        int g[11] = {1};
        int deg = 0;
        for (int r = 0; r < 10; r++) {   // multiply by (x + alpha^r)
            for (int i = deg + 1; i >= 1; i--) g[i] = g[i - 1] ^ mul(g[i], exp[r]);
            g[0] = mul(g[0], exp[r]);
            deg++;
        }
        // g[] now holds lowest power first
        for (int i = 0; i <= 10; i++) gen[i] = (uint8_t)g[10 - i];
    }
    int mul(int a, int b) const { return (a && b) ? exp[log[a] + log[b]] : 0; }
    int inv(int a) const { return exp[255 - log[a]]; }
};
const Gf& gf() { static const Gf g; return g; }
} // namespace

void rsEncode120(const uint8_t* data, uint8_t* cw) {
    const Gf& G = gf();
    uint8_t rem[10] = {0};
    for (int i = 0; i < 110; i++) {
        cw[i] = data[i];
        const int fb = data[i] ^ rem[0];
        for (int j = 0; j < 9; j++) rem[j] = (uint8_t)(rem[j + 1] ^ G.mul(fb, G.gen[j + 1]));
        rem[9] = (uint8_t)G.mul(fb, G.gen[10]);
    }
    for (int j = 0; j < 10; j++) cw[110 + j] = rem[j];
}

int rsDecode120(uint8_t* cw) {
    const Gf& G = gf();
    const int n = 120, nr = 10;
    int syn[10];
    bool any = false;
    for (int i = 0; i < nr; i++) {
        int s = 0;
        for (int j = 0; j < n; j++) s = G.mul(s, G.exp[i]) ^ cw[j];
        syn[i] = s;
        any |= s != 0;
    }
    if (!any) return 0;
    // Berlekamp-Massey
    int C[11] = {1}, B[11] = {1}, L = 0, m = 1, b = 1;
    for (int k = 0; k < nr; k++) {
        int d = syn[k];
        for (int i = 1; i <= L; i++) d ^= G.mul(C[i], syn[k - i]);
        if (d == 0) { m++; continue; }
        int T[11];
        std::memcpy(T, C, sizeof T);
        const int coef = G.mul(d, G.inv(b));
        for (int i = 0; i + m <= nr; i++) C[i + m] ^= G.mul(coef, B[i]);
        if (2 * L <= k) { L = k + 1 - L; std::memcpy(B, T, sizeof B); b = d; m = 1; } else m++;
    }
    if (L > 5) return -1;
    // Chien search over the 120 positions (power of alpha = n - 1 - position)
    int pos[10], np = 0;
    for (int j = 0; j < n; j++) {
        const int p = n - 1 - j;
        const int xinv = (255 - p) % 255;   // log of alpha^-p
        int v = 0;
        for (int i = 0; i <= L; i++) v ^= G.mul(C[i], G.exp[(xinv * i) % 255]);
        if (v == 0) { if (np < 10) pos[np] = j; np++; }
    }
    if (np != L) return -1;
    int omega[10] = {0};
    for (int i = 0; i < nr; i++) for (int j = 0; j <= i; j++) omega[i] ^= G.mul(syn[j], C[i - j]);
    uint8_t out[120];
    std::memcpy(out, cw, sizeof out);
    for (int e = 0; e < np; e++) {
        const int p = n - 1 - pos[e];
        const int xinv = (255 - p) % 255;
        int num = 0, den = 0;
        for (int i = 0; i < nr; i++) num ^= G.mul(omega[i], G.exp[(xinv * i) % 255]);
        for (int i = 1; i <= L; i += 2) den ^= G.mul(C[i], G.exp[(xinv * (i - 1)) % 255]);
        if (den == 0) return -1;
        out[pos[e]] ^= (uint8_t)G.mul(G.mul(num, G.inv(den)), G.exp[p % 255]);
    }
    for (int i = 0; i < nr; i++) {   // verify
        int s = 0;
        for (int j = 0; j < n; j++) s = G.mul(s, G.exp[i]) ^ out[j];
        if (s) return -1;
    }
    std::memcpy(cw, out, sizeof out);
    return np;
}

} // namespace rs120
void rs120Encode(const uint8_t* in, uint8_t* out) { rs120::rsEncode120(in, out); }
int rs120Decode(uint8_t* cw) { return rs120::rsDecode120(cw); }

// ---------------------------------------------------------------- DVB-T2 BCH, EN 302 755 section 6.1.1
Bch::Bch(bool shortFrame, int t) : m_(shortFrame ? 14 : 16), t_(t), deg_(m_ * t) {
    const uint32_t prim = shortFrame ? 0x402B : 0x1002D;
    const int n = (1 << m_) - 1;
    exp_.assign(2 * n + 2, 0);
    log_.assign(n + 1, 0);
    uint32_t x = 1;
    for (int i = 0; i < n; i++) {
        exp_[i] = x;
        log_[x] = i;
        x <<= 1;
        if (x & (1u << m_)) x ^= prim;
    }
    for (int i = n; i < 2 * n + 2; i++) exp_[i] = exp_[i - n];
    const uint8_t* const pn[12] = {dect2::k_polyn01, dect2::k_polyn02, dect2::k_polyn03, dect2::k_polyn04, dect2::k_polyn05, dect2::k_polyn06, dect2::k_polyn07, dect2::k_polyn08, dect2::k_polyn09, dect2::k_polyn10, dect2::k_polyn11, dect2::k_polyn12};
    const uint8_t* const ps[12] = {dect2::k_polys01, dect2::k_polys02, dect2::k_polys03, dect2::k_polys04, dect2::k_polys05, dect2::k_polys06, dect2::k_polys07, dect2::k_polys08, dect2::k_polys09, dect2::k_polys10, dect2::k_polys11, dect2::k_polys12};
    const int len = m_ + 1;
    std::vector<uint8_t> g(1, 1);
    for (int k = 0; k < t; k++) {
        const uint8_t* p = shortFrame ? ps[k] : pn[k];
        std::vector<uint8_t> out(g.size() + len - 1, 0);
        for (size_t i = 0; i < g.size(); i++) if (g[i]) for (int j = 0; j < len; j++) if (p[j]) out[i + j] ^= 1;
        g = out;
    }
    gen_ = g; // degree deg_
}

int Bch::fieldMul(int a, int b) const {
    if (!a || !b) return 0;
    return (int)exp_[log_[a] + log_[b]];
}

// remainder of the bit string (first bit = highest power) modulo g(x)
void Bch::remainder(const std::vector<uint8_t>& bits, std::vector<uint8_t>& rem) const {
    const int W = (deg_ + 63) / 64;
    std::vector<uint64_t> g(W, 0), r(W, 0);
    for (int i = 0; i < deg_; i++) if (gen_[i]) g[i >> 6] |= 1ull << (i & 63);
    const int topw = (deg_ - 1) >> 6, topb = (deg_ - 1) & 63;
    const uint64_t lastMask = (deg_ & 63) ? ((1ull << (deg_ & 63)) - 1) : ~0ull;
    auto stepBit = [&](unsigned bit) {
        uint64_t top = (r[topw] >> topb) & 1;
        for (int w = W - 1; w > 0; w--) r[w] = (r[w] << 1) | (r[w - 1] >> 63);
        r[0] = (r[0] << 1) | bit;
        r[W - 1] &= lastMask;
        if (top) for (int w = 0; w < W; w++) r[w] ^= g[w];
    };
    // byte-at-a-time table: tab[t] = (t(x) * x^deg) mod g(x) for the 8 bits t that leave the register
    {
        std::lock_guard<std::mutex> lk(tabMu_);
        if (tab_.empty()) {
            std::vector<uint64_t> save = r;
            tab_.assign((size_t)256 * W, 0);
            for (int t = 0; t < 256; t++) {
                std::fill(r.begin(), r.end(), 0ull);
                for (int b = 7; b >= 0; b--) stepBit((t >> b) & 1);
                for (int z = 0; z < deg_; z++) stepBit(0);
                for (int w = 0; w < W; w++) tab_[(size_t)t * W + w] = r[w];
            }
            r = save;
        }
    }
    size_t k = 0;
    const size_t nb8 = bits.size() & ~(size_t)7;
    for (; k < nb8; k += 8) {
        const unsigned byte = (bits[k] << 7) | (bits[k + 1] << 6) | (bits[k + 2] << 5) | (bits[k + 3] << 4) | (bits[k + 4] << 3) | (bits[k + 5] << 2) | (bits[k + 6] << 1) | bits[k + 7];
        // the 8 highest register bits (deg-1 .. deg-8)
        const int hb = deg_ - 8, hw = hb >> 6, ho = hb & 63;
        uint64_t t = r[hw] >> ho;
        if (ho > 56 && hw + 1 < W) t |= r[hw + 1] << (64 - ho);
        t &= 0xFF;
        for (int w = W - 1; w > 0; w--) r[w] = (r[w] << 8) | (r[w - 1] >> 56);
        r[0] = (r[0] << 8) | byte;
        r[W - 1] &= lastMask;
        const uint64_t* T = &tab_[(size_t)t * W];
        for (int w = 0; w < W; w++) r[w] ^= T[w];
    }
    for (; k < bits.size(); k++) stepBit(bits[k]);
    rem.assign(deg_, 0);
    for (int i = 0; i < deg_; i++) rem[i] = (r[i >> 6] >> (i & 63)) & 1;
}

void Bch::encode(std::vector<uint8_t>& bits, int k) const {
    std::vector<uint8_t> msg(bits.begin(), bits.begin() + k);
    msg.resize(k + deg_, 0); // message * x^deg
    std::vector<uint8_t> rem;
    remainder(msg, rem);
    bits.resize(k + deg_);
    for (int i = 0; i < deg_; i++) bits[k + i] = rem[deg_ - 1 - i];
}

int Bch::decode(std::vector<uint8_t>& bits) const {
    const int n = (int)bits.size();
    std::vector<uint8_t> rem;
    remainder(bits, rem);
    bool zero = true;
    for (auto v : rem) if (v) { zero = false; break; }
    if (zero) return 0;
    const int N = (1 << m_) - 1;
    std::vector<int> S(2 * t_ + 1, 0);
    for (int j = 1; j <= 2 * t_; j++) {
        int s = 0;
        for (int i = 0; i < deg_; i++) if (rem[i]) s ^= (int)exp_[((long long)i * j) % N];
        S[j] = s;
    }
    // Berlekamp-Massey
    std::vector<int> C(2 * t_ + 2, 0), B(2 * t_ + 2, 0), T;
    C[0] = B[0] = 1;
    int L = 0, mm = 1, b = 1;
    for (int nn = 0; nn < 2 * t_; nn++) {
        int d = S[nn + 1];
        for (int i = 1; i <= L; i++) d ^= fieldMul(C[i], S[nn + 1 - i]);
        if (d == 0) { mm++; continue; }
        T = C;
        int coef = fieldMul(d, (int)exp_[N - log_[b]]);
        for (int i = 0; i + mm < (int)C.size(); i++) if (B[i]) C[i + mm] ^= fieldMul(coef, B[i]);
        if (2 * L <= nn) { L = nn + 1 - L; B = T; b = d; mm = 1; } else mm++;
    }
    if (L > t_) return -1;
    // Chien search: error at coefficient position p iff C(alpha^-p) == 0
    std::vector<int> pos;
    for (int p = 0; p < n; p++) {
        int acc = 0;
        for (int i = 0; i <= L; i++) if (C[i]) acc ^= (int)exp_[(log_[C[i]] + (long long)(N - (p % N)) * i) % N];
        if (acc == 0) pos.push_back(p);
    }
    if ((int)pos.size() != L) return -1;
    for (int p : pos) bits[n - 1 - p] ^= 1;
    return L;
}


// ---------------------------------------------------------------- LDPC parity-check tables (the constructor of dsp/ldpc.h's LdpcCode used to build them)
bool ldpcBuild(int k, int n, const std::vector<std::vector<int>>& rows, LdpcTables& t) {
    t = LdpcTables();
    if (k < 0 || n <= k) return false;
    const int m = n - k, q = m / 360;
    if (q == 0) return false;
    for (const auto& r : rows) for (int a : r) if (a < 0) return false;
    t.m = m;
    t.q = q;
    const int groups = (int)rows.size();
    std::vector<std::vector<std::pair<int, int>>> layers(q);
    for (int g = 0; g < groups; g++) for (int a : rows[g]) layers[a % q].push_back({g, a / q});
    t.layerStart.assign(1, 0);
    for (int l = 0; l < q; l++) {
        for (const auto& c : layers[l]) { t.layerGroup.push_back(c.first); t.layerShift.push_back(c.second); }
        t.layerStart.push_back((int)t.layerGroup.size());
    }
    std::vector<std::vector<int>> chk(m);
    int im = 0;
    for (auto& row : rows) {
        for (int c = 0; c < 360; c++, im++) {
            for (int a : row) {
                int p = (int)(((long long)a + (long long)c * q) % m);
                t.encP.push_back(p);
                t.encD.push_back(im);
                chk[p].push_back(im);
            }
        }
    }
    // zig-zag parity structure: check j involves parity bits j and j-1
    for (int j = 0; j < m; j++) {
        chk[j].push_back(k + j);
        if (j > 0) chk[j].push_back(k + j - 1);
    }
    t.chkStart.resize(m + 1);
    t.chkStart[0] = 0;
    for (int j = 0; j < m; j++) t.chkStart[j + 1] = t.chkStart[j] + (int)chk[j].size();
    t.chkVar.reserve(t.chkStart[m]);
    for (int j = 0; j < m; j++) t.chkVar.insert(t.chkVar.end(), chk[j].begin(), chk[j].end());
    return true;
}

} // namespace dect2::fecp
#endif // !DECT2_RUST_FEC

// ---------------------------------------------------------------- K = 7 Viterbi (DVB-T / ISDB-T), NEON / SSE2 / AVX2 / portable. The Rust NEON one (ONAIR_RUST_VITERBI, aarch64) is ~4% slower than this one, so it is opt-in (DECT2_RUST_VITERBI).
#if !defined(DECT2_RUST_FEC) || !defined(ONAIR_RUST_VITERBI) || !defined(__aarch64__)
#if defined(__SSE2__) && !defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
#include <emmintrin.h>
#if defined(__GNUC__) && defined(__x86_64__)
#include <immintrin.h>
#define DECT2_VIT_AVX2 1
#endif
#endif
#if defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
#include <arm_neon.h>
#endif
namespace dect2::fecp {
namespace {
struct Trellis {
    // for each new state ns and predecessor bit b: expected code bits (x,y) as +1 / -1 signs
    int8_t sx[64][2], sy[64][2];
    Trellis() {
        for (int ns = 0; ns < 64; ns++) {
            const int u = ns >> 5;
            for (int b = 0; b < 2; b++) {
                const int s = ((ns & 31) << 1) | b;           // predecessor state
                const unsigned reg = ((unsigned)u << 6) | (unsigned)s;
                sx[ns][b] = __builtin_parity(reg & 0x79) ? -1 : 1;
                sy[ns][b] = __builtin_parity(reg & 0x5B) ? -1 : 1;
            }
        }
    }
};
const Trellis& trellis() { static Trellis t; return t; }

#if defined(__ARM_NEON) && !defined(DECT2_NO_SIMD)
#include <arm_neon.h>
#include <array>
#include <cstdint>
#include <vector>
// NEON path. States 2j and 2j+1 feed new states j and j+32; both branch outputs flip when either the new bit or the oldest
// state bit flips (both generator polynomials have those taps), so one metric M_j = +/-sx +/- sy per butterfly is enough:
//   new[j]    = max(old[2j] + M, old[2j+1] - M)      new[j+32] = max(old[2j] - M, old[2j+1] + M)
struct NeonTables {
    int16_t sigX[32], sigY[32];
    NeonTables() {
        for (int j = 0; j < 32; j++) {
            const unsigned reg = (unsigned)(2 * j);
            sigX[j] = __builtin_parity(reg & 0x79) ? -1 : 1;
            sigY[j] = __builtin_parity(reg & 0x5B) ? -1 : 1;
        }
    }
};
const NeonTables& neonTables() { static NeonTables t; return t; }


void viterbiRangeCpp(const int8_t* soft, size_t steps, std::vector<uint8_t>& bits, long* metricOut) {
    const NeonTables& T = neonTables();
    std::vector<uint64_t> dec(steps);
    long off = 0; // total subtracted by the renormalisation: the absolute path metric is off + pm
    alignas(16) int16_t pm[64] = {};
    int16x8_t sigX[4], sigY[4];
    for (int q = 0; q < 4; q++) { sigX[q] = vld1q_s16(T.sigX + 8 * q); sigY[q] = vld1q_s16(T.sigY + 8 * q); }
    // The 64 path metrics live in eight registers for the whole run (no store and reload per step: that round trip made the
    // chain of steps latency-bound). N[q] holds states 8q..8q+7.
    int16x8_t N[8];
    for (int q = 0; q < 8; q++) N[q] = vdupq_n_s16(0);
    static const uint8_t w8[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    const uint8x16_t wt = vld1q_u8(w8);
    for (size_t t = 0; t < steps; t++) {
        const int16_t a = soft[2 * t], b = soft[2 * t + 1];
        int16x8_t nn[8];
        uint16x8_t lo[4], hi[4];
        for (int q = 0; q < 4; q++) {
            const int16x8x2_t o = vuzpq_s16(N[2 * q], N[2 * q + 1]);   // even states, odd states
            const int16x8_t M = vmlaq_n_s16(vmulq_n_s16(sigX[q], a), sigY[q], b);
            const int16x8_t A = o.val[0], B = o.val[1];
            const int16x8_t p0 = vqaddq_s16(A, M), p1 = vqsubq_s16(B, M);
            const int16x8_t r0 = vqsubq_s16(A, M), r1 = vqaddq_s16(B, M);
            nn[q] = vmaxq_s16(p0, p1);
            nn[4 + q] = vmaxq_s16(r0, r1);
            lo[q] = vcgtq_s16(p1, p0);
            hi[q] = vcgtq_s16(r1, r0);
        }
        // the 64 decision bits (lane i of the 64 comparison results -> bit i): narrow to bytes, weight by 1,2,4..128 and add
        // neighbours three times, which leaves each group of eight lanes as one byte
        const uint8x16_t c0 = vandq_u8(vcombine_u8(vmovn_u16(lo[0]), vmovn_u16(lo[1])), wt);
        const uint8x16_t c1 = vandq_u8(vcombine_u8(vmovn_u16(lo[2]), vmovn_u16(lo[3])), wt);
        const uint8x16_t c2 = vandq_u8(vcombine_u8(vmovn_u16(hi[0]), vmovn_u16(hi[1])), wt);
        const uint8x16_t c3 = vandq_u8(vcombine_u8(vmovn_u16(hi[2]), vmovn_u16(hi[3])), wt);
        uint8x16_t r = vpaddq_u8(vpaddq_u8(c0, c1), vpaddq_u8(c2, c3));
        r = vpaddq_u8(r, r);
        dec[t] = vgetq_lane_u64(vreinterpretq_u64_u8(r), 0);
        // renormalise every 4 steps so int16 never saturates
        if ((t & 3) == 3) {
            int16x8_t mx = nn[0];
            for (int q = 1; q < 8; q++) mx = vmaxq_s16(mx, nn[q]);
            const int16_t top = vmaxvq_s16(mx);
            off += top;
            const int16x8_t m = vdupq_n_s16(top);
            for (int q = 0; q < 8; q++) N[q] = vsubq_s16(nn[q], m);
        } else for (int q = 0; q < 8; q++) N[q] = nn[q];
    }
    for (int q = 0; q < 8; q++) vst1q_s16(pm + 8 * q, N[q]);
    int state = 0;
    int bm = pm[0];
    for (int s = 1; s < 64; s++) if (pm[s] > bm) { bm = pm[s]; state = s; }
    bits.assign(steps, 0);
    for (size_t t = steps; t-- > 0;) {
        bits[t] = (uint8_t)(state >> 5);
        const int b = (int)((dec[t] >> state) & 1);
        state = ((state & 31) << 1) | b;
    }
    if (metricOut) *metricOut = off + bm;
}
#elif defined(__SSE2__) && !defined(DECT2_NO_SIMD)
// SSE2 path (every x86-64 CPU): the same butterflies and int16 metrics as the NEON path above, eight states per register.
struct SseTables {
    alignas(16) int16_t sigX[32], sigY[32];
    SseTables() {
        for (int j = 0; j < 32; j++) {
            const unsigned reg = (unsigned)(2 * j);
            sigX[j] = __builtin_parity(reg & 0x79) ? -1 : 1;
            sigY[j] = __builtin_parity(reg & 0x5B) ? -1 : 1;
        }
    }
};
const SseTables& sseTables() { static SseTables t; return t; }

// states 16q..16q+15: the even ones (old[2j]) to A, the odd ones (old[2j+1]) to B, like NEON's vld2q_s16
inline void deinterleave(const __m128i x0, const __m128i x1, __m128i& A, __m128i& B) {
    A = _mm_packs_epi32(_mm_srai_epi32(_mm_slli_epi32(x0, 16), 16), _mm_srai_epi32(_mm_slli_epi32(x1, 16), 16));
    B = _mm_packs_epi32(_mm_srai_epi32(x0, 16), _mm_srai_epi32(x1, 16));
}
// one bit per lane of a comparison result, lane i to bit i
inline uint8_t maskBits(__m128i m) { return (uint8_t)_mm_movemask_epi8(_mm_packs_epi16(m, _mm_setzero_si128())); }
inline int16_t maxLane(__m128i v) {
    v = _mm_max_epi16(v, _mm_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2)));
    v = _mm_max_epi16(v, _mm_shuffle_epi32(v, _MM_SHUFFLE(2, 3, 0, 1)));
    v = _mm_max_epi16(v, _mm_shufflelo_epi16(v, _MM_SHUFFLE(2, 3, 0, 1)));
    return (int16_t)_mm_cvtsi128_si32(v);
}

void viterbiRangeSse(const int8_t* soft, size_t steps, std::vector<uint8_t>& bits, long* metricOut) {
    const SseTables& T = sseTables();
    std::vector<uint64_t> dec(steps);
    long off = 0; // total subtracted by the renormalisation: the absolute path metric is off + pm
    alignas(16) int16_t pm[64] = {};
    __m128i sigX[4], sigY[4];
    for (int q = 0; q < 4; q++) { sigX[q] = _mm_load_si128((const __m128i*)(T.sigX + 8 * q)); sigY[q] = _mm_load_si128((const __m128i*)(T.sigY + 8 * q)); }
    // the 64 path metrics stay in eight registers (N[q] = states 8q..8q+7) instead of a store and reload per step
    __m128i N[8];
    for (int q = 0; q < 8; q++) N[q] = _mm_setzero_si128();
    for (size_t t = 0; t < steps; t++) {
        const __m128i a = _mm_set1_epi16(soft[2 * t]), b = _mm_set1_epi16(soft[2 * t + 1]);
        __m128i nn[8];
        uint64_t d = 0;
        for (int q = 0; q < 4; q++) {
            __m128i A, B;
            deinterleave(N[2 * q], N[2 * q + 1], A, B);
            const __m128i M = _mm_add_epi16(_mm_mullo_epi16(sigX[q], a), _mm_mullo_epi16(sigY[q], b));
            const __m128i p0 = _mm_adds_epi16(A, M), p1 = _mm_subs_epi16(B, M);
            const __m128i r0 = _mm_subs_epi16(A, M), r1 = _mm_adds_epi16(B, M);
            nn[q] = _mm_max_epi16(p0, p1);
            nn[4 + q] = _mm_max_epi16(r0, r1);
            d |= (uint64_t)maskBits(_mm_cmpgt_epi16(p1, p0)) << (8 * q);
            d |= (uint64_t)maskBits(_mm_cmpgt_epi16(r1, r0)) << (32 + 8 * q);
        }
        dec[t] = d;
        // renormalise every 4 steps so int16 never saturates
        if ((t & 3) == 3) {
            __m128i mx = nn[0];
            for (int q = 1; q < 8; q++) mx = _mm_max_epi16(mx, nn[q]);
            const int16_t top = maxLane(mx);
            off += top;
            const __m128i m = _mm_set1_epi16(top);
            for (int q = 0; q < 8; q++) N[q] = _mm_sub_epi16(nn[q], m);
        } else for (int q = 0; q < 8; q++) N[q] = nn[q];
    }
    for (int q = 0; q < 8; q++) _mm_store_si128((__m128i*)(pm + 8 * q), N[q]);
    int state = 0;
    int bm = pm[0];
    for (int s = 1; s < 64; s++) if (pm[s] > bm) { bm = pm[s]; state = s; }
    bits.assign(steps, 0);
    for (size_t t = steps; t-- > 0;) {
        bits[t] = (uint8_t)(state >> 5);
        const int b = (int)((dec[t] >> state) & 1);
        state = ((state & 31) << 1) | b;
    }
    if (metricOut) *metricOut = off + bm;
}
#ifdef DECT2_VIT_AVX2
// The same trellis with 256-bit registers (16 states each): identical decisions and metrics, half the instructions.
__attribute__((target("avx2")))
inline void deinterleave256(const __m256i x0, const __m256i x1, __m256i& A, __m256i& B) {
    // packs works within the 128-bit halves, so the quarters come out as 0 2 1 3 and are put back in order
    const __m256i a = _mm256_packs_epi32(_mm256_srai_epi32(_mm256_slli_epi32(x0, 16), 16), _mm256_srai_epi32(_mm256_slli_epi32(x1, 16), 16));
    const __m256i b = _mm256_packs_epi32(_mm256_srai_epi32(x0, 16), _mm256_srai_epi32(x1, 16));
    A = _mm256_permute4x64_epi64(a, 0xD8);
    B = _mm256_permute4x64_epi64(b, 0xD8);
}
// the comparison results of two registers (32 lanes) as 32 bits, lane i to bit i
__attribute__((target("avx2")))
inline uint32_t maskBits256(__m256i m0, __m256i m1) {
    return (uint32_t)_mm256_movemask_epi8(_mm256_permute4x64_epi64(_mm256_packs_epi16(m0, m1), 0xD8));
}

__attribute__((target("avx2")))
void viterbiRangeAvx2(const int8_t* soft, size_t steps, std::vector<uint8_t>& bits, long* metricOut) {
    const SseTables& T = sseTables();
    std::vector<uint64_t> dec(steps);
    long off = 0;
    alignas(32) int16_t pm[64] = {};
    const __m256i sigX0 = _mm256_loadu_si256((const __m256i*)T.sigX), sigX1 = _mm256_loadu_si256((const __m256i*)(T.sigX + 16));
    const __m256i sigY0 = _mm256_loadu_si256((const __m256i*)T.sigY), sigY1 = _mm256_loadu_si256((const __m256i*)(T.sigY + 16));
    __m256i N0 = _mm256_setzero_si256(), N1 = N0, N2 = N0, N3 = N0;   // states 0-15, 16-31, 32-47, 48-63
    for (size_t t = 0; t < steps; t++) {
        const __m256i a = _mm256_set1_epi16(soft[2 * t]), b = _mm256_set1_epi16(soft[2 * t + 1]);
        __m256i A0, B0, A1, B1;
        deinterleave256(N0, N1, A0, B0);   // old states 0-31: new states 0-15 and 32-47
        deinterleave256(N2, N3, A1, B1);   // old states 32-63: new states 16-31 and 48-63
        const __m256i M0 = _mm256_add_epi16(_mm256_mullo_epi16(sigX0, a), _mm256_mullo_epi16(sigY0, b));
        const __m256i M1 = _mm256_add_epi16(_mm256_mullo_epi16(sigX1, a), _mm256_mullo_epi16(sigY1, b));
        const __m256i p00 = _mm256_adds_epi16(A0, M0), p10 = _mm256_subs_epi16(B0, M0);
        const __m256i r00 = _mm256_subs_epi16(A0, M0), r10 = _mm256_adds_epi16(B0, M0);
        const __m256i p01 = _mm256_adds_epi16(A1, M1), p11 = _mm256_subs_epi16(B1, M1);
        const __m256i r01 = _mm256_subs_epi16(A1, M1), r11 = _mm256_adds_epi16(B1, M1);
        __m256i n0 = _mm256_max_epi16(p00, p10), n1 = _mm256_max_epi16(p01, p11);
        __m256i n2 = _mm256_max_epi16(r00, r10), n3 = _mm256_max_epi16(r01, r11);
        dec[t] = (uint64_t)maskBits256(_mm256_cmpgt_epi16(p10, p00), _mm256_cmpgt_epi16(p11, p01))
               | (uint64_t)maskBits256(_mm256_cmpgt_epi16(r10, r00), _mm256_cmpgt_epi16(r11, r01)) << 32;
        if ((t & 3) == 3) {
            __m256i mx = _mm256_max_epi16(_mm256_max_epi16(n0, n1), _mm256_max_epi16(n2, n3));
            const int16_t top = maxLane(_mm_max_epi16(_mm256_castsi256_si128(mx), _mm256_extracti128_si256(mx, 1)));
            off += top;
            const __m256i m = _mm256_set1_epi16(top);
            n0 = _mm256_sub_epi16(n0, m); n1 = _mm256_sub_epi16(n1, m); n2 = _mm256_sub_epi16(n2, m); n3 = _mm256_sub_epi16(n3, m);
        }
        N0 = n0; N1 = n1; N2 = n2; N3 = n3;
    }
    _mm256_store_si256((__m256i*)pm, N0); _mm256_store_si256((__m256i*)(pm + 16), N1);
    _mm256_store_si256((__m256i*)(pm + 32), N2); _mm256_store_si256((__m256i*)(pm + 48), N3);
    int state = 0;
    int bm = pm[0];
    for (int s = 1; s < 64; s++) if (pm[s] > bm) { bm = pm[s]; state = s; }
    bits.assign(steps, 0);
    for (size_t t = steps; t-- > 0;) {
        bits[t] = (uint8_t)(state >> 5);
        const int b = (int)((dec[t] >> state) & 1);
        state = ((state & 31) << 1) | b;
    }
    if (metricOut) *metricOut = off + bm;
}
#endif

void viterbiRangeCpp(const int8_t* soft, size_t steps, std::vector<uint8_t>& bits, long* metricOut) {
#ifdef DECT2_VIT_AVX2
    static const bool avx2 = __builtin_cpu_supports("avx2");
    if (avx2) return viterbiRangeAvx2(soft, steps, bits, metricOut);
#endif
    viterbiRangeSse(soft, steps, bits, metricOut);
}
#else
void viterbiRangeCpp(const int8_t* soft, size_t steps, std::vector<uint8_t>& bits, long* metricOut) {
    const Trellis& T = trellis();
    std::vector<uint64_t> dec(steps);
    long off = 0;
    int32_t pm[64] = {}, nm[64];
    for (size_t t = 0; t < steps; t++) {
        const int sx = soft[2 * t], sy = soft[2 * t + 1];
        uint64_t d = 0;
        for (int ns = 0; ns < 64; ns++) {
            const int p0 = ((ns & 31) << 1), p1 = p0 | 1;
            const int m0 = pm[p0] + T.sx[ns][0] * sx + T.sy[ns][0] * sy;
            const int m1 = pm[p1] + T.sx[ns][1] * sx + T.sy[ns][1] * sy;
            if (m1 > m0) { nm[ns] = m1; d |= 1ull << ns; } else nm[ns] = m0;
        }
        int best = nm[0];
        for (int s = 1; s < 64; s++) best = std::max(best, nm[s]);
        off += best;
        for (int s = 0; s < 64; s++) pm[s] = nm[s] - best;
        dec[t] = d;
    }
    int state = 0, bm = pm[0];
    for (int s = 1; s < 64; s++) if (pm[s] > bm) { bm = pm[s]; state = s; }
    bits.assign(steps, 0);
    for (size_t t = steps; t-- > 0;) {
        bits[t] = (uint8_t)(state >> 5);
        const int b = (int)((dec[t] >> state) & 1);
        state = ((state & 31) << 1) | b;
    }
    if (metricOut) *metricOut = off + bm;
}
#endif
} // namespace


void viterbiRange(const int8_t* soft, size_t steps, std::vector<uint8_t>& bits, long* metricOut) { viterbiRangeCpp(soft, steps, bits, metricOut); }
} // namespace dect2::fecp
#endif // C++ Viterbi
