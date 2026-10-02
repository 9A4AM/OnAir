#include "dect2/atsc_rx.h"
#include "dect2/atsc.h"
#include "dect2/atsc_gen.h"   // rrcValue
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#ifdef __APPLE__
#include <Accelerate/Accelerate.h>
#endif
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace dect2 {
namespace atsc {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kPilotHz = -(3.0e6 - 0.309440559e6);   // pilot relative to the centre of the channel
constexpr int kSegSamples = 2 * kSegSyms;               // 1664 samples at 2 per symbol
constexpr int kNF = 24;                                 // equaliser taps after the cursor (future samples)
constexpr int kNP = 103;                                // taps before the cursor
constexpr int kL = kNF + kNP + 1;                       // 128
constexpr int kInterpJ = 8;                             // symbol interpolator half length
constexpr int kInterpPh = 64;

inline cf32 expj(double a) { return cf32((float)std::cos(a), (float)std::sin(a)); }

// Solves (A + lambda I) w = b for a symmetric positive definite A (dimension n, row-major), in place
bool solveSpd(std::vector<double>& A, std::vector<double>& b, int n) {
    for (int j = 0; j < n; j++) {
        double d = A[j * n + j];
        for (int k = 0; k < j; k++) d -= A[j * n + k] * A[j * n + k];
        if (d <= 1e-12) return false;
        d = std::sqrt(d);
        A[j * n + j] = d;
        for (int i = j + 1; i < n; i++) {
            double s = A[i * n + j];
            for (int k = 0; k < j; k++) s -= A[i * n + k] * A[j * n + k];
            A[i * n + j] = s / d;
        }
    }
    for (int i = 0; i < n; i++) { double s = b[i]; for (int k = 0; k < i; k++) s -= A[i * n + k] * b[k]; b[i] = s / A[i * n + i]; }
    for (int i = n - 1; i >= 0; i--) { double s = b[i]; for (int k = i + 1; k < n; k++) s -= A[k * n + i] * b[k]; b[i] = s / A[i * n + i]; }
    return true;
}
}

struct Core {
    // ---- configuration
    double fin = 0, fs2 = 2.0 * kSymbolRate;
    bool ok = false;
    // ---- stage A: matched filter + resampler (complex in -> 2 samples/symbol out)
    int J = 24;
    int NPH = 128;
    std::vector<float> taps;
    std::vector<float> inRe, inIm;   // input samples, split
    uint64_t inBase = 0;
    double pos = 0, step = 0;
    // ---- stage B: pilot loop
    double theta = 0, omega = 0, omega0 = 0;
    cf32 ph{1, 0}, rot{1, 0}, pf{0, 0};
    double pfA = 0, pfAt = 0;
    bool tracking = false;
    int goodRun = 0, badRun = 0;
    int pllCnt = 0;
    double dc = 0, pw = 0;
    bool dcInit = false;
    float pilotRel = 0;
    bool pilotLock = false;
    int pilotGood = 0;
    // ---- stage C: symbol clock
    std::vector<float> d;           // 2 samples/symbol, normalised
    uint64_t dBase = 0;
    uint64_t accIdx = 0;            // next sample for the segment sync correlation
    std::vector<float> acc;         // correlation per sample position modulo 1664
    uint64_t lastSearch = 0;
    int lastPeak = -1, peakRun = 0;
    bool seg = false;
    double tau = 0, rate = 2.0, rr = 0;
    double tauSync[4] = {};
    int segPos = 0;
    std::vector<float> segSyms;
    int lowQ = 0;
    double syncQ = 0;
    std::vector<float> interp;      // [kInterpPh][2*kInterpJ]
    // ---- stage D: fields
    std::vector<float> raw;         // unequalised symbols since lock
    uint64_t rawBase = 0;           // absolute symbol index of raw[0]
    uint64_t symAbs = 0;
    uint64_t segAbs = 0;            // completed segments since the symbol clock locked
    bool fieldLock = false;
    int64_t fieldStartSeg = -1;     // absolute segment index of the next field sync
    int fsMisses = 0;
    int parity = 0;                 // 1/2: last detected polarity
    int64_t lastFsSeg = -1;
    bool fsSeen = false;
    FieldDecoder dec;
    std::function<void(const uint8_t*, size_t, double)> cb;
    // ---- statistics
    AtscTelemetry t;
    double snrAvg = 0, dataSnrAvg = 0;
    double segErr = 0;
    std::vector<float> lastTaps;
    int tsFlow = 0;
    std::chrono::steady_clock::time_point lastPub;
    std::vector<float> levelsOut;
    // equaliser working state
    std::vector<float> w;
    float bias = 0;

    void configure(double rateHz) {
        fin = rateHz;
        ok = fin >= 6.5e6;
        if (!ok) return;
        step = fin / fs2;
        J = (int)std::ceil(2.4e-6 * fin) + 2;
        taps.assign((size_t)NPH * 2 * J, 0.f);
        const double Rb = kSymbolRate / 2.0;
        for (int p = 0; p < NPH; p++) {
            const double mu = (double)p / NPH;
            for (int j = -J + 1; j <= J; j++) {
                const double x = mu - j;
                const double win = std::cos(0.5 * kPi * (x) / (J + 0.5));
                taps[(size_t)p * 2 * J + (j + J - 1)] = (float)(Rb / fin * rrcValue(x / fin) * (getenv("ATSC_NOWIN") ? 1.0 : win * win));
            }
        }
        interp.assign((size_t)kInterpPh * 2 * kInterpJ, 0.f);
        for (int p = 0; p < kInterpPh; p++) {
            const double mu = (double)p / kInterpPh;
            double sum = 0;
            for (int j = -kInterpJ + 1; j <= kInterpJ; j++) {
                const double x = j - mu;
                const double s = x == 0 ? 1.0 : std::sin(kPi * x * 0.92) / (kPi * x * 0.92);
                const double wv = 0.5 + 0.5 * std::cos(kPi * x / (kInterpJ + 0.5));
                const float v = (float)(s * wv);
                interp[(size_t)p * 2 * kInterpJ + (j + kInterpJ - 1)] = v;
                sum += v;
            }
            for (int j = 0; j < 2 * kInterpJ; j++) interp[(size_t)p * 2 * kInterpJ + j] /= (float)sum;
        }
        reset();
    }

    void reset() {
        inRe.clear(); inIm.clear(); inBase = 0; pos = J;
        omega0 = 2.0 * kPi * kPilotHz / fs2; omega = omega0; theta = 0; ph = cf32(1, 0); rot = expj(-omega); pf = cf32(0, 0);
        pfA = 1.0 - std::exp(-1.0 / (fs2 * 5e-6)); pfAt = 1.0 - std::exp(-1.0 / (fs2 * 40e-6)); tracking = false; goodRun = badRun = 0;
        pllCnt = 0; dcInit = false; dc = 0; pw = 0; pilotLock = false; pilotGood = 0; pilotRel = 0;
        d.clear(); dBase = 0; accIdx = 0; acc.assign(kSegSamples, 0.f); lastSearch = 0; lastPeak = -1; peakRun = 0;
        seg = false; tau = 0; rate = 2.0; rr = 0; segPos = 0; segSyms.clear(); lowQ = 0; syncQ = 0;
        raw.clear(); rawBase = 0; symAbs = 0; segAbs = 0;
        fieldLock = false; fieldStartSeg = -1; fsMisses = 0; lastFsSeg = -1; fsSeen = false; pendingStart = -1;
        dec.reset();
        w.assign(kL, 0.f); bias = 0;
        snrAvg = 0; dataSnrAvg = 0; segErr = 0; tsFlow = 0;
        AtscTelemetry keep;
        keep.seq = t.seq;
        t = keep;
        t.valid = true;
    }

    // ---------------------------------------------------------------- stage A + B
    double tPfb = 0, tCd = 0, tDec = 0;
    static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    void processInput(std::vector<cf32>& in, size_t n_) {
        (void)n_;
        const double tA = now();
        for (const cf32& v : in) { inRe.push_back(v.real()); inIm.push_back(v.imag()); }
        std::vector<float> dchunk;
        dchunk.reserve(inRe.size() * 3);
        const int T = 2 * J;
        while (pos + J + 2 < (double)(inBase + inRe.size())) {
            int64_t i0 = (int64_t)std::floor(pos);
            int phi = (int)((pos - (double)i0) * NPH + 0.5);
            if (phi >= NPH) { phi = 0; i0++; }
            const float* tp = &taps[(size_t)phi * T];
            const size_t b0 = (size_t)(i0 - (int64_t)inBase - J + 1);
            const float* pr = &inRe[b0];
            const float* pi_ = &inIm[b0];
            float re = 0, im = 0;
#if defined(__ARM_NEON)
            {
                float32x4_t ar = vdupq_n_f32(0), ai = vdupq_n_f32(0);
                int j = 0;
                for (; j + 4 <= T; j += 4) {
                    const float32x4_t t4 = vld1q_f32(tp + j);
                    ar = vfmaq_f32(ar, vld1q_f32(pr + j), t4);
                    ai = vfmaq_f32(ai, vld1q_f32(pi_ + j), t4);
                }
                re = vaddvq_f32(ar); im = vaddvq_f32(ai);
                for (; j < T; j++) { re += pr[j] * tp[j]; im += pi_[j] * tp[j]; }
            }
#else
            for (int j = 0; j < T; j++) { re += pr[j] * tp[j]; im += pi_[j] * tp[j]; }
#endif
            pos += step;
            // pilot loop
            const cf32 z = cf32(re, im) * ph;
            ph *= rot;
            pf += (float)(tracking ? pfAt : pfA) * (z - pf);
            dchunk.push_back(z.real());
            if (++pllCnt >= 4) {
                pllCnt = 0;
                float e = std::atan2(pf.imag(), pf.real());
                e = std::max(-1.2f, std::min(1.2f, e));
                const double alpha = getenv("ATSC_NOPLL") ? 1e-6 : (tracking ? 0.0035 : 0.035), beta = alpha * alpha / 4.0 / (tracking ? 1.0 : 4.0);
                ph *= expj(-alpha * e);
                omega += beta * e;
                rot = expj(-omega);
                if (dbg && (++dbgCnt % 500000) == 0) fprintf(stderr, "[atsc] pll e=%.3f cfo=%.0f Hz |pf|=%.3f re=%.3f im=%.3f pw=%.3f dc=%.3f\n", e, (omega - omega0) * fs2 / (2 * kPi), std::abs(pf), pf.real(), pf.imag(), pw, dc);
                if (++renorm >= 256) { renorm = 0; ph /= std::abs(ph); }
            }
        }
        const int64_t keepFrom = (int64_t)std::floor(pos) - J - 1;
        if (keepFrom > (int64_t)inBase) {
            const size_t drop = std::min((size_t)(keepFrom - (int64_t)inBase), inRe.size());
            inRe.erase(inRe.begin(), inRe.begin() + drop);
            inIm.erase(inIm.begin(), inIm.begin() + drop);
            inBase += drop;
        }
        tPfb += now() - tA;
        if (dchunk.empty()) return;
        // DC (pilot) and gain
        if (!dcInit) {
            double m = 0, p = 0;
            for (float v : dchunk) m += v;
            m /= dchunk.size();
            for (float v : dchunk) p += (v - m) * (v - m);
            p /= dchunk.size();
            dc = m; pw = p; dcInit = true;
        }
        const double a = 4e-7;
        for (float& v : dchunk) {
            dc += a * (v - dc);
            const double c = v - dc;
            pw += a * (c * c - pw);
            v = (float)(c * (4.58 / std::sqrt(std::max(pw, 1e-12))));
        }
        // pilot lock: the filtered carrier sits on the real axis and is strong enough; once it has for a while the loop is narrowed
        {
            const double lvl = std::sqrt(std::max(pw, 1e-12));
            pilotRel = (float)(pf.real() / lvl);
            const bool good = std::fabs(pf.imag()) < (tracking ? 0.35 : 0.5) * std::fabs(pf.real()) && pf.real() > 0.06 * lvl;
            const int nUpd = (int)(dchunk.size() / 4);
            if (good) { goodRun += nUpd; badRun = 0; } else { badRun += nUpd; goodRun = 0; }
            if (!tracking && goodRun > 60000) { tracking = true; }          // ~11 ms of a steady pilot
            if (tracking && badRun > 120000) { tracking = false; }
            pilotLock = tracking && goodRun > 20000;
        }
        d.insert(d.end(), dchunk.begin(), dchunk.end());
        const double tB = now();
        stageC();
        tCd += now() - tB;
    }
    int renorm = 0;
    int64_t fastSegs = getenv("ATSC_FAST") ? atoi(getenv("ATSC_FAST")) : 300;
    double kpT = getenv("ATSC_KP") ? atof(getenv("ATSC_KP")) : 0.003, kiT = getenv("ATSC_KI") ? atof(getenv("ATSC_KI")) : 1e-5;
    bool dbg = getenv("ATSC_DBG") != nullptr;
    long dbgCnt = 0;

    // ---------------------------------------------------------------- stage C: symbol clock
    float interpAt(double t) const {
        const int64_t i0 = (int64_t)std::floor(t);
        const int ph = std::min(kInterpPh - 1, (int)((t - (double)i0) * kInterpPh));
        const float* tp = &interp[(size_t)ph * 2 * kInterpJ];
        const float* base = &d[(size_t)(i0 - (int64_t)dBase - kInterpJ + 1)];
        float s = 0;
        for (int j = 0; j < 2 * kInterpJ; j++) s += base[j] * tp[j];
        return s;
    }
    uint64_t dEnd() const { return dBase + d.size(); }

    void stageC() {
        if (!seg) {
            // correlate the sync pattern at every sample position, averaged over segments
            while (accIdx + 8 < dEnd()) {
                const size_t k = (size_t)(accIdx - dBase);
                const float c = d[k] - d[k + 2] - d[k + 4] + d[k + 6];
                float& A = acc[accIdx % kSegSamples];
                A += (1.f / 12.f) * (c - A);
                accIdx++;
                if (accIdx - lastSearch >= (uint64_t)kSegSamples) {
                    lastSearch = accIdx;
                    int m = 0;
                    for (int i = 1; i < kSegSamples; i++) if (acc[i] > acc[m]) m = i;
                    double rms = 0; int cnt = 0;
                    for (int i = 0; i < kSegSamples; i++) {
                        int dd = std::abs(i - m); dd = std::min(dd, kSegSamples - dd);
                        if (dd > 8) { rms += acc[i] * acc[i]; cnt++; }
                    }
                    rms = std::sqrt(rms / std::max(1, cnt));
                    const bool strong = acc[m] > 8.0 && acc[m] > 5.0 * rms;
                    if (strong && lastPeak >= 0 && std::min(std::abs(m - lastPeak), kSegSamples - std::abs(m - lastPeak)) <= 1) peakRun++;
                    else peakRun = strong ? 1 : 0;
                    lastPeak = strong ? m : -1;
                    if (peakRun >= 6) {
                        const float a0 = acc[(m + kSegSamples - 1) % kSegSamples], a1 = acc[m], a2 = acc[(m + 1) % kSegSamples];
                        const double den = a0 - 2 * a1 + a2;
                        const double delta = den != 0 ? 0.5 * (a0 - a2) / den : 0.0;
                        // most recent occurrence of that position, safely inside the buffered samples
                        uint64_t idx = accIdx - 40;
                        idx -= (idx % kSegSamples + kSegSamples - m) % kSegSamples;
                        tau = (double)idx + delta;
                        rate = 2.0; rr = 0; segPos = 0; segSyms.clear(); lowQ = 0;
                        seg = true; segAbs = 0; symAbs = 0; raw.clear(); rawBase = 0;
                        fieldLock = false; fieldStartSeg = -1; fsMisses = 0; lastFsSeg = -1; pendingStart = -1;
                        dec.reset();
                        break;
                    }
                }
            }
            trimD(accIdx > 12000 ? accIdx - 12000 : 0);
        }
        if (seg) {
            while (tau + kInterpJ + 2 < (double)dEnd() && tau - kInterpJ - 1 >= (double)dBase) {
                if (segPos < 4) tauSync[segPos] = tau;
                segSyms.push_back(interpAt(tau));
                segPos++;
                symAbs++;
                tau += rate;
                if (segPos == kSegSyms) { onSegment(); segPos = 0; segSyms.clear(); if (!seg) return; }
            }
            trimD((uint64_t)std::max(0.0, std::floor(tau) - 16));
            if (tau - kInterpJ - 1 < (double)dBase) { seg = false; accIdx = dEnd(); }   // fell behind the buffer: reacquire
        }
    }

    void trimD(uint64_t keepFrom) {
        if (keepFrom > dBase + 8192) {
            const size_t drop = (size_t)(keepFrom - dBase);
            d.erase(d.begin(), d.begin() + std::min(drop, d.size()));
            dBase += drop;
        }
    }

    // ---------------------------------------------------------------- segment / field level
    void onSegment() {
        // timing error from the four sync symbols: early-late of the correlation with +,-,-,+
        static const float pat[4] = {1, -1, -1, 1};
        double e = 0, q = 0;
        for (int j = 0; j < 4; j++) {
            if (tauSync[j] - 1.0 < (double)dBase) continue;
            e += pat[j] * (interpAt(tauSync[j] + 0.5) - interpAt(tauSync[j] - 0.5));
            q += pat[j] * segSyms[j];
        }
        q /= 20.0;
        syncQ += 0.1 * (q - syncQ);
        const double eps = e / -16.8 * -1.0;   // estimated lateness of the sampling instant, in samples (late = positive)
        // late sampling gives a negative e: move the next instants earlier
        // wide loop while the clock offset is being found, then a narrow one: the data symbols next to the sync pattern make each
        // single measurement noisy and a wide loop would turn that into timing jitter
        const bool fastLoop = (int64_t)segAbs < fastSegs;
        const double kp = fastLoop ? 0.12 : kpT, ki = fastLoop ? 0.01 : kiT;
        tau += kp * (e / 16.8);
        rr += ki * (e / 16.8) / kSegSyms;
        rr = std::max(-0.01, std::min(0.01, rr));
        rate = 2.0 + rr;
        (void)eps;
        if (q < 0.3) { if (++lowQ > 12) { seg = false; accIdx = dEnd(); lastSearch = accIdx; std::fill(acc.begin(), acc.end(), 0.f); lastPeak = -1; peakRun = 0; fieldLock = false; dec.reset(); t.lockLosses++; return; } }
        else lowQ = 0;
        raw.insert(raw.end(), segSyms.begin(), segSyms.end());
        segAbs++;
        checkFieldSync();
        publish(false);
    }

    // field sync match on raw symbols of the segment that just completed
    bool fieldSyncAt(const float* s, int* par) {
        double m = 0;
        for (int i = 0; i < kSegSyms; i++) m += s[i];
        m /= kSegSyms;
        int err = 0;
        for (int j = 0; j < 511 && err < 90; j++) err += ((s[4 + j] - m) >= 0) != (kPn511[j] != 0);
        if (err >= 90) return false;
        int e2 = 0;
        for (int j = 0; j < 63; j++) e2 += ((s[4 + 511 + 63 + j] - m) >= 0) != (kPn63[j] != 0);
        *par = e2 < 32 ? 1 : 2;
        return true;
    }

    void checkFieldSync() {
        const size_t n = raw.size();
        const float* last = &raw[n - kSegSyms];
        const int64_t thisSeg = (int64_t)segAbs - 1;   // the segment that just completed
        if (!fieldLock) {
            int par = 0;
            if (fieldSyncAt(last, &par)) {
                if (lastFsSeg >= 0 && thisSeg - lastFsSeg == kFieldSegs) {
                    // two field syncs one field apart: the field that began at lastFsSeg is complete
                    fieldLock = true; fsMisses = 0;
                    pendingStart = lastFsSeg; pendingParity = lastFsParity;
                    nextFs = thisSeg; nextParity = par; parity = par;
                    finishFieldAt(thisSeg);
                } else { lastFsSeg = thisSeg; lastFsParity = par; }
            } else if (lastFsSeg >= 0 && thisSeg - lastFsSeg > 2 * kFieldSegs) lastFsSeg = -1;
        } else if (thisSeg == nextFs) {
            int par = 0;
            if (fieldSyncAt(last, &par)) { fsMisses = 0; nextParity = par; parity = par; }
            else {
                t.fieldSyncMisses++;
                nextParity = 3 - pendingParity;   // the polarity alternates every field
                if (++fsMisses > 3) { fieldLock = false; lastFsSeg = -1; pendingStart = -1; dec.reset(); t.lockLosses++; return; }
            }
            finishFieldAt(thisSeg);
        }
        trimRaw();
    }
    int64_t pendingStart = -1, nextFs = -1, lastFsParity = 0;
    int pendingParity = 1, nextParity = 1;

    void finishFieldAt(int64_t thisSeg) {
        decodeField(pendingStart, pendingParity);
        pendingStart = thisSeg;
        pendingParity = nextParity;
        nextFs = thisSeg + kFieldSegs;
    }

    void trimRaw() {
        // keep the previous field's tail for the equaliser history plus everything not yet decoded
        const int64_t keepSeg = (pendingStart >= 0 ? pendingStart : (int64_t)segAbs) - 2;
        const int64_t dropSegs = keepSeg - (int64_t)(rawBase / kSegSyms);
        if (dropSegs > 40) {
            raw.erase(raw.begin(), raw.begin() + (size_t)dropSegs * kSegSyms);
            rawBase += (uint64_t)dropSegs * kSegSyms;
        }
    }

    float rawAt(int64_t symIndex) const {   // absolute symbol index since lock; zero outside the buffer
        const int64_t k = symIndex - (int64_t)rawBase;
        if (k < 0 || k >= (int64_t)raw.size()) return 0.f;
        return raw[(size_t)k];
    }

    void decodeField(int64_t startSeg, int fieldParity) {
        const double tD0 = now();
        struct Timer { double& acc; double t0; ~Timer() { acc += now() - t0; } } timer{tDec, tD0};
        const int64_t f0 = startSeg * kSegSyms;   // absolute index of the field sync's first symbol
        // known training symbols
        uint8_t prev12[12] = {};
        uint8_t fsSym[kSegSyms];
        const bool field2 = fieldParity == 2;
        fieldSyncSymbols(field2, prev12, fsSym);
        struct Tr { int64_t t; float d; };
        std::vector<Tr> tr;
        tr.reserve(728 + 312 * 4);
        for (int i = 0; i < 728; i++) tr.push_back({f0 + i, levelOf(fsSym[i])});
        static const float sy[4] = {5, -5, -5, 5};
        for (int s = 1; s < kFieldSegs; s++) for (int j = 0; j < 4; j++) tr.push_back({f0 + (int64_t)s * kSegSyms + j, sy[j]});
        // normal equations
        const int n = kL + 1;
        std::vector<double> A((size_t)n * n, 0.0), b(n, 0.0);
        std::vector<float> x(n);
        for (auto& r : tr) {
            for (int i = 0; i < kL; i++) x[i] = rawAt(r.t + kNF - i);
            x[kL] = 1.f;
            for (int i = 0; i < n; i++) {
                const double xi = x[i];
                double* row = &A[(size_t)i * n];
                for (int j = 0; j <= i; j++) row[j] += xi * x[j];
                b[i] += xi * r.d;
            }
        }
        double tr0 = 0;
        for (int i = 0; i < n; i++) tr0 += A[(size_t)i * n + i];
        const double lambda = (getenv("ATSC_LAMBDA") ? atof(getenv("ATSC_LAMBDA")) : 2e-5) * tr0 / n;
        for (int i = 0; i < n; i++) A[(size_t)i * n + i] += lambda;
        std::vector<double> sol = b;
        const bool ok2 = solveSpd(A, sol, n);
        if (ok2) { for (int i = 0; i < kL; i++) w[i] = (float)sol[i]; bias = (float)sol[kL]; }
        t.eqTrained = ok2;
        // equalise the field's data region (and the sync segment for the telemetry)
        std::vector<float> lv((size_t)kFieldSyms, 0.f);
        double mse = 0; int nm = 0;
        auto eqAt = [&](int64_t tt) {
            float y = bias;
            const int64_t k0 = tt + kNF - (int64_t)rawBase;
            if (k0 - (kL - 1) >= 0 && k0 < (int64_t)raw.size()) {
                const float* r = &raw[(size_t)(k0 - (kL - 1))];
                // taps are stored for decreasing sample index
                for (int i = 0; i < kL; i++) y += w[i] * r[kL - 1 - i];
            } else for (int i = 0; i < kL; i++) y += w[i] * rawAt(tt + kNF - i);
            return y;
        };
        for (auto& r : tr) { const double e = eqAt(r.t) - r.d; mse += e * e; nm++; }
        mse /= std::max(1, nm);
        for (int i = kSegSyms; i < kFieldSyms; i++) lv[i] = eqAt(f0 + i);
        const double snr = 10.0 * std::log10(25.0 / std::max(mse, 1e-6));
        snrAvg = snrAvg == 0 ? snr : snrAvg + 0.3 * (snr - snrAvg);
        // data symbols: distance to the nearest level
        {
            double dd = 0; int cnt = 0;
            levelsOut.clear();
            for (int i = kSegSyms; i < kFieldSyms; i++) {
                if (i % kSegSyms < 4) continue;
                const float v = lv[i];
                const float nearest = std::max(-7.f, std::min(7.f, 2.f * std::floor(v * 0.5f) + 1.f));
                dd += (v - nearest) * (v - nearest); cnt++;
                if ((i * 2654435761u >> 7) % 128 == 0) levelsOut.push_back(v);
            }
            dd /= std::max(1, cnt);
            const double dsnr = 10.0 * std::log10(21.0 / std::max(dd, 1e-6));
            dataSnrAvg = dataSnrAvg == 0 ? dsnr : dataSnrAvg + 0.3 * (dsnr - dataSnrAvg);
        }
        if (!ok2 || snr < 9.0) {   // the equaliser did not find a usable channel: do not feed the decoder garbage
            dec.reset();
            tsFlow = 0;
            t.eqTrained = false;
            lastTaps.assign(w.begin(), w.end());
            publish(true);
            return;
        }
        FieldStats st;
        std::vector<uint8_t> out((size_t)kDataSegs * kTsBytes);
        const int np = dec.decode(lv.data(), out.data(), &st);
        t.fields++;
        if (dbg) fprintf(stderr, "[atsc] field %llu start seg %lld parity %d: snr %.1f data %.1f  RS clean %d corr %d fail %d np %d\n", (unsigned long long)t.fields, (long long)startSeg, fieldParity, snr, dataSnrAvg, st.rsClean, st.rsCorrected, st.rsFailed, np);
        t.rsClean += st.rsClean; t.rsCorrected += st.rsCorrected; t.rsFailed += st.rsFailed;
        t.segments += st.segments;
        if (st.segments) { const double r = (double)st.rsFailed / st.segments; segErr = segErr + 0.3 * (r - segErr); }
        if (np > 0 && cb) {
            const double secs = (double)np * 0.0000774 * 1.0;   // one packet per data segment: 832 symbols / 10.76 MHz
            cb(out.data(), (size_t)np, secs);
            tsFlow = 6;
        } else if (tsFlow > 0) tsFlow--;
        lastTaps.assign(w.begin(), w.end());
        publish(true);
    }

    void publish(bool force) {
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - lastPub < std::chrono::milliseconds(60)) return;
        lastPub = now;
        std::lock_guard<std::mutex> lk(mu);
        AtscTelemetry nt = t;
        nt.valid = true;
        nt.pilot = pilotLock;
        nt.segSync = seg;
        nt.fieldSync = fieldLock;
        nt.tsOk = tsFlow > 0;
        nt.fieldParity = parity;
        nt.cfoHz = (omega - omega0) * fs2 / (2.0 * kPi);
        nt.sroPpm = rr / 2.0 * 1e6;
        nt.snrDb = snrAvg;
        nt.dataSnrDb = dataSnrAvg;
        nt.pilotDb = pilotRel > 0 ? 20.0 * std::log10(pilotRel) : -99;
        nt.syncQuality = syncQ;
        nt.segErrorRate = segErr;
        nt.eqTaps = lastTaps;
        nt.eqCursor = kL - 1 - kNF;
        nt.levels = levelsOut;
        nt.seq = pub.seq + 1;
        pub = std::move(nt);
        t.seq = pub.seq;
    }
    std::mutex mu;
    AtscTelemetry pub;
};

} // namespace atsc

struct AtscReceiver::Impl {
    atsc::Core c;
};

AtscReceiver::AtscReceiver() : p_(new Impl) {}
AtscReceiver::~AtscReceiver() = default;
void AtscReceiver::configure(double inputRateHz) { p_->c.configure(inputRateHz); }
void AtscReceiver::reset() { if (p_->c.ok) p_->c.reset(); }
bool AtscReceiver::rateOk() const { return p_->c.ok; }
void AtscReceiver::setPacketCallback(std::function<void(const uint8_t*, size_t, double)> cb) { p_->c.cb = std::move(cb); }
void AtscReceiver::feed(const cf32* x, size_t n) {
    if (!p_->c.ok || !n) return;
    std::vector<cf32> v(x, x + n);
    p_->c.processInput(v, n);
    static long cnt = 0;
    if (getenv("ATSC_PROF") && ++cnt % 380 == 0) fprintf(stderr, "[atsc] time: pfb+pll %.2f s, symbol clock %.2f s, field decode %.2f s\n", p_->c.tPfb, p_->c.tCd, p_->c.tDec);
}
bool AtscReceiver::telemetry(AtscTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->c.mu);
    if (p_->c.pub.seq <= lastSeq) return false;
    out = p_->c.pub;
    return true;
}
int AtscReceiver::detectLevel() const {
    const auto& c = p_->c;
    if (c.tsFlow > 0) return 4;
    if (c.fieldLock) return 3;
    if (c.seg) return 2;
    if (c.pilotLock) return 1;
    return 0;
}

} // namespace dect2
