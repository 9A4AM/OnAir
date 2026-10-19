// KAAL streaming voice receiver. Port of kaal_rx.py (batch) to a chunked pipeline; see kaal_rx.h for the overview.
#include "dect2/kaal/kaal_rx.h"
#include "dect2/dsp/fftutil.h"
#include "dect2/dsp/resampler.h"
#include "dect2/kaal/kaal_codec.h"
#include "dect2/kaal/kaal_fec.h"
#include "dect2/kaal/kaal_phy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <vector>

namespace dect2::kaal {
namespace {

using cd = std::complex<double>;
const double kPi = 3.14159265358979323846;
constexpr int kSweepLen = (int)(kSweepPeriodS * kFs);   // 24000 samples
constexpr size_t kAcqLen = 3 * (size_t)kFs;             // samples gathered before the static parameters are estimated
constexpr int kAlignFrames = 27;                        // 6 blocks at the largest offset
constexpr int kMaxPendingWeak = 3;

double pymod(double x, double m) { return x - m * std::floor(x / m); }

struct Layout {
    std::vector<int> pilotIdx, dataIdx;
    std::vector<cd> pre;
    cd pilot;
    Layout() {
        for (int g = 0; g < kNPilots; g++) pilotIdx.push_back(kPreambleLen + kPilotEvery * g + (kPilotEvery - 1));
        std::vector<char> isP(kFrameSymbols, 0);
        for (int p : pilotIdx) isP[p] = 1;
        for (int i = kPreambleLen; i < kFrameSymbols; i++) if (!isP[i]) dataIdx.push_back(i);
        for (const cf& c : preamble()) pre.emplace_back(c.real(), c.imag());
        const cf p = pilotSymbol();
        pilot = cd(p.real(), p.imag());
    }
};
const Layout& layout() { static const Layout l; return l; }

// ---------------------------------------------------------------- small numeric helpers
// np.unwrap: corrections accumulate over the series
std::vector<double> unwrapped(const std::vector<double>& in) {
    std::vector<double> out(in.size());
    double off = 0;
    for (size_t i = 0; i < in.size(); i++) {
        if (i > 0) {
            const double d = in[i] - in[i - 1];
            const double w = d - 2 * kPi * std::floor((d + kPi) / (2 * kPi));
            off += w - d;
        }
        out[i] = in[i] + off;
    }
    return out;
}
void polyfit1(const std::vector<double>& x, const std::vector<double>& y, double& slope, double& icpt) {
    const size_t n = x.size();
    double mx = 0, my = 0;
    for (size_t i = 0; i < n; i++) { mx += x[i]; my += y[i]; }
    mx /= n; my /= n;
    double sxx = 0, sxy = 0;
    for (size_t i = 0; i < n; i++) { sxx += (x[i] - mx) * (x[i] - mx); sxy += (x[i] - mx) * (y[i] - my); }
    slope = sxx > 0 ? sxy / sxx : 0;
    icpt = my - slope * mx;
}
double parabola(double a, double b, double c) {
    const double den = a - 2 * b + c;
    return den < 0 ? std::min(0.5, std::max(-0.5, 0.5 * (a - c) / den)) : 0.0;
}
double sincd(double u) { return std::fabs(u) < 1e-12 ? 1.0 : std::sin(kPi * u) / (kPi * u); }

// 16-tap windowed-sinc interpolation of buf (whose first element has absolute index base) at the float position t; zero outside.
cd interpAt(const std::vector<cd>& buf, int64_t base, double t) {
    const int64_t i0 = (int64_t)std::floor(t);
    const double f = t - (double)i0;
    if (i0 - 7 < base || i0 + 8 >= base + (int64_t)buf.size()) return cd(0, 0);
    cd acc(0, 0);
    for (int k = -7; k <= 8; k++) {
        const double u = k - f;
        const double h = sincd(u) * (0.5 + 0.5 * std::cos(kPi * u / 8.5));
        acc += buf[(size_t)(i0 + k - base)] * h;
    }
    return acc;
}

// ---------------------------------------------------------------- sweep
// Subtracts g * template from x[lo, hi) in place, g = least-squares complex gain. The template is the sweep at start s0 with clock error epsw.
// sp/dp (optional) accumulate the removed power and the residual power.
void fitSweepPeriod(cd* x, int64_t lo, int64_t hi, double s0, double epsw, double* sp, double* dp) {
    const double slope = (kSweepHighHz - kSweepLowHz) / kSweepPeriodS;
    const size_t n = (size_t)(hi - lo);
    std::vector<cd> t(n);
    cd num(0, 0);
    for (size_t i = 0; i < n; i++) {
        double tt = (((double)(lo + (int64_t)i) - s0) / (1.0 + epsw)) / kFs;
        tt = std::min(kSweepPeriodS, std::max(0.0, tt));
        const double ph = 2 * kPi * (kSweepLowHz * tt + 0.5 * slope * tt * tt);
        t[i] = cd(std::cos(ph), std::sin(ph));
        num += std::conj(t[i]) * x[i];
    }
    const cd g = num / (double)n;
    for (size_t i = 0; i < n; i++) {
        const cd e = g * t[i];
        if (sp) *sp += std::norm(e);
        x[i] -= e;
        if (dp) *dp += std::norm(x[i]);
    }
}

// Batch sweep removal with FFT correlation over the whole acquisition buffer (kaal_rx.remove_sweep). x is cleaned in place.
bool removeSweepBatch(std::vector<cd>& x, double& a0, double& b0, double& ratioDb) {
    const int64_t N = (int64_t)x.size(), L = kSweepLen;
    if (N < 2 * L) return false;
    const std::vector<cf> P = sweepTemplate((size_t)L, 0.0);
    int nfft = 1;
    while (nfft < N + L) nfft <<= 1;
    Fft fft(nfft);
    std::vector<cf32> A((size_t)nfft, cf32(0, 0)), B((size_t)nfft, cf32(0, 0));
    for (int64_t i = 0; i < N; i++) A[(size_t)i] = cf32((float)x[(size_t)i].real(), (float)x[(size_t)i].imag());
    for (int64_t i = 0; i < L; i++) B[(size_t)i] = cf32(P[(size_t)i].real(), P[(size_t)i].imag());
    fft.forward(A.data());
    fft.forward(B.data());
    for (int i = 0; i < nfft; i++) A[(size_t)i] *= std::conj(B[(size_t)i]);
    fft.inverse(A.data());
    const int64_t nm = N - L + 1;
    std::vector<double> mag((size_t)nm), fold((size_t)L, 0.0);
    for (int64_t i = 0; i < nm; i++) { mag[(size_t)i] = std::abs(A[(size_t)i]); fold[(size_t)(i % L)] += mag[(size_t)i]; }
    int64_t tau = 0;
    for (int64_t i = 1; i < L; i++) if (fold[(size_t)i] > fold[(size_t)tau]) tau = i;
    const double delta = parabola(fold[(size_t)((tau - 1 + L) % L)], fold[(size_t)tau], fold[(size_t)((tau + 1) % L)]);
    std::vector<double> ks, ps;
    for (int64_t k = -1;; k++) {
        const int64_t c0 = tau + L * k;
        if (c0 - 4 >= nm) break;
        if (c0 - 4 >= 0) {
            const int64_t hiW = std::min(c0 + 5, nm);
            if (hiW - (c0 - 4) == 9) {
                int j = 0;
                double mx = 0;
                for (int i = 0; i < 9; i++) if (mag[(size_t)(c0 - 4 + i)] > mx) { mx = mag[(size_t)(c0 - 4 + i)]; j = i; }
                if (mx > 0 && j > 0 && j < 8) {
                    const double d = parabola(mag[(size_t)(c0 - 4 + j - 1)], mag[(size_t)(c0 - 4 + j)], mag[(size_t)(c0 - 4 + j + 1)]);
                    ks.push_back((double)k);
                    ps.push_back((double)(c0 - 4 + j) + d);
                }
            }
        }
    }
    a0 = (double)tau + delta; b0 = (double)L;
    if (ks.size() >= 3) {
        double sl, ic;
        polyfit1(ks, ps, sl, ic);
        if (std::fabs(sl - (double)L) <= 0.02 * (double)L) { b0 = sl; a0 = ic; }
    }
    const double epsw = b0 / (double)L - 1.0;
    double sp = 0, dp = 0;
    int64_t covered = 0;
    for (int64_t k = -2;; k++) {
        const double s0 = a0 + b0 * (double)k;
        if (s0 >= (double)N) break;
        const int64_t lo = std::max<int64_t>((int64_t)std::ceil(s0), 0), hi = std::min<int64_t>((int64_t)std::ceil(s0 + b0), N);
        if (hi - lo >= 2000) { fitSweepPeriod(x.data() + lo, lo, hi, s0, epsw, &sp, &dp); covered += hi - lo; }
    }
    ratioDb = (covered > 0 && dp > 0) ? 10 * std::log10(sp / dp) : 0.0;
    return covered > 0;
}

// ---------------------------------------------------------------- timing, sync (batch, acquisition)
void energyPhases(const cd* seg, size_t n, double* E) {
    for (int p = 0; p < kSps; p++) {
        double s = 0;
        size_t cnt = 0;
        for (size_t i = (size_t)p; i + (size_t)kSps < n + (size_t)p; i += kSps) { s += std::norm(seg[i]); cnt++; }
        E[p] = cnt ? s / (double)cnt : 0;
    }
}
void findTimingConst(const std::vector<cd>& mf, double& tau0) {
    const size_t len = std::min(mf.size(), (size_t)(6 * kFs));
    const size_t n = len / kSps * kSps;
    double E[kSps];
    energyPhases(mf.data(), n, E);
    int p = 0;
    for (int i = 1; i < kSps; i++) if (E[i] > E[p]) p = i;
    tau0 = (double)p + parabola(E[(p - 1 + kSps) % kSps], E[p], E[(p + 1) % kSps]);
    tau0 = pymod(tau0, kSps);
}
void trackTiming(const std::vector<cd>& mf, double& tau0, double& eps) {
    const size_t blk = (size_t)kSym * kSps / 4;
    const size_t nb = mf.size() / blk;
    eps = 0;
    if (nb < 4) { findTimingConst(mf, tau0); return; }
    std::vector<double> pw(nb);
    double pmax = 0;
    for (size_t b = 0; b < nb; b++) {
        double s = 0;
        for (size_t i = b * blk; i < (b + 1) * blk; i++) s += std::norm(mf[i]);
        pw[b] = s / (double)blk;
        pmax = std::max(pmax, pw[b]);
    }
    std::vector<double> taus, cs;
    for (size_t b = 0; b < nb; b++) {
        if (!(pw[b] > 0.5 * pmax)) continue;
        double E[kSps];
        energyPhases(mf.data() + b * blk, blk, E);
        int p = 0;
        for (int i = 1; i < kSps; i++) if (E[i] > E[p]) p = i;
        taus.push_back((double)p + parabola(E[(p - 1 + kSps) % kSps], E[p], E[(p + 1) % kSps]));
        cs.push_back((double)(b * blk) + (double)blk / 2.0);
    }
    bool constant = taus.size() < 3;
    double slope = 0, icpt = 0;
    if (!constant) {
        std::vector<double> ph(taus.size());
        for (size_t i = 0; i < taus.size(); i++) ph[i] = 2 * kPi * taus[i] / kSps;
        ph = unwrapped(ph);
        for (double& v : ph) v = v * kSps / (2 * kPi);
        polyfit1(cs, ph, slope, icpt);
        if (std::fabs(slope) * (cs.back() - cs.front()) < 0.5) constant = true;
    }
    if (constant) {
        const double ref = taus[0];
        double dev = 0;
        for (double t : taus) dev += pymod(t - ref + kSps / 2.0, kSps) - kSps / 2.0;
        tau0 = pymod(ref + dev / (double)taus.size(), kSps);
        return;
    }
    tau0 = pymod(icpt, kSps);
    eps = slope;
}

// Differential-correlation preamble metric at symbol j (0 .. n-32).
double syncMetricAt(const cd* y, size_t j, const std::vector<cd>& w) {
    cd num(0, 0);
    double en = 0;
    for (size_t i = 0; i < w.size(); i++) {
        const cd q = y[j + i + 1] * std::conj(y[j + i]);
        num += q * std::conj(w[i]);
        en += std::abs(q);
    }
    return std::abs(num) / (en + 1e-12);
}
const std::vector<cd>& diffPreamble() {
    static const std::vector<cd> w = [] {
        std::vector<cd> v;
        const auto& p = layout().pre;
        for (size_t i = 1; i < p.size(); i++) v.push_back(p[i] * std::conj(p[i - 1]));
        return v;
    }();
    return w;
}

void findFrames(const std::vector<cd>& y, std::vector<int64_t>& starts, double& mmax) {
    starts.clear();
    mmax = 0;
    const int64_t n = (int64_t)y.size();
    if (n < kPreambleLen + 2) return;
    const size_t nm = (size_t)n - 31;
    std::vector<double> m(nm);
    int64_t best = 0;
    for (size_t j = 0; j < nm; j++) { m[j] = syncMetricAt(y.data(), j, diffPreamble()); if (m[j] > m[(size_t)best]) best = (int64_t)j; }
    mmax = m[(size_t)best];
    const double thr = std::max(0.55, 0.5 * mmax);
    if (m[(size_t)best] < 0.3) return;
    std::map<int64_t, double> pos;
    pos[best] = m[(size_t)best];
    for (int dir = 1; dir >= -1; dir -= 2) {
        int64_t cur = best;
        int weak = 0;
        std::vector<int64_t> pend;
        for (;;) {
            const int64_t nxt = cur + dir * kFrameSymbols;
            if (nxt < 0 || nxt + kFrameSymbols > n) break;
            const int64_t lo = std::max<int64_t>(nxt - 3, 0), hi = std::min<int64_t>(nxt + 4, (int64_t)nm);
            if (lo >= hi) break;
            int64_t j = lo;
            for (int64_t i = lo; i < hi; i++) if (m[(size_t)i] > m[(size_t)j]) j = i;
            if (m[(size_t)j] >= thr) {
                for (int64_t pj : pend) pos[pj] = m[(size_t)pj];
                pend.clear();
                pos[j] = m[(size_t)j]; cur = j; weak = 0;
            } else {
                weak++; cur = nxt; pend.push_back(j);
                if (weak >= kMaxPendingWeak) break;
            }
        }
    }
    for (const auto& kv : pos) if (kv.first + kFrameSymbols <= n) starts.push_back(kv.first);
}

struct Static {   // parameters found at acquisition
    cd dc = cd(0, 0);
    double a0 = 0, b0 = kSweepLen, sweepDb = 0, cfoPre = 0, tau0 = 0, eps = 0, rate = 0, cfoHz = 0, mref = 0;
    int64_t firstStart = 0;
};

}  // namespace

// ================================================================= receiver
struct VoiceRx::Impl {
    std::function<void(const std::string&)> log;
    double inRate = kFs;
    RationalResampler rs;
    bool resampling = false;
    RxStats st;
    std::vector<cf32> rsOut;

    // acquisition
    std::vector<cd> acq;           // raw 48 kHz samples (DC not removed)
    bool tracking = false;
    Static P;

    // sweep stage
    std::vector<cd> raw;           // DC-removed input waiting for its sweep period to complete
    int64_t rawBase = 0;
    double swPh = 0, swPer = kSweepLen;
    int64_t swLo = 0;
    std::vector<cf> tmpl;
    // matched filter
    std::vector<double> taps;
    std::vector<cd> xr;            // rotated, sweep-free samples (history only)
    int64_t xrBase = 0;
    std::vector<cd> mf;
    int64_t mfBase = 0;
    // symbol sampling
    double tNext = 0, step = kSps;
    int64_t symNext = 0;
    std::vector<cd> ysym;
    std::vector<double> ytime;
    int64_t yBase = 0;
    // tracker
    bool locked = false;
    int64_t fstart = 0;
    int weak = 0;
    std::vector<int64_t> pend;
    double nvGlob = 0;
    int64_t nvCount = 0;
    // blocks
    bool aligned = false;
    std::vector<std::vector<float>> alignQ, blockQ;
    KaalCodec::StreamDecoder dec;
    bool primed = false;
    std::deque<int16_t> out;

    void say(const std::string& s) { if (log) log(s); }

    void restart() {
        acq.clear();
        tracking = false;
        locked = false;
        raw.clear(); rawBase = 0; xr.clear(); xrBase = 0; mf.clear(); mfBase = 0;
        ysym.clear(); ytime.clear(); yBase = 0; symNext = 0;
        pend.clear(); weak = 0;
        aligned = false; alignQ.clear(); blockQ.clear();
        nvGlob = 0; nvCount = 0;
        st.locked = false;
    }
    void fullReset() {
        restart();
        st = RxStats();
        out.clear();
        primed = false;
        dec.reset();
        if (resampling) rs.reset();
    }

    // ------------------------------------------------------------ input
    void feed48(const cf32* x, size_t n) {
        if (!n) return;
        if (!tracking) {
            for (size_t i = 0; i < n; i++) acq.emplace_back(x[i].real(), x[i].imag());
            if (acq.size() >= kAcqLen) {
                if (acquire()) {
                    std::vector<cd> buf;
                    buf.swap(acq);
                    ingest(buf.data(), buf.size());
                } else {
                    const size_t drop = 2 * (size_t)kFs;   // nothing found: slide the window
                    acq.erase(acq.begin(), acq.begin() + (std::ptrdiff_t)std::min(drop, acq.size()));
                }
            }
        } else {
            std::vector<cd> buf(n);
            for (size_t i = 0; i < n; i++) buf[i] = cd(x[i].real(), x[i].imag());
            ingest(buf.data(), n);
        }
    }

    // ------------------------------------------------------------ acquisition (batch, faithful to decode_stream steps 1-5b)
    bool acquire() {
        const size_t N = acq.size();
        Static s;
        std::vector<cd> x(acq);
        cd mean(0, 0);
        for (const cd& v : x) mean += v;
        mean /= (double)N;
        for (cd& v : x) v -= mean;
        s.dc = mean;
        double a0 = 0, b0 = 0, ratio = 0;
        if (!removeSweepBatch(x, a0, b0, ratio)) return false;
        s.a0 = a0; s.b0 = b0; s.sweepDb = ratio;
        const std::vector<double> tp = rrcTaps();
        const size_t nt = tp.size();
        const Layout& lay = layout();
        std::vector<cd> mfb, y;
        std::vector<int64_t> starts;
        double mmax = 0, om0 = 0, cfo = 0;
        for (int it = 0; it < 2; it++) {
            mfb.assign(N + nt - 1, cd(0, 0));
            for (size_t i = 0; i < N; i++) {
                const cd v = x[i];
                for (size_t k = 0; k < nt; k++) mfb[i + k] += v * tp[k];
            }
            trackTiming(mfb, s.tau0, s.eps);
            const double stp = kSps * (1.0 + s.eps);
            const int64_t nsym = (int64_t)(((double)mfb.size() - 8.0 - s.tau0) / stp);
            if (nsym < kFrameSymbols + 40) return false;
            y.resize((size_t)nsym);
            for (int64_t m = 0; m < nsym; m++) y[(size_t)m] = interpAt(mfb, 0, s.tau0 + stp * (double)m);
            findFrames(y, starts, mmax);
            if (starts.empty()) return false;
            std::vector<double> om;
            for (int64_t s_ : starts) {
                cd acc(0, 0), prev(0, 0);
                for (int i = 0; i < kPreambleLen; i++) {
                    const cd z = y[(size_t)(s_ + i)] * std::conj(lay.pre[(size_t)i]);
                    if (i > 0) acc += z * std::conj(prev);
                    prev = z;
                }
                om.push_back(std::arg(acc));
            }
            std::sort(om.begin(), om.end());
            om0 = om.size() % 2 ? om[om.size() / 2] : 0.5 * (om[om.size() / 2 - 1] + om[om.size() / 2]);
            cfo = om0 * kSym / (2 * kPi);
            if (it == 0 && std::fabs(cfo) > 100) {
                s.cfoPre = cfo;
                for (size_t n = 0; n < N; n++) {
                    const double ph = -2 * kPi * pymod(s.cfoPre * (double)n / kFs, 1.0);
                    x[n] *= cd(std::cos(ph), std::sin(ph));
                }
                continue;
            }
            break;
        }
        // spurious frames: preamble gain much weaker than the median
        std::vector<double> gpre;
        for (int64_t s_ : starts) {
            cd acc(0, 0);
            for (int i = 0; i < kPreambleLen; i++) {
                const double ph = -om0 * i;
                acc += y[(size_t)(s_ + i)] * std::conj(lay.pre[(size_t)i]) * cd(std::cos(ph), std::sin(ph));
            }
            gpre.push_back(std::abs(acc / (double)kPreambleLen));
        }
        std::vector<double> gs(gpre);
        std::sort(gs.begin(), gs.end());
        const double med = gs.size() % 2 ? gs[gs.size() / 2] : 0.5 * (gs[gs.size() / 2 - 1] + gs[gs.size() / 2]);
        std::vector<int64_t> kept;
        for (size_t i = 0; i < starts.size(); i++) {
            bool k = gpre[i] > 0.5 * med;
            if (!k) {
                const bool a = std::find(starts.begin(), starts.end(), starts[i] - kFrameSymbols) != starts.end();
                const bool b = std::find(starts.begin(), starts.end(), starts[i] + kFrameSymbols) != starts.end();
                k = a && b;
            }
            if (k) kept.push_back(starts[i]);
        }
        if (kept.empty()) return false;
        starts = kept;
        // coarse CFO removal, then the fine slope from preamble means and pilots
        for (size_t m = 0; m < y.size(); m++) { const double ph = -om0 * (double)m; y[m] *= cd(std::cos(ph), std::sin(ph)); }
        std::vector<double> ang, pidx, pang;
        std::vector<char> isp;
        std::vector<double> idx;
        for (int64_t s_ : starts) {
            cd g(0, 0);
            for (int i = 0; i < kPreambleLen; i++) g += y[(size_t)(s_ + i)] * std::conj(lay.pre[(size_t)i]);
            g /= (double)kPreambleLen;
            idx.push_back((double)s_ + (kPreambleLen - 1) / 2.0); ang.push_back(std::arg(g)); isp.push_back(0);
            for (int pi : lay.pilotIdx) {
                idx.push_back((double)(s_ + pi)); ang.push_back(std::arg(y[(size_t)(s_ + pi)] / lay.pilot)); isp.push_back(1);
            }
        }
        ang = unwrapped(ang);
        for (size_t i = 0; i < ang.size(); i++) if (isp[i]) { pidx.push_back(idx[i]); pang.push_back(ang[i]); }
        double slope = 0, ic = 0;
        if (pidx.size() >= 2) polyfit1(pidx, pang, slope, ic);
        const double cfoFine = slope * kSym / (2 * kPi);
        s.rate = om0;
        if (std::fabs(cfoFine) > 10) s.rate += slope;
        s.cfoHz = s.rate * kSym / (2 * kPi) + s.cfoPre;
        s.mref = mmax;
        s.firstStart = starts.front();
        P = s;
        {
            char b[200];
            std::snprintf(b, sizeof b, "KAAL: acquired, %zu frames, sweep %.1f dB, cfo %.1f Hz, clock %.1f ppm", starts.size(), ratio, s.cfoHz, s.eps * 1e6);
            say(b);
        }
        // pipeline init
        taps = tp;
        raw.clear(); rawBase = 0; xr.clear(); xrBase = 0; mf.clear(); mfBase = 0;
        ysym.clear(); ytime.clear(); yBase = 0; symNext = 0;
        tNext = P.tau0; step = kSps * (1.0 + P.eps);
        tmpl = sweepTemplate((size_t)kSweepLen, 0.0);
        swPer = P.b0;
        swPh = P.a0 + P.b0 * std::floor(-P.a0 / P.b0);
        swLo = 0;
        locked = true; fstart = P.firstStart; weak = 0; pend.clear();
        aligned = false; alignQ.clear(); blockQ.clear();
        nvGlob = 0; nvCount = 0;
        tracking = true;
        st.locked = true;
        st.sweepDb = (float)ratio;
        st.cfoHz = (float)P.cfoHz;
        return true;
    }

    // ------------------------------------------------------------ streaming pipeline
    void ingest(const cd* x, size_t n) {
        for (size_t i = 0; i < n; i++) raw.push_back(x[i] - P.dc);
        sweepRun();
        trackRun();
        trim();
    }

    void pushCleaned(const cd* seg, int64_t first, size_t n) {
        const double fcfo = P.cfoPre;
        for (size_t i = 0; i < n; i++) {
            cd v = seg[i];
            if (fcfo != 0) {
                const double ph = -2 * kPi * pymod(fcfo * (double)(first + (int64_t)i) / kFs, 1.0);
                v *= cd(std::cos(ph), std::sin(ph));
            }
            xr.push_back(v);
            // matched filter output for this sample
            const int64_t nn = xrBase + (int64_t)xr.size() - 1;
            cd acc(0, 0);
            const size_t nt = taps.size();
            for (size_t k = 0; k < nt; k++) {
                const int64_t idx = nn - (int64_t)k;
                if (idx < xrBase) break;
                acc += xr[(size_t)(idx - xrBase)] * taps[k];
            }
            mf.push_back(acc);
        }
        // keep only the filter history
        const size_t keep = taps.size() + 4;
        if (xr.size() > keep + 4096) {
            const size_t cut = xr.size() - keep;
            xr.erase(xr.begin(), xr.begin() + (std::ptrdiff_t)cut);
            xrBase += (int64_t)cut;
        }
    }

    void sweepRun() {
        const int64_t L = kSweepLen;
        for (;;) {
            const int64_t rawEnd = rawBase + (int64_t)raw.size();
            double s0 = swPh;
            const int64_t c0 = (int64_t)std::llround(s0);
            const int64_t hi0 = (int64_t)std::ceil(s0 + swPer);
            const int64_t need = std::max(hi0 + 5, c0 + L + 5);
            if (rawEnd < need) break;
            // position measurement: correlation against the sweep template at c0-4 .. c0+4
            double err = 0;
            bool valid = false;
            if (c0 - 4 >= std::max<int64_t>(rawBase, 0)) {
                double mag[9];
                for (int d = 0; d < 9; d++) {
                    const int64_t c = c0 - 4 + d;
                    cd acc(0, 0);
                    const cd* px = &raw[(size_t)(c - rawBase)];
                    for (int64_t i = 0; i < L; i++) acc += px[i] * std::conj(cd(tmpl[(size_t)i].real(), tmpl[(size_t)i].imag()));
                    mag[d] = std::abs(acc);
                }
                int j = 0;
                for (int d = 1; d < 9; d++) if (mag[d] > mag[j]) j = d;
                if (mag[j] > 0 && j > 0 && j < 8) {
                    const double meas = (double)(c0 - 4 + j) + parabola(mag[j - 1], mag[j], mag[j + 1]);
                    err = meas - s0;
                    valid = std::fabs(err) <= 4.0;
                }
            }
            if (valid) { s0 += 0.3 * err; }
            const double epsw = swPer / (double)L - 1.0;
            const int64_t lo = std::max<int64_t>(swLo, 0);
            const int64_t hi = (int64_t)std::ceil(s0 + swPer);
            if (hi > lo) {
                if (hi - lo >= 2000 && lo >= rawBase) fitSweepPeriod(&raw[(size_t)(lo - rawBase)], lo, hi, s0, epsw, nullptr, nullptr);
                if (lo >= rawBase) pushCleaned(&raw[(size_t)(lo - rawBase)], lo, (size_t)(hi - lo));
                swLo = hi;
            }
            if (valid) swPer = std::min(1.02 * L, std::max(0.98 * L, swPer + 0.02 * err));
            swPh = s0 + swPer;
            // drop consumed raw samples, keeping a margin for the next measurement
            const int64_t keepFrom = std::min<int64_t>(swLo, (int64_t)std::llround(swPh) - 8);
            if (keepFrom > rawBase + 4096) {
                const size_t cut = (size_t)(keepFrom - rawBase);
                raw.erase(raw.begin(), raw.begin() + (std::ptrdiff_t)cut);
                rawBase += (int64_t)cut;
            }
        }
    }

    void produceSymbols() {
        const int64_t mfEnd = mfBase + (int64_t)mf.size();
        const int64_t want = fstart + 3 + kFrameSymbols + 2;   // symbols needed for the next frame search
        while (yBase + (int64_t)ysym.size() < want) {
            const int64_t i0 = (int64_t)std::floor(tNext);
            if (i0 + 8 >= mfEnd) break;
            cd y = interpAt(mf, mfBase, tNext);
            const double ph = -P.rate * (double)symNext;
            const double phw = pymod(ph, 2 * kPi);
            y *= cd(std::cos(phw), std::sin(phw));
            ysym.push_back(y);
            ytime.push_back(tNext);
            tNext += step;
            symNext++;
        }
    }

    void trackRun() {
        while (locked) {
            produceSymbols();
            const int64_t yEnd = yBase + (int64_t)ysym.size();
            const int64_t lo = std::max<int64_t>(std::max<int64_t>(fstart - 3, yBase), 0), hi = fstart + 3;
            if (yEnd < hi + kFrameSymbols + 1) break;
            int64_t j = lo;
            double mj = -1;
            for (int64_t c = lo; c <= hi; c++) {
                const double m = syncMetricAt(ysym.data() + (c - yBase), 0, diffPreamble());
                if (m > mj) { mj = m; j = c; }
            }
            const double thr = std::max(0.55, 0.5 * P.mref);
            if (mj >= thr) {
                for (int64_t pj : pend) emitFrame(pj, false);
                pend.clear();
                emitFrame(j, true);
                fstart = j + kFrameSymbols;
                weak = 0;
            } else {
                weak++;
                pend.push_back(j);
                fstart = fstart + kFrameSymbols;
                if (weak >= kMaxPendingWeak) {
                    say("KAAL: sync lost");
                    const uint64_t keepGood = st.framesGood, keepBad = st.framesBad, keepIn = st.samplesIn;
                    const float snr = st.snrDb;
                    restart();
                    st.framesGood = keepGood; st.framesBad = keepBad; st.samplesIn = keepIn; st.snrDb = snr;
                    return;
                }
            }
            // trim symbols no longer needed
            const int64_t keepFrom = fstart - 3 * kFrameSymbols - 8;
            if (keepFrom > yBase + 2048) {
                const size_t cut = (size_t)(keepFrom - yBase);
                ysym.erase(ysym.begin(), ysym.begin() + (std::ptrdiff_t)cut);
                ytime.erase(ytime.begin(), ytime.begin() + (std::ptrdiff_t)cut);
                yBase += (int64_t)cut;
            }
        }
    }

    void trim() {
        if (ytime.empty()) return;
        const int64_t keepFrom = (int64_t)std::floor(std::min(ytime.front(), tNext)) - 16;
        if (keepFrom > mfBase + 8192) {
            const size_t cut = (size_t)(keepFrom - mfBase);
            mf.erase(mf.begin(), mf.begin() + (std::ptrdiff_t)cut);
            mfBase += (int64_t)cut;
        }
    }

    // ------------------------------------------------------------ one frame
    void emitFrame(int64_t j, bool strong) {
        const Layout& lay = layout();
        const size_t s = (size_t)(j - yBase);
        if (j < yBase || s + kFrameSymbols > ysym.size()) return;   // trimmed away or not there: lost
        const cd* fr = ysym.data() + s;
        // complex-linear gain fit g = a + b t on preamble + pilots
        std::array<int, kPreambleLen + kNPilots> kt;
        std::array<cd, kPreambleLen + kNPilots> kr;
        for (int i = 0; i < kPreambleLen; i++) { kt[(size_t)i] = i; kr[(size_t)i] = lay.pre[(size_t)i]; }
        for (int g = 0; g < kNPilots; g++) { kt[(size_t)(kPreambleLen + g)] = lay.pilotIdx[(size_t)g]; kr[(size_t)(kPreambleLen + g)] = lay.pilot; }
        const double tc = (kFrameSymbols - 1) / 2.0;
        double S0 = 0, S1 = 0, S2 = 0;
        cd r0(0, 0), r1(0, 0);
        for (size_t i = 0; i < kt.size(); i++) {
            const double t = kt[i] - tc, w = std::norm(kr[i]);
            const cd c = std::conj(kr[i]) * fr[kt[i]];
            S0 += w; S1 += w * t; S2 += w * t * t;
            r0 += c; r1 += c * t;
        }
        const double det = S0 * S2 - S1 * S1;
        const cd a = (r0 * S2 - r1 * S1) / det, b = (r1 * S0 - r0 * S1) / det;
        double res = 0;
        for (size_t i = 0; i < kt.size(); i++) {
            const double t = kt[i] - tc;
            res += std::norm(fr[kt[i]] - (a + b * t) * kr[i]);
        }
        const double nvi = (res / std::max<double>((double)kt.size() - 2, 1.0)) / std::max(std::norm(a), 1e-12);
        nvCount++;
        nvGlob += (nvi - nvGlob) / (double)std::min<int64_t>(nvCount, 400);
        const double nvf = 0.5 * nvi + 0.5 * nvGlob;
        std::vector<cf> syms(lay.dataIdx.size());
        for (size_t i = 0; i < syms.size(); i++) {
            const int idx = lay.dataIdx[i];
            const cd g = a + b * (idx - tc);
            const cd v = fr[idx] / g;
            syms[i] = cf((float)v.real(), (float)v.imag());
        }
        std::vector<float> llr = qam16DemapSoft(syms.data(), syms.size(), (float)std::max(nvf, 1e-3));
        st.snrDb = (float)(-10 * std::log10(std::max(nvGlob, 1e-12)));
        if (strong) timingUpdate(j);
        handleFrame(std::move(llr));
    }

    // Early-late check on the preamble: nudges the symbol clock phase and rate for the symbols not yet sampled.
    void timingUpdate(int64_t j) {
        const Layout& lay = layout();
        cd cp(0, 0), cm(0, 0);
        for (int i = 0; i < kPreambleLen; i++) {
            const int64_t k = j - yBase + i;
            const double t = ytime[(size_t)k];
            const double ph = pymod(-P.rate * (double)(j + i), 2 * kPi);
            const cd rot(std::cos(ph), std::sin(ph));
            const cd ref = std::conj(lay.pre[(size_t)i]);
            cp += interpAt(mf, mfBase, t + 0.5) * rot * ref;
            cm += interpAt(mf, mfBase, t - 0.5) * rot * ref;
        }
        const double ap = std::abs(cp), am = std::abs(cm);
        if (ap + am < 1e-9) return;
        const double d = std::min(1.0, std::max(-1.0, 16.0 * (ap - am) / (ap + am)));
        tNext += 0.02 * d;
        step += 0.0006 * d / kFrameSymbols;
        step = std::min(kSps * 1.01, std::max(kSps * 0.99, step));
    }

    // ------------------------------------------------------------ block alignment, LDPC, codec
    void handleFrame(std::vector<float>&& llr) {
        if (!aligned) {
            alignQ.push_back(std::move(llr));
            if ((int)alignQ.size() >= kAlignFrames) align();
            return;
        }
        blockQ.push_back(std::move(llr));
        if (blockQ.size() == 4) decodeBlock();
    }

    std::vector<float> deintBlock(const std::vector<const std::vector<float>*>& rows) {
        std::vector<float> in(4 * kCodedBits, 0.f);
        for (size_t r = 0; r < 4; r++) if (rows[r]) std::copy(rows[r]->begin(), rows[r]->end(), in.begin() + (std::ptrdiff_t)(r * kCodedBits));
        return deinterleave(in);
    }

    void align() {
        // first frame sits at block position `off`; score each hypothesis on the first 24 rows from there
        int best = 0;
        double bestScore = -1;
        for (int off = 0; off < 4; off++) {
            double sum = 0;
            int rows = 0;
            const int nBlocks = (off + 24 + 3) / 4;
            for (int b = 0; b < nBlocks; b++) {
                std::vector<const std::vector<float>*> r(4, nullptr);
                for (int k = 0; k < 4; k++) {
                    const int fi = b * 4 + k - off;
                    if (fi >= 0 && fi < (int)alignQ.size()) r[(size_t)k] = &alignQ[(size_t)fi];
                }
                const std::vector<float> d = deintBlock(r);
                for (int k = 0; k < 4; k++) {
                    const int row = b * 4 + k;
                    if (row >= off && row < off + 24) { sum += ldpcSyndromeScore(&d[(size_t)k * kCodedBits]); rows++; }
                }
            }
            const double sc = rows ? sum / rows : 0;
            if (sc > bestScore) { bestScore = sc; best = off; }
        }
        const int fb = (4 - best) % 4;
        char b[120];
        std::snprintf(b, sizeof b, "KAAL: block offset %d (syndrome score %.3f)", best, bestScore);
        say(b);
        aligned = true;
        std::vector<std::vector<float>> q;
        q.swap(alignQ);
        for (size_t i = (size_t)fb; i < q.size(); i++) {
            blockQ.push_back(std::move(q[i]));
            if (blockQ.size() == 4) decodeBlock();
        }
    }

    void decodeBlock() {
        std::vector<const std::vector<float>*> r(4);
        for (size_t k = 0; k < 4; k++) r[k] = &blockQ[k];
        const std::vector<float> d = deintBlock(r);
        for (size_t k = 0; k < 4; k++) {
            uint8_t info[kLdpcK];
            const bool ok = ldpcDecode(&d[k * kCodedBits], info);
            if (ok) st.framesGood++; else st.framesBad++;
            int16_t pcm[KaalCodec::kFrame];
            dec.push(ok ? info : nullptr, pcm);
            if (!primed) { primed = true; continue; }   // the first decoded frame is the codec delay
            out.insert(out.end(), pcm, pcm + KaalCodec::kFrame);
        }
        blockQ.clear();
    }
};

// ================================================================= public interface
VoiceRx::VoiceRx() : p_(new Impl) {}
VoiceRx::~VoiceRx() = default;

void VoiceRx::configure(double sampleRate) {
    Impl& s = *p_;
    s.inRate = sampleRate > 0 ? sampleRate : (double)kFs;
    s.resampling = std::fabs(s.inRate - (double)kFs) > 1e-6;
    if (s.resampling) s.resampling = s.rs.configure(s.inRate, (double)kFs);
    s.fullReset();
}

void VoiceRx::reset() { p_->fullReset(); }

void VoiceRx::feed(const std::complex<float>* x, size_t n) {
    Impl& s = *p_;
    s.st.samplesIn += n;
    if (s.resampling) {
        s.rsOut.clear();
        s.rs.process(x, n, s.rsOut);
        s.feed48(s.rsOut.data(), s.rsOut.size());
    } else {
        s.feed48(x, n);
    }
    s.st.locked = s.locked && s.tracking;
}

size_t VoiceRx::popAudio(int16_t* out, size_t maxSamples) {
    Impl& s = *p_;
    const size_t n = std::min(maxSamples, s.out.size());
    for (size_t i = 0; i < n; i++) out[i] = s.out[i];
    s.out.erase(s.out.begin(), s.out.begin() + (std::ptrdiff_t)n);
    return n;
}

RxStats VoiceRx::stats() const { return p_->st; }

void VoiceRx::setLogCallback(std::function<void(const std::string&)> cb) { p_->log = std::move(cb); }

}  // namespace dect2::kaal
