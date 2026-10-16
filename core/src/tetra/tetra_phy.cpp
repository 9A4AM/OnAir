// TETRA downlink physical layer and lower MAC (tetra_phy.h).
#include "dect2/tetra/tetra_phy.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include "dect2/common/crc.h"

namespace dect2::tetra {

const uint8_t kTrainN[22] = {1,1, 0,1, 0,0, 0,0, 1,1, 1,0, 1,0, 0,1, 1,1, 0,1, 0,0};
const uint8_t kTrainP[22] = {0,1, 1,1, 1,0, 1,0, 0,1, 0,0, 0,0, 1,1, 0,1, 1,1, 1,0};
const uint8_t kTrainQ[22] = {1,0, 1,1, 0,1, 1,1, 0,0, 0,0, 0,1, 1,0, 1,0, 1,1, 0,1};
const uint8_t kTrainY[38] = {1,1, 0,0, 0,0, 0,1, 1,0, 0,1, 1,1, 0,0, 1,1, 1,0, 1,0, 0,1, 1,1, 0,0, 0,0, 0,1, 1,0, 0,1, 1,1};
const uint8_t kFreqCorr[80] = {1,1,1,1,1,1,1,1, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1};

int dibitPhase(int b1, int b2) {
    if (!b1 && !b2) return 1;
    if (!b1 && b2) return 3;
    if (b1 && b2) return -3;
    return -1;
}

// ---------------------------------------------------------------- CRC

uint16_t crc16(const uint8_t* b, int n) { return (uint16_t)crc::bits(b, (size_t)n, 16, 0x1021, 0xFFFF, 0xFFFF); }

// ---------------------------------------------------------------- RCPC

namespace {

inline void mother(int u, int s, int g[4]) {   // s: bit 0 the newest delayed input (D), bit 3 the oldest (D^4)
    const int d1 = s & 1, d2 = (s >> 1) & 1, d3 = (s >> 2) & 1, d4 = (s >> 3) & 1;
    g[0] = u ^ d1 ^ d4;
    g[1] = u ^ d2 ^ d3 ^ d4;
    g[2] = u ^ d1 ^ d2 ^ d4;
    g[3] = u ^ d1 ^ d3 ^ d4;
}

// the mother code bit (0-based: input bit * 4 + generator) of punctured bit j (0-based), rate 2/3
inline int punct23(int j) {
    static const int P[3] = {1, 2, 5};
    return 8 * (j / 3) + P[j % 3] - 1;
}

} // namespace

Bits rcpcEncode23(const Bits& t2) {
    const int n = (int)t2.size();
    std::vector<uint8_t> m((size_t)n * 4);
    int s = 0;
    for (int i = 0; i < n; i++) {
        int g[4];
        const int u = t2[(size_t)i] & 1;
        mother(u, s, g);
        for (int k = 0; k < 4; k++) m[(size_t)(4 * i + k)] = (uint8_t)g[k];
        s = ((s << 1) | u) & 15;
    }
    const int n3 = n * 3 / 2;
    Bits out((size_t)n3);
    for (int j = 0; j < n3; j++) out[(size_t)j] = m[(size_t)punct23(j)];
    return out;
}

Bits viterbiDecode23(const float* llr3, int n3) {
    const int n = n3 * 2 / 3;
    std::vector<float> m((size_t)n * 4, 0.f);
    for (int j = 0; j < n3; j++) m[(size_t)punct23(j)] = llr3[j];
    // branch outputs, per state and input
    static int out[16][2][4];
    static bool init = false;
    if (!init) { for (int s = 0; s < 16; s++) for (int u = 0; u < 2; u++) mother(u, s, out[s][u]); init = true; }
    float pm[16], nm[16];
    for (int s = 0; s < 16; s++) pm[s] = s == 0 ? 0.f : -1e30f;
    std::vector<uint8_t> dec((size_t)n * 16);   // the previous state's oldest bit that won
    for (int i = 0; i < n; i++) {
        const float* l = &m[(size_t)i * 4];
        for (int s = 0; s < 16; s++) nm[s] = -1e30f;
        for (int s = 0; s < 16; s++) {
            if (pm[s] < -1e29f) continue;
            for (int u = 0; u < 2; u++) {
                const int* o = out[s][u];
                float bm = 0;
                for (int k = 0; k < 4; k++) bm += o[k] ? l[k] : -l[k];
                const int ns = ((s << 1) | u) & 15;
                const float v = pm[s] + bm;
                if (v > nm[ns]) { nm[ns] = v; dec[(size_t)i * 16 + (size_t)ns] = (uint8_t)((s >> 3) & 1); }
            }
        }
        std::copy(nm, nm + 16, pm);
    }
    Bits t2((size_t)n);
    int s = 0;   // the tail brings the encoder back to state 0
    for (int i = n - 1; i >= 0; i--) {
        t2[(size_t)i] = (uint8_t)(s & 1);
        const int old = dec[(size_t)i * 16 + (size_t)s];
        s = (s >> 1) | (old << 3);
    }
    return t2;
}

// ---------------------------------------------------------------- interleaving, scrambling

void interleave(const uint8_t* in, uint8_t* out, int K, int a) {
    for (int i = 1; i <= K; i++) out[(a * i) % K] = in[i - 1];
}

void deinterleave(const float* in, float* out, int K, int a) {
    for (int i = 1; i <= K; i++) out[i - 1] = in[(a * i) % K];
}

uint32_t scramblerInit(int mcc, int mnc, int cc) {
    const uint32_t e = ((uint32_t)(cc & 0x3F)) | ((uint32_t)(mnc & 0x3FFF) << 6) | ((uint32_t)(mcc & 0x3FF) << 20);
    return (e << 2) | 3u;
}

namespace {
inline uint8_t lfsrNext(uint32_t& r) {
    auto t = [&](int y) { return (r >> (32 - y)) & 1u; };
    const uint32_t b = (t(32) ^ t(26) ^ t(23) ^ t(22) ^ t(16) ^ t(12) ^ t(11) ^ t(10) ^ t(8) ^ t(7) ^ t(5) ^ t(4) ^ t(2) ^ t(1)) & 1u;
    r = (r >> 1) | (b << 31);
    return (uint8_t)b;
}
} // namespace

void scramble(uint8_t* b, int n, uint32_t init) {
    uint32_t r = init;
    for (int i = 0; i < n; i++) b[i] ^= lfsrNext(r);
}

void descramble(float* l, int n, uint32_t init) {
    uint32_t r = init;
    for (int i = 0; i < n; i++) if (lfsrNext(r)) l[i] = -l[i];
}

// ---------------------------------------------------------------- Reed-Muller (30,14)

namespace {

const uint8_t kRmGen[14][16] = {
    {1,0,0,1,1,0,1,1,0,1,1,0,0,0,0,0}, {0,0,1,0,1,1,0,1,1,1,1,0,0,0,0,0}, {1,1,1,1,1,1,0,0,0,0,1,0,0,0,0,0},
    {1,1,1,0,0,0,0,0,0,0,1,1,1,1,0,0}, {1,0,0,1,1,0,0,0,0,0,1,1,1,0,1,0}, {0,1,0,1,0,1,0,0,0,0,1,1,0,1,1,0},
    {0,0,1,0,1,1,0,0,0,0,1,0,1,1,1,0}, {1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1}, {1,0,0,0,0,0,1,1,0,0,1,1,1,0,0,1},
    {0,1,0,0,0,0,1,0,1,0,1,1,0,1,0,1}, {0,0,1,0,0,0,0,1,1,0,1,0,1,1,0,1}, {0,0,0,1,0,0,1,0,0,1,1,1,0,0,1,1},
    {0,0,0,0,1,0,0,1,0,1,1,0,1,0,1,1}, {0,0,0,0,0,1,0,0,1,1,1,0,0,1,1,1}};

uint16_t rmParity(uint16_t info) {
    uint16_t p = 0;
    for (int i = 0; i < 14; i++) {
        if (!((info >> (13 - i)) & 1)) continue;
        uint16_t row = 0;
        for (int j = 0; j < 16; j++) row = (uint16_t)((row << 1) | kRmGen[i][j]);
        p ^= row;
    }
    return p;
}

struct RmTable {
    std::unordered_map<uint16_t, uint32_t> syn;   // syndrome -> the error pattern of fewest bits
    RmTable() {
        auto add = [&](uint32_t e) {
            const uint16_t s = (uint16_t)((e & 0xFFFF) ^ rmParity((uint16_t)(e >> 16)));
            syn.emplace(s, e);
        };
        add(0);
        for (int a = 0; a < 30; a++) add(1u << a);
        for (int a = 0; a < 30; a++) for (int b = a + 1; b < 30; b++) add((1u << a) | (1u << b));
        for (int a = 0; a < 30; a++) for (int b = a + 1; b < 30; b++) for (int c = b + 1; c < 30; c++) add((1u << a) | (1u << b) | (1u << c));
    }
};

} // namespace

uint32_t rm3014Encode(uint16_t info) {
    info &= 0x3FFF;
    return ((uint32_t)info << 16) | rmParity(info);
}

bool rm3014Decode(uint32_t rx, uint16_t& info, int* corrected) {
    static const RmTable tab;
    rx &= 0x3FFFFFFF;
    const uint16_t s = (uint16_t)((rx & 0xFFFF) ^ rmParity((uint16_t)(rx >> 16)));
    auto it = tab.syn.find(s);
    if (it == tab.syn.end()) return false;
    const uint32_t c = rx ^ it->second;
    info = (uint16_t)(c >> 16);
    if (corrected) { int k = 0; for (uint32_t e = it->second; e; e &= e - 1) k++; *corrected = k; }
    return true;
}

// ---------------------------------------------------------------- logical channels

Bits encodeChannel(const Bits& t1, const ChanSpec& c, uint32_t scr) {
    Bits t2(t1.begin(), t1.end());
    t2.resize((size_t)c.type1, 0);
    const uint16_t p = crc16(t2.data(), c.type1);
    for (int i = 15; i >= 0; i--) t2.push_back((uint8_t)((p >> i) & 1));
    for (int i = 0; i < 4; i++) t2.push_back(0);
    const Bits t3 = rcpcEncode23(t2);
    Bits t5((size_t)c.K);
    interleave(t3.data(), t5.data(), c.K, c.a);
    scramble(t5.data(), c.K, scr);
    return t5;
}

bool decodeChannel(const float* llr5, const ChanSpec& c, uint32_t scr, Bits& t1) {
    std::vector<float> a(llr5, llr5 + c.K), b((size_t)c.K);
    descramble(a.data(), c.K, scr);
    deinterleave(a.data(), b.data(), c.K, c.a);
    const Bits t2 = viterbiDecode23(b.data(), c.K);
    if ((int)t2.size() < c.type1 + 20) return false;
    const uint16_t p = crc16(t2.data(), c.type1);
    uint16_t got = 0;
    for (int i = 0; i < 16; i++) got = (uint16_t)((got << 1) | t2[(size_t)(c.type1 + i)]);
    if (p != got) return false;
    t1.assign(t2.begin(), t2.begin() + c.type1);
    return true;
}

Bits encodeAach(uint16_t aa, uint32_t scr) {
    const uint32_t cw = rm3014Encode(aa);
    Bits b(30);
    for (int i = 0; i < 30; i++) b[(size_t)i] = (uint8_t)((cw >> (29 - i)) & 1);
    scramble(b.data(), 30, scr);
    return b;
}

bool decodeAach(const float* llr30, uint32_t scr, uint16_t& aa) {
    float l[30];
    std::copy(llr30, llr30 + 30, l);
    descramble(l, 30, scr);
    uint32_t cw = 0;
    for (int i = 0; i < 30; i++) cw = (cw << 1) | (l[i] > 0 ? 1u : 0u);
    return rm3014Decode(cw, aa);
}

// ---------------------------------------------------------------- bursts

namespace {

// the phase adjustment bits: a dibit whose step brings the sum of the steps over symbols n1 .. n2 (1-based) to a multiple of 2 pi
void phaseAdjust(Bits& b, int at, int n1, int n2) {
    int sum = 0;
    for (int n = n1; n <= n2; n++) sum += dibitPhase(b[(size_t)(2 * n - 2)], b[(size_t)(2 * n - 1)]);
    int adj = -(sum % 8);
    if (adj > 3) adj -= 8;
    if (adj < -3) adj += 8;
    int b1 = 0, b2 = 0;
    if (adj == 3) b2 = 1; else if (adj == -3) { b1 = 1; b2 = 1; } else if (adj == -1) b1 = 1;
    b[(size_t)at] = (uint8_t)b1; b[(size_t)at + 1] = (uint8_t)b2;
}

void put(Bits& b, int at, const uint8_t* v, int n) { for (int i = 0; i < n; i++) b[(size_t)(at + i)] = v[i]; }

} // namespace

Bits syncBurst(const Bits& sb, const Bits& bb, const Bits& bkn2) {
    Bits b((size_t)kSlotBits, 0);
    put(b, 0, kTrainQ + 10, 12);
    put(b, 14, kFreqCorr, 80);
    put(b, kSbSb, sb.data(), 120);
    put(b, kTrainYAt, kTrainY, 38);
    put(b, kSbBb, bb.data(), 30);
    put(b, kSbBkn2, bkn2.data(), 216);
    put(b, 500, kTrainQ, 10);
    phaseAdjust(b, 12, 8, 108);     // HC
    phaseAdjust(b, 498, 109, 249);  // HD
    return b;
}

Bits normalBurst(const Bits& bkn1, const Bits& bb, const Bits& bkn2, bool two) {
    Bits b((size_t)kSlotBits, 0);
    put(b, 0, kTrainQ + 10, 12);
    put(b, kNbBkn1, bkn1.data(), 216);
    put(b, kNbBb1, bb.data(), 14);
    put(b, kTrainNAt, two ? kTrainP : kTrainN, 22);
    put(b, kNbBb2, bb.data() + 14, 16);
    put(b, kNbBkn2, bkn2.data(), 216);
    put(b, 500, kTrainQ, 10);
    phaseAdjust(b, 12, 8, 122);     // HA
    phaseAdjust(b, 498, 123, 249);  // HB
    return b;
}

// ---------------------------------------------------------------- modulation

std::vector<float> rrcTaps(int sps, int span) {
    const int n = 2 * span * sps + 1;
    std::vector<float> h((size_t)n);
    const double a = kRolloff;
    double e = 0;
    for (int i = 0; i < n; i++) {
        const double t = (double)(i - span * sps) / sps;
        double v;
        if (std::fabs(t) < 1e-9) v = 1 - a + 4 * a / M_PI;
        else if (std::fabs(std::fabs(t) - 1 / (4 * a)) < 1e-9)
            v = a / std::sqrt(2.0) * ((1 + 2 / M_PI) * std::sin(M_PI / (4 * a)) + (1 - 2 / M_PI) * std::cos(M_PI / (4 * a)));
        else v = (std::sin(M_PI * t * (1 - a)) + 4 * a * t * std::cos(M_PI * t * (1 + a))) / (M_PI * t * (1 - (4 * a * t) * (4 * a * t)));
        h[(size_t)i] = (float)v;
        e += v * v;
    }
    const double g = 1.0 / std::sqrt(e / sps);   // a symbol of unit power gives samples of unit mean power
    for (auto& v : h) v = (float)(v * g);
    return h;
}

namespace { constexpr int kSpan = 8; }

Modulator::Modulator() : h_(rrcTaps(kSps, kSpan)), hist_((size_t)(2 * kSpan + 1), cf32(0, 0)) {}

void Modulator::push(const Bits& bits, std::vector<cf32>& out) {
    const int L = 2 * kSpan + 1;
    for (size_t k = 0; k + 1 < bits.size(); k += 2) {
        phase_ = (phase_ + dibitPhase(bits[k], bits[k + 1]) + 8) & 7;
        const double ph = phase_ * M_PI / 4;
        std::rotate(hist_.begin(), hist_.begin() + 1, hist_.end());
        hist_[(size_t)(L - 1)] = cf32((float)std::cos(ph), (float)std::sin(ph));
        // the samples of the symbol in the middle of the history: symbol c + m sits at hist_[kSpan + m]
        for (int j = 0; j < kSps; j++) {
            cf32 y(0, 0);
            for (int m = -kSpan; m <= kSpan; m++) {
                const int idx = -kSps * m + j + kSpan * kSps;
                if (idx < 0 || idx >= (int)h_.size()) continue;
                y += hist_[(size_t)(kSpan + m)] * h_[(size_t)idx];
            }
            out.push_back(y);
        }
    }
}

} // namespace dect2::tetra
