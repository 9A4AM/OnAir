// GOES HRIT receiver (goes_rx.h).
#include "dect2/goes_rx.h"
#include "dect2/results_io.h"
#include "dect2/fftutil.h"
#include "dect2/goes_proto.h"
#include "dect2/goes_tel.h"
#include "dect2/meteor_proto.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>

namespace dect2 {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kRolloff = 0.5;
constexpr double kGoesSymbolRate = 927000.0;
constexpr int kFrameSyms = 2 * 8 * meteor::kCaduBytes;    // 16384 code symbols a frame
constexpr int kSyncFrames = 3;                             // frames the sync search looks over
constexpr int kAcqFft = 4096, kAcqBlocks = 8;

double rrc(double t, double a) {
    if (std::fabs(t) < 1e-9) return 1 - a + 4 * a / kPi;
    if (std::fabs(std::fabs(t) - 1 / (4 * a)) < 1e-9)
        return a / std::sqrt(2.0) * ((1 + 2 / kPi) * std::sin(kPi / (4 * a)) + (1 - 2 / kPi) * std::cos(kPi / (4 * a)));
    return (std::sin(kPi * t * (1 - a)) + 4 * a * t * std::cos(kPi * t * (1 + a))) / (kPi * t * (1 - (4 * a * t) * (4 * a * t)));
}

// complex FIR with the history kept twice over, so every output is one contiguous dot product
struct Fir {
    std::vector<float> h;
    std::vector<cf32> buf;
    size_t pos = 0;
    void set(std::vector<float> taps) { h = std::move(taps); buf.assign(2 * h.size(), cf32(0, 0)); pos = 0; }
    void push(cf32 x) { buf[pos] = x; buf[pos + h.size()] = x; pos = (pos + 1) % h.size(); }
    cf32 out() const {
        const cf32* b = buf.data() + pos;   // oldest first
        float re = 0, im = 0;
        const size_t n = h.size();
        for (size_t i = 0; i < n; i++) { re += h[n - 1 - i] * b[i].real(); im += h[n - 1 - i] * b[i].imag(); }
        return cf32(re, im);
    }
    void clear() { std::fill(buf.begin(), buf.end(), cf32(0, 0)); pos = 0; }
};

inline cf32 cubic(const cf32* y, double t) {
    const int i = (int)std::floor(t);
    const float f = (float)(t - i);
    const cf32 p0 = y[i - 1], p1 = y[i], p2 = y[i + 1], p3 = y[i + 2];
    // Catmull-Rom
    const cf32 a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    const cf32 b = p0 - 2.5f * p1 + 2.f * p2 - 0.5f * p3;
    const cf32 c = -0.5f * p0 + 0.5f * p2;
    return ((a * f + b) * f + c) * f + p1;
}

void loopGains(double bnT, double zeta, double& alpha, double& beta) {
    const double th = bnT / (zeta + 1 / (4 * zeta)), d = 1 + 2 * zeta * th + th * th;
    alpha = 4 * zeta * th / d;
    beta = 4 * th * th / d;
}
} // namespace

struct GoesReceiver::Impl {
    std::mutex mu;
    GoesTelemetry pub;
    std::function<void(const std::string&)> logCb;
    std::function<void(const std::vector<uint8_t>&, int, int)> fileCb;
    std::atomic<bool> resetReq{false}, clearReq{false};
    bool loadReq = false;                // loaded products (mu): the feed thread hands them to the assembler
    std::vector<std::shared_ptr<goes::Product>> loadIn;
    std::atomic<double> offset{0};
    double rateSet = 0;
    double symbolRate = kGoesSymbolRate;

    // front end
    double rate = 0, fs2 = 0, sps = 0;
    int D = 1, decPhase = 0;
    Fir lpf, mf;
    double appliedOffset = 0;
    double cfo = 0;                       // coarse carrier offset (Hz) the mixer takes out
    std::complex<double> nco{1, 0}, rot{1, 0};
    int ncoCount = 0;
    // timing
    std::vector<cf32> y;
    double pos = 0, spsEst = 0;
    cf32 yprev{0, 0};
    float agc = 1;
    double tAlpha = 0, tBeta = 0;
    // carrier
    double cPhase = 0, cFreq = 0, cAlpha = 0, cBeta = 0;
    float lockM = 0, meanAbs = 0.5f, meanIm2 = 0.1f;
    bool acq = true, carrierLock = false;
    std::vector<cf32> acqBuf;
    std::vector<float> acqSpec;
    int acqBlocks = 0;
    int64_t sinceAcq = 0, lowLock = 0;
    uint64_t okAtAcq = 0;
    std::vector<double> rejected;         // carrier offsets tried that did not lock (a DC spike, a symbol rate line): skipped next time
    std::unique_ptr<Fft> fft;
    // symbols -> code
    int64_t symIdx = 0;
    std::vector<float> syncBuf;           // soft symbols (> 0 = code bit 1) for the sync search
    int64_t syncBase = 0;                 // symbol index of syncBuf[0]
    int64_t nextSearch = 0;
    bool symSync = false, g2inv = true;
    int alignParity = 0;
    float firstOfPair = 0;
    meteor::Viterbi27 vit;
    uint64_t asmPattern = 0;
    // bits -> frames
    int fstate = 0;                       // 0 search, 1 collect, 2 check the sync word
    uint32_t reg = 0;
    int regBits = 0, collected = 0, misses = 0;
    bool invBits = false;
    uint8_t frame[meteor::kCodedBytes];
    int64_t lastGoodSym = 0;
    goes::Assembler asmb;
    // counters
    uint64_t frames = 0, ok = 0, bad = 0, corrected = 0;
    int64_t nIn = 0;
    double nextReport = 0;
    double levelAcc = 0;
    int64_t levelN = 0;
    std::deque<cf32> constel;
    std::deque<float> snrH, errH, rateH;
    uint64_t lastOk = 0, lastCorr = 0, lastFrames = 0;
    std::deque<std::string> logLines;
    bool frameLockReported = false, carrierReported = false;
    uint64_t seq = 0;

    Impl() {
        asmPattern = meteor::encodedAsm();
        asmb.onLog = [this](const std::string& s) { addLog(s, false); };
        asmb.onFile = [this](const std::vector<uint8_t>& f, int vc, int ap) {
            std::function<void(const std::vector<uint8_t>&, int, int)> cb;
            { std::lock_guard<std::mutex> lk(mu); cb = fileCb; }
            if (cb) cb(f, vc, ap);
        };
    }

    void addLog(const std::string& s, bool engine) {
        logLines.push_front(s);
        while (logLines.size() > kGoesLogMax) logLines.pop_back();
        if (engine) {
            std::function<void(const std::string&)> cb;
            { std::lock_guard<std::mutex> lk(mu); cb = logCb; }
            if (cb) cb(s);
        }
    }

    void setup(double r) {
        rate = r;
        D = std::max(1, (int)std::floor(r / (2.3 * symbolRate)));
        fs2 = r / D;
        sps = fs2 / symbolRate;
        if (D > 1) {
            const int n = 8 * D + 1;
            std::vector<float> h((size_t)n);
            const double fc = 0.85 * symbolRate / r;
            double sum = 0;
            for (int i = 0; i < n; i++) {
                const double m = i - (n - 1) / 2.0, w = 0.54 - 0.46 * std::cos(2 * kPi * i / (n - 1));
                h[(size_t)i] = (float)(w * (std::fabs(m) < 1e-9 ? 2 * fc : std::sin(2 * kPi * fc * m) / (kPi * m)));
                sum += h[(size_t)i];
            }
            for (auto& v : h) v = (float)(v / sum);
            lpf.set(h);
        }
        const int half = (int)std::ceil(6 * sps);
        std::vector<float> h((size_t)(2 * half + 1));
        double e = 0;
        for (int i = -half; i <= half; i++) { h[(size_t)(i + half)] = (float)rrc(i / sps, kRolloff); e += h[(size_t)(i + half)] * h[(size_t)(i + half)]; }
        for (auto& v : h) v = (float)(v / std::sqrt(e));
        mf.set(h);
        fft = std::make_unique<Fft>(kAcqFft);
        loopGains(0.004, 0.707, tAlpha, tBeta);
        resetState();
    }

    void setRot() {
        const double w = -2 * kPi * (appliedOffset + cfo) / rate;
        rot = std::complex<double>(std::cos(w), std::sin(w));
    }

    void resetCarrier() { cPhase = 0; cFreq = 0; lockM = 0; double a, b; loopGains(0.01, 0.707, a, b); cAlpha = a; cBeta = b; }

    void resetFec() {
        symSync = false; vit.reset(); vit.out().clear();
        fstate = 0; reg = 0; regBits = 0; collected = 0; misses = 0;
        syncBuf.clear(); syncBase = symIdx; nextSearch = symIdx + (int64_t)kSyncFrames * kFrameSyms + 64;
    }

    void resetState() {
        cfo = 0; appliedOffset = offset.load(); setRot(); nco = 1;
        lpf.clear(); mf.clear(); decPhase = 0;
        y.clear(); pos = 2 + sps; spsEst = sps; yprev = 0; agc = 1;
        resetCarrier();
        rejected.clear();
        acq = true; carrierLock = false; acqBuf.clear(); acqSpec.assign(kAcqFft, 0.f); acqBlocks = 0; sinceAcq = 0; lowLock = 0;
        meanAbs = 0.5f; meanIm2 = 0.1f;
        resetFec();
        asmb.reset();
        frameLockReported = carrierReported = false;
    }

    // ---- coarse carrier from the squared signal
    void acquire(cf32 v) {
        acqBuf.push_back(v * v);
        if ((int)acqBuf.size() < kAcqFft) return;
        fft->forward(acqBuf.data());
        for (int i = 0; i < kAcqFft; i++) acqSpec[(size_t)i] += std::norm(acqBuf[(size_t)i]);
        acqBuf.clear();
        if (++acqBlocks < kAcqBlocks) return;
        // the strongest line, leaving out offsets that were tried and did not lock (the radio's DC spike, the lines a symbol rate
        // either side of the carrier's)
        if (rejected.size() > 6) rejected.clear();
        const double binHz = fs2 / kAcqFft / 2;
        int best = -1;
        float bv = 0;
        double mean = 0;
        for (int i = 0; i < kAcqFft; i++) {
            mean += acqSpec[(size_t)i];
            const double cand = cfo + (i < kAcqFft / 2 ? i : i - kAcqFft) * binHz;
            bool skip = false;
            for (double r : rejected) skip |= std::fabs(cand - r) < 4 * binHz;
            if (skip) continue;
            if (acqSpec[(size_t)i] > bv) { bv = acqSpec[(size_t)i]; best = i; }
        }
        mean /= kAcqFft;
        if (best >= 0 && bv > 12 * mean) {
            const float l = acqSpec[(size_t)((best + kAcqFft - 1) % kAcqFft)], c = acqSpec[(size_t)best], r = acqSpec[(size_t)((best + 1) % kAcqFft)];
            const float den = l - 2 * c + r;
            const double frac = std::fabs(den) > 1e-20f ? 0.5 * (l - r) / den : 0;
            const double k = (best < kAcqFft / 2 ? best : best - kAcqFft) + frac;
            cfo += k * fs2 / kAcqFft / 2;
            setRot();
            resetCarrier();
            acq = false; sinceAcq = 0; okAtAcq = ok;
        }
        std::fill(acqSpec.begin(), acqSpec.end(), 0.f);
        acqBlocks = 0;
    }

    // ---- a symbol out of the timing loop
    void symbol(cf32 v) {
        const cf32 z = v * cf32((float)std::cos(-cPhase), (float)std::sin(-cPhase));
        const float re = z.real(), im = z.imag();
        // carrier loop
        const float e = (re >= 0 ? im : -im);
        cPhase += cAlpha * e + cFreq;
        cFreq += cBeta * e;
        if (cPhase > kPi) cPhase -= 2 * kPi; else if (cPhase < -kPi) cPhase += 2 * kPi;
        const float p = re * re + im * im + 1e-12f;
        lockM += 0.002f * ((re * re - im * im) / p - lockM);
        meanAbs += 0.002f * (std::fabs(re) - meanAbs);
        meanIm2 += 0.002f * (im * im - meanIm2);
        constel.push_back(z);
        if (constel.size() > 512) constel.pop_front();
        if (!acq) {
            sinceAcq++;
            if (sinceAcq == 20000) { double a, b; loopGains(0.004, 0.707, a, b); cAlpha = a; cBeta = b; }
            if (sinceAcq > 30000) {
                const bool l = lockM > 0.35f;
                if (l != carrierLock) {
                    carrierLock = l;
                    if (l && !carrierReported) { carrierReported = true; char b[96]; snprintf(b, sizeof b, "GOES HRIT: carrier locked, offset %.1f kHz", carrierHz() / 1000); addLog(b, true); }
                }
                lowLock = lockM < 0.2f ? lowLock + 1 : 0;
                if (lowLock > 60000) { acq = true; carrierLock = false; lowLock = 0; resetFec(); }
                // never locked to this one: try another line
                if (!carrierLock && ok == okAtAcq && sinceAcq > 120000) { rejected.push_back(cfo); acq = true; resetFec(); }
            }
        }
        // soft code bit: BPSK +1 is code bit 0
        const float s = -re;
        syncBuf.push_back(s);
        const int64_t k = symIdx++;
        if (symSync) {
            if (((k - alignParity) & 1) == 0) firstOfPair = s;
            else {
                const float sc = 40.f / std::max(0.05f, meanAbs);
                const int a = (int)std::max(-127.f, std::min(127.f, firstOfPair * sc));
                int b = (int)std::max(-127.f, std::min(127.f, s * sc));
                if (g2inv) b = -b;
                vit.push(a, b);
                auto& o = vit.out();
                if (!o.empty()) { for (uint8_t bit : o) frameBit(bit); o.clear(); }
            }
        }
        if (symIdx >= nextSearch) syncSearch();
    }

    double minInputRate() const { return std::max(200000.0, 2.1 * symbolRate); }
    double carrierHz() const { return cfo + cFreq / (2 * kPi) * symbolRate; }

    // ---- where the frames start in the code symbols, the pairing and the G2 polarity
    void syncSearch() {
        const int64_t need = (int64_t)kSyncFrames * kFrameSyms + 64;
        if ((int64_t)syncBuf.size() > need) { syncBuf.erase(syncBuf.begin(), syncBuf.end() - need); syncBase = symIdx - need; }
        nextSearch = symIdx + (int64_t)kFrameSyms * (fstate == 0 ? 2 : 4);
        if (fstate != 0 || (int64_t)syncBuf.size() < need || acq) return;
        // the 52 code bits of the sync word that do not depend on the encoder state, as +-1 for each G2 polarity
        float pat[2][64];
        for (int j = 0; j < 64; j++) {
            const int bit = (int)((asmPattern >> (63 - j)) & 1);
            pat[0][j] = bit ? 1.f : -1.f;
            pat[1][j] = (bit ^ (j & 1)) ? 1.f : -1.f;
        }
        double norm = 0;
        for (float v : syncBuf) norm += std::fabs(v);
        norm /= (double)syncBuf.size();
        double best = 0;
        int bo = 0, bg = 0;
        for (int o = 0; o < kFrameSyms; o++)
            for (int g = 0; g < 2; g++) {
                double sc = 0;
                for (int m = 0; m < kSyncFrames; m++) {
                    const float* s = syncBuf.data() + o + (size_t)m * kFrameSyms;
                    float c = 0;
                    for (int j = 12; j < 64; j++) c += s[j] * pat[g][j];
                    sc += std::fabs(c);
                }
                if (sc > best) { best = sc; bo = o; bg = g; }
            }
        const double q = best / (kSyncFrames * 52.0 * std::max(1e-9, norm));
        if (q < 0.45) return;
        const int64_t start = syncBase + bo;
        symSync = true;
        g2inv = bg == 1;
        alignParity = (int)(start & 1);
        vit.reset(); vit.out().clear();
        fstate = 0; reg = 0; regBits = 0;
        // run the Viterbi decoder over what is buffered from a little before the newest sync word on, so this frame is not lost
        const int64_t from = std::max(syncBase, start + (int64_t)(kSyncFrames - 1) * kFrameSyms - 256);
        const float sc = 40.f / std::max(0.05f, meanAbs);
        int64_t k = from;
        if (((k - alignParity) & 1) != 0) k++;
        for (; k + 1 < symIdx; k += 2) {
            const float a = syncBuf[(size_t)(k - syncBase)], b = syncBuf[(size_t)(k + 1 - syncBase)];
            int ia = (int)std::max(-127.f, std::min(127.f, a * sc)), ib = (int)std::max(-127.f, std::min(127.f, b * sc));
            if (g2inv) ib = -ib;
            vit.push(ia, ib);
        }
        if (k < symIdx) firstOfPair = syncBuf[(size_t)(k - syncBase)];
        auto& o = vit.out();
        for (uint8_t bit : o) frameBit(bit);
        o.clear();
    }

    // ---- decoded bits: the sync word and the 1020 bytes behind it
    void frameBit(uint8_t bit) {
        if (fstate == 0) {
            reg = reg << 1 | bit;
            if (++regBits < 32) return;
            const int e = __builtin_popcount(reg ^ meteor::kAsm), ei = __builtin_popcount(reg ^ ~meteor::kAsm);
            if (e <= 3 || ei <= 3) { invBits = ei < e; fstate = 1; collected = 0; misses = 0; memset(frame, 0, sizeof frame); }
            return;
        }
        if (fstate == 1) {
            if (bit ^ (invBits ? 1 : 0)) frame[collected >> 3] |= (uint8_t)(0x80 >> (collected & 7));
            if (++collected == meteor::kCodedBytes * 8) { doFrame(); fstate = 2; reg = 0; regBits = 0; }
            return;
        }
        reg = reg << 1 | bit;
        if (++regBits < 32) return;
        const int e = __builtin_popcount(reg ^ (invBits ? ~meteor::kAsm : meteor::kAsm));
        const int ei = __builtin_popcount(reg ^ (invBits ? meteor::kAsm : ~meteor::kAsm));
        if (ei <= 3) { invBits = !invBits; misses = 0; }       // a 180 degree slip of the carrier loop
        else if (e <= 6) misses = 0;
        else if (++misses > 3) { fstate = 0; regBits = 0; reg = 0; return; }
        fstate = 1; collected = 0; memset(frame, 0, sizeof frame);
    }

    void doFrame() {
        frames++;
        uint8_t v[meteor::kVcduBytes];
        const int r = meteor::decodeCadu(frame, v);
        if (r < 0) { bad++; return; }
        ok++;
        corrected += (uint64_t)r;
        lastGoodSym = symIdx;
        if (!frameLockReported) { frameLockReported = true; addLog("GOES HRIT: frames found, decoding", true); }
        asmb.push(v, (double)nIn / std::max(1.0, rate));
    }

    // ---- input
    void timing() {
        // symbols at pos (fractional index into y), Gardner's error from the sample half a symbol earlier
        const double half = spsEst / 2;
        while (pos + 3 < (double)y.size()) {
            if (pos - half - 2 < 0) { pos += 1; continue; }
            const cf32 yk = cubic(y.data(), pos), ym = cubic(y.data(), pos - half);
            agc += 0.001f * (std::norm(yk) - agc);
            const float g = 1.f / std::sqrt(std::max(1e-12f, agc));
            const cf32 a = yk * g, m = ym * g;
            const float e = ((yprev - a) * std::conj(m)).real();
            yprev = a;
            spsEst += tBeta * e;
            spsEst = std::max(sps * 0.995, std::min(sps * 1.005, spsEst));
            pos += spsEst + tAlpha * e * sps;
            symbol(a);
        }
        const int drop = (int)std::floor(pos - spsEst - 4);
        if (drop > 0 && drop <= (int)y.size()) { y.erase(y.begin(), y.begin() + drop); pos -= drop; }
    }

    void feed(const cf32* x, size_t n) {
        y.reserve(y.size() + n / D + 8);
        const double off = offset.load();
        if (off != appliedOffset) { appliedOffset = off; setRot(); }
        for (size_t i = 0; i < n; i++) {
            nco *= rot;
            if (++ncoCount >= 1024) { nco /= std::abs(nco); ncoCount = 0; }
            const cf32 v = x[i] * cf32((float)nco.real(), (float)nco.imag());
            levelAcc += std::norm(x[i]); levelN++;
            cf32 d = v;
            if (D > 1) {
                lpf.push(v);
                if (++decPhase < D) continue;
                decPhase = 0;
                d = lpf.out();
            }
            mf.push(d);
            const cf32 o = mf.out();
            y.push_back(o);
            if (acq) acquire(o * (1.f / std::sqrt(std::max(1e-12f, agc))));
        }
        timing();
    }

    void report() {
        GoesTelemetry t;
        const bool decoding = ok && symIdx - lastGoodSym < 4 * kFrameSyms;
        t.state = decoding ? 3 : fstate != 0 ? 2 : carrierLock ? 1 : 0;
        t.cfoHz = carrierHz();
        const float n2 = std::max(1e-9f, 2 * meanIm2);
        t.snrDb = (float)(10 * std::log10(std::max(1e-6f, meanAbs * meanAbs / n2)));
        t.levelDb = levelN ? (float)(10 * std::log10(std::max(1e-20, levelAcc / levelN))) : -200.f;
        levelAcc = 0; levelN = 0;
        t.dataValid = t.state >= 3;
        t.blocksOk = ok; t.blocksBad = bad;
        t.timeSec = nIn / std::max(1.0, rate);
        t.inputRate = rate;
        t.symbolRate = fs2 / std::max(1e-9, spsEst);
        t.carrierLock = carrierLock || decoding;
        t.frameLock = fstate != 0;
        t.invertedG2 = g2inv;
        t.lockMetric = std::max(0.f, lockM);
        const uint64_t df = frames - lastFrames, dc = corrected - lastCorr, dok = ok - lastOk;
        t.rsByteErrors = df ? (float)dc / (float)(df * meteor::kCodedBytes) : 0.f;
        t.frames = frames; t.rsCorrected = corrected;
        t.vcdus = asmb.vcdus; t.fillVcdus = asmb.fillVcdus; t.vcduGaps = asmb.vcduGaps; t.packets = asmb.packets; t.crcErrors = asmb.crcErrors;
        t.files = asmb.files; t.fileErrors = asmb.fileErrors; t.scid = asmb.scid;
        for (int v = 0; v < 63; v++) if (asmb.perVcid[v]) t.vcids.emplace_back(v, asmb.perVcid[v]);
        t.constellation.assign(constel.begin(), constel.end());
        auto push = [](std::deque<float>& h, float v) { h.push_back(v); if (h.size() > kGoesHist) h.pop_front(); };
        push(snrH, t.snrDb); push(errH, t.rsByteErrors * 100.f); push(rateH, (float)dok * 4.f);
        lastFrames = frames; lastCorr = corrected; lastOk = ok;
        t.snrHist.assign(snrH.begin(), snrH.end());
        t.errHist.assign(errH.begin(), errH.end());
        t.rateHist.assign(rateH.begin(), rateH.end());
        t.products = asmb.products();
        t.log.assign(logLines.begin(), logLines.end());
        std::lock_guard<std::mutex> lk(mu);
        t.seq = ++seq;
        pub = std::move(t);
    }
};

GoesReceiver::GoesReceiver() : p_(std::make_unique<Impl>()) {}
GoesReceiver::~GoesReceiver() = default;

void GoesReceiver::configure(double r) {
    Impl& m = *p_;
    { std::lock_guard<std::mutex> lk(m.mu); m.rateSet = r; }
    if (r <= 0) return;
    m.setup(r);
    m.nIn = 0; m.nextReport = 0.25 * r;
}
void GoesReceiver::setSymbolRate(double hz) {
    if (!(hz > 1000)) hz = kGoesSymbolRate;
    p_->symbolRate = hz;
    const double r = p_->rateSet;
    if (r > 0) configure(r);
}
double GoesReceiver::symbolRate() const { return p_->symbolRate; }
void GoesReceiver::setSignalOffset(double hz) { p_->offset = hz; }
bool GoesReceiver::ready() const { std::lock_guard<std::mutex> lk(p_->mu); return p_->rateSet >= p_->minInputRate() - 1; }
void GoesReceiver::reset() { p_->resetReq = true; }
void GoesReceiver::clearProducts() {
    { std::lock_guard<std::mutex> lk(p_->mu); p_->pub.products.clear(); p_->pub.log.clear(); p_->loadReq = false; p_->loadIn.clear(); p_->pub.seq++; }
    p_->clearReq = true;
}

namespace {
// a product as the results file holds it (Product itself carries a mutex)
struct GoesProductRec {
    uint64_t id = 0;
    int kind = 0, vcid = 0, apid = 0;
    goes::Headers hdr;
    std::string name;
    double startSec = 0;
    int w = 0, h = 0;
    std::vector<uint8_t> pix, rowDone;
    int rowsDone = 0, segsDone = 0, segsTotal = 0;
    std::vector<uint8_t> data;
    std::string text, innerName;
    bool complete = false, errors = false;
    uint64_t version = 0, bytes = 0;
};
constexpr size_t kGoesSaveBytes = 48u << 20;   // the newest products up to this many bytes of pictures and data go into the file
} // namespace

// the products already stay through reset() and configure(): only Clear empties them
void GoesReceiver::clearResults() { clearProducts(); }
std::string GoesReceiver::saveResults() const {
    std::vector<std::shared_ptr<goes::Product>> list;
    { std::lock_guard<std::mutex> lk(p_->mu); list = p_->loadReq ? p_->loadIn : p_->pub.products; }
    std::vector<GoesProductRec> v;
    size_t total = 0;
    for (const auto& q : list) {
        if (!q) continue;
        GoesProductRec r;
        r.id = q->id; r.kind = q->kind; r.vcid = q->vcid; r.apid = q->apid; r.hdr = q->hdr; r.name = q->name; r.startSec = q->startSec;
        {
            std::lock_guard<std::mutex> pl(q->mu);
            r.w = q->w; r.h = q->h; r.pix = q->pix; r.rowDone = q->rowDone; r.rowsDone = q->rowsDone; r.segsDone = q->segsDone; r.segsTotal = q->segsTotal;
            r.data = q->data; r.text = q->text; r.innerName = q->innerName; r.complete = q->complete; r.errors = q->errors; r.version = q->version; r.bytes = q->bytes;
        }
        total += r.pix.size() + r.data.size() + r.text.size();
        if (total > kGoesSaveBytes && !v.empty()) break;
        v.push_back(std::move(r));
    }
    return results::pack(v);
}
bool GoesReceiver::loadResults(const std::string& blob) {
    std::vector<GoesProductRec> v;
    if (!results::unpack(blob, v)) return false;
    std::vector<std::shared_ptr<goes::Product>> list;
    for (auto& r : v) {
        auto q = std::make_shared<goes::Product>();
        q->id = r.id; q->kind = r.kind; q->vcid = r.vcid; q->apid = r.apid; q->hdr = r.hdr; q->name = r.name; q->startSec = r.startSec;
        q->w = r.w; q->h = r.h; q->pix = std::move(r.pix); q->rowDone = std::move(r.rowDone); q->rowsDone = r.rowsDone; q->segsDone = r.segsDone; q->segsTotal = r.segsTotal;
        q->data = std::move(r.data); q->text = std::move(r.text); q->innerName = std::move(r.innerName); q->complete = r.complete; q->errors = r.errors; q->version = r.version; q->bytes = r.bytes;
        list.push_back(std::move(q));
    }
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->pub.products = list;
    p_->loadIn = std::move(list); p_->loadReq = true;
    p_->pub.seq++;
    return true;
}

void GoesReceiver::feed(const cf32* x, size_t n) {
    Impl& m = *p_;
    if (m.resetReq.exchange(false) && m.rate > 0) m.resetState();
    {
        std::lock_guard<std::mutex> lk(m.mu);
        if (m.loadReq) { m.asmb.adopt(std::move(m.loadIn)); m.loadIn.clear(); m.loadReq = false; }
    }
    if (m.clearReq.exchange(false)) {
        m.asmb.clear(); m.logLines.clear();
        m.frames = m.ok = m.bad = m.corrected = 0; m.lastFrames = m.lastCorr = m.lastOk = 0;
    }
    if (m.rate <= 0 || n == 0) return;
    // in pieces of at most a quarter second, so the reports keep their pace
    size_t i = 0;
    while (i < n) {
        const size_t left = (size_t)std::max<int64_t>(1, (int64_t)m.nextReport - m.nIn);
        const size_t k = std::min(n - i, left);
        if (m.rate >= m.minInputRate() - 1) m.feed(x + i, k);
        m.nIn += (int64_t)k;
        i += k;
        if ((double)m.nIn >= m.nextReport) { m.nextReport += 0.25 * m.rate; m.report(); }
    }
}

bool GoesReceiver::telemetry(GoesTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->pub.seq <= lastSeq) return false;
    out = p_->pub;
    return true;
}
void GoesReceiver::setLogCallback(std::function<void(const std::string&)> cb) { std::lock_guard<std::mutex> lk(p_->mu); p_->logCb = std::move(cb); }
void GoesReceiver::setFileCallback(std::function<void(const std::vector<uint8_t>&, int, int)> cb) { std::lock_guard<std::mutex> lk(p_->mu); p_->fileCb = std::move(cb); }

ModeTuning goesTuning() {
    ModeTuning t;
    t.stdMode = 62; t.id = "goes"; t.name = "GOES HRIT";
    t.minMhz = 1670; t.maxMhz = 1710; t.defMhz = 1694.1;   // GOES-R series HRIT / EMWIN downlink
    t.sampleRate = 2400000;                               // RTL-SDR friendly; 2.6 samples a symbol
    t.basebandHz = 0;
    t.bandwidthMhz = 1.4;                                 // 927 ksym/s x (1 + 0.5) roll-off
    t.minSampleRate = 2000000;
    t.tuneOffsetHz = 0;
    return t;
}

} // namespace dect2
