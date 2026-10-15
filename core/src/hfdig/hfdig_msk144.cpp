// MSK144 (meteor scatter) of the HF digital receiver: 144-bit frames of 72 ms (864 samples at 12 kHz, 2000 baud, minimum-shift keying = OQPSK
// with half-sine pulses of 12 samples) around a carrier fc (1500 Hz): 8 sync bits, 48 code bits, 8 sync bits, 80 code bits; the 128 code bits are the
// LDPC (128,90) code of the 77-bit message and its CRC-13; the frame repeats without a break for the whole T/R period (15 or 30 s), meteor
// bursts of 10 ms to 1 s are heard. Decoder: the 12 kHz analytic signal -> in steps of 18 ms the spectrum of its square (two lines at
// 2 (fc +- 500 Hz) mark a burst, and give fc) -> for the strongest bursts: a mix to baseband at a few trial frequencies, frames averaged
// coherently (several patterns), the frame start found by correlation with the two sync words, the phase from them, soft bits by the half-sine
// matched filter, belief propagation (then OSD) and the CRC-13. Reference: WSJT-X lib/mskrtd.f90, msk144spd.f90, msk144sync.f90,
// msk144decodeframe.f90, genmsk_128_90.f90, encode_128_90.f90, crc13.cpp (K1JT, GPL-3). Not done: the 40-bit short messages (MSK40), the
// receiver equaliser, the adaptive 'long averages'.
#include "hfdig_wsjt_int.h"
#include "dect2/fftutil.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <set>

namespace dect2 {
namespace wsjt {

namespace {

typedef std::complex<float> cf;
constexpr int kSpm = 864;                       // samples per frame
constexpr double kRate = 12000.0;
const int kS8[8] = {0, 1, 1, 1, 0, 0, 1, 0};

// the 13-bit CRC of WSJT-X (crc13.cpp, boost augmented_crc<13, 0x15D7>) of the 77 message bits followed by zeros up to 96 bits
uint32_t crc13(const uint8_t* bits77) {
    uint32_t reg = 0;
    for (int i = 0; i < 96; i++) {
        const uint32_t bit = i < 77 ? (uint32_t)(bits77[i] & 1) : 0;
        const uint32_t top = (reg >> 12) & 1;
        reg = ((reg << 1) | bit) & 0x1FFF;
        if (top) reg ^= 0x15D7;
    }
    return reg;
}

} // namespace

bool msk144Frame(const std::string& msg, uint8_t* bits144) {
    uint8_t b[77], m90[90], cw[128];
    if (!ftx::pack77(msg, b)) return false;
    for (int i = 0; i < 77; i++) m90[i] = b[i] & 1;
    const uint32_t c = crc13(b);
    for (int i = 0; i < 13; i++) m90[77 + i] = (uint8_t)((c >> (12 - i)) & 1);
    ldpcEncode(kLdpc128_90, m90, cw);
    for (int i = 0; i < 8; i++) { bits144[i] = (uint8_t)kS8[i]; bits144[56 + i] = (uint8_t)kS8[i]; }
    for (int i = 0; i < 48; i++) bits144[8 + i] = cw[i];
    for (int i = 0; i < 80; i++) bits144[64 + i] = cw[48 + i];
    return true;
}

void addMsk144(const uint8_t* bits144, double fc, double amp, double rate, double start, double lenSec, std::vector<float>& out) {
    // the tones of WSJT-X's genmsk_128_90 (the MSK as two-tone FSK, 6 samples per symbol of 0.5 ms): fc - 500 Hz for 0, fc + 500 Hz for 1
    int b[145];
    for (int k = 1; k <= 144; k++) b[k] = 2 * bits144[k - 1] - 1;
    int tone[145];
    for (int i = 1; i <= 72; i++) {
        tone[2 * i - 1] = (b[2 * i] * b[2 * i - 1] + 1) / 2;
        tone[2 * i] = -(b[2 * i] * b[(2 * i) % 144 + 1] - 1) / 2;
    }
    const double sym = 1.0 / 2000.0;
    const long nsym = lenSec > 0 ? (long)std::ceil(lenSec / sym) : 0;
    std::vector<double> fr((size_t)nsym);
    for (long k = 0; k < nsym; k++) fr[(size_t)k] = tone[1 + (k % 144)] ? fc - 500.0 : fc + 500.0;   // i4tone = 1 - tone
    addFsk(fr, sym, amp, rate, start, out);
}

namespace {

class Msk144 {
public:
    explicit Msk144(const Slot& s) : s_(s) {
        for (int i = 0; i < 12; i++) pp_[i] = (float)std::sin(i * M_PI / 12.0);
        // the sync template: Q pulses of the odd bits, I pulses of the even ones, bit k at sample 6 (k - 2) of the frame
        int s8[8];
        for (int i = 0; i < 8; i++) s8[i] = 2 * kS8[i] - 1;
        for (int i = 0; i < 42; i++) {
            float q = 0, in = 0;
            if (i < 6) q = pp_[i + 6] * s8[0];
            else if (i < 18) q = pp_[i - 6] * s8[2];
            else if (i < 30) q = pp_[i - 18] * s8[4];
            else q = pp_[i - 30] * s8[6];
            if (i < 12) in = pp_[i] * s8[1];
            else if (i < 24) in = pp_[i - 12] * s8[3];
            else if (i < 36) in = pp_[i - 24] * s8[5];
            else in = pp_[i - 36] * s8[7];
            cb_[i] = cf(in, q);
        }
    }

    std::vector<FtxDecode> run() {
        std::vector<FtxDecode> res;
        y_ = resample8k(s_.x, s_.n, kRate);
        if (y_.size() < 4 * (size_t)kSpm) return res;
        analytic();
        // detection metric every 216 samples
        const int step = 216;
        const int nstep = (int)((z_.size() - kSpm) / step);
        if (nstep < 4) return res;
        const int NF = 1024;
        Fft fft(NF);
        std::vector<cf> buf((size_t)NF);
        const double df = kRate / NF;
        std::vector<float> det((size_t)nstep, 0.f), fcEst((size_t)nstep, 0.f), rat((size_t)nstep, 0.f);
        std::vector<float> spec((size_t)NF / 2), specSum((size_t)NF / 2, 0.f);
        const int fcLo = (int)std::floor(std::max(500.0, s_.fLo + 500.0)), fcHi = (int)std::ceil(std::min(2800.0, s_.fHi - 500.0));
        const int off = (int)std::lround(2000.0 / df);
        const int iLo = (int)std::floor(2.0 * (fcLo - 500) / df), iHi = (int)std::ceil(2.0 * (fcHi - 500) / df);
        // the two lines of the squared signal, 2000 Hz apart: the best sum of their (3-bin) peaks, its bin, fc, and the ratio to the mean of the band
        auto lines = [&](const std::vector<float>& sp, int& bi, double& fcOut) {
            float best = 0;
            bi = 0;
            for (int i = std::max(2, iLo); i <= iHi && i + off + 1 < NF / 2; i++) {
                const float v = std::max(sp[(size_t)i - 1], std::max(sp[(size_t)i], sp[(size_t)i + 1])) +
                                std::max(sp[(size_t)i + off - 1], std::max(sp[(size_t)i + off], sp[(size_t)i + off + 1]));
                if (v > best) { best = v; bi = i; }
            }
            fcOut = 0;
            if (bi > 2) {
                auto peak = [&](int i0) {
                    int p = i0;
                    for (int d = -1; d <= 1; d++) if (sp[(size_t)(i0 + d)] > sp[(size_t)p]) p = i0 + d;
                    const double a1 = sp[(size_t)p - 1], b1 = sp[(size_t)p], c1 = sp[(size_t)p + 1];
                    const double den = a1 - 2 * b1 + c1;
                    return (p + (den != 0 ? 0.5 * (a1 - c1) / den : 0.0)) * df;
                };
                fcOut = (peak(bi) + peak(bi + off)) / 4.0;   // fc = (f_lo + f_hi) / 4
            }
            const int a1 = std::max(2, iLo), b1 = std::min(NF / 2 - 2, iHi + off + 2);
            double tot = 0;
            for (int i = a1; i <= b1; i++) tot += sp[(size_t)i];
            return (float)(best / (2.0 * tot / std::max(1, b1 - a1 + 1) + 1e-30));
        };
        for (int k = 0; k < nstep; k++) {
            std::fill(buf.begin(), buf.end(), cf(0, 0));
            for (int i = 0; i < kSpm; i++) {
                const cf v = z_[(size_t)k * step + (size_t)i];
                float w = 1;
                if (i < 12) w = (float)(0.5 * (1 - std::cos(M_PI * i / 12.0)));
                else if (i >= kSpm - 12) w = (float)(0.5 * (1 - std::cos(M_PI * (kSpm - 1 - i) / 12.0)));
                buf[(size_t)i] = v * v * w;
            }
            fft.forward(buf.data());
            for (int i = 0; i < NF / 2; i++) { spec[(size_t)i] = std::norm(buf[(size_t)i]); specSum[(size_t)i] += spec[(size_t)i]; }
            int bi;
            double fcv;
            rat[(size_t)k] = lines(spec, bi, fcv);
            fcEst[(size_t)k] = (float)fcv;
            const float v = std::max(spec[(size_t)bi - 1], std::max(spec[(size_t)bi], spec[(size_t)bi + 1])) + std::max(spec[(size_t)bi + off - 1], std::max(spec[(size_t)bi + off], spec[(size_t)bi + off + 1]));
            det[(size_t)k] = bi > 2 ? v : 0.f;
        }
        int biAvg;
        double fcAvg;
        const float ratAvg = lines(specSum, biAvg, fcAvg);
        std::vector<float> sorted(det);
        std::sort(sorted.begin(), sorted.end());
        const float med = std::max(1e-20f, sorted[sorted.size() / 4]);
        if (wsjtDebug()) { std::vector<float> r2(rat); std::sort(r2.begin(), r2.end()); printf("MSK144: %d windows, median %.3g, max %.2f, ratio: median %.1f p90 %.1f max %.1f, average over time %.1f at %.1f Hz\n", nstep, med, sorted.back() / med, r2[r2.size() / 2], r2[r2.size() * 9 / 10], r2.back(), ratAvg, fcAvg); }
        // candidates: the strongest windows, at least 4 frames apart
        std::vector<int> cands;
        {
            std::vector<float> d2(nstep);
            for (int k = 3; k < nstep - 3; k++) d2[(size_t)k] = (det[(size_t)k] / med >= kMinDet || rat[(size_t)k] >= kMinRatio) ? det[(size_t)k] : 0.f;
            for (int n = 0; n < kMaxCand; n++) {
                int k = (int)(std::max_element(d2.begin(), d2.end()) - d2.begin());
                if (d2[(size_t)k] <= 0) break;
                cands.push_back(k);
                for (int j = std::max(0, k - 12); j <= std::min(nstep - 1, k + 12); j++) d2[(size_t)j] = 0;
            }
        }
        // a transmission that does not stop (a test, or a very long burst) shows up in the average over time: windows spread over the period
        std::vector<std::pair<int, double>> work;
        for (int k : cands) if (fcEst[(size_t)k] > 0) work.push_back({k, fcEst[(size_t)k]});
        if (ratAvg >= kMinAvg && fcAvg > 0) for (int k = 4; k < nstep - 4 && (int)work.size() < kMaxCand + 16; k += std::max(1, nstep / 16)) work.push_back({k, fcAvg});
        std::set<std::string> seen;
        for (const auto& wk : work) {
            const int k = wk.first;
            FtxDecode d;
            const bool ok = tryWindow((long)k * step, wk.second, d);
            if (wsjtDebug()) printf("  window %d (%.2f s) fc %.1f: %s\n", k, (double)k * step / kRate, wk.second, ok ? d.msg.c_str() : "no");
            if (ok && seen.insert(d.msg).second) res.push_back(d);
        }
        return res;
    }

private:
    static constexpr int kMaxCand = 12;
    static constexpr float kMinDet = 3.0f, kMinRatio = 40.0f, kMinAvg = 1.8f;

    void analytic() {
        int n = 1;
        while (n < (int)y_.size()) n <<= 1;
        Fft fft(n);
        std::vector<cf> buf((size_t)n, cf(0, 0));
        for (size_t i = 0; i < y_.size(); i++) buf[i] = cf(y_[i], 0.f);
        fft.forward(buf.data());
        for (int i = 1; i < n / 2; i++) buf[(size_t)i] *= 2.f;
        for (int i = n / 2 + 1; i < n; i++) buf[(size_t)i] = cf(0, 0);
        fft.inverse(buf.data());
        z_.resize(y_.size());
        for (size_t i = 0; i < y_.size(); i++) z_[i] = buf[i] / (float)n;
    }

    // coherent average of frames of the mix to baseband at fo; the patterns of 1, 2 and 3 frames
    bool tryWindow(long ns, double fc, FtxDecode& out) {
        const long ib = std::max(0L, std::min(ns - kSpm, (long)z_.size() - 3 * kSpm));
        static const int pat[6][3] = {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}, {1, 1, 0}, {0, 1, 1}, {1, 1, 1}};
        for (int ip = 0; ip < 6; ip++) {
            for (int fo = -3; fo <= 3; fo++) {
                const double f = fc + 2.0 * fo / std::max(1, pat[ip][0] + pat[ip][1] + pat[ip][2]) * 1.0;
                std::vector<cf> c((size_t)kSpm, cf(0, 0));
                for (int m = 0; m < 3; m++) {
                    if (!pat[ip][m]) continue;
                    for (int i = 0; i < kSpm; i++) {
                        const long n = ib + (long)m * kSpm + i;
                        const double ph = -2 * M_PI * f * (double)n / kRate;
                        c[(size_t)i] += z_[(size_t)n] * cf((float)std::cos(ph), (float)std::sin(ph));
                    }
                }
                // frame start: the correlation with the two sync words
                float bestV = 0;
                int bestT = 0;
                std::vector<float> metric((size_t)kSpm);
                for (int t = 0; t < kSpm; t++) {
                    cf a(0, 0), b(0, 0);
                    for (int i = 0; i < 42; i++) {
                        a += c[(size_t)((t + i) % kSpm)] * std::conj(cb_[i]);
                        b += c[(size_t)((t + 336 + i) % kSpm)] * std::conj(cb_[i]);
                    }
                    metric[(size_t)t] = std::abs(a + b);
                    if (metric[(size_t)t] > bestV) { bestV = metric[(size_t)t]; bestT = t; }
                }
                // the best two peaks, and the neighbours of each (the slicer dither of WSJT-X)
                int order[2] = {bestT, -1};
                float second = 0;
                for (int t = 0; t < kSpm; t++) {
                    const int d = std::min((t - bestT + kSpm) % kSpm, (bestT - t + kSpm) % kSpm);
                    if (d > 6 && metric[(size_t)t] > second) { second = metric[(size_t)t]; order[1] = t; }
                }
                for (int p = 0; p < 2; p++) {
                    if (order[p] < 0) continue;
                    for (int dd : {0, -1, 1}) {
                        const int t = ((order[p] + dd) % kSpm + kSpm) % kSpm;
                        if (decodeFrame(c, t, out)) {
                            out.mode = ftx::kMsk144;
                            out.hz = (float)(f - 500.0);   // the lower of the two tones, as for the other modes
                            out.dt = (float)(((double)ib + (double)t) / kRate + s_.x0);
                            out.pass = pat[ip][0] + pat[ip][1] + pat[ip][2] - 1;
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    bool decodeFrame(const std::vector<cf>& c0, int t, FtxDecode& out) {
        std::vector<cf> c((size_t)kSpm);
        for (int i = 0; i < kSpm; i++) c[(size_t)i] = c0[(size_t)((t + i) % kSpm)];
        cf cca(0, 0), ccb(0, 0);
        for (int i = 0; i < 42; i++) { cca += c[(size_t)i] * std::conj(cb_[i]); ccb += c[(size_t)(336 + i)] * std::conj(cb_[i]); }
        const cf sum = cca + ccb;
        if (std::abs(sum) <= 0) return false;
        const cf rot = std::conj(sum / std::abs(sum));
        for (auto& v : c) v *= rot;
        float soft[144];
        // bit 1 (Q): the second half of its pulse at the start of the frame and the first half at its end
        {
            float a = 0;
            for (int i = 0; i < 6; i++) a += c[(size_t)i].imag() * pp_[6 + i];
            for (int i = 0; i < 6; i++) a += c[(size_t)(kSpm - 6 + i)].imag() * pp_[i];
            soft[0] = a;
            float b = 0;
            for (int i = 0; i < 12; i++) b += c[(size_t)i].real() * pp_[i];
            soft[1] = b;
        }
        for (int i = 2; i <= 72; i++) {
            float a = 0, b = 0;
            for (int j = 0; j < 12; j++) {
                a += c[(size_t)((i - 1) * 12 - 6 + j)].imag() * pp_[j];
                b += c[(size_t)((i - 1) * 12 + 6 - 6 + j)].real() * pp_[j];
            }
            soft[2 * i - 2] = a;
            soft[2 * i - 1] = b;
        }
        int bad = 0;
        for (int k = 0; k < 8; k++) {
            bad += ((soft[k] >= 0) != (kS8[k] == 1));
            bad += ((soft[56 + k] >= 0) != (kS8[k] == 1));
        }
        if (bad > 4) return false;
        double sav = 0, s2av = 0, ma = 0;
        for (int k = 0; k < 144; k++) { sav += soft[k]; s2av += (double)soft[k] * soft[k]; ma += std::fabs(soft[k]); }
        sav /= 144; s2av /= 144; ma /= 144;
        const double ssig = std::sqrt(std::max(1e-30, s2av - sav * sav));
        float llr[128];
        const double sigma = 0.60;
        for (int k = 0; k < 48; k++) llr[k] = (float)(2.0 * (soft[8 + k] / ssig) / (sigma * sigma));
        for (int k = 0; k < 80; k++) llr[48 + k] = (float)(2.0 * (soft[64 + k] / ssig) / (sigma * sigma));
        uint8_t cw[128];
        // belief propagation only (an ordered-statistics decoder with the 13-bit CRC alone would pass many false messages from the thousands of tries)
        if (ldpcBp(kLdpc128_90, llr, 30, cw) != 0 || !crcOk(cw)) return false;
        int hardErr = 0;
        for (int i = 0; i < 128; i++) hardErr += (llr[i] > 0) != (cw[i] != 0);
        if (hardErr >= 18) return false;
        uint8_t b77[77];
        for (int i = 0; i < 77; i++) b77[i] = cw[i];
        {   // the message types of WSJT-X 2.x: i3 / n3 of the 77 bits (bits 72 .. 77)
            const int n3 = b77[71] * 4 + b77[72] * 2 + b77[73], i3 = b77[74] * 4 + b77[75] * 2 + b77[76];
            if ((i3 == 0 && (n3 == 1 || n3 == 3 || n3 == 4 || n3 > 5)) || i3 == 3 || i3 > 5) return false;
        }
        const std::string msg = ftx::unpack77(b77, &hash_);
        if (msg.empty()) return false;
        out.msg = msg;
        // the signal to noise ratio from the soft bits: the mean of their size against the spread around it (about the 2000 Hz of the matched filter)
        double var = 0;
        for (int k = 0; k < 144; k++) { const double d = std::fabs(soft[k]) - ma; var += d * d; }
        var /= 144;
        out.snrDb = (float)(10 * std::log10(std::max(1e-3, ma * ma / std::max(1e-30, var)) * 2000.0 / 2500.0 * 0.5));
        fillCallGrid(out);
        return true;
    }

    static bool crcOk(const uint8_t* cw) {
        const uint32_t c = crc13(cw);
        uint32_t r = 0;
        for (int i = 0; i < 13; i++) r = (r << 1) | cw[77 + i];
        return c == r;
    }

    Slot s_;
    float pp_[12];
    cf cb_[42];
    std::vector<float> y_;
    std::vector<cf> z_;
    ftx::CallHash hash_;
};

} // namespace

std::vector<FtxDecode> decodeMsk144(const Slot& s) { Msk144 d(s); return d.run(); }

} // namespace wsjt
} // namespace dect2
