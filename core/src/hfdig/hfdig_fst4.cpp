// FST4 and FST4W of the HF digital receiver: 160 symbols of nsps / 12000 s (T/R 15 .. 1800 s), 4-GFSK (BT 2.0) with tone spacing = symbol rate,
// five 8-symbol sync words, 120 data symbols = 240 bits of an LDPC code: FST4 (240,101) with the 77-bit message (scrambled with the FT4 vector) and
// CRC-24; FST4W (240,74) with the 50-bit WSPR message and CRC-24. Decoder: the audio resampled so that a symbol is a power of two samples ->
// spectrogram -> sync words -> non-coherent fit -> coherent refinement of frequency and time on the five sync words -> complex tone amplitudes
// of every symbol (referred to the start of the frame, the phase is the same in every symbol, for any tone) -> bit metrics of single symbols and of 2 and 4 coherently combined
// symbols -> belief propagation, then ordered-statistics decoding (order 2) -> CRC-24. Not done: the 8-symbol combination, multi-pass
// subtraction, a-priori (AP) information. Reference: WSJT-X lib/fst4/*.f90, lib/fst4_decode.f90 (K1JT, G4WJS, GPL-3).
#include "hfdig_wsjt_int.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace dect2 {
namespace wsjt {

namespace {

typedef std::complex<float> cf;

const int kSync1[8] = {0, 1, 3, 2, 1, 0, 2, 3}, kSync2[8] = {2, 3, 1, 0, 3, 2, 0, 1};
const int kGray[4] = {0, 1, 3, 2};   // is -> tone (and tone -> is)

int nspsOf(double period) {
    static const double per[7] = {15, 30, 60, 120, 300, 900, 1800};
    static const int ns[7] = {720, 1680, 3888, 8200, 21504, 66560, 134400};
    for (int i = 0; i < 7; i++) if (std::fabs(per[i] - period) < 0.5) return ns[i];
    return 3888;
}

// the symbol is N = 2^k samples at rate = N * baud (7 .. 14 kHz)
int symLen(double baud) { return 1 << (int)std::lround(std::log2(10000.0 / baud)); }

// CRC-24 of WSJT-X (get_crc24.f90): the remainder of the bits (the CRC positions zero when encoding, the CRC included when checking: 0 = good)
uint32_t crc24(const uint8_t* mc, int len) {
    static const int p[25] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,0,1,1,0,1,1};
    int r[25];
    for (int i = 0; i < 25; i++) r[i] = mc[i];
    for (int i = 0; i <= len - 25; i++) {
        r[24] = mc[i + 24];
        const int f = r[0];
        for (int k = 0; k < 25; k++) r[k] = (r[k] + f * p[k]) & 1;
        const int first = r[0];
        for (int k = 0; k < 24; k++) r[k] = r[k + 1];
        r[24] = first;
    }
    uint32_t v = 0;
    for (int i = 0; i < 24; i++) v = (v << 1) | (uint32_t)r[i];
    return v;
}

// message text -> the data bits of the code (101 or 74 with the CRC)
bool messageBits(const std::string& msg, bool w, uint8_t* bits) {
    if (!w) {
        uint8_t b[77];
        if (!ftx::pack77(msg, b)) return false;
        std::memset(bits, 0, 101);
        for (int i = 0; i < 77; i++) bits[i] = (uint8_t)((b[i] ^ ftx::kFt4Rvec[i]) & 1);
        const uint32_t c = crc24(bits, 101);
        for (int i = 0; i < 24; i++) bits[77 + i] = (uint8_t)((c >> (23 - i)) & 1);
    } else {
        uint8_t d[7];
        if (!ftx::wsprPack(msg, d)) return false;
        std::memset(bits, 0, 74);
        for (int i = 0; i < 50; i++) bits[i] = (uint8_t)((d[i / 8] >> (7 - i % 8)) & 1);
        const uint32_t c = crc24(bits, 74);
        for (int i = 0; i < 24; i++) bits[50 + i] = (uint8_t)((c >> (23 - i)) & 1);
    }
    return true;
}

// which symbol of the frame carries data symbol i (0 .. 119), and which positions are sync
const std::vector<int>& dataPositions() {
    static const std::vector<int> v = [] {
        std::vector<int> d;
        for (int g = 0; g < 4; g++) for (int k = 0; k < 30; k++) d.push_back(8 + g * 38 + k);
        return d;
    }();
    return v;
}
const std::vector<int8_t>& syncTones() {
    static const std::vector<int8_t> v = [] {
        std::vector<int8_t> t(160, -1);
        for (int g = 0; g < 5; g++) for (int k = 0; k < 8; k++) t[(size_t)(g * 38 + k)] = (int8_t)(g % 2 == 0 ? kSync1[k] : kSync2[k]);
        return t;
    }();
    return v;
}

} // namespace

double fst4Baud(double period) { return 12000.0 / nspsOf(period); }

std::vector<int> fst4Tones(const std::string& msg, bool w) {
    uint8_t bits[101], cw[240];
    if (!messageBits(msg, w, bits)) return {};
    ldpcEncode(w ? kLdpc240_74 : kLdpc240_101, bits, cw);
    std::vector<int> t(160, 0);
    const auto& sy = syncTones();
    for (int i = 0; i < 160; i++) if (sy[(size_t)i] >= 0) t[(size_t)i] = sy[(size_t)i];
    const auto& dp = dataPositions();
    for (int i = 0; i < 120; i++) {
        const int is = cw[2 * i + 1] + 2 * cw[2 * i];
        t[(size_t)dp[(size_t)i]] = kGray[is];
    }
    return t;
}

void addFst4(const std::vector<int>& tones, double period, double f0, double amp, double rate, double start, std::vector<float>& out) {
    const int nsps = nspsOf(period);
    const double tsym = nsps / 12000.0, baud = 1.0 / tsym;
    const double c = M_PI * std::sqrt(2.0 / std::log(2.0)), bt = 2.0;
    auto pulse = [&](double x) { return 0.5 * (std::erf(c * bt * (x + 0.5)) - std::erf(c * bt * (x - 0.5))); };
    const double total = (double)tones.size() * tsym;
    const long n0 = std::max(0L, (long)std::floor(start * rate)), n1 = std::min((long)out.size(), (long)std::ceil((start + total) * rate));
    // the phase at n0: the integral of the frequency from the start (a coarse sum, the starting phase is arbitrary but must be smooth)
    double ph = 0;
    for (long n = n0; n < n1; n++) {
        const double t = (double)n / rate - start;
        const double u = t / tsym;
        const int jc = (int)std::floor(u);
        double dev = 0;
        for (int j = jc - 2; j <= jc + 2; j++)
            if (j >= 0 && j < (int)tones.size()) dev += tones[(size_t)j] * pulse(u - j - 0.5);
        double env = 1;
        const double q = tsym / 4;
        if (t < q) env = 0.5 * (1 - std::cos(M_PI * t / q));
        else if (t > total - q) env = 0.5 * (1 - std::cos(M_PI * (total - t) / q));
        out[(size_t)n] += (float)(amp * env * std::sin(ph));
        ph += 2 * M_PI * (f0 + baud * dev) / rate;
        if (ph > 1e5) ph = std::fmod(ph, 2 * M_PI);
    }
}

namespace {

// sum of y e^{-j w (m - origin)} over m = start .. start + n
cf corrAbs(const std::vector<float>& y, double rate, long start, int n, double freq, long origin) {
    const double w = 2 * M_PI * freq / rate;
    const long i0 = std::max(0L, -start), i1 = std::min((long)n, (long)y.size() - start);
    if (i1 <= i0) return cf(0, 0);
    const float cs = (float)std::cos(w), sn = (float)std::sin(w);
    double ar = 0, ai = 0;
    constexpr int kBlock = 64;
    for (long b = i0; b < i1; b += kBlock) {
        const long e = std::min(i1, b + kBlock);
        const double ph = w * (double)(start + b - origin);
        float cr = (float)std::cos(ph), ci = (float)-std::sin(ph);
        const float* p = &y[(size_t)(start + b)];
        float sr = 0, si = 0;
        for (long k = 0; k < e - b; k++) {
            sr += p[k] * cr; si += p[k] * ci;
            const float t = cr * cs + ci * sn;
            ci = ci * cs - cr * sn;
            cr = t;
        }
        ar += sr; ai += si;
    }
    return cf((float)ar, (float)ai);
}

class Fst4 {
public:
    Fst4(const Slot& s, bool w) : s_(s), w_(w) {
        nsps_ = nspsOf(s.period);
        baud_ = 12000.0 / nsps_;
        N_ = symLen(baud_);
        R_ = baud_ * N_;
        t0_ = s.period < 20 ? 0.5 : 1.0;
    }

    std::vector<FtxDecode> run() {
        std::vector<FtxDecode> res;
        y_ = resample8k(s_.x, s_.n, R_);
        const int hop = N_ / 4;
        const double df = baud_;
        const int binLo = std::max(2, (int)std::floor(s_.fLo / df));
        const int fMaxBin = (int)std::ceil(s_.fHi / df);
        const int binHi = std::min(N_ / 2 - 2, fMaxBin + 6);
        const Spectrogram sg = spectrogram(y_, N_, hop, binLo, binHi);
        if (wsjtDebug()) printf("FST4 start: %zu samples at %.1f Hz, %d frames\n", y_.size(), R_, sg.frames);
        if (sg.frames < 4 * 159 + 1) return res;
        const int lagLo = std::max(0, (int)std::floor((t0_ + s_.dtMin - s_.x0) * R_ / hop));
        const int lagHi = std::min(sg.frames - 1 - 4 * 159, (int)std::ceil((t0_ + s_.dtMax - s_.x0) * R_ / hop));
        if (lagHi < lagLo) return res;
        const int nb = std::min(sg.nBins, fMaxBin - binLo + 1);
        const auto cands = syncScan(sg, syncTones(), 1, 4, lagLo, lagHi, nb, 1.0f, 10, 3);
        if (wsjtDebug()) printf("FST4%s: N=%d R=%.1f %d frames lags %d..%d %d candidates\n", w_ ? "W" : "", N_, R_, sg.frames, lagLo, lagHi, (int)cands.size());
        std::set<std::string> seen;
        for (const Cand& c : cands) {
            FtxDecode d;
            const bool ok = tryCandidate(c, hop, d);
            if (wsjtDebug()) printf("  cand bin %d (%.2f Hz) lag %d score %.2f: %s\n", c.bin, c.bin * df, c.lag, c.score, ok ? d.msg.c_str() : "no");
            if (ok && seen.insert(d.msg).second) res.push_back(d);
        }
        return res;
    }

private:
    // the coherent power of the sync words at (start, f0 = tone 0 frequency)
    double cohScore(long start, double f0) const {
        const auto& sy = syncTones();
        double tot = 0;
        for (int g = 0; g < 5; g++) {
            cf acc(0, 0);
            for (int m = 0; m < 8; m++) {
                const int sym = g * 38 + m;
                const cf c = corrAbs(*yy_, R_, start + (long)sym * N_, N_, f0 + sy[(size_t)sym] * baud_, start);
                acc += c;
            }
            tot += std::norm(acc);
        }
        return tot;
    }

    bool tryCandidate(const Cand& c, int hop, FtxDecode& out) {
        long bs = (long)c.lag * hop;
        double bf = c.bin * baud_;
        fineFit(y_, R_, N_, syncTones(), baud_, bs, bf, baud_);
        const double eps = driftSearch(y_, R_, N_, syncTones(), baud_, bs, bf, baud_);
        std::vector<float> yw;
        if (eps != 0) {
            yw = warpDrift(y_, eps, (double)bs + 0.5 * 160 * N_, R_);
            fineFit(yw, R_, N_, syncTones(), baud_, bs, bf, baud_);
        }
        yy_ = eps != 0 ? &yw : &y_;
        // coherent refinement
        {
            double best = -1;
            long s0 = bs; double f1 = bf;
            for (int pass = 0; pass < 2; pass++) {
                const double fstep = baud_ / (pass ? 128.0 : 32.0);
                const long sstep = std::max<long>(1, N_ / (pass ? 64 : 32));
                const int fr = pass ? 2 : 6, sr = 1;
                const long sc = s0; const double fc = f1;
                for (int k = -sr; k <= sr; k++)
                    for (int q = -fr; q <= fr; q++) {
                        const double e = cohScore(sc + k * sstep, fc + q * fstep);
                        if (e > best) { best = e; s0 = sc + k * sstep; f1 = fc + q * fstep; }
                    }
            }
            bs = s0; bf = f1;
        }
        // the complex amplitude of every tone of every symbol
        std::vector<std::array<cf, 4>> cs(160);
        for (int i = 0; i < 160; i++)
            for (int t = 0; t < 4; t++) cs[(size_t)i][(size_t)t] = corrAbs(*yy_, R_, bs + (long)i * N_, N_, bf + t * baud_, bs);
        // hard sync symbols
        const auto& sy = syncTones();
        int nsync = 0;
        double es = 0, nn = 0;
        for (int i = 0; i < 160; i++) {
            if (sy[(size_t)i] < 0) continue;
            int am = 0;
            for (int t = 1; t < 4; t++) if (std::norm(cs[(size_t)i][(size_t)t]) > std::norm(cs[(size_t)i][(size_t)am])) am = t;
            if (am == sy[(size_t)i]) nsync++;
            es += std::norm(cs[(size_t)i][(size_t)sy[(size_t)i]]);
            for (int t = 0; t < 4; t++) if (t != sy[(size_t)i]) nn += std::norm(cs[(size_t)i][(size_t)t]) / 3.0;
        }
        es /= 40; nn /= 40;
        if (nsync < 16 || nn <= 0) return false;
        const double snr = 10 * std::log10(std::max(es - nn, 0.01 * nn) / nn * baud_ / 2500.0);
        // bit metrics for 1, 2 and 4 combined symbols
        for (int ns : {1, 2, 4}) {
            std::vector<float> bm(320, 0.f);
            bitMetrics(cs, ns, bm);
            std::vector<float> llr(240);
            const auto& dp = dataPositions();
            for (int i = 0; i < 120; i++) { llr[(size_t)(2 * i)] = bm[(size_t)(2 * dp[(size_t)i])]; llr[(size_t)(2 * i + 1)] = bm[(size_t)(2 * dp[(size_t)i] + 1)]; }
            double mean = 0, var = 0;
            for (float v : llr) mean += v;
            mean /= 240;
            for (float v : llr) var += (v - mean) * (v - mean);
            const double sd = std::sqrt(var / 240 + 1e-30);
            for (float& v : llr) v = (float)(2.83 * v / sd);
            if (decodeBits(llr.data(), out)) {
                out.mode = w_ ? ftx::kFst4w : ftx::kFst4;
                out.hz = (float)bf;
                out.dt = (float)((double)bs / R_ + s_.x0 - t0_);
                out.snrDb = (float)snr;
                out.pass = ns == 1 ? 0 : ns == 2 ? 1 : 2;
                return true;
            }
        }
        return false;
    }

    // the metric of every bit of the frame (2 per symbol) from groups of ns symbols combined coherently (referred to the start of the frame the phase is the same in every symbol)
    static void bitMetrics(const std::vector<std::array<cf, 4>>& cs, int ns, std::vector<float>& bm) {
        const int combos = 1 << (2 * ns);
        std::vector<float> mag((size_t)combos);
        for (int g = 0; g + ns <= 160; g += ns) {
            for (int v = 0; v < combos; v++) {
                cf acc(0, 0);
                for (int m = 0; m < ns; m++) {
                    const int is = (v >> (2 * (ns - 1 - m))) & 3;
                    const cf c = cs[(size_t)(g + m)][(size_t)kGray[is]];
                    acc += c;
                }
                mag[(size_t)v] = std::abs(acc);
            }
            for (int ib = 0; ib < 2 * ns; ib++) {
                float m1 = -1e30f, m0 = -1e30f;
                const int bit = 2 * ns - 1 - ib;   // MSB first
                for (int v = 0; v < combos; v++) {
                    if ((v >> bit) & 1) m1 = std::max(m1, mag[(size_t)v]); else m0 = std::max(m0, mag[(size_t)v]);
                }
                bm[(size_t)(2 * g + ib)] = m1 - m0;
            }
        }
    }

    struct Ctx { const LdpcTab* t; int kbits; };
    static bool crcOk(const uint8_t* cw, void* c) {
        const Ctx* x = (const Ctx*)c;
        return crc24(cw, x->kbits) == 0;
    }

    bool decodeBits(const float* llr, FtxDecode& d) {
        const LdpcTab& t = w_ ? kLdpc240_74 : kLdpc240_101;
        uint8_t cw[240];
        Ctx ctx{&t, t.k};
        bool ok = false;
        if (ldpcBp(t, llr, 50, cw) == 0 && crc24(cw, t.k) == 0) ok = true;
        if (!ok) ok = ldpcOsd(t, llr, 2, cw, crcOk, &ctx);
        if (!ok) return false;
        std::string msg;
        if (!w_) {
            uint8_t b[77];
            for (int i = 0; i < 77; i++) b[i] = (uint8_t)((cw[i] ^ ftx::kFt4Rvec[i]) & 1);
            msg = ftx::unpack77(b, &hash_);
        } else {
            uint8_t db[7] = {};
            for (int i = 0; i < 50; i++) if (cw[i]) db[i / 8] |= (uint8_t)(0x80 >> (i % 8));
            int dbm = 0;
            msg = ftx::wsprUnpack(db, wsprHash_, &dbm);
            d.dbm = dbm;
        }
        if (msg.empty()) return false;
        d.msg = msg;
        if (w_) {
            // WSPR style: the calling station and its grid
            const size_t a = msg.find(' ');
            if (a != std::string::npos) { d.call = msg.substr(0, a); if (d.call.size() > 2 && d.call.front() == '<') d.call = d.call.substr(1, d.call.size() - 2); const size_t b = msg.rfind(' '); if (b > a) d.grid = msg.substr(a + 1, b - a - 1); }
            d.cq = true;
        } else fillCallGrid(d);
        return true;
    }

    Slot s_;
    bool w_;
    int nsps_, N_;
    double baud_, R_, t0_;
    std::vector<float> y_;
    const std::vector<float>* yy_ = nullptr;   // the audio of the candidate being tried (y_, or a copy with the drift taken out)
    ftx::CallHash hash_;
    std::map<uint32_t, std::string> wsprHash_;
};

} // namespace

std::vector<FtxDecode> decodeFst4(const Slot& s, bool w) { Fst4 d(s, w); return d.run(); }

} // namespace wsjt
} // namespace dect2
