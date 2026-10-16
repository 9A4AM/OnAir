// Wi-Fi HaLow (802.11ah) network monitor (see halow_rx.h).
#include "dect2/halow/halow_rx.h"
#include "dect2/dsp/fftutil.h"
#include "dect2/dsp/gen_util.h"
#include "dect2/halow/halow_proto.h"
#include "dect2/halow/halow_tel.h"
#include "dect2/results/results_io.h"
#include "dect2/wifi/wifi_proto.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "dect2/common/mathconst.h"

namespace dect2 {

using namespace halow;

namespace {
using dect2::math::kPi;
constexpr int kW = 96;                   // detector window: 24 us
constexpr int kRunMin = 64;              // samples the detector must stay above the threshold
constexpr float kThr = 0.55f;
constexpr int kDelta = 8;                // FFT windows start this far inside the guard interval
constexpr int64_t kGate = 2300;          // samples after the detection before the SIG field of any width is in the buffer
constexpr int kBack = 520;               // samples kept before the detection

struct Cand {
    int64_t fire = 0;
    double cfo = 0;                      // from the STF
    int stage = 0;                       // 0 preamble not yet looked at, 1 SIG read, waiting for the data field
    int64_t s0 = 0;                      // first sample of the working segment
    double cfoTotal = 0;
    int w = 0;
    int64_t b1 = 0;                      // first long training symbol
    Sig sig;
    std::array<cf32, kFft> H;
    float rssi = -200, snr = 0;
    int64_t needEnd = 0;
};

int bodyOff(int w) { return w == W1 ? 640 : 288; }   // the first SIG symbol's FFT window, from the first long training symbol
int ltfLag(int w) { return w == W1 ? kSym : kFft; }
int ltfBlocks(int w) { return w == W1 ? 4 : 2; }
}

struct HalowReceiver::Impl {
    std::mutex mu;
    std::function<void(const std::string&)> log;
    double rateSet = 0, offsetSet = 0;
    std::atomic<bool> resetReq{true}, clearReq{false};
    std::atomic<double> centerHz{0};

    double rate = 0, offset = 0;
    int64_t nIn = 0;
    // front end: mixer, low-pass FIR (only where an output sample needs it), linear interpolation to 4 Msps
    std::complex<double> lo{1, 0}, loStep{1, 0};
    int loCount = 0;
    int L = 1;
    std::vector<float> taps, hr, hi;
    int hp = 0;
    double step = 1;
    int64_t outK = 0;
    std::complex<float> yF{0, 0};
    bool direct = false;
    // detector at 4 Msps
    std::vector<cf32> buf;
    int64_t base = 0, g = 0;
    cf32 prodRing[kW] = {};
    float p1Ring[kW] = {}, p2Ring[kW] = {};
    int rp = 0;
    std::complex<double> sa{0, 0};
    double sp1 = 0, sp2 = 0;
    int run = 0;
    bool armed = true;
    int64_t quietUntil = 0;
    std::deque<Cand> cands;
    // results
    HalowTelemetry tel, pub;
    std::unordered_map<std::string, size_t> netIdx;
    std::unordered_map<uint32_t, std::string> ssidByComp;
    double power = 0; int64_t nPower = 0;
    double nextReport = 0, lastGood = -100;
    double snrAvg = 0;
    double nextPps = 1; int ppsAcc = 0;

    double sigSec() const { return rate > 0 ? nIn / rate : 0; }
    double hintMhz() const { const double c = centerHz.load(); return c > 0 ? (c + offset) / 1e6 : 0; }

    void initFront() {
        lo = 1; loCount = 0;
        loStep = rate > 0 ? std::polar(1.0, -2 * kPi * offset / rate) : std::complex<double>(1, 0);
        direct = rate == kRate;
        step = rate / kRate;
        const double fc = std::min(1.95e6, 0.45 * rate) / rate;
        L = std::max(15, (int)std::ceil(3.3 * rate / 0.5e6)) | 1;
        taps.assign((size_t)L, 0.f);
        double sum = 0;
        for (int i = 0; i < L; i++) {
            const double m = i - (L - 1) / 2.0;
            const double s = m == 0 ? 2 * fc : std::sin(2 * kPi * fc * m) / (kPi * m);
            const double w = 0.54 - 0.46 * std::cos(2 * kPi * i / (L - 1));
            taps[(size_t)i] = (float)(s * w);
            sum += s * w;
        }
        for (float& t : taps) t = (float)(t / sum);
        std::reverse(taps.begin(), taps.end());
        hr.assign((size_t)L * 2, 0.f); hi.assign((size_t)L * 2, 0.f); hp = 0;
        outK = 0; nIn = 0;
        yF = std::complex<float>(0, 0);
    }

    void resetDemod() {
        buf.clear(); base = 0; g = 0;
        for (auto& p : prodRing) p = 0;
        for (float& v : p1Ring) v = 0;
        for (float& v : p2Ring) v = 0;
        rp = 0; sa = 0; sp1 = sp2 = 0; run = 0; armed = true; quietUntil = 0;
        cands.clear();
    }

    // The tables and the frame log stay through resets: Clear and a loaded file reach the feed thread's copy through these requests (mu)
    bool resClear = false, resLoad = false;
    HalowTelemetry loaded;
    double loadedAt = 0;
    static void moveLists(HalowTelemetry& to, HalowTelemetry& from) {
        to.nets = std::move(from.nets); to.log = std::move(from.log);
        from.nets.clear(); from.log.clear();
    }
    static void shiftTimes(HalowTelemetry& t, double dt) {
        for (auto& n : t.nets) { n.firstSec += dt; n.lastSec += dt; }
        for (auto& f : t.log) f.sec += dt;
    }
    void takeRequests() {
        std::lock_guard<std::mutex> lk(mu);
        if (!resClear && !resLoad) return;
        if (resClear) { HalowTelemetry e; moveLists(tel, e); resClear = false; }
        if (resLoad) { shiftTimes(loaded, tel.timeSec - loadedAt); moveLists(tel, loaded); resLoad = false; }
        reindex();
    }
    void resetState() {
        { std::lock_guard<std::mutex> lk(mu); rate = rateSet; offset = offsetSet; }
        initFront();
        resetDemod();
        power = 0; nPower = 0; nextReport = 0.25 * rate; lastGood = -100;
        tel.inputRate = rate;
        shiftTimes(tel, -tel.timeSec);
        tel.timeSec = 0;
        nextPps = rate > 0 ? rate : 1; ppsAcc = 0;
        tel.ppsHist.clear(); tel.pps = 0;
    }
    void clearAll() {
        const uint64_t seq = tel.seq;
        tel = HalowTelemetry(); tel.seq = seq; tel.inputRate = rate;
        netIdx.clear(); ssidByComp.clear();
        snrAvg = 0;
        nextPps = rate > 0 ? (double)nIn + rate : 1; ppsAcc = 0;
    }
    void reindex() {
        netIdx.clear(); ssidByComp.clear();
        for (size_t i = 0; i < tel.nets.size(); i++) {
            netIdx[tel.nets[i].bssid] = i;
            if (tel.nets[i].hasSsid && !tel.nets[i].ssid.empty()) ssidByComp[wifi::crc32(reinterpret_cast<const uint8_t*>(tel.nets[i].ssid.data()), tel.nets[i].ssid.size())] = tel.nets[i].ssid;
        }
    }

    // ---------------------------------------------------------------- front end

    void input(float re, float im) {
        const std::complex<double> v = std::complex<double>(re, im) * lo;
        lo *= loStep;
        if (++loCount >= 4096) { lo /= std::abs(lo); loCount = 0; }
        if (direct) { demod(cf32((float)v.real(), (float)v.imag())); return; }
        hr[(size_t)hp] = hr[(size_t)(hp + L)] = (float)v.real();
        hi[(size_t)hp] = hi[(size_t)(hp + L)] = (float)v.imag();
        hp = hp + 1 == L ? 0 : hp + 1;
        const int64_t i = nIn;
        double t = outK * step;
        int64_t f = (int64_t)std::floor(t);
        if (i != f && i != f + 1) return;
        float o[2];
        genutil::dot2(taps.data(), hr.data() + hp, hi.data() + hp, L, o);
        const std::complex<float> y(o[0], o[1]);
        for (;;) {
            t = outK * step; f = (int64_t)std::floor(t);
            const float fr = (float)(t - (double)f);
            if (i == f) {
                yF = y;
                if (fr < 1e-6f) { outK++; demod(y); continue; }
                break;
            }
            if (i == f + 1) { outK++; demod(yF * (1 - fr) + y * fr); continue; }
            break;
        }
    }

    // ---------------------------------------------------------------- detector

    void demod(cf32 y) {
        const int64_t n = g;
        buf.push_back(y);
        g++;
        const cf32 y32 = n - kLag >= base ? buf[(size_t)(n - kLag - base)] : cf32(0, 0);
        const cf32 a = y * std::conj(y32);
        const float p1 = std::norm(y), p2 = std::norm(y32);
        sa += std::complex<double>(a.real() - prodRing[rp].real(), a.imag() - prodRing[rp].imag());
        sp1 += (double)p1 - p1Ring[rp]; sp2 += (double)p2 - p2Ring[rp];
        prodRing[rp] = a; p1Ring[rp] = p1; p2Ring[rp] = p2;
        rp = rp + 1 == kW ? 0 : rp + 1;
        const double m = std::abs(sa) / std::sqrt(sp1 * sp2 + 1e-30);
        if (m > kThr && sp1 > 1e-8) run++; else run = 0;
        if (m < 0.3) armed = true;
        if (armed && run >= kRunMin && n >= quietUntil) {
            armed = false;
            if (cands.size() >= 8) { tel.preambles++; return; }   // noise looks like STF forever; unbounded candidates blew the heap on Start
            std::complex<double> s(0, 0);
            for (int k = 1; k <= 64; k++) { const cf32& p = prodRing[(rp - k + 2 * kW) % kW]; s += std::complex<double>(p.real(), p.imag()); }
            Cand c;
            c.fire = n;
            c.cfo = std::arg(s) * kRate / (2 * kPi * kLag);
            cands.push_back(c);
            tel.preambles++;
        }
    }

    // ---------------------------------------------------------------- the working segment

    struct Seg {
        std::vector<cf32> x;
        int64_t s0 = 0;
    };

    bool makeSeg(Seg& s, int64_t s0, int64_t end, double cfo) const {
        if (s0 < base || end > g || end <= s0) return false;
        if (end - s0 > 131072) return false;   // ~400 data symbols plus preamble; a waiting candidate must not copy the whole buf
        s.s0 = s0;
        s.x.resize((size_t)(end - s0));
        const std::complex<double> st = std::polar(1.0, -2 * kPi * cfo / kRate);
        std::complex<double> r(1, 0);
        for (size_t i = 0; i < s.x.size(); i++) {
            const cf32& v = buf[(size_t)(s0 - base) + i];
            s.x[i] = cf32(v.real() * (float)r.real() - v.imag() * (float)r.imag(), v.real() * (float)r.imag() + v.imag() * (float)r.real());
            r *= st;
            if ((i & 1023) == 1023) r /= std::abs(r);
        }
        return true;
    }

    static void fftWin(const cf32* x, cf32* Y) {
        static thread_local Fft f(kFft);
        for (int i = 0; i < kFft; i++) Y[i] = x[i];
        f.forward(Y);
        for (int i = 0; i < kFft; i++) Y[i] *= 1.f / kFft;
    }

    // One OFDM symbol: equalise, track the common phase and the slope across the pilots, soft bits per logical data tone (copies add)
    static bool demodSym(const cf32* x, const std::vector<Layout>& lays, int polIdx, const cf32* H, std::vector<float>& llr, double& nsum, int& ncnt) {
        if (lays.empty() || !x || !H) return false;
        cf32 Y[kFft];
        fftWin(x, Y);
        const int pol = pilotPolarity(polIdx);
        std::complex<double> acc(0, 0);
        std::vector<std::pair<int, std::complex<double>>> pr;
        for (const Layout& l : lays)
            for (size_t i = 0; i < l.pilotK.size(); i++) {
                const int k = l.pilotK[i];
                const cf32 r = Y[bin(k)] * std::conj(H[bin(k)]) * (float)((i % 4 == 3 ? -1 : 1) * pol);
                pr.push_back({k, std::complex<double>(r.real(), r.imag())});
                acc += pr.back().second;
            }
        const double a = std::arg(acc);
        double num = 0, den = 0;
        for (auto& p : pr) { const double th = std::arg(p.second * std::polar(1.0, -a)); num += p.first * th; den += (double)p.first * p.first; }
        const double b = den > 0 ? num / den : 0;
        llr.assign(lays[0].dataK.size(), 0.f);
        for (const Layout& l : lays)
            for (size_t i = 0; i < l.dataK.size(); i++) {
                const int k = l.dataK[i];
                if (!k) continue;
                const cf32 z = Y[bin(k)] * std::conj(H[bin(k)]) * (cf32)std::polar(1.0, -(a + b * k));
                llr[i] += z.real();
                const float h2 = std::norm(H[bin(k)]);
                if (h2 > 1e-12f) { const float e = std::fabs(z.real() / h2) - 1.f, q = z.imag() / h2; nsum += e * e + q * q; ncnt++; }
            }
        return true;
    }

    // coded bit LLRs of one symbol in transmit order (deinterleaved; the two copies of a repeated symbol added)
    static void symCoded(const std::vector<float>& llr, int ncbps, int ncol, int nrow, bool rep, std::vector<float>& out) {
        if (ncbps <= 0 || ncol <= 0 || nrow <= 0) return;
        const int last = interleave(ncol, nrow, ncbps - 1);
        if (last < 0 || (size_t)last >= llr.size()) return;
        std::vector<float> raw((size_t)ncbps);
        for (int k = 0; k < ncbps; k++) raw[(size_t)k] = llr[(size_t)interleave(ncol, nrow, k)];
        if (rep) for (int i = 0; i < ncbps / 2; i++) out.push_back(raw[(size_t)i] + raw[(size_t)(i + ncbps / 2)]);
        else out.insert(out.end(), raw.begin(), raw.end());
    }

    // ---------------------------------------------------------------- one candidate

    // 0 failed (drop it), 1 done, 2 wait for more samples
    int tryCand(Cand& c) {
        if (c.stage == 0) {
            if (g < c.fire + kGate) return 2;
            const int64_t s0 = std::max(base, c.fire - kBack);
            Seg seg;
            if (!makeSeg(seg, s0, std::min(g, c.fire + kGate + 200), c.cfo)) return 0;
            struct Hyp { int w; int64_t b1; double q; };
            std::vector<Hyp> hyps;
            for (int w : {W2, W1, W4}) {
                const int lag = ltfLag(w), K = ltfBlocks(w);
                cf32 Lt[kFft];
                ltfTime(w, Lt);
                double eL = 0;
                for (const cf32& v : Lt) eL += std::norm(v);
                const int64_t bExp = c.fire - 150 + info(w).stfSamples + (w == W1 ? kGi : kFft / 2);
                double bestM = 0; int64_t bestB = -1;
                for (int64_t b = bExp - 128; b <= bExp + 128; b++) {
                    const int64_t r = b - s0;
                    if (r < 0 || r + (K - 1) * lag + kFft > (int64_t)seg.x.size()) continue;
                    double m = 0;
                    for (int k = 0; k < K; k++) {
                        std::complex<float> s(0, 0);
                        const cf32* p = seg.x.data() + r + k * lag;
                        for (int i = 0; i < kFft; i++) s += p[i] * std::conj(Lt[i]);
                        m += std::abs(s);
                    }
                    if (m > bestM) { bestM = m; bestB = b; }
                }
                if (bestB < 0) continue;
                double eW = 0;
                const cf32* p = seg.x.data() + (bestB - s0);
                for (int k = 0; k < K; k++) for (int i = 0; i < kFft; i++) eW += std::norm(p[k * lag + i]);
                const double q = bestM / std::sqrt(eL * eW / K + 1e-30) / K;
                if (q > 0.2) hyps.push_back({w, bestB, q});
            }
            std::sort(hyps.begin(), hyps.end(), [](const Hyp& a, const Hyp& b) { return a.q > b.q; });
            for (const Hyp& h : hyps) { Seg work = seg; if (sigAttempt(c, work, h.w, h.b1)) break; }
            if (c.stage == 0) { tel.sigBad++; return 0; }
            tel.sigOk++;
            tel.perWidth[c.w]++;
            quietUntil = std::max(quietUntil, c.b1 + bodyOff(c.w) + (int64_t)(info(c.w).sigSyms + std::max(0, dataSymbols(c.w, c.sig.mcs, c.sig.len))) * kSym);
        }
        // the data field
        const int nd = dataBitsPerSym(c.w, c.sig.mcs);
        if (nd <= 0 || c.sig.len < 1 || c.sig.agg || c.sig.nsts != 0) { undecoded(c); return 1; }
        const int nsym = dataSymbols(c.w, c.sig.mcs, c.sig.len);
        if (nsym <= 0 || nsym > 1024) { undecoded(c); return 1; }   // 1 MHz MCS 10, 300 byte PSDU: 403 symbols; 511 bytes: 685
        c.needEnd = c.b1 - kDelta + bodyOff(c.w) + (int64_t)(info(c.w).sigSyms + nsym) * kSym + 8;
        if (g < c.needEnd) return 2;
        Seg seg;
        if (!makeSeg(seg, c.s0, c.needEnd, c.cfoTotal)) return 0;
        const WidthInfo& wi = info(c.w);
        const bool rep = c.w == W1 && c.sig.mcs == 10;
        std::vector<float> coded;
        std::vector<float> llr;
        double nsum = 0; int ncnt = 0;
        for (int j = 0; j < nsym; j++) {
            const int64_t at = c.b1 - kDelta - c.s0 + bodyOff(c.w) + (int64_t)(wi.sigSyms + j) * kSym;
            if (at < 0 || at + kFft > (int64_t)seg.x.size()) { tel.blocksBad++; return 1; }
            demodSym(seg.x.data() + at, {wi.data}, wi.sigSyms + j, c.H.data(), llr, nsum, ncnt);
            if (llr.size() < (size_t)std::max(1, wi.ncbps)) { tel.blocksBad++; return 1; }
            symCoded(llr, wi.ncbps, wi.ncol, wi.nrow, rep, coded);
        }
        const std::vector<uint8_t> bits = viterbi(coded);
        const int need = 8 + 8 * c.sig.len;
        if (need <= 8 || (int)bits.size() < need) { tel.blocksBad++; return 1; }
        const std::vector<uint8_t> sc = scramblerFrom(bits.data(), bits.size());
        if ((int)sc.size() < need) { tel.blocksBad++; return 1; }
        std::vector<uint8_t> psdu((size_t)c.sig.len, 0);
        for (int i = 0; i < c.sig.len; i++) for (int b = 0; b < 8; b++) psdu[(size_t)i] |= (uint8_t)((bits[(size_t)(8 + 8 * i + b)] ^ sc[(size_t)(8 + 8 * i + b)]) << b);
        const double noise = ncnt ? nsum / ncnt : 1;
        accept(psdu, c, noise);
        return 1;
    }

    // the long training symbols at b1: residual offset, channel, SIG field; true when the SIG field passed
    bool sigAttempt(Cand& c, Seg& seg, int w, int64_t b1) {
        const WidthInfo& wi = info(w);
        const int lag = ltfLag(w), K = ltfBlocks(w);
        const int64_t r1 = b1 - seg.s0 - kDelta;
        if (r1 < 0) return false;
        const int64_t endSig = r1 + bodyOff(w) + (int64_t)wi.sigSyms * kSym;
        if (endSig + 8 > (int64_t)seg.x.size()) return false;
        if (r1 + (int64_t)(K - 1) * lag + kFft > (int64_t)seg.x.size()) return false;
        float lt[kFft];
        ltfSpectrum(w, lt);
        // residual carrier offset from the repeats of the training symbol
        std::vector<std::array<cf32, kFft>> Y((size_t)K);
        for (int k = 0; k < K; k++) fftWin(seg.x.data() + r1 + k * lag, Y[(size_t)k].data());
        std::complex<double> d(0, 0);
        for (int k = 0; k + 1 < K; k++) for (int t = 0; t < kFft; t++) if (lt[t] != 0.f) { const cf32 z = Y[(size_t)k + 1][(size_t)t] * std::conj(Y[(size_t)k][(size_t)t]); d += std::complex<double>(z.real(), z.imag()); }
        const double fres = std::arg(d) * kRate / (2 * kPi * lag);
        {
            const std::complex<double> st = std::polar(1.0, -2 * kPi * fres / kRate);
            std::complex<double> rr(1, 0);
            for (size_t i = 0; i < seg.x.size(); i++) { seg.x[i] *= cf32((float)rr.real(), (float)rr.imag()); rr *= st; if ((i & 1023) == 1023) rr /= std::abs(rr); }
        }
        for (int k = 0; k < K; k++) fftWin(seg.x.data() + r1 + k * lag, Y[(size_t)k].data());
        std::array<cf32, kFft> H;
        for (int t = 0; t < kFft; t++) {
            H[(size_t)t] = cf32(0, 0);
            if (lt[t] == 0.f) continue;
            cf32 s(0, 0);
            for (int k = 0; k < K; k++) s += Y[(size_t)k][(size_t)t];
            H[(size_t)t] = s * (1.f / (float)K) * lt[t];   // lt = +-1: the same as dividing
        }
        // SIG field
        std::vector<float> coded, llr;
        double nsum = 0; int ncnt = 0;
        const bool rep = w == W1;
        for (int j = 0; j < wi.sigSyms; j++) {
            if (!demodSym(seg.x.data() + r1 + bodyOff(w) + (int64_t)j * kSym, wi.sig, j, H.data(), llr, nsum, ncnt)) return false;
            if ((int)llr.size() < std::max(1, wi.sigNcbps)) return false;
            symCoded(llr, wi.sigNcbps, wi.sigNcol, wi.sigNrow, rep, coded);
        }
        const std::vector<uint8_t> bits = viterbi(coded);
        Sig sg;
        if ((int)bits.size() < wi.sigInfoBits || !parseSig(bits.data(), w, sg)) {
            // the segment was rotated for this width: undo nothing, the next width starts from its own copy
            return false;
        }
        c.stage = 1; c.w = w; c.b1 = b1; c.sig = sg; c.H = H;
        c.s0 = seg.s0; c.cfoTotal = c.cfo + fres;
        double pw = 0; const int64_t n0 = r1 + kDelta, n1 = std::min<int64_t>((int64_t)seg.x.size(), n0 + 256);
        for (int64_t i = n0; i < n1; i++) pw += std::norm(seg.x[(size_t)i]);
        c.rssi = (float)(10 * std::log10(pw / std::max<int64_t>(1, n1 - n0) + 1e-20));
        const double noise = ncnt ? nsum / ncnt : 1;
        c.snr = (float)std::max(0.0, std::min(40.0, -10 * std::log10(noise + 1e-6)));
        return true;
    }

    // ---------------------------------------------------------------- tables

    void trimLog() { while (tel.log.size() > kHlLogMax) tel.log.pop_back(); }

    void pushLog(HlFrame f) {
        tel.log.insert(tel.log.begin(), std::move(f));
        trimLog();
    }

    void undecoded(const Cand& c) {
        tel.undecoded++;
        lastGood = sigSec();
        ppsAcc++;
        tel.cfoHz = c.cfoTotal;
        snrAvg = snrAvg == 0 ? c.snr : 0.8 * snrAvg + 0.2 * c.snr;
        HlFrame f;
        f.wallTime = (int64_t)std::time(nullptr); f.sec = sigSec();
        f.widthMhz = widthMhz(c.w); f.mcs = c.sig.mcs; f.len = c.sig.len; f.rssi = c.rssi;
        f.type = "PPDU";
        char b[96];
        snprintf(b, sizeof b, "%d MHz MCS %d, %d bytes: SIG field read, data rate not decoded", widthMhz(c.w), c.sig.mcs, c.sig.len);
        f.info = b;
        pushLog(std::move(f));
    }

    HlNet& netEntry(const std::string& key) {
        auto it = netIdx.find(key);
        if (it != netIdx.end()) return tel.nets[it->second];
        if (tel.nets.size() >= kHlNetMax) {
            size_t old = 0;
            for (size_t i = 1; i < tel.nets.size(); i++) if (tel.nets[i].lastSec < tel.nets[old].lastSec) old = i;
            tel.nets.erase(tel.nets.begin() + (ptrdiff_t)old);
            reindex();
        }
        HlNet n;
        n.bssid = key;
        n.firstSec = sigSec();
        tel.nets.push_back(std::move(n));
        netIdx[key] = tel.nets.size() - 1;
        if (log && tel.nets.size() <= 20) { char b[96]; snprintf(b, sizeof b, "HaLow: new network %s", key.c_str()); log(b); }
        return tel.nets.back();
    }

    void accept(const std::vector<uint8_t>& psdu, const Cand& c, double noise) {
        if (!fcsOk(psdu.data(), psdu.size())) { tel.blocksBad++; return; }
        tel.blocksOk++;
        ppsAcc++;
        lastGood = sigSec();
        tel.cfoHz = c.cfoTotal;
        const double snr = std::max(0.0, std::min(40.0, -10 * std::log10(noise + 1e-6)));
        snrAvg = snrAvg == 0 ? snr : 0.8 * snrAvg + 0.2 * snr;
        const int64_t wall = (int64_t)std::time(nullptr);
        halow::Frame f;
        parseFrame(psdu.data(), psdu.size() - 4, f);
        HlFrame fr;
        fr.wallTime = wall; fr.sec = sigSec(); fr.widthMhz = widthMhz(c.w); fr.mcs = c.sig.mcs; fr.len = (int)psdu.size(); fr.decoded = true; fr.rssi = c.rssi;
        const bool net = f.s1gBeacon || f.probeResp || f.mgmtBeacon;
        fr.type = f.s1gBeacon ? "S1G beacon" : f.probeResp ? "probe response" : f.mgmtBeacon ? "beacon" : f.summary;
        if (net || f.type == 2) fr.bssid = macText(f.bssid);
        fr.info = f.summary;
        if (net) {
            tel.beaconsSeen++;
            HlNet& n = netEntry(macText(f.bssid));
            n.vendor = wifi::vendorOf(f.bssid);
            n.seenWidthMhz = widthMhz(c.w); n.mcs = c.sig.mcs;
            n.rssi = c.rssi; n.rssiMax = std::max(n.rssiMax, c.rssi);
            n.packets++; if (f.s1gBeacon || f.mgmtBeacon) n.beacons++;
            n.lastSec = sigSec(); n.lastWall = wall; n.last = f.summary;
            if (f.hasSsid) {
                n.hasSsid = !f.ssid.empty(); n.hidden = f.ssid.empty(); n.ssid = f.ssid;
                if (!f.ssid.empty()) {
                    const uint32_t cr = wifi::crc32(reinterpret_cast<const uint8_t*>(f.ssid.data()), f.ssid.size());
                    ssidByComp[cr] = f.ssid;
                    for (HlNet& o : tel.nets) if (!o.hasSsid && o.comp == cr) { o.ssid = f.ssid; o.hasSsid = true; }
                }
            }
            if (f.hasComp) n.comp = f.compSsid;
            if (!n.hasSsid && n.comp) { auto it = ssidByComp.find(n.comp); if (it != ssidByComp.end()) { n.ssid = it->second; n.hasSsid = true; n.hidden = false; } }
            if (f.hasOp) { n.ccf = f.ccf; n.opClass = f.opClass; n.opWidthMhz = f.opWidthMhz; const double m = chanMhz(f.opClass, f.ccf); if (m > 0) n.freqMhz = m; }
            if (n.freqMhz <= 0) n.freqMhz = hintMhz();
            n.security = securityName(f.security);
            if (f.intervalTu > 0) n.intervalTu = f.intervalTu;
        }
        pushLog(std::move(fr));
    }

    // ---------------------------------------------------------------- feed side

    void process() {
        while (!cands.empty()) {
            Cand& c = cands.front();
            if (c.fire < quietUntil && c.stage == 0) { cands.pop_front(); continue; }
            const int r = tryCand(c);
            if (r == 2) break;
            cands.pop_front();
        }
        // keep the buffer short
        int64_t keep = g - 16384;
        if (!cands.empty()) keep = std::min(keep, cands.front().stage == 0 ? cands.front().fire - kBack - 8 : cands.front().s0);
        if (keep > base && keep - base > 262144) {
            size_t drop = (size_t)(keep - base - 16384);
            if (drop > buf.size()) drop = buf.size();
            if (drop) {
                buf.erase(buf.begin(), buf.begin() + (ptrdiff_t)drop);
                base += (int64_t)drop;
            }
        }
        while (cands.size() > 8) cands.pop_front();
    }

    void report() {
        tel.seq++;
        tel.timeSec = sigSec();
        tel.levelDb = nPower ? (float)(10 * std::log10(power / (double)nPower + 1e-20)) : -200.f;
        power = 0; nPower = 0;
        tel.centerMhz = hintMhz();
        tel.chanHint = tel.centerMhz > 0 && inHalowBand(tel.centerMhz) ? nearestChannel(tel.centerMhz, 1, tel.centerMhz < 870) : 0;
        tel.snrDb = (float)snrAvg;
        tel.state = sigSec() - lastGood < 5 ? 1 : 0;
        tel.dataValid = tel.state >= 1;
        while ((double)nIn >= nextPps && rate > 0) {
            tel.pps = (float)ppsAcc;
            tel.ppsHist.push_back((float)ppsAcc);
            if (tel.ppsHist.size() > kHlPpsHist) tel.ppsHist.erase(tel.ppsHist.begin());
            ppsAcc = 0; nextPps += rate;
        }
        HalowTelemetry out = tel;
        std::stable_sort(out.nets.begin(), out.nets.end(), [](const HlNet& x, const HlNet& y) { return x.lastSec > y.lastSec; });
        std::lock_guard<std::mutex> lk(mu);
        pub = std::move(out);
    }
};

HalowReceiver::HalowReceiver() : p_(std::make_unique<Impl>()) {}
HalowReceiver::~HalowReceiver() = default;

void HalowReceiver::configure(double r) { std::lock_guard<std::mutex> lk(p_->mu); p_->rateSet = r; p_->resetReq = true; }
void HalowReceiver::setSignalOffset(double hz) { std::lock_guard<std::mutex> lk(p_->mu); if (hz != p_->offsetSet) { p_->offsetSet = hz; p_->resetReq = true; } }
void HalowReceiver::setCenterHz(double hz) { p_->centerHz = hz; }
bool HalowReceiver::ready() const { std::lock_guard<std::mutex> lk(p_->mu); return p_->rateSet >= halowTuning().minSampleRate - 1; }
void HalowReceiver::reset() { p_->resetReq = true; }
void HalowReceiver::clearResults() {
    {
        std::lock_guard<std::mutex> lk(p_->mu);
        HalowTelemetry e;
        Impl::moveLists(p_->pub, e);
        p_->resLoad = false;
        p_->pub.seq++;
    }
    p_->clearReq = true;
}

void HalowReceiver::feed(const cf32* x, size_t n) {
    Impl& m = *p_;
    if (m.resetReq.exchange(false)) m.resetState();
    if (m.clearReq.exchange(false)) m.clearAll();
    m.takeRequests();
    if (m.rate < halowTuning().minSampleRate - 1 || n == 0) {
        if (m.rate > 0) { m.nIn += (int64_t)n; if ((double)m.nIn >= m.nextReport) { m.nextReport += 0.25 * m.rate; m.report(); } }
        return;
    }
    for (size_t i = 0; i < n; i++) {
        float re = x[i].real(), im = x[i].imag();
        if (!std::isfinite(re) || !std::isfinite(im)) re = im = 0;
        m.power += (double)re * re + (double)im * im;
        m.input(re, im);
        m.nIn++;
        if ((double)m.nIn >= m.nextReport) { m.process(); m.nextReport += 0.25 * m.rate; m.report(); }
    }
    m.nPower += (int64_t)n;
    m.process();
}

std::string HalowReceiver::saveResults() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return results::pack(p_->pub.timeSec, p_->pub.nets, p_->pub.log);
}
bool HalowReceiver::loadResults(const std::string& blob) {
    double t0 = 0;
    HalowTelemetry l;
    if (!results::unpack(blob, t0, l.nets, l.log)) return false;
    std::lock_guard<std::mutex> lk(p_->mu);
    Impl::shiftTimes(l, p_->pub.timeSec - t0);
    HalowTelemetry c = l;
    Impl::moveLists(p_->pub, c);
    Impl::moveLists(p_->loaded, l);
    p_->loadedAt = p_->pub.timeSec;
    p_->resLoad = true;
    p_->pub.seq++;
    return true;
}
bool HalowReceiver::telemetry(HalowTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->pub.seq <= lastSeq) return false;
    out = p_->pub;
    return true;
}
void HalowReceiver::setLogCallback(std::function<void(const std::string&)> cb) { std::lock_guard<std::mutex> lk(p_->mu); p_->log = std::move(cb); }

ModeTuning halowTuning() {
    ModeTuning t;
    t.stdMode = 72; t.id = "halow"; t.name = "Wi-Fi HaLow (802.11ah)";
    t.minMhz = 863; t.maxMhz = 928; t.defMhz = 863.5;   // the first EU 1 MHz channel
    t.sampleRate = 4000000;                  // one channel of up to 4 MHz: 1 and 2 MHz channels also work from 2.4 Msps (an RTL-SDR)
    t.basebandHz = 0;
    t.bandwidthMhz = 4;
    t.minSampleRate = 2000000;
    t.tuneOffsetHz = 0;                      // OFDM leaves the centre tone empty: no DC spike to avoid
    return t;
}

} // namespace dect2
