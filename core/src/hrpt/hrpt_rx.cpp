// HRPT / AHRPT receiver (see hrpt_rx.h). The demodulator follows the Meteor LRPT one; the code layers are meteor_proto.h's.
#include "dect2/hrpt/hrpt_rx.h"
#include "dect2/results/results_io.h"
#include "dect2/hrpt/hrpt_tel.h"
#include "dect2/hrpt/hrpt_proto.h"
#include "dect2/meteor/meteor_proto.h"
#include "dect2/dsp/dsp_compat.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstring>
#include <mutex>
#include <vector>

namespace dect2 {

namespace {

using cd = std::complex<double>;
constexpr double kPi = 3.14159265358979323846;
constexpr double kPubSec = 0.25;
constexpr int kHist = 4096;             // decimated samples kept (power of two)
constexpr int kAcqLog2 = 10, kAcqN = 1 << kAcqLog2, kAcqBlocks = 48;
constexpr double kSearchHz = 75000;     // coarse carrier search range (L-band Doppler is up to about +-40 kHz)

double rrcTap(double t, double a) {     // t in symbols
    if (std::fabs(t) < 1e-9) return 1 - a + 4 * a / kPi;
    if (std::fabs(std::fabs(t) - 1 / (4 * a)) < 1e-9)
        return a / std::sqrt(2.0) * ((1 + 2 / kPi) * std::sin(kPi / (4 * a)) + (1 - 2 / kPi) * std::cos(kPi / (4 * a)));
    return (std::sin(kPi * t * (1 - a)) + 4 * a * t * std::cos(kPi * t * (1 + a))) / (kPi * t * (1 - 16 * a * a * t * t));
}

struct Fir {   // decimating FIR on a doubled ring, real and imaginary parts apart and the taps padded to 8 so the loop vectorizes
    std::vector<float> h, bre, bim;
    int n = 1, np = 8, pos = 0, d = 1, ph = 0;
    void init(const std::vector<float>& taps, int dec) {
        n = (int)taps.size(); np = (n + 7) / 8 * 8;
        h.assign((size_t)np, 0.f);
        for (int i = 0; i < n; i++) h[(size_t)i] = taps[(size_t)i];
        bre.assign(2 * (size_t)np, 0.f); bim.assign(2 * (size_t)np, 0.f);
        d = dec; pos = 0; ph = 0;
    }
    bool push(cf32 x, cf32& y) {
        bre[(size_t)pos] = bre[(size_t)(pos + np)] = x.real();
        bim[(size_t)pos] = bim[(size_t)(pos + np)] = x.imag();
        pos = pos + 1 == np ? 0 : pos + 1;
        if (++ph < d) return false;
        ph = 0;
        const float* br = &bre[(size_t)pos];
        const float* bi = &bim[(size_t)pos];
        float ar[8] = {}, ai[8] = {};
        for (int i = 0; i < np; i += 8)
            for (int k = 0; k < 8; k++) { ar[k] += br[i + k] * h[(size_t)(i + k)]; ai[k] += bi[i + k] * h[(size_t)(i + k)]; }
        float re = 0, im = 0;
        for (int k = 0; k < 8; k++) { re += ar[k]; im += ai[k]; }
        y = cf32(re, im);
        return true;
    }
};

inline int8_t sat8(float v) { return (int8_t)std::clamp((int)std::lround(v), -127, 127); }
inline int sgn(float v) { return v >= 0 ? 1 : -1; }

// the 8 phase ambiguities on a pair of soft values seen as (I, Q): 4 rotations, mirrored or not
inline void xform(int t, int a, int b, int& oa, int& ob) {
    if (t & 4) b = -b;
    switch (t & 3) {
    case 0: oa = a; ob = b; break;
    case 1: oa = -b; ob = a; break;
    case 2: oa = -a; ob = -b; break;
    default: oa = b; ob = -a; break;
    }
}

constexpr uint64_t kMask60 = (1ull << 60) - 1;
constexpr int kNoaaBits = hrpt::kNoaaWords * 10;
// retries of the variants in automatic: NOAA (retired satellites) once in five
constexpr int kAutoSeq[] = {1, 2, 1, 2, 3};

} // namespace

double hrptSymbolRate(int v) { return v == 2 ? 2.8e6 : v == 3 ? 1.3308e6 : 2.33e6; }

struct HrptReceiver::Impl {
    std::mutex mu;
    std::atomic<bool> resetReq{true}, clearReq{false};
    std::atomic<int> varSet{0};
    double rateSet = 0, offsetSet = 0;
    std::function<void(const std::string&)> log;

    // run settings
    double rate = 0, offset = 0;
    int var = 1, varRunSetting = 0, autoIdx = 0;
    double rs = 2.33e6, alpha = 0.6;
    bool bpsk = false;

    // front end
    cd rot{1, 0}, step{1, 0};
    double coarseHz = 0;
    int dec = 1;
    double fd = 0, sps = 4;
    Fir lpf, rrc;
    std::vector<cf32> hist;
    int64_t hN = 0;
    double tau = 0, spsEst = 4;
    double theta = 0, omega = 0;
    double lalpha = 0, lbeta = 0, kp = 0, ki = 0;
    float amp = 0.1f;
    float prevI = 0, prevQ = 0;
    int64_t nSym = 0;
    float lockAvg = 0, varAvg = 1;
    // coarse search
    bool coarseDone = false;
    std::vector<float> acqRe, acqIm, psd;
    int acqFill = 0, acqBlocks = 0;

    // AHRPT chain
    std::vector<int8_t> pairs;           // soft pairs a, b
    int64_t pairsSinceSearch = 0;
    bool searchLock = false;
    int xt = -1;
    hrpt::Viterbi vit;
    uint32_t sr = 0;
    int64_t bitCount = 0;
    bool inFrame = false, inv = false, frameLock = false;
    int frameBits = 0, misses = 0;
    uint8_t frame[meteor::kCodedBytes];
    int64_t sinceFrame = 0;
    double lastGoodSec = -1e9, lastAsmSec = -1e9, varStartSec = 0, lockLostSec = 0;
    meteor::PacketDemux demux[64];

    // NOAA chain: Manchester chips, 60-bit sync search in both pairings, 11090 words a frame
    float prevChip = 0;
    int64_t chipN = 0;
    uint64_t nsr[2] = {0, 0};
    bool noaaLock = false, nInv = false;
    int nPhase = 0, nBits = 0, nMiss = 0;
    std::vector<uint16_t> nWords;
    int nAcc = 0;                        // bits of the word being collected

    // picture
    HrptImage img;
    bool imgLoad = false;
    std::shared_ptr<const HrptImage> imgIn;
    bool imgDirty = false;
    uint64_t imgId = 0;
    bool haveLine = false;
    uint32_t lastMs = 0;
    int lastIdx = 0;

    // telemetry
    HrptTelemetry tel, pub;
    uint64_t seq = 0;
    int64_t nIn = 0;
    double nextReport = 0, power = 0;
    int64_t nPower = 0;
    std::vector<std::pair<float, float>> constel;
    size_t constelPos = 0;

    double nowSec() const { return rate > 0 ? (double)nIn / rate : 0; }
    void say(const std::string& s) { if (log) log("HRPT: " + s); }
    bool locked() const { return bpsk ? noaaLock : searchLock; }

    void setVar(int v) {
        var = v;
        rs = hrptSymbolRate(v);
        bpsk = v == 3;
        alpha = bpsk ? 1.0 : 0.6;
    }

    void initDemod() {
        if (rate <= 0) return;
        dec = std::max(1, (int)std::floor(rate / (4 * rs)));
        fd = rate / dec;
        sps = fd / rs;
        const double bw1 = rs * (1 + alpha) / 2;
        const double df = std::max(fd - 2 * bw1, 0.2 * fd);
        int n = 2 * (int)std::ceil(3.5 * rate / df / 2) + 1;
        n = std::clamp(n, 9, 401);
        std::vector<float> h((size_t)n);
        double sum = 0;
        const double fc = (fd / 2) / rate;
        for (int i = 0; i < n; i++) {
            const double t = i - (n - 1) / 2.0;
            const double w = 0.54 - 0.46 * std::cos(2 * kPi * i / (n - 1));
            const double s = std::fabs(t) < 1e-9 ? 2 * fc : std::sin(2 * kPi * fc * t) / (kPi * t);
            h[(size_t)i] = (float)(s * w); sum += s * w;
        }
        for (auto& v : h) v = (float)(v / sum);
        if (dec == 1) h.assign(1, 1.f);
        lpf.init(h, dec);
        const int span = 4;
        const int m = 2 * (int)std::ceil(span * sps) + 1;
        std::vector<float> g((size_t)m);
        double e = 0;
        for (int i = 0; i < m; i++) { const double t = (i - (m - 1) / 2.0) / sps; g[(size_t)i] = (float)rrcTap(t, alpha); e += g[(size_t)i]; }
        for (auto& v : g) v = (float)(v / e);
        rrc.init(g, 1);
        hist.assign(kHist, cf32(0, 0));
        hN = 0;
        tau = 8; spsEst = sps;
        theta = 0; omega = 0;
        amp = 0; prevI = prevQ = 0; nSym = 0; lockAvg = 0; varAvg = 1;
        loopGains(0.02);
        kp = 0.02 * sps; ki = 2e-5 * sps;
        coarseDone = false; acqFill = 0; acqBlocks = 0;
        acqRe.assign(kAcqN, 0); acqIm.assign(kAcqN, 0); psd.assign(kAcqN, 0);
        step = std::polar(1.0, -2 * kPi * (offset + coarseHz) / rate);
        resetChain();
    }

    void loopGains(double bn) {
        const double z = 0.707, th = bn / (z + 1 / (4 * z)), d = 1 + 2 * z * th + th * th;
        lalpha = 4 * z * th / d; lbeta = 4 * th * th / d;
    }

    void resetChain() {
        pairs.clear(); pairsSinceSearch = 0; searchLock = false; xt = -1;
        vit.reset();
        sr = 0; bitCount = 0; inFrame = false; frameLock = false; frameBits = 0; misses = 0; sinceFrame = 0;
        for (auto& d : demux) d.reset();
        prevChip = 0; chipN = 0; nsr[0] = nsr[1] = 0; noaaLock = false; nMiss = 0; nBits = 0; nAcc = 0;
        nWords.assign((size_t)hrpt::kNoaaWords, 0);
    }

    void resetState() {
        { std::lock_guard<std::mutex> lk(mu); rate = rateSet; offset = offsetSet; }
        varRunSetting = varSet.load();
        autoIdx = 0;
        setVar(varRunSetting ? varRunSetting : kAutoSeq[0]);
        coarseHz = 0;
        rot = cd(1, 0);
        initDemod();
        varStartSec = nowSec(); lastGoodSec = -1e9; lastAsmSec = -1e9;
        tel.varSetting = varRunSetting;
        if (rate > 0 && sps < 1.5) say("the sample rate is low for this signal: use 6 Msps or more");
    }

    void newImage() {
        img = HrptImage();
        img.id = ++imgId;
        haveLine = false; lastIdx = 0;
        imgDirty = true;
    }

    // --- coarse carrier ---
    // QPSK: the raised-cosine shape fitted to the averaged power spectrum. BPSK: the line the squared signal has at twice the offset.
    void coarseSample(cf32 y) {
        if (bpsk) y = y * y;
        acqRe[(size_t)acqFill] = y.real(); acqIm[(size_t)acqFill] = y.imag();
        if (++acqFill < kAcqN) return;
        acqFill = 0;
        for (int i = 0; i < kAcqN; i++) { const float w = (float)(0.5 - 0.5 * std::cos(2 * kPi * i / kAcqN)); acqRe[(size_t)i] *= w; acqIm[(size_t)i] *= w; }
        fftSplit(acqRe.data(), acqIm.data(), kAcqLog2, false);
        for (int i = 0; i < kAcqN; i++) psd[(size_t)i] += acqRe[(size_t)i] * acqRe[(size_t)i] + acqIm[(size_t)i] * acqIm[(size_t)i];
        if (++acqBlocks < kAcqBlocks) return;
        acqBlocks = 0;
        const double bin = fd / kAcqN;
        double est = 0;
        if (bpsk) {
            const int maxB = std::max(1, (int)std::min(2 * kSearchHz / bin, fd / 4 / bin));
            int best = 0; double bv = -1;
            std::vector<double> c((size_t)(2 * maxB + 1));
            for (int s = -maxB; s <= maxB; s++) {
                const double v = psd[(size_t)((s + kAcqN) % kAcqN)];
                c[(size_t)(s + maxB)] = v;
                if (v > bv) { bv = v; best = s + maxB; }
            }
            double frac = 0;
            if (best > 0 && best + 1 < (int)c.size()) {
                const double a = c[(size_t)best - 1], b = c[(size_t)best], d = c[(size_t)best + 1];
                const double den = a - 2 * b + d;
                if (den < 0) frac = 0.5 * (a - d) / den;
            }
            est = (best - maxB + frac) * bin / 2;
        } else {
            const double f1 = rs * (1 - alpha) / 2, f2 = rs * (1 + alpha) / 2;
            const int maxShift = std::max(1, (int)std::min(kSearchHz / bin, (fd / 2 - f2) / bin));
            std::vector<double> c((size_t)(2 * maxShift + 1));
            for (int s = -maxShift; s <= maxShift; s++) {
                double acc = 0;
                for (int k = -kAcqN / 2; k < kAcqN / 2; k++) {
                    const double f = std::fabs((k - s) * bin);
                    if (f >= f2) continue;
                    const double tpl = f <= f1 ? 1.0 : 0.5 * (1 + std::cos(kPi * (f - f1) / (rs * alpha)));
                    acc += tpl * psd[(size_t)((k + kAcqN) % kAcqN)];
                }
                c[(size_t)(s + maxShift)] = acc;
            }
            int bi = 0;
            for (int i = 1; i < (int)c.size(); i++) if (c[(size_t)i] > c[(size_t)bi]) bi = i;
            double frac = 0;
            if (bi > 0 && bi + 1 < (int)c.size()) {
                const double a = c[(size_t)bi - 1], b = c[(size_t)bi], d = c[(size_t)bi + 1];
                const double den = a - 2 * b + d;
                if (den < 0) frac = 0.5 * (a - d) / den;
            }
            est = (bi - maxShift + frac) * bin;
        }
        std::fill(psd.begin(), psd.end(), 0.f);
        coarseHz += est;
        step = std::polar(1.0, -2 * kPi * (offset + coarseHz) / rate);
        coarseDone = true;
        theta = 0; omega = 0; nSym = 0; loopGains(0.02);
    }

    // --- symbols ---
    cf32 interp(double t) const {
        const int64_t i = (int64_t)std::floor(t);
        const float mu = (float)(t - (double)i);
        const cf32 xm = hist[(size_t)((i - 1) & (kHist - 1))], x0 = hist[(size_t)(i & (kHist - 1))], x1 = hist[(size_t)((i + 1) & (kHist - 1))],
                   x2 = hist[(size_t)((i + 2) & (kHist - 1))];
        const float cm = -mu * (mu - 1) * (mu - 2) / 6, c0 = (mu + 1) * (mu - 1) * (mu - 2) / 2, c1 = -(mu + 1) * mu * (mu - 2) / 2, c2 = (mu + 1) * mu * (mu - 1) / 6;
        return xm * cm + x0 * c0 + x1 * c1 + x2 * c2;
    }

    void decimated(cf32 y) {
        if (!coarseDone) { coarseSample(y); return; }
        cf32 z;
        rrc.push(y, z);
        hist[(size_t)(hN & (kHist - 1))] = z;
        hN++;
        if (amp <= 0) amp = 1e-9f;
        while (tau + spsEst / 2 + 3 < (double)hN) {
            if (tau - spsEst - 2 < (double)(hN - kHist)) { tau = (double)hN - spsEst; continue; }
            symbol();
        }
    }

    void symbol() {
        const double T = spsEst;
        const float c0 = (float)std::cos(theta), s0 = (float)std::sin(theta);
        const float hw = (float)(omega / 2), ch = 1.f - hw * hw / 2, sh = hw;   // omega is small: the half step by its series
        const cf32 r0(c0, -s0), r1(c0 * ch + s0 * sh, -(s0 * ch - c0 * sh));    // rotation by -theta and by -(theta - omega / 2)
        const cf32 a = interp(tau) * r0;
        const cf32 m = interp(tau - T / 2) * r1;
        const float I = a.real(), Q = a.imag();
        const float eT = bpsk ? (prevI - I) * m.real() : (prevI - I) * m.real() + (prevQ - Q) * m.imag();
        prevI = I; prevQ = Q;
        const float mag = bpsk ? std::fabs(I) : 0.5f * (std::fabs(I) + std::fabs(Q));
        if (nSym == 0) amp = std::max(mag, 1e-9f);
        amp += 0.002f * (mag - amp);
        const float In = I / amp, Qn = Q / amp;
        const float eC = bpsk ? std::clamp((float)sgn(In) * Qn, -1.f, 1.f) : std::clamp((float)sgn(In) * Qn - (float)sgn(Qn) * In, -1.f, 1.f);
        theta += omega + lalpha * eC;
        omega += lbeta * eC;
        omega = std::clamp(omega, -0.25, 0.25);
        if (theta > kPi) theta -= 2 * kPi; else if (theta < -kPi) theta += 2 * kPi;
        const float eTn = std::clamp(eT / (amp * amp), -1.f, 1.f);
        tau += T + kp * eTn;
        spsEst += ki * eTn;
        spsEst = std::clamp(spsEst, sps * 0.995, sps * 1.005);
        nSym++;
        if (nSym == (int64_t)(rs * 0.002)) loopGains(0.008);
        const float r2 = In * In + Qn * Qn;
        if (bpsk) {
            lockAvg += 0.002f * (In * In - Qn * Qn) / std::max(r2, 1e-6f) - 0.002f * lockAvg;
            const float dv = (std::fabs(In) - 1) * (std::fabs(In) - 1);
            varAvg += 0.002f * (dv - varAvg);
        } else {
            if (r2 > 1e-6f) {
                const float re4 = (In * In - Qn * Qn) * (In * In - Qn * Qn) - 4 * In * In * Qn * Qn;
                lockAvg += 0.002f * (-re4 / (r2 * r2) - lockAvg);
            }
            const float dv = (std::fabs(In) - 1) * (std::fabs(In) - 1) + (std::fabs(Qn) - 1) * (std::fabs(Qn) - 1);
            varAvg += 0.002f * (0.5f * dv - varAvg);
        }
        if (nSym % 4 == 0) {
            if (constel.size() < 256) constel.emplace_back(In, bpsk ? 0.f : Qn); else { constel[constelPos] = {In, bpsk ? 0.f : Qn}; constelPos = (constelPos + 1) % 256; }
        }
        if (bpsk) chip(In); else afterSoft(sat8(In * 40), sat8(Qn * 40));
    }

    // ------------------------------------------------------------ NOAA: Manchester chips
    void chip(float s) {
        const float d = prevChip - s;       // first chip minus second: positive is a one in one polarity
        prevChip = s;
        const int p = (int)(chipN & 1);
        chipN++;
        if (noaaLock) {
            if (p != nPhase) return;
            noaaBit((d > 0) != nInv);
            return;
        }
        uint64_t& r = nsr[p];
        r = (r << 1 | (d > 0 ? 1u : 0u)) & kMask60;
        const int e = __builtin_popcountll(r ^ hrpt::noaaSync60());
        if (e > 5 && 60 - e > 5) return;
        nInv = 60 - e < e; nPhase = p; noaaLock = true; nMiss = 0;
        nBits = 0; nAcc = 0;
        std::fill(nWords.begin(), nWords.end(), 0);
        const uint64_t v = nInv ? (~r & kMask60) : r;
        for (int i = 59; i >= 0; i--) noaaBit(v >> i & 1);
        tel.asmHits++; tel.rotation = (nInv ? 1 : 0) + 2 * nPhase; lastAsmSec = nowSec();
        say(std::string("sync words found (polarity ") + (nInv ? "inverted" : "normal") + ", Manchester phase " + std::to_string(nPhase) + ")");
    }

    void noaaBit(bool b) {
        uint16_t& w = nWords[(size_t)(nBits / 10)];
        w = (uint16_t)(w << 1 | (b ? 1 : 0));
        if (++nBits < kNoaaBits) return;
        nBits = 0;
        gotNoaaFrame();
    }

    void gotNoaaFrame() {
        hrpt::Line L;
        if (!hrpt::decodeNoaaFrame(nWords.data(), L)) {
            tel.blocksBad++;
            if (++nMiss > 3) { noaaLock = false; nsr[0] = nsr[1] = 0; say("frame sync lost"); }
            std::fill(nWords.begin(), nWords.end(), 0);
            return;
        }
        nMiss = 0;
        tel.blocksOk++; tel.asmHits++; tel.apidPackets[2]++;
        lastGoodSec = nowSec(); lastAsmSec = lastGoodSec;
        placeLine(L);
        std::fill(nWords.begin(), nWords.end(), 0);
    }

    // ------------------------------------------------------------ AHRPT bit chain
    void afterSoft(int8_t a, int8_t b) {
        if (searchLock) {
            int oa, ob;
            xform(xt, a, b, oa, ob);
            vitPush(oa, ob);
            return;
        }
        pairs.push_back(a); pairs.push_back(b);
        pairsSinceSearch++;
        const int64_t need = 8192 * 2 + 64;
        if ((int64_t)pairs.size() / 2 > 2 * need) pairs.erase(pairs.begin(), pairs.begin() + (std::ptrdiff_t)(pairs.size() - (size_t)need * 2));
        if (pairsSinceSearch >= 4096 && (int64_t)pairs.size() / 2 >= 8192 + 64) { pairsSinceSearch = 0; ambiguitySearch(); }
    }

    // the encoded sync word in the hard decisions, per ambiguity; two hits a frame apart settle it, then the Viterbi decoder takes over
    void ambiguitySearch() {
        static const uint64_t pat = meteor::encodedAsm();
        const uint64_t mask = (1ull << 52) - 1;
        const int np = (int)(pairs.size() / 2);
        for (int t = 0; t < 8; t++) {
            uint64_t w = 0;
            std::vector<int> hits;
            for (int i = 0; i < np; i++) {
                int oa, ob;
                xform(t, pairs[(size_t)i * 2], pairs[(size_t)i * 2 + 1], oa, ob);
                w = w << 2 | (uint64_t)(oa > 0) << 1 | (uint64_t)(ob > 0);
                if (i < 31) continue;
                if (__builtin_popcountll((w ^ pat) & mask) <= 7) hits.push_back(i);
            }
            int at = -1;
            for (size_t x = 0; x < hits.size() && at < 0; x++)
                for (size_t y = x + 1; y < hits.size(); y++) if (hits[y] - hits[x] == 8192) { at = hits[y]; break; }
            if (at < 0) continue;
            searchLock = true; xt = t;
            tel.rotation = t;
            lastAsmSec = nowSec();
            vit.reset();
            sr = 0; inFrame = false; frameLock = false;
            const int from = std::max(0, at - 31 - 8192 - 40);
            for (int i = from; i < np; i++) {
                int oa, ob;
                xform(xt, pairs[(size_t)i * 2], pairs[(size_t)i * 2 + 1], oa, ob);
                vitPush(oa, ob);
            }
            pairs.clear();
            say(std::string("sync word found (") + hrptVarName(var) + ", ambiguity " + std::to_string(t) + ")");
            return;
        }
    }

    void vitPush(int a, int b) {
        vit.push(a, b);
        auto& o = vit.out();
        for (uint8_t bit : o) frameBit(bit);
        o.clear();
    }

    void frameBit(uint8_t bit) {
        sr = sr << 1 | bit;
        sinceFrame++;
        if (inFrame) {
            const uint8_t v = (uint8_t)(bit ^ (inv ? 1 : 0));
            uint8_t& by = frame[frameBits >> 3];
            by = (uint8_t)(by << 1 | v);
            if (++frameBits == meteor::kCodedBytes * 8) { inFrame = false; gotFrame(); sinceFrame = 0; }
            return;
        }
        const int e = __builtin_popcount(sr ^ meteor::kAsm), ei = __builtin_popcount(~sr ^ meteor::kAsm);
        if (frameLock) {
            if (sinceFrame < 32) return;
            if (std::min(e, ei) <= 10) misses = 0; else misses++;
            if (misses > 4) { frameLock = false; say("frame sync lost"); }
            else { if (std::min(e, ei) <= 10) inv = ei < e; startFrame(); tel.asmHits++; lastAsmSec = nowSec(); return; }
        }
        if (e <= 3 || ei <= 3) { inv = ei < e; startFrame(); frameLock = true; misses = 0; tel.asmHits++; lastAsmSec = nowSec(); }
    }

    void startFrame() { inFrame = true; frameBits = 0; std::memset(frame, 0, sizeof frame); }

    void gotFrame() {
        uint8_t v[meteor::kVcduBytes];
        const int e = meteor::decodeCadu(frame, v);
        if (e < 0) { tel.blocksBad++; return; }
        tel.blocksOk++; tel.rsCorrected += (uint64_t)e;
        lastGoodSec = nowSec();
        meteor::Vcdu vc;
        if (!meteor::parseVcdu(v, vc)) return;
        tel.vcdus++; tel.scid = vc.scid; tel.vcid = vc.vcid; tel.vcduCounter = vc.counter;
        tel.vcidFrames[vc.vcid & 63]++;
        meteor::PacketDemux& d = demux[vc.vcid & 63];
        if (!d.onPacket) d.onPacket = [this](const meteor::Packet& p) { packet(p); };
        const uint64_t lost0 = d.dropped;
        d.push(vc);
        tel.packetsLost += d.dropped - lost0;
    }

    void packet(const meteor::Packet& p) {
        tel.packets++;
        if (p.apid == hrpt::kApid3a || p.apid == hrpt::kApid3b) {
            tel.apidPackets[p.apid == hrpt::kApid3a ? 0 : 1]++;
            hrpt::Line L;
            if (var == 1 && hrpt::decodeAvhrrPacket(p.data, L)) placeLine(L);
        } else tel.apidPackets[3]++;
    }

    // --- the picture: a line's place from its on-board time (six lines a second), so lost packets leave gaps ---
    void placeLine(const hrpt::Line& L) {
        int idx = 0;
        if (haveLine) {
            double d = (double)L.ms - (double)lastMs;
            if (d < -43200000.0) d += 86400000.0;
            const long n = std::lround(d / hrpt::kLineMs);
            if (n < 0) return;
            idx = n > 20000 ? lastIdx + 1 : lastIdx + (int)n;
        }
        haveLine = true; lastMs = L.ms; lastIdx = idx;
        tel.lines++; tel.onboardMs = L.ms;
        if (idx >= kHrptMaxLines) return;
        auto ln = std::make_shared<HrptLine>();
        ln->idx = idx; ln->ms = L.ms;
        ln->px.resize(L.px.size());
        for (size_t i = 0; i < L.px.size(); i++) ln->px[i] = (uint8_t)(L.px[i] >> 2);
        if ((int)img.line.size() <= idx) img.line.resize((size_t)idx + 1);
        img.line[(size_t)idx] = ln;
        img.lines = std::max(img.lines, idx + 1);
        imgDirty = true;
    }

    // --- supervision: lost lock, automatic variant ---
    void supervise() {
        const double now = nowSec();
        if (!bpsk && searchLock && now - std::max(lastGoodSec, lastAsmSec) > 3) {
            searchLock = false; frameLock = false; pairs.clear(); tel.rotation = -1;
            say("no frames: searching again");
        }
        if (bpsk && noaaLock && now - lastGoodSec > 3 && now - lastAsmSec > 3) { noaaLock = false; nsr[0] = nsr[1] = 0; tel.rotation = -1; }
        const double since = now - std::max(lastGoodSec, varStartSec);
        if (coarseDone && !locked() && since > (bpsk ? 4 : 8) && now - lockLostSec > 2) {
            lockLostSec = now;
            if (varRunSetting == 0) { autoIdx = (autoIdx + 1) % (int)(sizeof kAutoSeq / sizeof kAutoSeq[0]); setVar(kAutoSeq[autoIdx]); }
            coarseHz = 0;
            initDemod();
            varStartSec = now;
        }
    }

    void report() {
        supervise();
        const double now = nowSec();
        tel.seq = ++seq;
        tel.variant = var; tel.varSetting = varRunSetting;
        tel.symbolRate = rs; tel.inputRate = rate; tel.timeSec = now;
        tel.cfoHz = coarseHz + omega * rs / (2 * kPi);
        tel.clockPpm = (sps / spsEst - 1) * 1e6;
        tel.carrierLock = std::clamp(lockAvg, 0.f, 1.f);
        tel.snrDb = (float)(10 * std::log10(1.0 / std::max(1e-4f, varAvg)));
        tel.levelDb = nPower ? (float)(10 * std::log10(std::max(1e-20, power / (double)nPower))) : -200.f;
        power = 0; nPower = 0;
        tel.frameLock = bpsk ? noaaLock : frameLock;
        const bool good = now - lastGoodSec < 2;
        tel.state = good ? 3 : tel.frameLock ? 2 : coarseDone && tel.carrierLock > 0.5f ? 1 : 0;
        tel.dataValid = good;
        tel.constellation = constel;
        if (imgDirty) { tel.image = std::make_shared<const HrptImage>(img); tel.imageSeq++; imgDirty = false; }
        std::lock_guard<std::mutex> lk(mu);
        pub = tel;
    }

    void input(cf32 x) {
        const cd v = cd(x.real(), x.imag()) * rot;
        rot *= step;
        if ((nIn & 1023) == 0) rot /= std::abs(rot);
        nIn++;
        cf32 y;
        if (lpf.push(cf32((float)v.real(), (float)v.imag()), y)) decimated(y);
    }
};

HrptReceiver::HrptReceiver() : p_(std::make_unique<Impl>()) {}
HrptReceiver::~HrptReceiver() = default;

void HrptReceiver::configure(double r) { std::lock_guard<std::mutex> lk(p_->mu); p_->rateSet = r; p_->resetReq = true; }
void HrptReceiver::setSignalOffset(double hz) { std::lock_guard<std::mutex> lk(p_->mu); if (hz != p_->offsetSet) { p_->offsetSet = hz; p_->resetReq = true; } }
void HrptReceiver::setVariant(int v) { v = std::clamp(v, 0, 3); if (p_->varSet.exchange(v) != v) p_->resetReq = true; }
int HrptReceiver::variant() const { return p_->varSet.load(); }
bool HrptReceiver::ready() const { std::lock_guard<std::mutex> lk(p_->mu); return p_->rateSet >= hrptTuning().minSampleRate - 1; }
void HrptReceiver::reset() { p_->resetReq = true; }
void HrptReceiver::clearImage() {
    { std::lock_guard<std::mutex> lk(p_->mu); p_->imgLoad = false; p_->imgIn.reset(); p_->pub.image.reset(); p_->pub.imageSeq++; p_->pub.seq++; }
    p_->clearReq = true;
}
void HrptReceiver::clearResults() { clearImage(); }
std::string HrptReceiver::saveResults() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return results::pack(p_->pub.image);
}
bool HrptReceiver::loadResults(const std::string& blob) {
    std::shared_ptr<const HrptImage> im;
    if (!results::unpack(blob, im)) return false;
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->pub.image = im; p_->pub.imageSeq++; p_->pub.seq++;
    p_->imgIn = std::move(im); p_->imgLoad = p_->imgIn != nullptr;
    return true;
}

void HrptReceiver::feed(const cf32* x, size_t n) {
    Impl& m = *p_;
    if (m.resetReq.exchange(false)) m.resetState();
    if (m.clearReq.exchange(false)) m.newImage();
    {
        std::lock_guard<std::mutex> lk(m.mu);
        if (m.imgLoad) { m.img = *m.imgIn; m.imgId = std::max(m.imgId, m.img.id); m.imgIn.reset(); m.imgLoad = false; m.imgDirty = true; }
    }
    if (m.rate < hrptTuning().minSampleRate - 1 || n == 0) {
        if (m.rate > 0) { m.nIn += (int64_t)n; if ((double)m.nIn >= m.nextReport) { m.nextReport += kPubSec * m.rate; m.report(); } }
        return;
    }
    for (size_t i = 0; i < n; i++) {
        float re = x[i].real(), im = x[i].imag();
        if (!std::isfinite(re) || !std::isfinite(im)) re = im = 0;
        m.power += (double)re * re + (double)im * im;
        m.input(cf32(re, im));
        if ((double)m.nIn >= m.nextReport) { m.nextReport += kPubSec * m.rate; m.report(); }
    }
    m.nPower += (int64_t)n;
}

bool HrptReceiver::telemetry(HrptTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->pub.seq <= lastSeq) return false;
    out = p_->pub;
    return true;
}

void HrptReceiver::setLogCallback(std::function<void(const std::string&)> cb) { std::lock_guard<std::mutex> lk(p_->mu); p_->log = std::move(cb); }

ModeTuning hrptTuning() {
    ModeTuning t;
    t.stdMode = 66; t.id = "hrpt"; t.name = "HRPT / AHRPT";
    t.minMhz = 1690; t.maxMhz = 1710; t.defMhz = 1701.3;
    t.sampleRate = 6e6; t.basebandHz = 0; t.bandwidthMhz = 4.5;
    t.minSampleRate = 4e6;
    t.tuneOffsetHz = 0;
    return t;
}

const HrptPreset* hrptPresets(int& count) {
    static const HrptPreset p[] = {
        {"1701.300 MetOp-B / MetOp-C AHRPT", 1701.3, 1},
        {"1701.400 FY-3 AHRPT (check the satellite)", 1701.4, 2},
        {"1698.000 NOAA-19 HRPT (retired)", 1698.0, 3},
        {"1702.500 NOAA-15 HRPT (retired)", 1702.5, 3},
        {"1707.000 NOAA-18 HRPT (retired)", 1707.0, 3}};
    count = (int)(sizeof p / sizeof p[0]);
    return p;
}

} // namespace dect2
