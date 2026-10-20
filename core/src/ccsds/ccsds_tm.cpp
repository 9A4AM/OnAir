// CCSDS telemetry codeblocks (see ccsds_tm.h). Adapted from DecSAT by Arclamp (CasualArclamp), GPL-3.0-or-later (decsat-modem/src/ccsds.rs), in C++.
#include "dect2/ccsds/ccsds_tm.h"
#include <algorithm>
#include <cstdio>

namespace dect2 {
namespace ccsds {

namespace {
struct Gf {
    uint8_t exp[512], log[256], toDual[256], fromDual[256];
    Gf() {
        unsigned x = 1;
        for (int i = 0; i < 255; i++) { exp[i] = (uint8_t)x; log[x] = (uint8_t)i; x <<= 1; if (x & 0x100) x ^= 0x187; }
        for (int i = 255; i < 512; i++) exp[i] = exp[i - 255];
        log[0] = 0;
        // the dual basis: the rows of the matrix T of 131.0-B annex F, applied to the conventional bits
        static const uint8_t tal[8] = {0x8d, 0xef, 0xec, 0x86, 0xfa, 0x99, 0xaf, 0x7b};
        for (int v = 0; v < 256; v++) {
            uint8_t d = 0;
            for (int k = 0; k < 8; k++) if (v & (1 << k)) d ^= tal[7 - k];
            toDual[v] = d; fromDual[d] = (uint8_t)v;
        }
    }
    uint8_t mul(uint8_t a, uint8_t b) const { return (a == 0 || b == 0) ? 0 : exp[log[a] + log[b]]; }
    uint8_t div(uint8_t a, uint8_t b) const { return a == 0 ? 0 : exp[(log[a] + 255 - log[b]) % 255]; }
    uint8_t pow(size_t i) const { return exp[i % 255]; }
};
const Gf& gf() { static Gf g; return g; }
const int kPrim = 11;
}

std::vector<uint8_t> randomiser(size_t n) {
    std::vector<uint8_t> out;
    unsigned s = 0xFF;
    for (size_t i = 0; i < n; i++) {
        uint8_t byte = 0;
        for (int k = 0; k < 8; k++) {
            byte = (uint8_t)((byte << 1) | (s & 1));
            const unsigned fb = (s ^ (s >> 3) ^ (s >> 5) ^ (s >> 7)) & 1;
            s = (s >> 1) | (fb << 7);
        }
        out.push_back(byte);
    }
    return out;
}

uint16_t crcCcitt(const uint8_t* p, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

Rs::Rs(int e, int n) : e_(e), n_(n) {
    const Gf& G = gf();
    g_ = {1};
    for (int j = 128 - e; j < 128 + e; j++) {   // g(x) = prod (x - beta^j), j = 128 - E .. 127 + E
        const uint8_t root = G.pow((size_t)kPrim * (size_t)j);
        std::vector<uint8_t> next(g_.size() + 1, 0);
        for (size_t i = 0; i < g_.size(); i++) { next[i] ^= g_[i]; next[i + 1] ^= G.mul(g_[i], root); }
        g_ = next;
    }
}

void Rs::encode(uint8_t* cw, bool dual) const {
    const Gf& G = gf();
    const int p = 2 * e_, kk = k();
    std::vector<uint8_t> rem((size_t)p, 0);
    for (int i = 0; i < kk; i++) {
        const uint8_t m = dual ? G.fromDual[cw[i]] : cw[i];
        const uint8_t fb = (uint8_t)(m ^ rem[0]);
        std::rotate(rem.begin(), rem.begin() + 1, rem.end());
        rem[(size_t)p - 1] = 0;
        if (fb) for (int r = 0; r < p; r++) rem[(size_t)r] ^= G.mul(fb, g_[(size_t)r + 1]);
    }
    for (int i = 0; i < p; i++) cw[kk + i] = dual ? G.toDual[rem[(size_t)i]] : rem[(size_t)i];
}

int Rs::decode(uint8_t* cw, bool dual) const {
    const Gf& G = gf();
    const int p = 2 * e_, fcr = 128 - e_;
    std::vector<uint8_t> conv((size_t)n_);
    for (int i = 0; i < n_; i++) conv[(size_t)i] = dual ? G.fromDual[cw[i]] : cw[i];
    std::vector<uint8_t> s((size_t)p, 0);
    bool clean = true;
    for (int i = 0; i < p; i++) {
        const uint8_t x = G.pow((size_t)kPrim * (size_t)(fcr + i));
        uint8_t acc = 0;
        for (int j = 0; j < n_; j++) acc = (uint8_t)(G.mul(acc, x) ^ conv[(size_t)j]);
        s[(size_t)i] = acc;
        clean = clean && acc == 0;
    }
    if (clean) return 0;
    // Berlekamp-Massey
    std::vector<uint8_t> lambda((size_t)p + 1, 0), b;
    lambda[0] = 1;
    b = lambda;
    int l = 0, m = 1;
    uint8_t bb = 1;
    for (int r = 0; r < p; r++) {
        uint8_t d = s[(size_t)r];
        for (int i = 1; i <= l; i++) d ^= G.mul(lambda[(size_t)i], s[(size_t)(r - i)]);
        if (d == 0) m++;
        else if (2 * l <= r) {
            const std::vector<uint8_t> t = lambda;
            const uint8_t coef = G.div(d, bb);
            for (int i = m; i <= p; i++) lambda[(size_t)i] ^= G.mul(coef, b[(size_t)(i - m)]);
            l = r + 1 - l; b = t; bb = d; m = 1;
        } else {
            const uint8_t coef = G.div(d, bb);
            for (int i = m; i <= p; i++) lambda[(size_t)i] ^= G.mul(coef, b[(size_t)(i - m)]);
            m++;
        }
    }
    if (l > e_) return -1;
    // Chien search over the degrees actually sent
    std::vector<int> locs;
    for (int e = 0; e < n_; e++) {
        const uint8_t xinv = G.pow((size_t)kPrim * (size_t)(255 - e % 255));
        uint8_t acc = 0, xp = 1;
        for (int i = 0; i <= l; i++) { acc ^= G.mul(lambda[(size_t)i], xp); xp = G.mul(xp, xinv); }
        if (acc == 0) locs.push_back(e);
    }
    if ((int)locs.size() != l) return -1;
    // omega = S(x) lambda(x) mod x^p
    std::vector<uint8_t> omega((size_t)p, 0);
    for (int i = 0; i < p; i++) { uint8_t acc = 0; for (int j = 0; j <= std::min(i, l); j++) acc ^= G.mul(lambda[(size_t)j], s[(size_t)(i - j)]); omega[(size_t)i] = acc; }
    std::vector<uint8_t> fixed = conv;
    for (int e : locs) {
        const uint8_t x = G.pow((size_t)kPrim * (size_t)e);
        const uint8_t xinv = G.pow((size_t)kPrim * (size_t)(255 - e % 255));
        uint8_t num = 0, xp = 1;
        for (int i = 0; i < p; i++) { num ^= G.mul(omega[(size_t)i], xp); xp = G.mul(xp, xinv); }
        uint8_t den = 0;
        for (int i = 1; i <= l; i += 2) { uint8_t t = lambda[(size_t)i]; for (int k2 = 0; k2 < i - 1; k2++) t = G.mul(t, xinv); den ^= t; }
        if (den == 0) return -1;
        const uint8_t xf = G.pow(((size_t)kPrim * (size_t)e * (size_t)fcr) % 255);
        const uint8_t y = G.mul(G.div(G.mul(num, x), den), G.div(1, xf));
        fixed[(size_t)(n_ - 1 - e)] ^= y;
    }
    for (int i = 0; i < n_; i++) cw[i] = dual ? G.toDual[fixed[(size_t)i]] : fixed[(size_t)i];
    return (int)locs.size();
}

std::string Format::describe() const {
    char b[96];
    std::string s;
    if (rs) { snprintf(b, sizeof b, "RS(%d,%d) I = %d%s", layout.n, layout.n - 2 * layout.e, layout.interleave, layout.dual ? "" : ", conventional basis"); s = b; }
    else s = "no Reed-Solomon";
    if (randomised) s += ", randomised";
    return s;
}

bool Format::decode(const uint8_t* block, size_t n, std::vector<uint8_t>& frame, int& corrected) const {
    corrected = 0;
    std::vector<uint8_t> b(block, block + std::min(n, blockBytes));
    b.resize(blockBytes, 0);
    if (randomised) { const auto r = randomiser(blockBytes); for (size_t i = 0; i < b.size(); i++) b[i] ^= r[i]; }
    if (!rs) { frame = b; return true; }
    const Rs code(layout.e, layout.n);
    const int k = code.k(), I = layout.interleave;
    frame.assign((size_t)I * (size_t)k, 0);
    bool failed = false;
    std::vector<uint8_t> cw((size_t)layout.n);
    for (int i = 0; i < I; i++) {
        for (int j = 0; j < layout.n; j++) cw[(size_t)j] = b[(size_t)j * (size_t)I + (size_t)i];
        const int c = code.decode(cw.data(), layout.dual);
        if (c < 0) failed = true; else corrected += c;
        for (int j = 0; j < k; j++) frame[(size_t)j * (size_t)I + (size_t)i] = cw[(size_t)j];
    }
    return !failed;
}

std::vector<uint8_t> encodeBlock(const Format& f, const std::vector<uint8_t>& frame) {
    std::vector<uint8_t> b;
    if (!f.rs) b = frame;
    else {
        const Rs code(f.layout.e, f.layout.n);
        const int k = code.k(), I = f.layout.interleave;
        b.assign((size_t)I * (size_t)f.layout.n, 0);
        std::vector<uint8_t> cw((size_t)f.layout.n);
        for (int i = 0; i < I; i++) {
            for (int j = 0; j < k; j++) cw[(size_t)j] = frame[(size_t)j * (size_t)I + (size_t)i];
            code.encode(cw.data(), f.layout.dual);
            for (int j = 0; j < f.layout.n; j++) b[(size_t)j * (size_t)I + (size_t)i] = cw[(size_t)j];
        }
    }
    if (f.randomised) { const auto r = randomiser(f.blockBytes); for (size_t i = 0; i < b.size() && i < r.size(); i++) b[i] ^= r[i]; }
    return b;
}

bool findFormat(const std::vector<std::vector<uint8_t>>& blocks, Format& out) {
    if (blocks.empty()) return false;
    const size_t len = blocks[0].size();
    if (len < 8) return false;
    for (const auto& b : blocks) if (b.size() < len) return false;
    for (int rnd = 1; rnd >= 0; rnd--) {
        Format f;
        f.blockBytes = len; f.rs = false; f.randomised = rnd != 0;
        bool ok = true;
        for (const auto& b : blocks) {
            std::vector<uint8_t> fr; int c;
            f.decode(b.data(), b.size(), fr, c);
            if (fr.size() <= 8 || crcCcitt(fr.data(), fr.size() - 2) != (uint16_t)(fr[fr.size() - 2] << 8 | fr[fr.size() - 1])) { ok = false; break; }
        }
        if (ok) { out = f; return true; }
    }
    for (int e : {16, 8})
        for (int I = 1; I <= 8; I++) {
            if (len % (size_t)I) continue;
            const size_t n = len / (size_t)I;
            if (n > 255 || n <= (size_t)(2 * e + 4)) continue;
            for (int rnd = 1; rnd >= 0; rnd--)
                for (int dual = 1; dual >= 0; dual--) {
                    Format f;
                    f.blockBytes = len; f.rs = true; f.layout.e = e; f.layout.interleave = I; f.layout.n = (int)n; f.layout.dual = dual != 0; f.randomised = rnd != 0;
                    bool ok = true;
                    // every word must decode, and the frames must look like transfer frames (the randomiser sequence is itself a code word of some layouts, so
                    // a wrong guess about it decodes too, to frames whose header is not one)
                    for (const auto& b : blocks) { std::vector<uint8_t> fr; int c; Header h; if (!f.decode(b.data(), b.size(), fr, c) || !parseHeader(fr.data(), fr.size(), h)) { ok = false; break; } }
                    if (ok) { out = f; return true; }
                }
        }
    return false;
}

bool parseHeader(const uint8_t* f, size_t n, Header& h) {
    if (n < 6) return false;
    const unsigned w = (unsigned)(f[0] << 8 | f[1]);
    h = Header();
    switch (w >> 14) {
    case 0:
        h.version = 0; h.scid = (int)((w >> 4) & 0x3FF); h.vcid = (int)((w >> 1) & 7); h.ocf = (w & 1) != 0;
        h.mcCount = f[2]; h.vcCount = f[3]; h.firstHeader = (f[4] << 8 | f[5]) & 0x7FF;
        return true;
    case 1:
        h.version = 1; h.scid = (int)((w >> 6) & 0xFF); h.vcid = (int)(w & 0x3F);
        h.vcCount = (uint32_t)f[2] << 16 | (uint32_t)f[3] << 8 | f[4];
        return true;
    default: return false;
    }
}

std::vector<uint8_t> buildTmFrame(size_t len, int scid, int vcid, int mc, int vc, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> f(len, 0);
    const unsigned w = (unsigned)(((scid & 0x3FF) << 4) | ((vcid & 7) << 1));
    f[0] = (uint8_t)(w >> 8); f[1] = (uint8_t)w; f[2] = (uint8_t)mc; f[3] = (uint8_t)vc; f[4] = 0x18; f[5] = 0x00;
    for (size_t i = 0; i + 8 < len; i++) f[6 + i] = i < data.size() ? data[i] : 0x55;
    const uint16_t crc = crcCcitt(f.data(), len - 2);
    f[len - 2] = (uint8_t)(crc >> 8); f[len - 1] = (uint8_t)crc;
    return f;
}

} // namespace ccsds
} // namespace dect2
