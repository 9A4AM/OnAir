#include "dect2/t2rx.h"
#include "dect2/t2ofdm.h"
#include "dect2/t2pilots.h"
#include "dect2/t2l1.h"
#include "dect2/t2interleave.h"

#include <Accelerate/Accelerate.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <deque>
#include <mutex>

namespace dect2 {

using cd = std::complex<double>;
static constexpr double kTwoPi = 6.283185307179586;

namespace {

constexpr double kP1Threshold = 0.30; // C-A-B correlation needed to try decoding
constexpr double kP1MinConf = 0.40;   // decoded S1/S2 sequence correlation needed to accept
constexpr int kPeakHalfWidth = 700;

struct Frame {
    int64_t anchor;      // absolute index of the first symbol after P1
    int next = 0;        // next symbol index to process
    int maxSyms = 0;
};

} // namespace

struct T2Receiver::Impl {
    // ---- configuration
    double inRate = 0, fn = 0;
    bool decimate = false, rateOk = true;
    std::vector<float> decTaps;
    std::vector<cf32> decIn;

    // ---- native-rate buffer
    std::vector<cf32> buf;
    int64_t base = 0; // absolute index of buf[0]
    int64_t end() const { return base + (int64_t)buf.size(); }
    const cf32& at(int64_t abs) const { return buf[(size_t)(abs - base)]; }

    // ---- FFT
    std::array<FFTSetup, 17> fft{};
    std::vector<float> fr, fi;
    FFTSetup setup(int log2n) {
        if (!fft[log2n]) fft[log2n] = vDSP_create_fftsetup(log2n, kFFTRadix2);
        return fft[log2n];
    }
    void doFft(int log2n, bool inverse = false) {
        DSPSplitComplex sc{fr.data(), fi.data()};
        vDSP_fft_zip(setup(log2n), &sc, 1, log2n, inverse ? FFT_INVERSE : FFT_FORWARD);
    }

    // ---- P1 scanner
    int64_t scanPos = 0;
    bool scanFirst = true;
    std::array<cd, 1024> phiTab; // exp(-j 2 pi g / 1024)
    int64_t lastP1Abs = INT64_MIN / 2;
    std::vector<float> trace;

    // ---- state
    int state = 0;
    P1Info p1;
    uint64_t p1Count = 0;
    double prevPos = -1;
    int64_t giAnchor = 0;
    int giIdx = -1;
    float giScore[kNumGi] = {};
    float giMargin = 0;
    int fftN = 0, guard = 0, carriers = 0, nP2 = 0;
    int fftCode = -1, curS1 = -1;
    int frameSyms = 0;
    double frameLen = 0, sro = 0, frameMsv = 0;
    std::deque<Frame> frames;
    double cfoEst = 0;
    double cpCorrAvg = 0, timingAvg = 0;
    int lowCount = 0;
    int64_t gridOff = 0;
    uint64_t symbols = 0, symCounter = 0;
    std::vector<cf32> prevCells;
    int64_t prevSymAbs = -1;
    int64_t lastP1Seen = 0;
    double frameCfo = 0;   // CFO used for every symbol of the current frame (constant, so phase stays continuous)
    int64_t curAnchor = 0;

    // ---- P2 stage
    int kMax = 0;
    std::vector<std::vector<cf32>> p2cells;
    GridInterpolator interp;
    bool chValid = false, extDetected = false;
    std::vector<cf32> chH;
    int chK = 0, irMin = 0;
    std::vector<float> chMag, chPh, irDb, snrDbv;
    float p2Snr = 0;
    std::vector<cf32> eqP2;
    L1Pre l1pre;
    L1Post l1post;
    bool l1preOk = false, l1postOk = false;
    uint64_t l1preGood = 0, l1preBad = 0, l1postGood = 0, l1postBad = 0;
    int l1Iters = 0;
    float l1N0 = 0;
    // data stage
    std::vector<std::vector<cf32>> frameCells;
    bool dataValid = false;
    float dataSnr = 0;
    int dataDx = 0, dataDy = 0, dataPp = 0;
    std::vector<cf32> eqData;
    std::vector<float> dataSnrCar;
    uint64_t dataFrames = 0;

    // ---- outputs
    std::mutex mu;
    RxTelemetry tel;
    std::vector<cf32> p1Const, diffCells, rawCells;

    Impl() {
        for (int i = 0; i < 1024; i++) phiTab[i] = std::polar(1.0, -kTwoPi * i / 1024.0);
    }
    ~Impl() {
        for (auto s : fft) if (s) vDSP_destroy_fftsetup(s);
    }

    void resetAll() {
        decIn.clear();
        buf.clear();
        base = 0;
        scanPos = 0;
        scanFirst = true;
        lastP1Abs = INT64_MIN / 2;
        trace.clear();
        state = 0;
        p1 = P1Info();
        p1Count = 0;
        prevPos = -1;
        giIdx = -1;
        std::fill(std::begin(giScore), std::end(giScore), 0.f);
        giMargin = 0;
        fftN = guard = carriers = nP2 = 0;
        fftCode = curS1 = -1;
        frameSyms = 0;
        frameLen = sro = frameMsv = 0;
        frames.clear();
        cfoEst = cpCorrAvg = timingAvg = 0;
        lowCount = 0;
        gridOff = 0;
        symbols = symCounter = 0;
        prevCells.clear();
        prevSymAbs = -1;
        p1Const.clear(); diffCells.clear(); rawCells.clear();
        p2cells.clear(); chValid = false; chH.clear(); chMag.clear(); chPh.clear(); irDb.clear(); snrDbv.clear(); eqP2.clear();
        l1pre = L1Pre(); l1post = L1Post(); l1preOk = l1postOk = false; l1preGood = l1preBad = l1postGood = l1postBad = 0;
        frameCells.clear(); dataValid = false; eqData.clear(); dataSnrCar.clear(); dataFrames = 0;
    }

    // ------------------------------------------------------------ decimation (2x -> 1x)
    void designDecimator() {
        const int taps = 63;
        decTaps.assign(taps, 0.f);
        const double fc = 0.25, beta = 6.0;
        auto i0 = [](double x) {
            double s = 1, t = 1;
            for (int k = 1; k < 30; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; }
            return s;
        };
        double sum = 0;
        for (int i = 0; i < taps; i++) {
            double m = i - (taps - 1) / 2.0;
            double sinc = m == 0 ? 2 * fc : std::sin(kTwoPi * fc * m) / (M_PI * m);
            double r = 2.0 * i / (taps - 1) - 1.0;
            double w = i0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / i0(beta);
            decTaps[i] = (float)(sinc * w);
            sum += decTaps[i];
        }
        for (auto& t : decTaps) t = (float)(t / sum);
    }

    void decimateAppend(const cf32* x, size_t n) {
        decIn.insert(decIn.end(), x, x + n);
        const size_t taps = decTaps.size();
        if (decIn.size() < taps + 1) return;
        size_t nOut = (decIn.size() - taps) / 2 + 1;
        size_t used = 2 * (nOut - 1) + taps;
        std::vector<float> re(used), im(used), ore(nOut), oim(nOut);
        for (size_t i = 0; i < used; i++) { re[i] = decIn[i].real(); im[i] = decIn[i].imag(); }
        vDSP_desamp(re.data(), 2, decTaps.data(), ore.data(), nOut, taps);
        vDSP_desamp(im.data(), 2, decTaps.data(), oim.data(), nOut, taps);
        size_t old = buf.size();
        buf.resize(old + nOut);
        for (size_t i = 0; i < nOut; i++) buf[old + i] = cf32(ore[i], oim[i]);
        decIn.erase(decIn.begin(), decIn.begin() + 2 * nOut);
    }

    // ------------------------------------------------------------ P1 detection
    void scanP1() {
        const int64_t W = kPeakHalfWidth;
        int64_t e = end();
        int64_t lo = std::max(scanPos, base);
        if (e - lo < kP1Len + 4 * W + 16) return;
        int64_t len = e - lo;
        int64_t lastD = len - kP1Len; // last valid d index (relative)
        std::vector<cd> pq1(len + 1), pq2(len + 1);
        std::vector<double> pe(len + 1);
        for (int64_t i = 0; i < len; i++) {
            const cf32 x = at(lo + i);
            cd xc(x.real(), x.imag());
            cd ph = phiTab[(lo + i) & 1023];
            cd q1 = 0, q2 = 0;
            if (i + kP1CLen < len) { cf32 y = at(lo + i + kP1CLen); q1 = xc * std::conj(cd(y.real(), y.imag())) * ph; }
            if (i >= kP1BLen) { cf32 y = at(lo + i - kP1BLen); q2 = xc * std::conj(cd(y.real(), y.imag())) * ph; }
            pq1[i + 1] = pq1[i] + q1;
            pq2[i + 1] = pq2[i] + q2;
            pe[i + 1] = pe[i] + (double)x.real() * x.real() + (double)x.imag() * x.imag();
        }
        std::vector<float> m(lastD + 1);
        for (int64_t d = 0; d <= lastD; d++) {
            cd sc = pq1[d + kP1CLen] - pq1[d];
            cd sb = pq2[d + kP1Len] - pq2[d + kP1CLen + kP1ALen];
            double en = 0.5 * (pe[d + kP1Len] - pe[d]);
            m[d] = en > 1e-12 ? (float)((std::abs(sc) + std::abs(sb)) / en) : 0.f;
        }
        int64_t cLo = scanFirst ? 0 : W;
        int64_t cHi = lastD - W;
        // decimated trace of the region that becomes final in this pass
        for (int64_t d = cLo; d + kTraceDecim <= cHi + 1; d += kTraceDecim) {
            float mx = 0;
            for (int k = 0; k < kTraceDecim; k++) mx = std::max(mx, m[d + k]);
            trace.push_back(mx);
        }
        if (trace.size() > 2048) trace.erase(trace.begin(), trace.begin() + (trace.size() - 2048));
        for (int64_t d = cLo; d <= cHi; d++) {
            if (m[d] < kP1Threshold) continue;
            bool peak = true;
            for (int64_t k = std::max<int64_t>(0, d - W); k <= std::min(lastD, d + W); k++)
                if (m[k] > m[d] || (m[k] == m[d] && k < d)) { peak = false; break; }
            if (!peak) continue;
            int64_t dAbs = lo + d;
            if (dAbs - lastP1Abs < 2 * kP1Len) continue;
            handleP1Candidate(dAbs, m[d]);
            d += W;
        }
        scanFirst = false;
        scanPos = lo + lastD - 2 * W;
    }

    struct P1Decode {
        bool ok = false;
        int s1 = 0, s2 = 0;
        double conf = 0, cfo = 0, frac = 0;
        std::vector<cf32> z;
    };

    P1Decode decodeP1(int64_t d, double cfoC) {
        P1Decode best;
        const double binHz = fn / 1024.0;
        double hypStep = fn / kP1CLen; // 1/(542 T)
        for (int k = -1; k <= 1; k++) {
            double cfo = cfoC + k * hypStep;
            fr.assign(1024, 0.f); fi.assign(1024, 0.f);
            for (int t = 0; t < 1024; t++) {
                cf32 x = at(d + kP1CLen + t);
                double a = -kTwoPi * cfo * t / fn;
                float c = (float)std::cos(a), s = (float)std::sin(a);
                fr[t] = x.real() * c - x.imag() * s;
                fi[t] = x.real() * s + x.imag() * c;
            }
            doFft(10);
            // differential decode across the active carriers
            std::vector<cf32> y(384), z(384);
            for (int i = 0; i < 384; i++) {
                int f = kP1ActiveCarriers[i] + 86 - 512;
                int b = (f + 1024) % 1024;
                y[i] = cf32(fr[b], fi[b]);
                // undo the transmit randomiser is part of the table sign; see rnd below
            }
            static int rnd[384];
            static bool init = false;
            if (!init) {
                int sr = 0x4e46;
                for (int i = 0; i < 384; i++) {
                    int b = (sr ^ (sr >> 1)) & 1;
                    rnd[i] = b ? -1 : 1;
                    sr >>= 1;
                    if (b) sr |= 0x4000;
                }
                init = true;
            }
            for (int i = 0; i < 384; i++) y[i] *= (float)rnd[i];
            z[0] = 0;
            double tot = 0;
            for (int i = 1; i < 384; i++) {
                z[i] = y[i] * std::conj(y[i - 1]);
                tot += std::abs(z[i]);
            }
            if (tot <= 0) continue;
            auto bit = [](const uint8_t* bytes, int i) { return (bytes[i >> 3] >> (7 - (i & 7))) & 1; };
            double bs1 = -1e30, bs2 = -1e30;
            int i1 = 0, i2 = 0;
            for (int s = 0; s < 8; s++) {
                double sc = 0;
                for (int i = 1; i < 64; i++) sc += (1 - 2 * bit(kS1Patterns[s], i)) * z[i].real();
                for (int i = 0; i < 64; i++) sc += (1 - 2 * bit(kS1Patterns[s], i)) * z[320 + i].real();
                if (sc > bs1) { bs1 = sc; i1 = s; }
            }
            for (int s = 0; s < 16; s++) {
                double sc = 0;
                for (int i = 0; i < 256; i++) sc += (1 - 2 * bit(kS2Patterns[s], i)) * z[64 + i].real();
                if (sc > bs2) { bs2 = sc; i2 = s; }
            }
            double conf = (bs1 + bs2) / tot;
            if (conf > best.conf) {
                best.conf = conf;
                best.s1 = i1;
                best.s2 = i2;
                best.cfo = cfo;
                best.z.assign(z.begin(), z.end());
                // sub-sample timing from the phase slope of the (now known) differential bits
                double bestMag = -1, bestDelta = 0;
                for (double delta = -24; delta <= 24; delta += 0.1) {
                    cd acc = 0;
                    for (int i = 1; i < 384; i++) {
                        int sg = 1;
                        if (i < 64) sg = 1 - 2 * bit(kS1Patterns[i1], i);
                        else if (i < 320) sg = 1 - 2 * bit(kS2Patterns[i2], i - 64);
                        else sg = 1 - 2 * bit(kS1Patterns[i1], i - 320);
                        double df = kP1ActiveCarriers[i] - kP1ActiveCarriers[i - 1];
                        cd w(z[i].real() * sg, z[i].imag() * sg);
                        acc += w * std::polar(1.0, -kTwoPi * delta * df / 1024.0);
                    }
                    if (std::abs(acc) > bestMag) { bestMag = std::abs(acc); bestDelta = delta; }
                }
                best.frac = bestDelta;
            }
        }
        (void)binHz;
        best.ok = best.conf >= kP1MinConf && best.s1 < 5;
        return best;
    }

    void handleP1Candidate(int64_t d, float metric) {
        // CFO from the correlation phases
        cd sc = 0, sb = 0;
        for (int i = 0; i < kP1CLen; i++) {
            cf32 a = at(d + i), b = at(d + i + kP1CLen);
            sc += cd(a.real(), a.imag()) * std::conj(cd(b.real(), b.imag())) * phiTab[(d + i) & 1023];
        }
        for (int i = 0; i < kP1BLen; i++) {
            int64_t g = d + kP1CLen + kP1ALen + i;
            cf32 a = at(g), b = at(g - kP1BLen);
            sb += cd(a.real(), a.imag()) * std::conj(cd(b.real(), b.imag())) * phiTab[g & 1023];
        }
        // arg(sc * exp(+j phi d)) = -2 pi eps 542 T
        cd rot = std::polar(1.0, kTwoPi * (double)(d & 1023) / 1024.0);
        double epsC = -std::arg(sc * rot) * fn / (kTwoPi * kP1CLen);
        P1Decode r = decodeP1(d, epsC);
        if (!r.ok) return;

        lastP1Abs = d;
        P1Info info;
        info.valid = true;
        info.s1 = r.s1;
        info.s2field1 = r.s2 >> 1;
        info.mixed = r.s2 & 1;
        const FftMode* fm = fftModeFromS2(info.s2field1);
        info.fftN = fm ? fm->n : 0;
        info.cfoHz = r.cfo;
        info.conf = (float)r.conf;
        info.metric = metric;
        info.timingFrac = -r.frac;
        info.pos = (uint64_t)d;
        double pos = (double)d - r.frac; // refined start of P1
        p1 = info;
        p1Count++;
        lastP1Seen = end();
        p1Const.assign(r.z.begin() + 1, r.z.end());
        float nrm = 0;
        for (auto& c : p1Const) nrm += std::abs(c);
        nrm = std::max(1e-9f, nrm / p1Const.size());
        for (auto& c : p1Const) c /= nrm;

        if (!fm || info.s1 >= 5 || info.s1 == 2) { // non-T2 or reserved: nothing to demodulate
            return;
        }
        bool changed = fm->n != fftN || info.s1 != curS1 || info.s2field1 != fftCode;
        if (changed) {
            fftN = fm->n;
            fftCode = info.s2field1;
            curS1 = info.s1;
            nP2 = fm->nP2;
            giIdx = -1;
            guard = 0;
            frameLen = 0; frameSyms = 0; sro = 0;
            frames.clear();
            prevPos = -1;
            cfoEst = info.cfoHz;
            prevCells.clear();
            p2cells.clear(); chValid = false;
            kMax = fm->kExt ? fm->kExt : fm->kNormal;
            state = 1;
            giAnchor = (int64_t)std::llround(pos) + kP1Len;
            carriers = fm->kNormal;
        }
        if (prevPos >= 0) onFrameSpacing(pos - prevPos);
        prevPos = pos;
        if (state == 2 && frameSyms > 0) {
            Frame f;
            f.anchor = (int64_t)std::llround(pos) + kP1Len;
            f.maxSyms = frameSyms;
            frames.push_back(f);
        } else if (state == 2) {
            Frame f;
            f.anchor = (int64_t)std::llround(pos) + kP1Len;
            f.maxSyms = std::max(6, nP2);
            frames.push_back(f);
        }
        if (cfoEst == 0) cfoEst = info.cfoHz;
        // between frames P1 gives a coarse CFO; keep CP-derived value if we already have one
        if (state != 2) cfoEst = info.cfoHz;
    }

    void onFrameSpacing(double L) {
        if (guard <= 0) return;
        int sym = fftN + guard;
        double nSyms = (L - kP1Len) / sym;
        double r = std::round(nSyms);
        double tol = std::max(40.0, L * 80e-6) / sym; // allow +-80 ppm of sample-clock error
        if (r >= 1 && std::fabs(nSyms - r) <= tol) {
            frameSyms = (int)r;
            double nominal = kP1Len + r * sym;
            double meas = (L - nominal) / nominal;
            sro = sro == 0 ? meas : sro + 0.3 * (meas - sro);
            frameLen = nominal;
            frameMsv = nominal / fn * 1e3;
        }
    }

    // ------------------------------------------------------------ guard-interval detection
    bool evaluateGi() {
        const int N = fftN;
        const int K = 3;
        int maxG = guardSamples(N, 3);
        if (end() < giAnchor + (int64_t)K * (N + maxG) + N + 16) return false;
        float score[kNumGi];
        for (int g = 0; g < kNumGi; g++) {
            int G = guardSamples(N, g);
            double acc = 0;
            for (int k = 0; k < K; k++) {
                int64_t s = giAnchor + (int64_t)k * (N + G);
                cd c = 0;
                double e = 0;
                for (int n = 0; n < G; n++) {
                    cf32 a = at(s + n), b = at(s + n + N);
                    c += cd(a.real(), a.imag()) * std::conj(cd(b.real(), b.imag()));
                    e += 0.5 * (std::norm(cd(a.real(), a.imag())) + std::norm(cd(b.real(), b.imag())));
                }
                acc += e > 0 ? std::abs(c) / e : 0;
            }
            score[g] = (float)(acc / K);
        }
        int best = 0;
        for (int g = 1; g < kNumGi; g++) if (score[g] > score[best]) best = g;
        float second = 0;
        for (int g = 0; g < kNumGi; g++) if (g != best) second = std::max(second, score[g]);
        std::copy(score, score + kNumGi, giScore);
        giMargin = second > 1e-6f ? score[best] / second : 99.f;
        if (score[best] < 0.25f) { // nothing usable - drop back to searching
            state = 0;
            fftN = 0;
            return true;
        }
        giIdx = best;
        guard = guardSamples(N, giIdx);
        state = 2;
        // process the symbols that follow this first P1 so something is displayed immediately
        Frame f;
        f.anchor = giAnchor;
        f.maxSyms = std::max(6, nP2);
        frames.push_back(f);
        return true;
    }

    // ------------------------------------------------------------ OFDM symbols
    void processFrames() {
        const int N = fftN, G = guard;
        while (!frames.empty()) {
            bool stalled = false;
            while (!frames.empty() && frames.front().next < frames.front().maxSyms) {
                Frame& f = frames.front();
                int64_t s = f.anchor + gridOff + (int64_t)std::llround((double)f.next * (N + G) * (1.0 + sro));
                if (s + N + G + 40 > end()) { stalled = true; break; }
                int idx = f.next++;
                processSymbol(s, idx); // may clear `frames` (loss of lock / guard-interval correction)
            }
            if (stalled || frames.empty()) break;
            frames.pop_front();
        }
    }

    // FFT of the symbol that starts at s (guard included); returns the K central carriers, CFO-corrected.
    void fftCells(int64_t s, std::vector<cf32>& cells, int K) {
        const int N = fftN, G = guard;
        int back = std::min(G / 4, 32);
        int64_t w0 = s + G - back;
        fr.assign(N, 0.f); fi.assign(N, 0.f);
        double dph = -kTwoPi * frameCfo / fn;
        double ph0 = dph * (double)(w0 - curAnchor);
        for (int n = 0; n < N; n++) {
            cf32 x = at(w0 + n);
            double a = ph0 + dph * n;
            float cs = (float)std::cos(a), sn = (float)std::sin(a);
            fr[n] = x.real() * cs - x.imag() * sn;
            fi[n] = x.real() * sn + x.imag() * cs;
        }
        doFft((int)std::lround(std::log2((double)N)));
        cells.resize(K);
        for (int kk = 0; kk < K; kk++) {
            int f = kk - (K - 1) / 2;
            int b = (f + N) % N;
            cells[kk] = cf32(fr[b], fi[b]);
        }
    }

    // Channel estimate from the P2 pilots, equalisation, L1-pre / L1-post decoding.
    bool p2Hypothesis(bool ext, bool final) {
        const FftMode* fm = fftModeFromS2(fftCode);
        const int G = guard, N = fftN;
        PilotConfig pc; pc.fftCode = fftCode; pc.ext = ext; pc.pp = 0;
        PilotMap pm(pc);
        if (!pm.valid()) { pc.pp = 1; pm = PilotMap(pc); }
        if (!pm.valid()) return false;
        const int K = pm.carriers(), off = (kMax - K) / 2;
        const int S = fftCode == 5 ? 6 : 3;
        const int M = (K - 1) / S + 1;
        std::vector<cf32> hg(M, cf32(0, 0));
        std::vector<int> cnt(M, 0);
        std::vector<std::vector<cf32>> obs(nP2, std::vector<cf32>(M, cf32(0, 0)));
        std::vector<uint8_t> types;
        for (int l = 0; l < nP2; l++) {
            pm.symbolTypes(l, nP2 + 2, types);
            for (int n = 0; n < M; n++) {
                int kk = S * n;
                if (types[kk] != kCellP2Pilot) continue;
                cf32 v = p2cells[l][off + kk] / pm.pilot(l, kk, kCellP2Pilot).real();
                obs[l][n] = v;
                hg[n] += v;
                cnt[n]++;
            }
        }
        std::vector<char> have(M);
        for (int n = 0; n < M; n++) { have[n] = cnt[n] > 0; if (have[n]) hg[n] /= (float)cnt[n]; }
        for (int n = 0; n < M; n++) {
            if (have[n]) continue;
            int a = n - 1, b = n + 1;
            while (a >= 0 && !have[a]) a--;
            while (b < M && !have[b]) b++;
            if (a >= 0 && b < M) hg[n] = hg[a] + (hg[b] - hg[a]) * ((float)(n - a) / (float)(b - a));
            else if (a >= 0) hg[n] = hg[a];
            else if (b < M) hg[n] = hg[b];
        }
        const int back = std::min(G / 4, 32);
        std::vector<cf32> H;
        interp.run(hg, S, K, N, (double)back + G / 2.0, H);

        // equalised P2 data cells, per symbol in carrier order, then frequency de-interleaved
        const int cP2 = pm.p2DataCells();
        std::vector<std::vector<cf32>> sym(nP2);
        std::vector<cf32> eqAll;
        for (int l = 0; l < nP2; l++) {
            pm.symbolTypes(l, nP2 + 2, types);
            std::vector<cf32> rx;
            rx.reserve(cP2);
            for (int kk = 0; kk < K; kk++)
                if (types[kk] == kCellData) rx.push_back(p2cells[l][off + kk] / H[kk]);
            std::vector<int> Hi;
            freqInterleaverSeq(fftCode, cP2, (l & 1) != 0, Hi);
            sym[l].assign(cP2, cf32(0, 0));
            for (int j = 0; j < cP2 && j < (int)rx.size(); j++) sym[l][Hi[j]] = rx[j];
            eqAll.insert(eqAll.end(), rx.begin(), rx.end());
        }
        std::vector<cf32> stream;
        p2Gather(sym, nP2, cP2, 1840, 0, stream);
        std::vector<cf32> preCells(stream.begin(), stream.begin() + 1840);
        float n0 = 0;
        for (auto& c : preCells) n0 += c.imag() * c.imag();
        n0 = std::max(1e-4f, n0 / 1840.f);
        L1Pre pre;
        L1Result r = decodeL1Pre(preCells, n0, pre);
        bool preOk = r.ok;
        l1Iters = r.ldpcIters;
        L1Post post;
        bool postOk = false;
        if (preOk) {
            std::vector<cf32> st2;
            p2Gather(sym, nP2, cP2, 1840, pre.postSize, st2);
            std::vector<cf32> postCells(st2.begin() + 1840, st2.begin() + 1840 + std::min<size_t>(pre.postSize, st2.size() - 1840));
            L1Result rp = decodeL1Post(postCells, n0, pre, nP2, (pre.s2 & 1) != 0, post);
            postOk = rp.ok;
        }
        if (!preOk && !final) return false;

        // accept this hypothesis: publish channel data
        extDetected = ext;
        chH = H; chK = K; chValid = true;
        l1preOk = preOk; l1postOk = postOk;
        if (preOk) { l1pre = pre; l1preGood++; } else l1preBad++;
        if (postOk) { l1post = post; l1postGood++; } else if (preOk) l1postBad++;
        l1N0 = n0;
        snrDbv.clear();
        float sigAll = 0, noiseAll = 0;
        if (nP2 >= 2) {
            std::vector<float> sig(M), nz(M);
            for (int n = 0; n < M; n++) {
                float v = 0, m2 = std::norm(hg[n]);
                int c = 0;
                for (int l = 0; l < nP2; l++) if (have[n]) { v += std::norm(obs[l][n] - hg[n]); c++; }
                sig[n] = m2;
                nz[n] = c > 1 ? v / (float)(c - 1) : 0.f; // per-observation noise variance (sample variance)
            }
            const int W = 12;
            for (int n = 0; n < M; n++) {
                double sa = 0, na = 0;
                for (int j = std::max(0, n - W); j <= std::min(M - 1, n + W); j++) { sa += sig[j]; na += nz[j]; }
                snrDbv.push_back((float)(10 * std::log10(std::max(1e-9, sa) / std::max(1e-12, na))));
            }
            for (int n = 0; n < M; n++) { sigAll += sig[n]; noiseAll += nz[n]; }
            p2Snr = (float)(10 * std::log10(std::max(1e-9f, sigAll) / std::max(1e-12f, noiseAll)));
        } else {
            // single P2 symbol: use the quadrature spread of the BPSK L1-pre cells
            p2Snr = (float)(10 * std::log10(0.5 / (2.0 * n0 > 1e-9 ? 2 * n0 : 1e-9)));
            (void)fm;
        }
        chMag.clear(); chPh.clear();
        int dec = std::max(1, K / 4096);
        for (int i = 0; i < K; i += dec) {
            chMag.push_back(20 * std::log10(std::max(1e-9f, std::abs(chH[i]))));
            chPh.push_back(std::arg(chH[i]));
        }
        irMin = -std::max(8, G / 4);
        impulseResponse(chH, N, irMin, G + std::max(8, G / 4), back, irDb);
        eqP2.clear();
        size_t step = std::max<size_t>(1, eqAll.size() / 4096);
        for (size_t i = 0; i < eqAll.size(); i += step) eqP2.push_back(eqAll[i]);
        if (preOk) {
            if (pre.guardInterval != giIdx && pre.guardInterval < kNumGi) {
                // the blind guard-interval guess was wrong: adopt the signalled value and resynchronise
                giIdx = pre.guardInterval;
                guard = guardSamples(fftN, giIdx);
                frames.clear();
                frameSyms = 0;
                frameLen = 0;
                prevPos = -1;
                dataValid = false;
                return preOk;
            }
            int total = nP2 + pre.numDataSyms;
            if (total != frameSyms) { frameSyms = total; frameMsv = (kP1Len + (double)total * (fftN + guard)) / fn * 1e3; }
            if (!frames.empty()) frames.front().maxSyms = std::max(frames.front().maxSyms, frameSyms);
        }
        return preOk;
    }

    void runP2Stage() {
        const FftMode* fm = fftModeFromS2(fftCode);
        if (!fm || p2cells.size() != (size_t)nP2) return;
        for (auto& c : p2cells) if ((int)c.size() != kMax) return;
        bool extPossible = fm->kExt != 0;
        bool guess = false;
        if (extPossible) {
            int off = (kMax - fm->kNormal) / 2;
            double edge = 0, mid = 0;
            int ne = 0, nm = 0;
            for (auto& c : p2cells) {
                for (int i = 0; i < off; i++) { edge += std::norm(c[i]) + std::norm(c[kMax - 1 - i]); ne += 2; }
                for (int i = kMax / 2 - 200; i < kMax / 2 + 200; i++) { mid += std::norm(c[i]); nm++; }
            }
            guess = ne && nm && (edge / ne) > 0.25 * (mid / nm);
        }
        // try the more likely carrier mode first; L1-pre CRC decides
        if (!extPossible) { p2Hypothesis(false, true); return; }
        if (p2Hypothesis(guess, false)) return;
        p2Hypothesis(!guess, false) || p2Hypothesis(guess, true);
    }

    // Scattered-pilot channel estimation for the data / frame-closing symbols of the frame just received.
    void runDataStage() {
        const FftMode* fm = fftModeFromS2(fftCode);
        if (!fm || !l1preOk) return;
        const int L = frameSyms, N = fftN, G = guard;
        for (int l = 0; l < L; l++) if ((int)frameCells[l].size() != kMax) return;
        PilotConfig pc;
        pc.fftCode = fftCode; pc.ext = l1pre.bwtExt != 0; pc.pp = l1pre.pilotPattern; pc.tr = (l1pre.papr & 2) != 0; pc.giIdx = giIdx;
        PilotMap pm(pc);
        if (!pm.valid()) { dataValid = false; return; }
        const int K = pm.carriers(), off = (kMax - K) / 2, dx = pm.dx(), dy = pm.dy();
        const int M = (K - 1) / dx + 1;
        struct Obs { int l; cf32 v; uint8_t type; };
        std::vector<std::vector<Obs>> obs(M);
        std::vector<std::vector<uint8_t>> types(L);
        for (int l = 0; l < L; l++) {
            pm.symbolTypes(l, L, types[l]);
            for (int n = 0; n < M; n++) {
                int k = dx * n;
                uint8_t t = types[l][k];
                if (t != kCellP2Pilot && t != kCellScattered && t != kCellContinual) continue;
                cf32 pv = pm.pilot(l, k, t);
                obs[n].push_back({l, frameCells[l][off + k] / pv.real(), t});
            }
        }
        // per-grid-point noise from scattered-pilot second differences
        std::vector<float> varN(M, 0.f), pw(M, 0.f);
        double sumSig = 0, sumNoise = 0;
        for (int n = 0; n < M; n++) {
            std::vector<const Obs*> sc;
            for (auto& o : obs[n]) if (o.type == kCellScattered) sc.push_back(&o);
            double acc = 0, p = 0;
            int c = 0;
            for (size_t j = 1; j + 1 < sc.size(); j++) {
                double w = (double)(sc[j]->l - sc[j - 1]->l) / (double)(sc[j + 1]->l - sc[j - 1]->l);
                cf32 pred = sc[j - 1]->v + (sc[j + 1]->v - sc[j - 1]->v) * (float)w;
                double e = std::norm(sc[j]->v - pred) / (1.0 + w * w + (1 - w) * (1 - w));
                acc += e;
                p += std::norm(sc[j]->v);
                c++;
            }
            if (c) { varN[n] = (float)(acc / c); pw[n] = (float)(p / c); sumSig += pw[n]; sumNoise += varN[n]; }
        }
        const float amp2 = [&] { cf32 pv = pm.pilot(nP2, 0, kCellScattered); return pv.real() * pv.real(); }();
        dataSnrCar.clear();
        const int Wd = 10;
        for (int n = 0; n < M; n++) {
            double sa = 0, na = 0;
            for (int j = std::max(0, n - Wd); j <= std::min(M - 1, n + Wd); j++) { sa += pw[j]; na += varN[j]; }
            dataSnrCar.push_back((float)(10 * std::log10(std::max(1e-9, sa) / std::max(1e-12, na * amp2))));
        }
        dataSnr = (float)(10 * std::log10(std::max(1e-9, sumSig) / std::max(1e-12, sumNoise * amp2)));

        // channel per data symbol: time-interpolate each grid point, then frequency-interpolate
        std::vector<size_t> ptr(M, 0);
        std::vector<cf32> grid(M), H;
        const int back = std::min(G / 4, 32);
        std::vector<cf32> all;
        const int firstData = nP2;
        for (int l = firstData; l < L; l++) {
            for (int n = 0; n < M; n++) {
                auto& o = obs[n];
                if (o.empty()) { grid[n] = n ? grid[n - 1] : cf32(1, 0); continue; }
                while (ptr[n] + 1 < o.size() && o[ptr[n] + 1].l <= l) ptr[n]++;
                const Obs& a = o[ptr[n]];
                if (a.l == l || ptr[n] + 1 >= o.size()) grid[n] = a.v;
                else if (a.l > l) grid[n] = a.v;
                else {
                    const Obs& b = o[ptr[n] + 1];
                    float w = (float)(l - a.l) / (float)(b.l - a.l);
                    grid[n] = a.v + (b.v - a.v) * w;
                }
            }
            interp.run(grid, dx, K, N, (double)back + G / 2.0, H);
            for (int kk = 0; kk < K; kk++)
                if (types[l][kk] == kCellData) all.push_back(frameCells[l][off + kk] / H[kk]);
            if (l == firstData + (L - firstData) / 2) { chH = H; chK = K; } // keep a mid-frame channel snapshot for display
        }
        eqData.clear();
        size_t step = std::max<size_t>(1, all.size() / 6000);
        for (size_t i = 0; i < all.size(); i += step) eqData.push_back(all[i]);
        dataValid = true;
        dataDx = dx; dataDy = dy; dataPp = pc.pp;
        dataFrames++;
        // refresh channel-derived displays from the data-symbol estimate
        chMag.clear(); chPh.clear();
        int dec = std::max(1, K / 4096);
        for (int i = 0; i < K; i += dec) {
            chMag.push_back(20 * std::log10(std::max(1e-9f, std::abs(chH[i]))));
            chPh.push_back(std::arg(chH[i]));
        }
        irMin = -std::max(8, G / 4);
        impulseResponse(chH, N, irMin, G + std::max(8, G / 4), back, irDb);
        snrDbv = dataSnrCar;
        p2Snr = dataSnr;
        (void)fm;
    }

    void processSymbol(int64_t s, int k) {
        const int N = fftN, G = guard;
        if (k == 0) { frameCfo = cfoEst; curAnchor = s; }
        // cyclic-prefix correlation
        auto cpCorr = [&](int64_t st, double& e) {
            cd c = 0;
            e = 0;
            for (int n = 0; n < G; n++) {
                cf32 a = at(st + n), b = at(st + n + N);
                c += cd(a.real(), a.imag()) * std::conj(cd(b.real(), b.imag()));
                e += 0.5 * (std::norm(cd(a.real(), a.imag())) + std::norm(cd(b.real(), b.imag())));
            }
            return c;
        };
        double e;
        cd c = cpCorr(s, e);
        double rho = e > 0 ? std::abs(c) / e : 0;
        cpCorrAvg += 0.1 * (rho - cpCorrAvg);
        if (rho > 0.15) {
            double epsCp = -std::arg(c) * fn / (kTwoPi * N);
            double bin = fn / N;
            double meas = epsCp + std::round((cfoEst - epsCp) / bin) * bin;
            cfoEst += 0.08 * (meas - cfoEst);
            lowCount = 0;
        } else if (++lowCount > 40) {
            state = 0; // lost the signal
            frames.clear();
            lowCount = 0;
            return;
        }
        // timing error from the CP correlation peak
        int D = std::min(8, std::max(2, G / 8));
        double mags[17];
        int best = D;
        for (int dd = -D; dd <= D; dd++) {
            double ee;
            cd cc = cpCorr(s + dd, ee);
            mags[dd + D] = ee > 0 ? std::abs(cc) / ee : 0;
            if (mags[dd + D] > mags[best]) best = dd + D;
        }
        double off = best - D;
        if (best > 0 && best < 2 * D) {
            double a = mags[best - 1], b = mags[best], cc = mags[best + 1];
            double den = a - 2 * b + cc;
            if (den < 0) off += 0.5 * (a - cc) / den;
        }
        timingAvg += 0.05 * (off - timingAvg);
        if (std::fabs(timingAvg) > 6 && rho > 0.3) { gridOff += (int64_t)std::llround(timingAvg); timingAvg = 0; }

        if (k < nP2) {
            if ((int)p2cells.size() != nP2) p2cells.assign(nP2, {});
            fftCells(s, p2cells[k], kMax);
            if (k == nP2 - 1) runP2Stage();
        }
        if (l1preOk && frameSyms > 0 && k < frameSyms && k >= 0) {
            if ((int)frameCells.size() != frameSyms) frameCells.assign(frameSyms, {});
            if (k >= nP2) fftCells(s, frameCells[k], kMax);
            else frameCells[k] = p2cells[k];
            if (k == frameSyms - 1) runDataStage();
        }
        symbols++;
        symCounter++;
        // display: FFT consecutive symbol pairs, at a limited rate
        int stride = std::max(2, (int)std::lround(fn / (N + G) / 100.0));
        int phase = (int)(symCounter % stride);
        if (phase > 1) return;
        int back = std::min(G / 4, 32);
        int64_t w0 = s + G - back;
        fr.assign(N, 0.f); fi.assign(N, 0.f);
        double ph0 = -kTwoPi * cfoEst * (double)(w0 - s) / fn;
        double dph = -kTwoPi * cfoEst / fn;
        for (int n = 0; n < N; n++) {
            cf32 x = at(w0 + n);
            double a = ph0 + dph * n;
            float cs = (float)std::cos(a), sn = (float)std::sin(a);
            fr[n] = x.real() * cs - x.imag() * sn;
            fi[n] = x.real() * sn + x.imag() * cs;
        }
        doFft((int)std::lround(std::log2((double)N)));
        int K = carriers;
        std::vector<cf32> cells(K);
        double pw = 0;
        for (int kk = 0; kk < K; kk++) {
            int f = kk - (K - 1) / 2;
            int b = (f + N) % N;
            cells[kk] = cf32(fr[b], fi[b]);
            pw += std::norm(cells[kk]);
        }
        float nrm = (float)std::sqrt(std::max(1e-20, pw / K));
        for (auto& cl : cells) cl /= nrm;
        const size_t maxPts = 4096;
        size_t step = std::max<size_t>(1, K / maxPts);
        rawCells.clear();
        for (size_t i = 0; i < (size_t)K; i += step) rawCells.push_back(cells[i]);
        if (phase == 1 && prevCells.size() == (size_t)K) {
            diffCells.clear();
            for (size_t i = 0; i < (size_t)K; i += step) diffCells.push_back(cells[i] * std::conj(prevCells[i]));
        }
        prevCells = std::move(cells);
        (void)k;
    }

    // ------------------------------------------------------------ driver
    void run() {
        for (int guardLoop = 0; guardLoop < 8; guardLoop++) {
            scanP1();
            if (state == 1) evaluateGi();
            if (state == 2) processFrames();
            if (state == 2 && frames.empty() && lastP1Seen > 0 && end() - lastP1Seen > (int64_t)(std::max(frameLen, 0.35 * fn) * 3 + 2 * fn * 0.05)) {
                state = 0; // no P1 for a long while
            }
            // limit memory
            int64_t need = scanPos;
            if (state == 1) need = std::min(need, giAnchor);
            if (!frames.empty()) need = std::min(need, frames.front().anchor);
            int64_t drop = need - base - 4096;
            if (drop > (1 << 20)) {
                buf.erase(buf.begin(), buf.begin() + drop);
                base += drop;
            }
            break;
        }
    }

    void publish() {
        std::lock_guard<std::mutex> lk(mu);
        RxTelemetry& t = tel;
        t.seq++;
        t.rateOk = rateOk;
        t.decimating = decimate;
        t.inputRate = inRate;
        t.nativeRate = fn;
        t.state = state;
        t.p1 = p1;
        t.p1Count = p1Count;
        t.secSinceP1 = lastP1Seen ? (double)(end() - lastP1Seen) / fn : 1e9;
        t.frameMs = frameMsv;
        t.symbolsPerFrame = frameSyms;
        t.sroPpm = sro * 1e6;
        std::copy(giScore, giScore + kNumGi, t.giScore);
        t.giIdx = giIdx;
        t.giMargin = giMargin;
        t.cfoHz = state >= 1 ? cfoEst : p1.cfoHz;
        t.cpCorr = (float)cpCorrAvg;
        double r = std::min(0.999, std::max(0.001, cpCorrAvg));
        t.cpSnrDb = (float)(10 * std::log10(r / (1 - r)));
        t.timingErr = (float)timingAvg;
        t.symbols = symbols;
        t.fftN = fftN;
        t.guard = guard;
        t.carriers = carriers;
        t.p1Trace = trace;
        t.p1Const = p1Const;
        t.cells = diffCells;
        t.rawCells = rawCells;
        t.chValid = chValid;
        t.extCarriers = extDetected;
        t.chCarriers = chK;
        t.chMagDb = chMag;
        t.chPhase = chPh;
        t.chDecim = chK > 0 ? std::max(1, chK / 4096) : 1;
        t.irDb = irDb;
        t.irTauMin = irMin;
        t.snrDb = snrDbv;
        t.snrStep = fftCode == 5 ? 6 : 3;
        t.p2SnrDb = p2Snr;
        t.eqCells = eqP2;
        t.l1preOk = l1preOk; t.l1postOk = l1postOk;
        t.l1pre = l1pre; t.l1post = l1post;
        t.l1preGood = l1preGood; t.l1preBad = l1preBad; t.l1postGood = l1postGood; t.l1postBad = l1postBad;
        t.l1Iters = l1Iters;
        t.dataValid = dataValid;
        t.dataPp = dataPp; t.dataDx = dataDx; t.dataDy = dataDy;
        t.dataSnrDb = dataSnr;
        t.eqData = eqData;
        t.dataSnrCarrier = dataSnrCar;
        t.dataFrames = dataFrames;
        if (dataValid) t.snrStep = dataDx;
    }
};

T2Receiver::T2Receiver() : p_(new Impl) { p_->designDecimator(); }
T2Receiver::~T2Receiver() = default;

void T2Receiver::configure(double inputRateHz, double bandwidthMhz) {
    Impl& I = *p_;
    std::lock_guard<std::mutex> lk(I.mu);
    I.inRate = inputRateHz;
    I.fn = nativeRateHz(bandwidthMhz);
    double r = inputRateHz / I.fn;
    I.decimate = false;
    I.rateOk = true;
    if (std::fabs(r - 1.0) < 0.01) {}
    else if (std::fabs(r - 2.0) < 0.02) I.decimate = true;
    else I.rateOk = false;
    I.resetAll();
}

void T2Receiver::reset() {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->resetAll();
}

void T2Receiver::feed(const cf32* x, size_t n) {
    Impl& I = *p_;
    if (!I.rateOk || I.fn <= 0) return;
    if (I.decimate) I.decimateAppend(x, n);
    else I.buf.insert(I.buf.end(), x, x + n);
    I.run();
}

bool T2Receiver::telemetry(RxTelemetry& out, uint64_t lastSeq) {
    Impl& I = *p_;
    I.publish();
    std::lock_guard<std::mutex> lk(I.mu);
    if (I.tel.seq <= lastSeq) return false;
    out = I.tel;
    return true;
}

} // namespace dect2
