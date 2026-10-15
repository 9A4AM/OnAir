// JT9 (A) of the HF digital receiver: 85 symbols of 6912 / 12000 s, 9-FSK (tone 0 sync at 16 symbols, tones 1 .. 8 data = 3 bits, Gray code),
// spacing 1.736 Hz, 60 s periods; 72-bit messages (as JT65: hfdig_wsjt.cpp) + 31 tail bits, K = 32 rate 1/2 convolutional code (206 bits), bit-reversal
// interleaving, decoded by sequential (Fano) decoding of soft bits. Decoder: a spectrogram at 7111.1 Hz (one symbol = 4096 samples = one FFT),
// the sync pattern over frequency and time, a fine fit, soft bits from the eight tone energies, Fano. JT9B .. H and the fast sub-modes are not
// decoded. Reference: WSJT-X lib/gen9.f90, jt9sync.f90, interleave9.f90, encode232.f90, jt9fano.f90 (K1JT et al., GPL-3).
#include "hfdig_wsjt_int.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <set>

namespace dect2 {
namespace wsjt {

namespace {

const uint8_t kSync[85] = {1,1,0,0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,
                           0,0,1,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,
                           0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,0,0,0,0,1,
                           0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,
                           0,0,1,0,1};
constexpr uint32_t kPoly1 = 0xF2D05351u, kPoly2 = 0xE4613C47u;
constexpr double kSym = 6912.0 / 12000.0, kBaud = 12000.0 / 6912.0;
constexpr double kRate = 4096.0 / (6912.0 / 12000.0);   // 7111.1 Hz: a symbol is 4096 samples
constexpr int kN = 4096, kHop = 1024;

inline int parity32(uint32_t x) { x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1; return (int)(x & 1); }

const std::vector<int>& interleaveTable() {   // j0[i]: the position of coded bit i after interleaving
    static const std::vector<int> j0 = [] {
        std::vector<int> t;
        for (int i = 0; i < 256; i++) {
            int n = 0;
            for (int b = 0; b < 8; b++) n = 2 * n + ((i >> b) & 1);
            if (n <= 205) t.push_back(n);
        }
        return t;
    }();
    return j0;
}

} // namespace

void convEncode32(const uint8_t* d, int nbits, uint8_t* out) {
    uint32_t reg = 0;
    for (int i = 0; i < nbits + 31; i++) {
        const int bit = i < nbits ? (d[i / 8] >> (7 - i % 8)) & 1 : 0;
        reg = (reg << 1) | (uint32_t)bit;
        out[2 * i] = (uint8_t)parity32(reg & kPoly1);
        out[2 * i + 1] = (uint8_t)parity32(reg & kPoly2);
    }
}

bool fano32(const float* llr, int nData, uint8_t* d, long maxCycles) {
    const int kBits = nData + 31;
    // the metric of a coded bit: log2(2 P(bit | y)) - rate, in units of 1/8 bit
    auto met = [](float l, int bit) {
        const float x = bit ? l : -l;
        const float p = 1.f / (1.f + std::exp(-std::max(-20.f, std::min(20.f, x))));
        return (int)std::lround(8.0 * (std::log2(2.0 * std::max(1e-6f, p)) - 0.5));
    };
    std::vector<std::array<int, 2>> m((size_t)(2 * kBits));
    for (int i = 0; i < 2 * kBits; i++) { m[(size_t)i][0] = met(llr[i], 0); m[(size_t)i][1] = met(llr[i], 1); }
    struct Node { uint32_t enc; int gamma; int tm[2]; int i; };
    std::vector<Node> nodes((size_t)kBits + 1);
    auto branch = [&](int k, uint32_t state) {
        return m[(size_t)(2 * k)][(size_t)parity32(state & kPoly1)] + m[(size_t)(2 * k + 1)][(size_t)parity32(state & kPoly2)];
    };
    auto fill = [&](int k) {
        Node& n = nodes[(size_t)k];
        const uint32_t s0 = n.enc;
        const int m0 = branch(k, s0);
        if (k >= nData) { n.tm[0] = m0; n.tm[1] = 0; return; }
        const int m1 = branch(k, s0 | 1);
        if (m0 >= m1) { n.tm[0] = m0; n.tm[1] = m1; }
        else { n.tm[0] = m1; n.tm[1] = m0; n.enc |= 1; }
    };
    const int delta = 16;
    int t = 0;
    nodes[0].enc = 0; nodes[0].gamma = 0; nodes[0].i = 0;
    fill(0);
    int np = 0;
    bool done = false;
    for (long cyc = 0; cyc < maxCycles; cyc++) {
        Node& n = nodes[(size_t)np];
        const int ng = n.gamma + n.tm[n.i];
        if (ng >= t) {
            if (n.gamma < t + delta) while (ng >= t + delta) t += delta;
            Node& nx = nodes[(size_t)np + 1];
            nx.gamma = ng;
            nx.enc = n.enc << 1;
            if (++np == kBits) { done = true; break; }
            fill(np);
            nodes[(size_t)np].i = 0;
            continue;
        }
        for (;;) {
            if (np == 0 || nodes[(size_t)np - 1].gamma < t) {
                t -= delta;
                if (nodes[(size_t)np].i != 0) { nodes[(size_t)np].i = 0; nodes[(size_t)np].enc ^= 1; }
                break;
            }
            --np;
            if (np < nData && nodes[(size_t)np].i != 1) { nodes[(size_t)np].i++; nodes[(size_t)np].enc ^= 1; break; }
        }
    }
    if (!done) return false;
    std::memset(d, 0, (size_t)(nData + 7) / 8);
    for (int k = 0; k < nData; k++) if (nodes[(size_t)k].enc & 1) d[k / 8] |= (uint8_t)(0x80 >> (k % 8));
    return true;
}

std::vector<int> jt9Tones(const std::string& msg) {
    uint8_t sym[12];
    if (!packJt72(msg, sym)) return {};
    uint8_t bytes[13] = {};
    for (int i = 0; i < 72; i++) if ((sym[i / 6] >> (5 - i % 6)) & 1) bytes[i / 8] |= (uint8_t)(0x80 >> (i % 8));
    uint8_t coded[206], scr[207] = {};
    convEncode32(bytes, 72, coded);
    const auto& j0 = interleaveTable();
    for (int i = 0; i < 206; i++) scr[j0[(size_t)i]] = coded[i];
    std::vector<int> tones(85, 0);
    int j = 0;
    for (int i = 0; i < 85; i++) {
        if (kSync[i]) continue;
        const int s = scr[3 * j] * 4 + scr[3 * j + 1] * 2 + scr[3 * j + 2];
        tones[(size_t)i] = (s ^ (s >> 1)) + 1;
        j++;
    }
    return tones;
}

void addJt9(const std::vector<int>& tones, double f0, double amp, double rate, double start, std::vector<float>& out) {
    std::vector<double> fr(tones.size());
    for (size_t i = 0; i < tones.size(); i++) fr[i] = f0 + tones[i] * kBaud;
    addFsk(fr, kSym, amp, rate, start, out);
}

namespace {

class Jt9 {
public:
    explicit Jt9(const Slot& s) : s_(s) {}

    std::vector<FtxDecode> run() {
        std::vector<FtxDecode> res;
        y_ = resample8k(s_.x, s_.n, kRate);
        const double t0 = 1.0, df = kRate / kN;
        const int binLo = std::max(2, (int)std::floor(s_.fLo / df));
        const int fMaxBin = (int)std::ceil(s_.fHi / df);
        const int binHi = std::min(kN / 2 - 2, fMaxBin + 12);
        sg_ = spectrogram(y_, kN, kHop, binLo, binHi);
        if (sg_.frames < 4 * 84 + 1) return res;
        const int lagLo = std::max(0, (int)std::floor((t0 + s_.dtMin - s_.x0) * kRate / kHop));
        const int lagHi = std::min(sg_.frames - 1 - 4 * 84, (int)std::ceil((t0 + s_.dtMax - s_.x0) * kRate / kHop));
        std::vector<int8_t> st(85, -1);
        for (int i = 0; i < 85; i++) if (kSync[i]) st[(size_t)i] = 0;
        const int nb = std::min(sg_.nBins, fMaxBin - binLo + 1);
        const auto cands = syncScan(sg_, st, 1, 4, lagLo, lagHi, nb, 1.0f, 24, 3);
        std::set<std::string> seen;
        for (const Cand& c : cands) {
            FtxDecode d;
            if (!tryCandidate(c, t0, df, st, d)) continue;
            if (!seen.insert(d.msg).second) continue;
            res.push_back(d);
        }
        return res;
    }

private:
    bool tryCandidate(const Cand& c, double t0, double df, const std::vector<int8_t>& st, FtxDecode& out) {
        long bs = (long)c.lag * kHop;
        double bf = c.bin * df;
        fineFit(y_, kRate, kN, st, df, bs, bf, df);
        const double eps = driftSearch(y_, kRate, kN, st, df, bs, bf, df);
        std::vector<float> yw;
        if (eps != 0) {
            yw = warpDrift(y_, eps, (double)bs + 0.5 * 85 * kN, kRate);
            fineFit(yw, kRate, kN, st, df, bs, bf, df);
        }
        const std::vector<float>& yy = eps != 0 ? yw : y_;
        const int b0 = (int)std::lround(bf / df);
        const double shift = bf - b0 * df;
        const int nb = 12;   // bins b0 - 2 .. b0 + 9: tone t at index t + 2
        std::vector<std::array<float, 12>> pw(85);
        for (int i = 0; i < 85; i++) symbolPowers(yy, kRate, bs + (long)i * kN, kN, shift, b0 - 2, nb, pw[(size_t)i].data());
        double es = 0, nn = 0;
        int ns = 0;
        for (int i = 0; i < 85; i++) {
            if (kSync[i]) { es += pw[(size_t)i][2]; ns++; }
            nn += 0.5 * (pw[(size_t)i][0] + pw[(size_t)i][11]);
        }
        es /= ns; nn /= 85;
        if (nn <= 0) return false;
        const double sig = std::max(es - nn, 0.1 * nn);
        // the soft bits of the 69 data symbols: log of the sum of the tone likelihoods with the bit 1 against 0
        float soft[207];
        int j = 0;
        for (int i = 0; i < 85; i++) {
            if (kSync[i]) continue;
            double l[8], mx = -1e300;
            for (int g = 0; g < 8; g++) { l[g] = logI0(2 * std::sqrt(sig * pw[(size_t)i][(size_t)(g + 1 + 2)]) / nn); mx = std::max(mx, l[g]); }
            double pb[3][2] = {};
            for (int g = 0; g < 8; g++) {
                const int sy = igray(g);
                const double e = std::exp(l[g] - mx);
                for (int b = 0; b < 3; b++) pb[b][(sy >> (2 - b)) & 1] += e;
            }
            for (int b = 0; b < 3; b++) soft[3 * j + b] = (float)std::log((pb[b][1] + 1e-30) / (pb[b][0] + 1e-30));
            j++;
        }
        soft[206] = 0;
        float coded[206];
        const auto& j0 = interleaveTable();
        for (int i = 0; i < 206; i++) coded[i] = soft[j0[(size_t)i]];
        uint8_t data[13];
        if (!fano32(coded, 72, data, 8000L * 103)) return false;
        uint8_t sym[12];
        for (int i = 0; i < 12; i++) {
            int v = 0;
            for (int b = 0; b < 6; b++) { const int k = i * 6 + b; v = v * 2 + ((data[k / 8] >> (7 - k % 8)) & 1); }
            sym[i] = (uint8_t)v;
        }
        bool freeText = false;
        const std::string msg = unpackJt72(sym, &freeText);
        if (msg.empty()) return false;
        // the decoded codeword against the soft bits
        uint8_t re[206];
        convEncode32(data, 72, re);
        double corr = 0, tot = 0;
        for (int i = 0; i < 206; i++) { corr += (re[i] ? 1 : -1) * coded[i]; tot += std::fabs(coded[i]); }
        lastQ = corr / (tot + 1e-30);
        if (!acceptable(lastQ, freeText)) return false;
        out.mode = ftx::kJt9;
        out.msg = msg;
        out.hz = (float)bf;
        out.dt = (float)((double)bs / kRate + s_.x0 - t0);
        out.snrDb = (float)(10 * std::log10(sig / nn * df / 2500.0));
        fillCallGrid(out);
        return true;
    }

    static int igray(int g) { int n = g; for (int sh = 1; (n >> sh) > 0; sh <<= 1) n ^= n >> sh; return n; }
    static bool acceptable(double q, bool freeText) { return q > (freeText ? 0.30 : 0.12); }

    Slot s_;
    std::vector<float> y_;
    Spectrogram sg_;
public:
    double lastQ = 0;
};

} // namespace

std::vector<FtxDecode> decodeJt9(const Slot& s) { Jt9 d(s); return d.run(); }

} // namespace wsjt
} // namespace dect2
