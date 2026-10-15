// NXDN common air interface (see nxdn_proto.h).
#include "dect2/nxdn_proto.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace dect2 {
namespace nxdn {

const int kFsw[kFswSym] = {-3, 1, -3, 3, -3, -3, 3, 3, -1, 3};

void pn9(uint8_t* out, int n) {
    uint32_t r = 0xE4;
    for (int i = 0; i < n; i++) {
        out[i] = (uint8_t)(r & 1);
        const uint32_t b = ((r >> 4) ^ r) & 1;
        r = (r >> 1) | (b << 8);
    }
}

uint8_t lichEncode(const Lich& l) {
    const int v = ((l.rfct & 3) << 5) | ((l.fct & 3) << 3) | ((l.opt & 3) << 1) | (l.dir & 1);
    const int p = ((v >> 6) ^ (v >> 5) ^ (v >> 4) ^ (v >> 3)) & 1;   // even parity over the channel types
    return (uint8_t)((v << 1) | p);
}
bool lichDecode(uint8_t x, Lich& l) {
    const int v = x >> 1;
    if ((((v >> 6) ^ (v >> 5) ^ (v >> 4) ^ (v >> 3)) & 1) != (x & 1)) return false;
    l.rfct = (v >> 5) & 3; l.fct = (v >> 3) & 3; l.opt = (v >> 1) & 3; l.dir = v & 1;
    return true;
}

namespace {
uint32_t crcGen(const uint8_t* b, int n, int w, uint32_t poly, uint32_t init) {
    const uint32_t mask = (1u << w) - 1;
    uint32_t c = init & mask;
    for (int i = 0; i < n; i++) {
        const uint32_t a = (b[i] & 1) ^ (c >> (w - 1));
        c = (c << 1) & mask;
        if (a) c ^= poly;
    }
    return c;
}
} // namespace
uint32_t crc6(const uint8_t* b, int n) { return crcGen(b, n, 6, 0x27, 0x3F); }       // x^6 + x^5 + x^2 + x + 1
uint32_t crc12(const uint8_t* b, int n) { return crcGen(b, n, 12, 0x80F, 0xFFF); }   // x^12 + x^11 + x^3 + x^2 + x + 1
uint32_t crc16(const uint8_t* b, int n) { return crcGen(b, n, 16, 0x1021, 0xFFFF); } // x^16 + x^12 + x^5 + 1

// ---------------------------------------------------------------- the coded channels
namespace {
struct Spec { int info, crc, rows, cols, plen; const uint8_t* punct; };
const uint8_t kPunSacch[12] = {1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0};   // read G1, G2, G1, G2 ...
const uint8_t kPunFacch[4] = {1, 0, 1, 1};
const uint8_t kPunCac[14] = {1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1};
const Spec& spec(Ch c) {
    static const Spec s[3] = {{26, 6, 12, 5, 12, kPunSacch}, {80, 12, 16, 9, 4, kPunFacch}, {155, 16, 12, 25, 14, kPunCac}};
    return s[(int)c];
}
uint32_t crcOf(Ch c, const uint8_t* b, int n) { return c == Ch::Sacch ? crc6(b, n) : c == Ch::Facch1 ? crc12(b, n) : crc16(b, n); }

void convEncode(const uint8_t* in, int n, uint8_t* out) {   // n bits (tail included) -> 2n
    int s = 0;
    for (int i = 0; i < n; i++) {
        const int b = in[i] & 1;
        out[2 * i] = (uint8_t)(b ^ ((s >> 2) & 1) ^ ((s >> 3) & 1));
        out[2 * i + 1] = (uint8_t)(b ^ (s & 1) ^ ((s >> 1) & 1) ^ ((s >> 3) & 1));
        s = ((s << 1) | b) & 15;
    }
}

void viterbi(const float* soft, int n, uint8_t* out) {   // 2n soft values, ends in state 0
    float pm[16], nm[16];
    for (int s = 0; s < 16; s++) pm[s] = s == 0 ? 0.f : -1e9f;
    std::vector<uint16_t> dec((size_t)n);
    for (int i = 0; i < n; i++) {
        const float a = soft[2 * i], b = soft[2 * i + 1];
        uint16_t d = 0;
        for (int ns = 0; ns < 16; ns++) {
            const int bit = ns & 1;
            float best = -2e9f; int bp = 0;
            for (int hi = 0; hi < 2; hi++) {
                const int s = (ns >> 1) | (hi << 3);
                const int g1 = bit ^ ((s >> 2) & 1) ^ ((s >> 3) & 1), g2 = bit ^ (s & 1) ^ ((s >> 1) & 1) ^ ((s >> 3) & 1);
                const float m = pm[s] + (g1 ? a : -a) + (g2 ? b : -b);
                if (m > best) { best = m; bp = hi; }
            }
            nm[ns] = best;
            if (bp) d |= (uint16_t)(1 << ns);
        }
        dec[(size_t)i] = d;
        memcpy(pm, nm, sizeof pm);
    }
    int s = 0;
    for (int i = n - 1; i >= 0; i--) {
        out[i] = (uint8_t)(s & 1);
        const int hi = (dec[(size_t)i] >> s) & 1;
        s = (s >> 1) | (hi << 3);
    }
}

int perm(const Spec& p, int k) { return k / p.cols + p.rows * (k % p.cols); }
} // namespace

int infoBits(Ch c) { return spec(c).info; }
int airBits(Ch c) { const Spec& p = spec(c); return p.rows * p.cols; }

void encode(Ch c, const uint8_t* info, uint8_t* air) {
    const Spec& p = spec(c);
    const int n = p.info + p.crc + 4;
    std::vector<uint8_t> u((size_t)n, 0), cod((size_t)2 * n), pun;
    for (int i = 0; i < p.info; i++) u[(size_t)i] = info[i] & 1;
    const uint32_t r = crcOf(c, u.data(), p.info);
    for (int i = 0; i < p.crc; i++) u[(size_t)(p.info + i)] = (uint8_t)((r >> (p.crc - 1 - i)) & 1);
    convEncode(u.data(), n, cod.data());
    for (int i = 0; i < 2 * n; i++) if (p.punct[i % p.plen]) pun.push_back(cod[(size_t)i]);
    const int L = p.rows * p.cols;
    for (int k = 0; k < L; k++) air[k] = pun[(size_t)perm(p, k)];
}

bool decode(Ch c, const float* soft, uint8_t* info, int* errs) {
    const Spec& p = spec(c);
    const int n = p.info + p.crc + 4, L = p.rows * p.cols;
    std::vector<float> pun((size_t)L), cod((size_t)2 * n, 0.f);
    for (int k = 0; k < L; k++) pun[(size_t)perm(p, k)] = soft[k];
    for (int i = 0, j = 0; i < 2 * n; i++) if (p.punct[i % p.plen]) cod[(size_t)i] = pun[(size_t)j++];
    std::vector<uint8_t> u((size_t)n);
    viterbi(cod.data(), n, u.data());
    uint32_t r = 0;
    for (int i = 0; i < p.crc; i++) r = (r << 1) | u[(size_t)(p.info + i)];
    for (int i = 0; i < p.info; i++) info[i] = u[(size_t)i];
    const bool ok = crcOf(c, u.data(), p.info) == r;
    if (ok && errs) {
        std::vector<uint8_t> air((size_t)L);
        encode(c, u.data(), air.data());
        int e = 0;
        for (int k = 0; k < L; k++) if ((soft[k] > 0) != (air[(size_t)k] != 0)) e++;
        *errs = e;
    }
    return ok;
}

// ---------------------------------------------------------------- layer 3
namespace {
struct W {
    uint8_t* b; int max, n = 0;
    void put(uint32_t v, int w) { for (int i = w - 1; i >= 0; i--) { if (n < max) b[n] = (uint8_t)((v >> i) & 1); n++; } }
};
struct R {
    const uint8_t* b; int n, p = 0;
    uint32_t get(int w) { uint32_t v = 0; for (int i = 0; i < w; i++) { v = (v << 1) | (p < n ? (b[p] & 1) : 0); p++; } return v; }
};
} // namespace

int buildMsg(const Msg& m, uint8_t* bits, int maxBits) {
    W w{bits, maxBits};
    for (int i = 0; i < maxBits; i++) bits[i] = 0;
    w.put((uint32_t)m.type & 0x3F, 8);
    switch (m.type) {
    case kVcall: case kTxRel:
        w.put((uint32_t)m.callType, 3); w.put((uint32_t)m.option, 5); w.put((uint32_t)m.src, 16); w.put((uint32_t)m.dst, 16);
        w.put((uint32_t)m.cipher, 2); w.put((uint32_t)m.keyId, 6);
        break;
    case kStatReq:
        w.put((uint32_t)m.callType, 3); w.put((uint32_t)m.option, 5); w.put((uint32_t)m.src, 16); w.put((uint32_t)m.dst, 16); w.put((uint32_t)m.status, 8);
        break;
    case kSdHeader:
        w.put((uint32_t)m.callType, 3); w.put((uint32_t)m.option, 5); w.put((uint32_t)m.src, 16); w.put((uint32_t)m.dst, 16); w.put((uint32_t)m.blocks, 8);
        break;
    case kSdData:
        w.put((uint32_t)m.block, 8);
        for (int i = 0; i < 8; i++) w.put(i < (int)m.text.size() ? (uint8_t)m.text[(size_t)i] : 0, 8);
        break;
    case kSiteInfo:
        w.put(m.location, 24); w.put((uint32_t)m.channel, 16); w.put((uint32_t)m.service, 16);
        break;
    case kSrvInfo:
        w.put(m.location, 24); w.put((uint32_t)m.service, 16);
        break;
    default: break;
    }
    return w.n;
}

bool parseMsg(const uint8_t* bits, int nBits, Msg& m) {
    R r{bits, nBits};
    m = Msg();
    m.type = (int)(r.get(8) & 0x3F);
    auto need = [&](int n) { return nBits >= n; };
    switch (m.type) {
    case kVcall: case kTxRel:
        if (!need(56)) return false;
        m.callType = (int)r.get(3); m.option = (int)r.get(5); m.src = (int)r.get(16); m.dst = (int)r.get(16); m.cipher = (int)r.get(2); m.keyId = (int)r.get(6);
        return true;
    case kStatReq:
        if (!need(56)) return false;
        m.callType = (int)r.get(3); m.option = (int)r.get(5); m.src = (int)r.get(16); m.dst = (int)r.get(16); m.status = (int)r.get(8);
        return true;
    case kSdHeader:
        if (!need(56)) return false;
        m.callType = (int)r.get(3); m.option = (int)r.get(5); m.src = (int)r.get(16); m.dst = (int)r.get(16); m.blocks = (int)r.get(8);
        return true;
    case kSdData:
        if (!need(80)) return false;
        m.block = (int)r.get(8);
        for (int i = 0; i < 8; i++) { const char ch = (char)r.get(8); if (ch) m.text.push_back((unsigned char)ch >= 32 && (unsigned char)ch < 127 ? ch : '.'); }
        return true;
    case kSiteInfo:
        if (!need(64)) return false;
        m.location = r.get(24); m.channel = (int)r.get(16); m.service = (int)r.get(16);
        return true;
    case kSrvInfo:
        if (!need(48)) return false;
        m.location = r.get(24); m.service = (int)r.get(16);
        return true;
    case kIdle: return true;
    default: return true;   // known by its type only
    }
}

const char* msgName(int t) {
    switch (t) {
    case kVcall: return "VCALL"; case kTxRel: return "TX_REL"; case kIdle: return "IDLE"; case kSiteInfo: return "SITE_INFO";
    case kSrvInfo: return "SRV_INFO"; case kStatReq: return "STAT_REQ"; case kSdHeader: return "SDCALL_HEADER"; case kSdData: return "SDCALL_DATA";
    default: return "other";
    }
}
const char* callTypeName(int t) {
    switch (t) { case 0: return "broadcast"; case 1: return "group"; case 4: return "individual"; case 6: return "interconnect"; default: return "other"; }
}

void ambeFrame(const uint8_t bits[72], char fr[4][24]) {
    static const int nW[36] = {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2, 0, 2};
    static const int nX[36] = {23, 10, 22, 9, 21, 8, 20, 7, 19, 6, 18, 5, 17, 4, 16, 3, 15, 2, 14, 1, 13, 0, 12, 10, 11, 9, 10, 8, 9, 7, 8, 6, 7, 5, 6, 4};
    static const int nY[36] = {0, 2, 0, 2, 0, 2, 0, 2, 0, 3, 0, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3};
    static const int nZ[36] = {5, 3, 4, 2, 3, 1, 2, 0, 1, 13, 0, 12, 22, 11, 21, 10, 20, 9, 19, 8, 18, 7, 17, 6, 16, 5, 15, 4, 14, 3, 13, 2, 12, 1, 11, 0};
    memset(fr, 0, 4 * 24);
    for (int j = 0; j < 36; j++) { fr[nW[j]][nX[j]] = (char)(bits[2 * j] & 1); fr[nY[j]][nZ[j]] = (char)(bits[2 * j + 1] & 1); }
}

} // namespace nxdn
} // namespace dect2
