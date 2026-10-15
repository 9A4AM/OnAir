// Q65 (A .. E, T/R 15 / 30 / 60 / 120 / 300 s) of the HF digital receiver: 85 symbols of nsps / 12000 s (1800, 3600, 7200, 16000, 41472 samples),
// 65-FSK (tone 0 sync at 22 symbols, tones 1 .. 64 data), tone spacing = baud x 1 / 2 / 4 / 8 / 16; the 77-bit message (as 12 six-bit symbols and 5 bits)
// and a CRC-12 go through the Q-ary repeat-accumulate code (15,65) over GF(64) of Nico Palermo, IV3NWV (two CRC symbols punctured): 63 channel
// symbols; decoded by belief propagation over GF(64) with the Bessel metric of the tone energies and a check of the CRC and of the likelihood.
// Decoder: audio resampled so that a symbol is a power of two samples -> spectrogram -> sync tone (tone 0 at the 22 sync symbols) over frequency and
// time -> fine fit -> the 64 tone energies of every symbol -> BP; signals decoded are subtracted and the search repeated. Spread (Doppler / multipath)
// is handled by also trying the energies smeared over the neighbouring bins (the simplest form of WSJT-X's fast-fading metric); a-priori (AP)
// decoding is not done. Reference: WSJT-X lib/qra/q65/*.c, genq65.f90, q65_decode.f90 (K1JT, IV3NWV, GPL-3).
#include "hfdig_wsjt_int.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

namespace dect2 {
namespace wsjt {

namespace {

const int kIsync[22] = {1, 9, 12, 13, 15, 22, 23, 26, 27, 33, 35, 38, 46, 50, 55, 60, 62, 66, 69, 74, 76, 85};

int nspsOf(double period) {
    static const double per[5] = {15, 30, 60, 120, 300};
    static const int ns[5] = {1800, 3600, 7200, 16000, 41472};
    for (int i = 0; i < 5; i++) if (std::fabs(per[i] - period) < 0.5) return ns[i];
    return 7200;
}

int symLen(double baud) { return 1 << (int)std::lround(std::log2(10000.0 / baud)); }

// ---- the GF(64) code: encoder and belief propagation (qracodes.c, q65.c)
int crc12(const int* x, int n, int* out2) {
    int sr = 0;
    for (int k = 0; k < n; k++) {
        int t = x[k];
        for (int j = 0; j < 6; j++) {
            if ((t ^ sr) & 1) sr = (sr >> 1) ^ 0xF01; else sr >>= 1;
            t >>= 1;
        }
    }
    out2[0] = sr & 0x3F; out2[1] = sr >> 6;
    return sr;
}

void qraEncode(const QraCode& c, int* y, const int* x) {
    std::memcpy(y, x, (size_t)c.K * sizeof(int));
    int chk = 0;
    for (int k = 0; k < c.NC; k++) {
        int t = x[c.acc_input_idx[k]];
        if (t) {
            t = (c.gflog[t] + c.acc_input_wlog[k]) % (c.M - 1);
            chk ^= c.gfexp[t];
        }
        y[c.K + k] = chk;
    }
}

// in-place Walsh-Hadamard transform of 64 values
void fwht64(float* x) {
    for (int len = 1; len < 64; len <<= 1)
        for (int i = 0; i < 64; i += 2 * len)
            for (int j = i; j < i + len; j++) { const float a = x[j], b = x[j + len]; x[j] = a + b; x[j + len] = a - b; }
}

float pdNorm(float* p) {
    float t = 0;
    for (int i = 0; i < 64; i++) t += p[i];
    if (t <= 0) { for (int i = 0; i < 64; i++) p[i] = 1.f / 64; return t; }
    const float r = 1.f / t;
    for (int i = 0; i < 64; i++) p[i] *= r;
    return t;
}

int qraExtrinsic(const QraCode& c, float* pex, const float* pix, int maxiter, float* v2c, float* c2v) {
    constexpr int M = 64;
    const int V = c.V, C = c.C;
    float msgout[M];
    std::memcpy(c2v, pix, (size_t)M * (size_t)V * sizeof(float));
    for (int nv = 0; nv < V; nv++) {
        const int ndeg = c.vdeg[nv];
        for (int k = 1; k < ndeg; k++) std::memcpy(v2c + (size_t)c.v2cmidx[nv * c.MAXVDEG + k] * M, pix + (size_t)nv * M, M * sizeof(float));
    }
    int rc = -1;
    for (int nit = 0; nit < maxiter; nit++) {
        for (int nc = V; nc < C; nc++) {
            const int ndeg = c.cdeg[nc];
            if (ndeg == 1) return -2;
            const int base = nc * c.MAXCDEG;
            for (int k = 0; k < ndeg; k++) fwht64(v2c + (size_t)c.c2vmidx[base + k] * M);
            for (int k = 0; k < ndeg; k++) {
                for (int i = 0; i < M; i++) msgout[i] = 1.f / M;
                for (int kk = 0; kk < ndeg; kk++)
                    if (kk != k) { const float* s = v2c + (size_t)c.c2vmidx[base + kk] * M; for (int i = 0; i < M; i++) msgout[i] *= s[i]; }
                msgout[0] += 1e-7f;
                fwht64(msgout);
                const int imsg = c.c2vmidx[base + k], w = c.msgw[imsg];
                float* dst = c2v + (size_t)imsg * M;
                if (w == 0) std::memcpy(dst, msgout, M * sizeof(float));
                else { const int* perm = c.gfpmat + (size_t)w * M; for (int i = 0; i < M; i++) dst[perm[i]] = msgout[i]; }
            }
        }
        for (int nv = 0; nv < V; nv++) {
            const int ndeg = c.vdeg[nv];
            const int base = nv * c.MAXVDEG;
            for (int k = 0; k < ndeg; k++) {
                for (int i = 0; i < M; i++) msgout[i] = 1.f / M;
                for (int kk = 0; kk < ndeg; kk++)
                    if (kk != k) { const float* s = c2v + (size_t)c.v2cmidx[base + kk] * M; for (int i = 0; i < M; i++) msgout[i] *= s[i]; }
                pdNorm(msgout);
                const int imsg = c.v2cmidx[base + k], w = c.msgw[imsg];
                float* dst = v2c + (size_t)imsg * M;
                if (w == 0) std::memcpy(dst, msgout, M * sizeof(float));
                else { const int* perm = c.gfpmat + (size_t)w * M; for (int i = 0; i < M; i++) dst[i] = msgout[perm[i]]; }
            }
        }
        float totex = 0;
        for (int nv = 0; nv < V; nv++) {
            float mx = 0;
            const float* p = v2c + (size_t)nv * M;
            for (int i = 0; i < M; i++) mx = std::max(mx, p[i]);
            totex += mx;
        }
        if (totex > (float)V - 0.01f) { rc = nit; break; }
    }
    std::memcpy(pex, v2c, (size_t)M * (size_t)V * sizeof(float));
    return rc;
}

} // namespace

void q65Encode(const int* msg13, int* cw63) {
    const QraCode& c = kQra15_65_64;
    int x[15], y[65];
    std::memcpy(x, msg13, 13 * sizeof(int));
    crc12(x, 13, x + 13);
    qraEncode(c, y, x);
    std::memcpy(cw63, y, 13 * sizeof(int));
    std::memcpy(cw63 + 13, y + 15, 50 * sizeof(int));
}

int q65Decode(const float* E, int* msg13, int maxIter, float* llhOut) {
    const QraCode& c = kQra15_65_64;
    constexpr int M = 64;
    // the Bessel metric of the energies (qra_mfskbesselmetric)
    const float esNoMetric = (float)(6.0 * (13.0 / 63.0) * std::pow(10.0, 0.28));
    std::vector<float> pix(63 * M), ix(65 * M), ex(65 * M), v2c((size_t)c.NMSG * M), c2v((size_t)c.NMSG * M);
    double rsum = 0;
    for (int i = 0; i < 63 * M; i++) { rsum += E[i]; pix[(size_t)i] = std::sqrt(E[i]); }
    rsum /= 63 * M;
    const float sigma = (float)std::sqrt(rsum / (1.0 + esNoMetric / M) / 2);
    const float cm = std::sqrt(2 * esNoMetric) / sigma;
    for (int k = 0; k < 63; k++) {
        float* r = &pix[(size_t)k * M];
        for (int i = 0; i < M; i++) {
            float v = r[i] * cm;
            const float vsq = v * v;
            v = vsq * (v + 0.039f) / (vsq * 0.9931f + v * 2.6936f + 0.5185f);
            if (v > 80.f) v = 80.f;
            r[i] = std::exp(v);
        }
        pdNorm(r);
    }
    std::memcpy(&ix[0], &pix[0], 13 * M * sizeof(float));
    for (int i = 0; i < 2 * M; i++) ix[(size_t)(13 * M + i)] = 1.f / M;
    std::memcpy(&ix[(size_t)15 * M], &pix[(size_t)13 * M], 50 * M * sizeof(float));
    if (qraExtrinsic(c, ex.data(), ix.data(), maxIter, v2c.data(), c2v.data()) < 0) return -1;
    int x[15];
    for (int k = 0; k < 15; k++) {
        float* r = &ex[(size_t)k * M];
        const float* p = &ix[(size_t)k * M];
        int am = 0;
        float mx = -1;
        for (int i = 0; i < M; i++) { r[i] *= p[i]; if (r[i] >= mx) { mx = r[i]; am = i; } }
        x[k] = am;
    }
    int cr[2];
    crc12(x, 13, cr);
    if (cr[0] != x[13] || cr[1] != x[14]) return -3;
    // the likelihood of the decoded codeword against the intrinsics
    int cw[63];
    q65Encode(x, cw);
    double t = 0;
    for (int k = 0; k < 63; k++) t += std::log(std::max(1e-36f, pix[(size_t)k * M + (size_t)cw[k]]));
    if (llhOut) *llhOut = (float)t;
    if (t < -260.0) return -4;
    std::memcpy(msg13, x, 13 * sizeof(int));
    return 0;
}

double q65Baud(double period) { return 12000.0 / nspsOf(period); }

std::vector<int> q65Tones(const std::string& msg) {
    uint8_t b[77];
    if (!ftx::pack77(msg, b)) return {};
    int d[13];
    for (int i = 0; i < 12; i++) { int v = 0; for (int k = 0; k < 6; k++) v = v * 2 + (b[i * 6 + k] & 1); d[i] = v; }
    int v5 = 0;
    for (int k = 0; k < 5; k++) v5 = v5 * 2 + (b[72 + k] & 1);
    d[12] = 2 * v5;
    int cw[63];
    q65Encode(d, cw);
    std::vector<int> t(85, 0);
    int j = 0, k = 0;
    for (int i = 1; i <= 85; i++) {
        if (j < 22 && i == kIsync[j]) { j++; t[(size_t)i - 1] = 0; }
        else t[(size_t)i - 1] = cw[k++] + 1;
    }
    return t;
}

void addQ65(const std::vector<int>& tones, int sub, double period, double f0, double amp, double rate, double start, std::vector<float>& out) {
    const double tsym = nspsOf(period) / 12000.0, step = (1.0 / tsym) * (double)(1 << std::max(0, std::min(4, sub)));
    std::vector<double> fr(tones.size());
    for (size_t i = 0; i < tones.size(); i++) fr[i] = f0 + tones[i] * step;
    addFsk(fr, tsym, amp, rate, start, out);
}

namespace {

class Q65 {
public:
    explicit Q65(const Slot& s) : s_(s) {
        mult_ = 1 << std::max(0, std::min(4, s.sub));
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
        const int binLo = std::max(2, (int)std::floor(s_.fLo / baud_));
        const int fMaxBin = (int)std::ceil(s_.fHi / baud_);
        const int binHi = std::min(N_ / 2 - 2, fMaxBin + 66 * mult_ + 2);
        std::vector<int8_t> st(85, -1);
        for (int i = 0; i < 22; i++) st[(size_t)kIsync[i] - 1] = 0;
        std::set<std::string> seen;
        for (int pass = 0; pass < 3; pass++) {
            const Spectrogram sg = spectrogram(y_, N_, hop, binLo, binHi);
            if (sg.frames < 4 * 84 + 1) return res;
            const int lagLo = std::max(0, (int)std::floor((t0_ + s_.dtMin - s_.x0) * R_ / hop));
            const int lagHi = std::min(sg.frames - 1 - 4 * 84, (int)std::ceil((t0_ + s_.dtMax - s_.x0) * R_ / hop));
            if (lagHi < lagLo) return res;
            const int nb = std::min(sg.nBins, fMaxBin - binLo + 1);
            const auto cands = syncScan(sg, st, mult_, 4, lagLo, lagHi, nb, 1.2f, 12, 3);
            if (wsjtDebug()) printf("Q65 pass %d: N=%d R=%.1f %d frames lags %d..%d %d candidates\n", pass, N_, R_, sg.frames, lagLo, lagHi, (int)cands.size());
            std::vector<std::pair<double, double>> taken;
            bool found = false;
            for (const Cand& c : cands) {
                const double f = c.bin * baud_;
                bool skip = false;
                for (const auto& sp : taken) if (f > sp.first - 3 * baud_ && f < sp.second + 3 * baud_) skip = true;
                if (skip) continue;
                FtxDecode d;
                const bool ok = tryCandidate(c, hop, st, d);
                if (wsjtDebug()) printf("  cand bin %d (%.2f Hz) lag %d score %.2f: %s\n", c.bin, f, c.lag, c.score, ok ? d.msg.c_str() : "no");
                if (!ok) continue;
                const auto tn = q65Tones(d.msg);
                if (!tn.empty()) {
                    std::vector<double> fr(tn.size());
                    for (size_t i = 0; i < tn.size(); i++) fr[i] = lastFreq_ + tn[i] * baud_ * mult_;
                    if (lastEps_ == 0) subtractTones(y_, R_, lastStart_, N_, fr);
                }
                taken.push_back({lastFreq_, lastFreq_ + 66 * mult_ * baud_});
                found = true;
                if (!seen.insert(d.msg).second) continue;
                d.pass = pass;
                res.push_back(d);
            }
            if (!found) break;
        }
        return res;
    }

private:
    bool tryCandidate(const Cand& c, int hop, const std::vector<int8_t>& st, FtxDecode& out) {
        long bs = (long)c.lag * hop;
        double bf = c.bin * baud_;
        fineFit(y_, R_, N_, st, baud_ * mult_, bs, bf, baud_);
        const double eps = driftSearch(y_, R_, N_, st, baud_ * mult_, bs, bf, baud_);
        std::vector<float> yw;
        if (eps != 0) {
            yw = warpDrift(y_, eps, (double)bs + 0.5 * 85 * N_, R_);
            fineFit(yw, R_, N_, st, baud_ * mult_, bs, bf, baud_);
        }
        const std::vector<float>& yy = eps != 0 ? yw : y_;
        lastEps_ = eps;
        const int b0 = (int)std::lround(bf / baud_);
        const double shift = bf - b0 * baud_;
        // the spectra of the 85 symbols on a grid four times finer than the tone spacing of A (fading spreads the energy of a tone over its neighbours)
        const int ov = 4;
        const int pad = (mult_ + 2) * ov;   // fine bins before tone 0
        const int nbf = (66 * mult_ + 2 * mult_ + 4) * ov;
        std::vector<std::vector<float>> pw(85, std::vector<float>((size_t)nbf));
        for (int i = 0; i < 85; i++) symbolPowersPad(yy, R_, bs + (long)i * N_, N_, ov * N_, shift, b0 * ov - pad, nbf, pw[(size_t)i].data());
        // the widths of the smoothing (sigma in fine bins): none, then growing with the tone spacing (the spread of the channel: ionoscatter, EME)
        std::vector<double> sigmas = {0.0};
        for (double sg : {0.6, 1.2, 2.4, 4.8}) if (sg <= 0.45 * mult_ * ov + 0.5) sigmas.push_back(sg);
        for (double sg : {7.0, 10.0}) if (sg <= 0.45 * mult_ * ov + 0.5 && mult_ >= 2) sigmas.push_back(sg);
        for (double sigma : sigmas) {
            std::vector<double> wt;
            int J = 0;
            if (sigma > 0) {
                J = (int)std::ceil(2.2 * sigma);
                double tot = 0;
                for (int j = -J; j <= J; j++) { wt.push_back(std::exp(-0.5 * j * j / (sigma * sigma))); tot += wt.back(); }
                for (double& v : wt) v /= tot;
            }
            std::vector<float> E(63 * 64);
            int k = 0, j0 = 0;
            for (int i = 1; i <= 85; i++) {
                if (j0 < 22 && i == kIsync[j0]) { j0++; continue; }
                for (int s = 0; s < 64; s++) {
                    const int bin = pad + (s + 1) * mult_ * ov;
                    float e = 0;
                    if (sigma <= 0) e = pw[(size_t)i - 1][(size_t)bin];
                    else for (int j = -J; j <= J; j++) e += (float)(wt[(size_t)(j + J)]) * pw[(size_t)i - 1][(size_t)(bin + j)];
                    E[(size_t)k * 64 + (size_t)s] = e;
                }
                k++;
            }
            int msg13[13];
            float llh = 0;
            if (q65Decode(E.data(), msg13, 100, &llh) != 0) continue;
            if (msg13[12] & 1) continue;
            uint8_t b[77];
            for (int i = 0; i < 12; i++) for (int q = 0; q < 6; q++) b[i * 6 + q] = (uint8_t)((msg13[i] >> (5 - q)) & 1);
            for (int q = 0; q < 5; q++) b[72 + q] = (uint8_t)((msg13[12] >> (5 - q)) & 1);
            const std::string msg = ftx::unpack77(b, &hash_);
            if (msg.empty()) continue;
            // Es/N0 from the energies of the decoded symbols
            int cw[63];
            q65Encode(msg13, cw);
            double sp = 0, nz = 0;
            for (int q = 0; q < 63; q++) for (int s = 0; s < 64; s++) { if (s == cw[q]) sp += E[(size_t)q * 64 + (size_t)s]; else nz += E[(size_t)q * 64 + (size_t)s]; }
            sp /= 63; nz /= 63.0 * 63;
            const double sig = sp < nz ? 0.316 * nz : sp - nz;
            out.msg = msg;
            out.mode = ftx::kQ65;
            out.hz = (float)bf;
            out.dt = (float)((double)bs / R_ + s_.x0 - t0_);
            out.snrDb = (float)(10 * std::log10(std::max(1e-9, sig / std::max(nz, 1e-30))) + 10 * std::log10(baud_ / 2500.0));
            fillCallGrid(out);
            lastStart_ = bs; lastFreq_ = bf;
            return true;
        }
        return false;
    }

    Slot s_;
    int mult_, nsps_, N_;
    double baud_, R_, t0_;
    std::vector<float> y_;
    ftx::CallHash hash_;
    long lastStart_ = 0;
    double lastFreq_ = 0, lastEps_ = 0;
};

} // namespace

std::vector<FtxDecode> decodeQ65(const Slot& s) { Q65 d(s); return d.run(); }

} // namespace wsjt
} // namespace dect2
