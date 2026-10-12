// Sigfox uplink receiver. See ism_sigfox.h.
//
// Sources: public reverse engineering of the Sigfox radio interface (Apvrille, "Sigfox security" / Hack.lu 2017 style frame breakdown;
// the open "sigfox-decoder" / "renard" descriptions): uplink = DBPSK, 100 bit/s (600 bit/s RC2 / RC4), preamble 0xAAAAA, frame type,
// length indicator, sequence number, device ID, payload, authentication code, CRC-16.
// NOT verified against a real capture (no Sigfox IQ was available): the exact preamble length (19 vs 20 bits), the 13-bit frame type
// values (shown as received, not used to find the length), the layout of the 16 bits after the frame type (LI 2, repeat flag 1,
// spare 1, sequence number 12), the authentication code length per payload length (16 + 8 * ceil(N / 4) bits is assumed; the
// payload length is found from the burst's length), whether a 0 bit is a phase inversion (assumed) or no change, the CRC polynomial.
// The authentication code and CRC are shown as received; the key is secret, so nothing can be checked.
#include "dect2/ism_sigfox.h"
#include "dect2/fftutil.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <ctime>
#include <random>

namespace dect2 {

namespace {
constexpr double kBandHz = 98e3;         // the watched span: +-98 kHz around the dial
constexpr int kAvg = 8;                  // spectrogram frames averaged
constexpr double kThr = 3.1;             // carrier threshold over the median of the averaged spectrum
constexpr double kTrackHz = 350;         // peaks within this of a track belong to it
constexpr double kMinDurSec = 0.1;
constexpr double kMaxDurSec = 3.2;
constexpr double kRingSec = 3.8;
constexpr int kPreambleBits = 19;
constexpr double kPi = 3.14159265358979323846;

int macBits(int n) { return 16 + 8 * ((n + 3) / 4); }
int frameBits(int n) { return kPreambleBits + 13 + 16 + 32 + 8 * n + macBits(n) + 16; }

uint16_t crc16(const std::vector<uint8_t>& bits, size_t a, size_t b) {   // CRC-16/CCITT over bits [a, b)
    uint16_t c = 0xFFFF;
    for (size_t i = a; i < b; i++) {
        const bool fb = ((c >> 15) & 1) ^ (bits[i] & 1);
        c = (uint16_t)(c << 1);
        if (fb) c ^= 0x1021;
    }
    return c;
}

std::string hexOf(uint64_t v, int digits) {
    char b[24];
    snprintf(b, sizeof b, "%0*llX", digits, (unsigned long long)v);
    return b;
}
}

// ------------------------------------------------------------------ test signal

std::vector<uint8_t> sigfoxFrameBits(uint32_t id, uint32_t seq, const std::vector<uint8_t>& payload, bool rep) {
    std::vector<uint8_t> b;
    auto put = [&](uint64_t v, int n) { for (int i = n - 1; i >= 0; i--) b.push_back((uint8_t)((v >> i) & 1)); };
    const int n = (int)std::min<size_t>(payload.size(), 12);
    for (int j = 0; j < kPreambleBits; j++) b.push_back((uint8_t)(j % 2 == 0));
    put((0x0B2Fu + 0x0111u * (unsigned)n) & 0x1FFF, 13);
    const size_t crcStart = b.size();
    put(0, 2); put(rep ? 1 : 0, 1); put(0, 1); put(seq & 0xFFF, 12);
    put(id, 32);
    for (int i = 0; i < n; i++) put(payload[i], 8);
    uint32_t h = id * 2654435761u ^ (seq * 40503u + 12345u);
    for (int i = 0; i < macBits(n) / 8; i++) { h = h * 1664525u + 1013904223u; put((h >> 16) & 0xFF, 8); }
    put(crc16(b, crcStart, b.size()), 16);
    return b;
}

std::vector<cf32> sigfoxSynth(double rate, double dialOff, double seconds, double snrDb, double cfoHz, const std::vector<SigfoxTx>& txs, uint32_t seed) {
    const size_t n = (size_t)(seconds * rate);
    std::vector<cf32> x(n);
    std::mt19937 rng(seed);
    std::normal_distribution<float> g(0.f, 0.1f);
    for (auto& v : x) v = cf32(g(rng), g(rng));
    const double n0 = 2 * 0.1 * 0.1 / rate;   // noise power per Hz
    for (const auto& t : txs) {
        const std::vector<uint8_t> bits = sigfoxFrameBits(t.id, t.seq, t.payload, t.repeatFlag);
        std::vector<int> s(bits.size() + 1, 1);
        for (size_t i = 0; i < bits.size(); i++) s[i + 1] = bits[i] ? s[i] : -s[i];
        const double amp = std::sqrt(std::pow(10.0, snrDb / 10.0) * 100.0 * n0) * std::pow(10.0, t.levelDb / 20.0);
        const size_t i0 = (size_t)(t.startSec * rate), cnt = (size_t)(s.size() / (double)t.baud * rate);
        const double w = 2 * kPi * (dialOff + t.offsetHz + cfoHz) / rate;
        std::complex<double> ph = std::polar(1.0, w * (double)i0), rot = std::polar(1.0, w);
        for (size_t k = 0; k < cnt && i0 + k < n; k++) {
            const double pos = (double)k / rate * t.baud;
            const size_t sy = std::min(s.size() - 1, (size_t)pos);
            const double fr = pos - (double)sy;
            double a = s[sy];
            if (sy > 0 && fr < 0.1) { const double q = 0.5 - 0.5 * std::cos(kPi * fr / 0.1); a = s[sy - 1] * (1 - q) + s[sy] * q; }
            const std::complex<double> v = ph * (amp * a);
            x[i0 + k] += cf32((float)v.real(), (float)v.imag());
            ph *= rot;
            if ((k & 1023) == 1023) ph /= std::abs(ph);
        }
    }
    return x;
}

// ------------------------------------------------------------------ receiver

struct SigfoxRx::Impl {
    double rate = 0, dialOff = 0, dialHz = 0;
    bool on = false;
    // front end
    int D1 = 1;
    double fs1 = 0;
    std::vector<float> taps;
    std::complex<double> osc{1, 0}, rot{1, 0};
    std::vector<cf32> pend;
    size_t pendPos = 0;
    // 400 kHz ring and spectrogram
    std::vector<cf32> ring;
    uint64_t total1 = 0;
    int N = 4096, hop = 2048, nb = 0, kLo = 0;
    double binHz = 100;
    std::unique_ptr<Fft> fft;
    std::vector<float> win;
    std::vector<cf32> fb;
    std::vector<std::vector<float>> hist;
    std::vector<float> sum, E, srt;
    int hpos = 0;
    uint64_t nframes = 0;
    std::vector<float> wfAcc;
    int wfFrames = 0, rowFrames = 20;
    struct Track { double f = 0; uint64_t start = 0, last = 0; float peak = 0; bool dead = false; };
    std::vector<Track> tracks;
    SigfoxTelemetry tel;
    uint64_t seqNo = 0;
    struct Rep { double sec; };
    std::vector<double> lastRepSec;   // per message (parallel to tel.messages): time of the latest repetition counted

    void configure(double r, double off, double dial) {
        rate = r; dialOff = off; dialHz = dial;
        on = SigfoxRx::onSigfoxBand(dial) && r >= 4e5;
        tel = SigfoxTelemetry();
        tel.active = on;
        lastRepSec.clear();
        tracks.clear();
        pend.clear(); pendPos = 0;
        total1 = 0; nframes = 0; hpos = 0; wfFrames = 0;
        if (!on) return;
        D1 = std::max(1, (int)std::floor(r / 400e3));
        fs1 = r / D1;
        const double fc = 108e3;
        const int nt = std::max(15, ((int)std::ceil(3.3 * r / std::max(50e3, fs1 - 2 * fc))) | 1);
        taps.resize(nt);
        double s = 0;
        for (int i = 0; i < nt; i++) {
            const double m = i - (nt - 1) / 2.0, f = fc / r;
            const double h = m == 0 ? 2 * f : std::sin(2 * kPi * f * m) / (kPi * m);
            taps[i] = (float)(h * (0.54 - 0.46 * std::cos(2 * kPi * i / (nt - 1))));
            s += taps[i];
        }
        for (auto& t : taps) t = (float)(t / s);
        osc = {1, 0};
        rot = std::polar(1.0, -2 * kPi * off / r);
        ring.assign((size_t)(kRingSec * fs1), cf32(0, 0));
        N = 256;
        while (N < 65536 && fs1 / (N * 2) >= 80.0) N *= 2;
        hop = N / 2;
        binHz = fs1 / N;
        fft = std::make_unique<Fft>(N);
        win.resize(N);
        for (int i = 0; i < N; i++) win[i] = (float)(0.5 - 0.5 * std::cos(2 * kPi * (i + 0.5) / N));
        fb.assign(N, cf32(0, 0));
        const int half = (int)(kBandHz / binHz);
        nb = 2 * half + 1;
        kLo = -half;
        hist.assign(kAvg, std::vector<float>(nb, 0.f));
        sum.assign(nb, 0.f); E.assign(nb, 0.f);
        wfAcc.assign(kSigfoxCols, 0.f);
        rowFrames = std::max(1, (int)std::lround(0.1 * fs1 / hop));
        tel.cols = kSigfoxCols;
    }

    void feed(const cf32* x, size_t n) {
        if (!on) return;
        const size_t nt = taps.size();
        size_t i = 0;
        while (i < n) {
            const size_t m = std::min<size_t>(4096, n - i);
            for (size_t k = 0; k < m; k++) {
                cf32 v = x[i + k];
                if (!std::isfinite(v.real()) || !std::isfinite(v.imag())) v = cf32(0, 0);
                osc *= rot;
                pend.push_back(v * cf32((float)osc.real(), (float)osc.imag()));
            }
            osc /= std::abs(osc);
            i += m;
            while (pendPos + nt <= pend.size()) {
                float re = 0, im = 0;
                const cf32* p = &pend[pendPos];
                for (size_t t = 0; t < nt; t++) { re += taps[t] * p[t].real(); im += taps[t] * p[t].imag(); }
                pendPos += D1;
                push1(cf32(re, im));
            }
            if (pendPos > 0) { pend.erase(pend.begin(), pend.begin() + (ptrdiff_t)pendPos); pendPos = 0; }
        }
    }

    void push1(cf32 v) {
        ring[total1 % ring.size()] = v;
        total1++;
        if (total1 % hop == 0 && total1 >= (uint64_t)N) frame();
    }

    double sec(uint64_t s1) const { return (double)s1 / fs1; }

    void frame() {
        const uint64_t base = total1 - N;
        for (int i = 0; i < N; i++) fb[i] = ring[(base + i) % ring.size()] * win[i];
        fft->forward(fb.data());
        std::vector<float>& h = hist[hpos];
        for (int j = 0; j < nb; j++) {
            const int k = (kLo + j + N) % N;
            const float p = std::norm(fb[k]);
            sum[j] += p - h[j];
            h[j] = p;
        }
        hpos = (hpos + 1) % kAvg;
        nframes++;
        if (nframes < (uint64_t)kAvg + 1) return;
        for (int j = 0; j < nb; j++) E[j] = std::max(0.f, sum[j]) / kAvg;
        srt = E;
        std::nth_element(srt.begin(), srt.begin() + nb / 2, srt.end());
        const float noise = std::max(1e-20f, srt[nb / 2]);
        const float thr = (float)(kThr * noise);
        // waterfall
        for (int j = 0; j < nb; j++) {
            const int c = (int)((int64_t)j * kSigfoxCols / nb);
            wfAcc[c] = std::max(wfAcc[c], E[j] / noise);
        }
        if (++wfFrames >= rowFrames) {
            wfFrames = 0;
            for (int c = 0; c < kSigfoxCols; c++) {
                const double db = 10 * std::log10(std::max(1e-3f, wfAcc[c]));
                tel.rows.push_back((uint8_t)std::max(0.0, std::min(255.0, db * 6.0)));
                wfAcc[c] = 0;
            }
            tel.rowCount++;
            if (tel.rows.size() > kSigfoxRowsMax * kSigfoxCols) tel.rows.erase(tel.rows.begin(), tel.rows.begin() + kSigfoxCols);
        }
        // narrow carriers
        std::vector<char> hit(tracks.size(), 0);
        for (int j = 1; j + 1 < nb; j++) {
            if (!(E[j] > thr && E[j] >= E[j - 1] && E[j] >= E[j + 1])) continue;
            const double a = std::log(E[j - 1] + 1e-30), b = std::log(E[j] + 1e-30), c = std::log(E[j + 1] + 1e-30);
            const double den = a - 2 * b + c;
            const double d = den < -1e-9 ? 0.5 * (a - c) / den : 0.0;
            const double f = (kLo + j + std::max(-1.0, std::min(1.0, d))) * binHz;
            int ti = -1;
            for (size_t t = 0; t < tracks.size(); t++) if (std::fabs(tracks[t].f - f) <= kTrackHz) { ti = (int)t; break; }
            if (ti < 0) {
                Track t;
                t.f = f; t.peak = E[j];
                t.last = total1;
                t.start = total1 > (uint64_t)(N + (kAvg + 2) * hop) ? total1 - (uint64_t)(N + (kAvg + 2) * hop) : 0;
                tracks.push_back(t);
                hit.push_back(1);
            } else {
                Track& t = tracks[ti];
                if (E[j] >= 0.7f * t.peak) t.f = 0.5 * t.f + 0.5 * f;
                t.peak = std::max(t.peak * 0.995f, E[j]);
                t.last = total1;
                hit[ti] = 1;
            }
        }
        for (size_t t = 0; t < tracks.size();) {
            Track& tr = tracks[t];
            if (sec(tr.last - tr.start) > kMaxDurSec) tr.dead = true;
            if (total1 - tr.last > (uint64_t)(4 * hop)) {
                if (!tr.dead && sec(tr.last - tr.start) >= kMinDurSec) finish(tr);
                tracks.erase(tracks.begin() + (ptrdiff_t)t);
            } else t++;
        }
    }

    // ---------------------------------------------------------------- one carrier

    void finish(const Track& tr) {
        tel.carriers++;
        const uint64_t pad = (uint64_t)(0.06 * fs1);
        uint64_t s0 = tr.start > pad ? tr.start - pad : 0, s1 = std::min(total1, tr.last + pad);
        if (total1 > ring.size() && s0 < total1 - ring.size()) s0 = total1 - ring.size();
        if (s1 <= s0 + 1000) return;
        std::vector<cf32> seg(s1 - s0);
        for (uint64_t i = s0; i < s1; i++) seg[i - s0] = ring[i % ring.size()];
        for (int baud : {100, 600}) {
            SigfoxMessage m;
            double f = 0;
            if (demod(seg, tr.f, baud, m, f)) {
                m.sec = sec(s0);
                report(m, f);
                return;
            }
        }
    }

    bool demod(const std::vector<cf32>& seg, double f0, int baud, SigfoxMessage& msg, double& fCarrier) {
        const size_t L = seg.size();
        const double R1 = 20.0 * baud, step = fs1 / R1;
        const size_t nOut = (size_t)((double)L / step);
        if (nOut < 20 * 40) return false;
        // mix to the coarse frequency and cut to 20 samples per symbol with a moving average (cumulative sum, fractional edges)
        std::vector<std::complex<double>> C(L + 1);
        {
            std::complex<double> o(1, 0), r = std::polar(1.0, -2 * kPi * f0 / fs1), acc(0, 0);
            C[0] = 0;
            for (size_t i = 0; i < L; i++) {
                o *= r;
                if ((i & 1023) == 1023) o /= std::abs(o);
                acc += std::complex<double>(seg[i].real(), seg[i].imag()) * o;
                C[i + 1] = acc;
            }
        }
        auto at = [&](double t) { const size_t i = std::min(L - 1, (size_t)t); const double fr = t - (double)i; return C[i] + (C[i + 1] - C[i]) * fr; };
        std::vector<std::complex<double>> y(nOut);
        for (size_t k = 0; k < nOut; k++) y[k] = (at((double)(k + 1) * step) - at((double)k * step)) / step;
        // fine frequency: the square of a BPSK signal is a line at twice the carrier
        int NF = 4096;
        while ((size_t)NF < nOut * 4) NF *= 2;
        Fft fz(NF);
        std::vector<cf32> z(NF, cf32(0, 0));
        for (size_t k = 0; k < nOut; k++) {
            const std::complex<double> q = y[k] * y[k] * (0.5 - 0.5 * std::cos(2 * kPi * ((double)k + 0.5) / (double)nOut));
            z[k] = cf32((float)q.real(), (float)q.imag());
        }
        fz.forward(z.data());
        const double df = R1 / NF, range = 0.12 * R1 * 2;
        const int kMax = (int)(range / df);
        std::vector<float> mag(2 * kMax + 1);
        int kb = 0;
        for (int d = -kMax; d <= kMax; d++) {
            mag[d + kMax] = std::norm(z[(d + NF) % NF]);
            if (mag[d + kMax] > mag[kb + kMax]) kb = d;
        }
        double fr = 0;
        {
            std::vector<float> s = mag;
            std::nth_element(s.begin(), s.begin() + (ptrdiff_t)s.size() / 2, s.end());
            if (mag[kb + kMax] > 25.f * s[s.size() / 2] && kb > -kMax && kb < kMax) {
                const double a = std::log(mag[kb + kMax - 1] + 1e-30), b = std::log(mag[kb + kMax] + 1e-30), c = std::log(mag[kb + kMax + 1] + 1e-30);
                const double den = a - 2 * b + c;
                const double d = den < -1e-9 ? 0.5 * (a - c) / den : 0.0;
                fr = (kb + std::max(-1.0, std::min(1.0, d))) * df / 2;
            }
        }
        fCarrier = f0 + fr;
        for (size_t k = 0; k < nOut; k++) y[k] *= std::polar(1.0, -2 * kPi * fr * (double)k / R1);
        // symbol timing: the alignment where the sums over a symbol are strongest
        const int sps = 20;
        int bp = 0;
        double bm = -1;
        for (int p = 0; p < sps; p++) {
            double m = 0;
            for (size_t k = (size_t)p; k + sps <= nOut; k += sps) {
                std::complex<double> s = 0;
                for (int i = 0; i < sps; i++) s += y[k + i];
                m += std::norm(s);
            }
            if (m > bm) { bm = m; bp = p; }
        }
        std::vector<std::complex<double>> S;
        for (size_t k = (size_t)bp; k + sps <= nOut; k += sps) {
            std::complex<double> s = 0;
            for (int i = 0; i < sps; i++) s += y[k + i];
            S.push_back(s / (double)sps);
        }
        const int ns = (int)S.size();
        if (ns < 40) return false;
        std::vector<double> pw(ns);
        for (int i = 0; i < ns; i++) pw[i] = std::norm(S[i]);
        std::vector<double> sp = pw;
        std::nth_element(sp.begin(), sp.begin() + (ptrdiff_t)(sp.size() * 6 / 10), sp.end());
        const double ref = sp[sp.size() * 6 / 10], gate = 0.3 * ref;
        int n0 = -1, n1 = -1;
        for (int i = 0; i + 1 < ns; i++) if (pw[i] > gate && pw[i + 1] > gate) { n0 = i; break; }
        for (int i = ns - 1; i > 0; i--) if (pw[i] > gate && pw[i - 1] > gate) { n1 = i; break; }
        if (n0 < 0 || n1 <= n0 + kPreambleBits) return false;
        std::vector<uint8_t> b(ns - 1);
        for (int i = 0; i + 1 < ns; i++) b[i] = (S[i + 1] * std::conj(S[i])).real() > 0 ? 1 : 0;   // 1: no phase change
        int i0 = -1;
        for (int sh = 0; sh < 4 && i0 < 0; sh++) {
            int ok = 0;
            for (int j = 0; j < kPreambleBits; j++) ok += b[n0 + sh + j] == (uint8_t)(j % 2 == 0);
            if (ok >= 18) i0 = n0 + sh;
        }
        if (i0 < 0) return false;
        const int nbits = n1 - i0;
        int N12 = -1, best = 99;
        for (int n = 0; n <= 12; n++) { const int d = std::abs(frameBits(n) - nbits); if (d < best) { best = d; N12 = n; } }
        if (best > 3 || i0 + frameBits(N12) > (int)b.size()) return false;
        size_t pos = (size_t)i0 + kPreambleBits;
        auto get = [&](int n) { uint64_t v = 0; for (int i = 0; i < n; i++) v = (v << 1) | b[pos++]; return v; };
        msg = SigfoxMessage();
        msg.baud = baud;
        msg.len = N12;
        msg.frameType = (uint32_t)get(13);
        msg.li = (int)get(2);
        msg.repeatFlag = get(1) != 0;
        get(1);
        msg.seq = (uint32_t)get(12);
        msg.id = hexOf(get(32), 8);
        for (int i = 0; i < N12; i++) msg.payload += hexOf(get(8), 2);
        for (int i = 0; i < macBits(N12) / 8; i++) msg.mac += hexOf(get(8), 2);
        msg.crc = hexOf(get(16), 4);
        double acc = 0;
        for (int i = n0; i <= n1; i++) acc += pw[i];
        msg.levelDb = (float)(10 * std::log10(std::max(1e-30, acc / (n1 - n0 + 1))));
        return true;
    }

    void report(SigfoxMessage m, double fRel) {
        const int64_t now = (int64_t)std::time(nullptr);
        const double fAbs = dialHz > 0 ? dialHz + fRel : fRel;
        tel.frames++;
        for (size_t i = 0; i < tel.messages.size(); i++) {
            SigfoxMessage& q = tel.messages[i];
            if (q.id != m.id || q.seq != m.seq || q.payload != m.payload || m.sec - lastRepSec[i] > 20.0 || m.sec < q.sec - 1.0) continue;
            bool same = false;   // the same transmission seen twice (a wide 600 bit/s signal gives two peaks)
            for (double f : q.freqs) same = same || (std::fabs(f - fAbs) < 2e3 && std::fabs(m.sec - lastRepSec[i]) < 0.5);
            if (!same) { q.reps++; q.freqs.push_back(fAbs); lastRepSec[i] = m.sec; }
            q.wallTime = now;
            q.levelDb = std::max(q.levelDb, m.levelDb);
            seqNo++;
            std::rotate(tel.messages.begin(), tel.messages.begin() + (ptrdiff_t)i, tel.messages.begin() + (ptrdiff_t)i + 1);
            std::rotate(lastRepSec.begin(), lastRepSec.begin() + (ptrdiff_t)i, lastRepSec.begin() + (ptrdiff_t)i + 1);
            return;
        }
        m.wallTime = m.firstWall = now;
        m.reps = 1;
        m.freqs = {fAbs};
        tel.messages.insert(tel.messages.begin(), m);
        lastRepSec.insert(lastRepSec.begin(), m.sec);
        if (tel.messages.size() > kSigfoxMsgMax) { tel.messages.pop_back(); lastRepSec.pop_back(); }
        seqNo++;
    }
};

SigfoxRx::SigfoxRx() : p_(std::make_unique<Impl>()) {}
SigfoxRx::~SigfoxRx() = default;
void SigfoxRx::configure(double r, double off, double dial) { p_->configure(r, off, dial); }
void SigfoxRx::feed(const cf32* x, size_t n) { p_->feed(x, n); }
const SigfoxTelemetry& SigfoxRx::telemetry() const { return p_->tel; }
void SigfoxRx::clear() { p_->tel.messages.clear(); p_->lastRepSec.clear(); p_->tel.carriers = p_->tel.frames = 0; }

bool SigfoxRx::onSigfoxBand(double dialHz) {
    if (dialHz <= 0) return true;
    for (double c : {868.130e6, 902.200e6, 920.800e6}) if (std::fabs(dialHz - c) < 0.15e6) return true;
    return false;
}

} // namespace dect2
