// Parts of the MFSK, THOR and DominoEX modems (hfdig_kbd.h): the rate converter for the 11.025 kHz modes, the sliding DFT bank, the
// convolutional encoder and Viterbi decoder, and the block interleaver, after fldigi (GPL, w1hkj/fldigi: src/filters/viterbi.cxx,
// src/mfsk/interleave.cxx, src/filters/sfft.cxx) and written again here.
#pragma once
#include "hfdig_kbd_int.h"
#include <climits>
#include <cstring>
#include "dect2/common/bits.h"

namespace dect2 {
namespace kbd {

// ------------------------------------------------------------------ rate converter
// Windowed sinc interpolation (Blackman window, 2 * kT taps), cutoff 3.8 kHz, for 8000 <-> 11025
class Resampler {
public:
    static constexpr int kT = 24, kPhases = 512;
    Resampler(double inRate, double outRate) : step_(inRate / outRate) {
        const double fcn = 3800.0 / inRate;   // cutoff in cycles per input sample
        tab_.resize((size_t)kPhases * 2 * kT);
        for (int p = 0; p < kPhases; p++) {
            const double frac = (double)p / kPhases;
            double sum = 0;
            for (int k = 0; k < 2 * kT; k++) {
                const double d = frac + (kT - 1 - k);   // distance of the output point from input sample floor(u) - kT + 1 + k
                const double w = 0.42 + 0.5 * std::cos(kPi * d / kT) + 0.08 * std::cos(2 * kPi * d / kT);
                const double a = 2 * kPi * fcn * d;
                const double v = (std::fabs(a) < 1e-9 ? 1.0 : std::sin(a) / a) * w;
                tab_[(size_t)p * 2 * kT + k] = (float)v;
                sum += v;
            }
            for (int k = 0; k < 2 * kT; k++) tab_[(size_t)p * 2 * kT + k] = (float)(tab_[(size_t)p * 2 * kT + k] / sum);
        }
    }
    void process(const float* x, size_t n, std::vector<float>& out) {
        buf_.insert(buf_.end(), x, x + n);
        // output point u_ (absolute input index); needs samples up to floor(u_) + kT
        for (;;) {
            const long fl = (long)std::floor(u_);
            if (fl + kT >= base_ + (long)buf_.size()) break;
            if (fl - kT + 1 < base_) { u_ += step_; continue; }
            const int ph = std::min(kPhases - 1, (int)((u_ - (double)fl) * kPhases));
            const float* h = &buf_[(size_t)(fl - kT + 1 - base_)];
            const float* t = &tab_[(size_t)ph * 2 * kT];
            float acc = 0;
            for (int k = 0; k < 2 * kT; k++) acc += h[k] * t[k];
            out.push_back(acc);
            u_ += step_;
        }
        const long keepFrom = (long)std::floor(u_) - kT;
        if (keepFrom > base_ + 4096) { buf_.erase(buf_.begin(), buf_.begin() + (keepFrom - base_)); base_ = keepFrom; }
    }
private:
    double step_, u_ = 0;
    long base_ = 0;
    std::vector<float> tab_, buf_;
};

// ------------------------------------------------------------------ sliding DFT bank
// The DFT over the last N samples at arbitrary frequencies, updated every sample (absolute phase, so the frequencies need not be bins)
class Bank {
public:
    void init(int n, double fs, const std::vector<double>& freqHz) {
        n_ = n;
        const int m = (int)freqHz.size();
        xr_.assign(m, 0); xi_.assign(m, 0); pr_.assign(m, 1); pi_.assign(m, 0);
        rr_.resize(m); ri_.resize(m); cr_.resize(m); ci_.resize(m);
        for (int j = 0; j < m; j++) {
            const double w = 2 * kPi * freqHz[j] / fs;
            rr_[j] = std::cos(w); ri_[j] = -std::sin(w);                  // e^{-jw}
            cr_[j] = std::cos(w * n); ci_[j] = std::sin(w * n);          // e^{+jwN}
        }
        ring_.assign(n, 0.0);
        pos_ = 0; count_ = 0;
    }
    int bins() const { return (int)xr_.size(); }
    void feed(double x) {
        const double xo = ring_[pos_];
        ring_[pos_] = x;
        if (++pos_ >= n_) pos_ = 0;
        const int m = (int)xr_.size();
        for (int j = 0; j < m; j++) {
            // X += (x - xo * e^{jwN}) * e^{-jwn}
            const double ar = x - xo * cr_[j], ai = -xo * ci_[j];
            xr_[j] += ar * pr_[j] - ai * pi_[j];
            xi_[j] += ar * pi_[j] + ai * pr_[j];
            const double nr = pr_[j] * rr_[j] - pi_[j] * ri_[j];
            pi_[j] = pr_[j] * ri_[j] + pi_[j] * rr_[j];
            pr_[j] = nr;
        }
        if (++count_ >= n_) {
            count_ = 0;
            for (int j = 0; j < m; j++) { const double a = 1.0 / std::hypot(pr_[j], pi_[j]); pr_[j] *= a; pi_[j] *= a; }
        }
    }
    double mag(int j) const { return std::hypot(xr_[j], xi_[j]); }
    double energy(int j) const { return xr_[j] * xr_[j] + xi_[j] * xi_[j]; }
    void clear() { std::fill(xr_.begin(), xr_.end(), 0.0); std::fill(xi_.begin(), xi_.end(), 0.0); std::fill(ring_.begin(), ring_.end(), 0.0); }
private:
    int n_ = 0, pos_ = 0, count_ = 0;
    std::vector<double> xr_, xi_, pr_, pi_, rr_, ri_, cr_, ci_, ring_;
};

// ------------------------------------------------------------------ convolutional code
inline int parity32(unsigned v) { return dect2::bits::parity((uint32_t)v); }

class ConvEnc {
public:
    ConvEnc(int k, unsigned p1, unsigned p2) : mask_((1u << k) - 1), p1_(p1), p2_(p2) {}
    // two bits: bit 0 from the first polynomial, bit 1 from the second
    int encode(int bit) { sh_ = (sh_ << 1) | (bit ? 1u : 0u); const unsigned s = sh_ & mask_; return parity32(p1_ & s) | (parity32(p2_ & s) << 1); }
    void reset() { sh_ = 0; }
private:
    unsigned sh_ = 0, mask_, p1_, p2_;
};

// Soft decision Viterbi decoder (symbols 0 .. 255, 128 = unknown), one bit per pair of symbols after the traceback
class Viterbi {
public:
    static constexpr int kPath = 256;
    Viterbi(int k, unsigned p1, unsigned p2, int traceback) : nst_(1 << (k - 1)), trace_(traceback) {
        out_.resize((size_t)1 << k);
        for (unsigned i = 0; i < out_.size(); i++) out_[i] = (uint8_t)(parity32(p1 & i) | (parity32(p2 & i) << 1));
        met_.assign((size_t)kPath * nst_, 0);
        hist_.assign((size_t)kPath * nst_, 0);
        seq_.assign(kPath, 0);
    }
    void reset() { std::fill(met_.begin(), met_.end(), 0); std::fill(hist_.begin(), hist_.end(), (uint16_t)0); ptr_ = 0; }
    // returns the decoded bit (0 / 1) and the metric difference in *metric (0 .. about 255 * ...)
    int decode(const uint8_t* sym, int* metric) {
        const unsigned cur = ptr_, prev = (cur + kPath - 1) % kPath;
        int mt[4];
        const int s0 = 128 - sym[0], s1 = 128 - sym[1], t0 = sym[0] - 128, t1 = sym[1] - 128;
        mt[0] = s1 + s0; mt[1] = s1 + t0; mt[2] = t1 + s0; mt[3] = t1 + t0;
        int* mc = &met_[(size_t)cur * nst_];
        const int* mp = &met_[(size_t)prev * nst_];
        uint16_t* hc = &hist_[(size_t)cur * nst_];
        for (int n = 0; n < nst_; n++) {
            const int a = n, b = n + nst_, pa = a >> 1, pb = b >> 1;
            const int m0 = mp[pa] + mt[out_[a]], m1 = mp[pb] + mt[out_[b]];
            if (m0 > m1) { mc[n] = m0; hc[n] = (uint16_t)pa; } else { mc[n] = m1; hc[n] = (uint16_t)pb; }
        }
        ptr_ = (ptr_ + 1) % kPath;
        // chunk size 1: trace back every time
        unsigned p = (ptr_ + kPath - 1) % kPath;
        int best = INT_MIN, bs = 0;
        const int* mm = &met_[(size_t)p * nst_];
        for (int i = 0; i < nst_; i++) if (mm[i] > best) { best = mm[i]; bs = i; }
        seq_[p] = bs;
        for (int i = 0; i < trace_; i++) {
            const unsigned pv = (p + kPath - 1) % kPath;
            seq_[pv] = hist_[(size_t)p * nst_ + seq_[p]];
            p = pv;
        }
        const int m0v = met_[(size_t)p * nst_ + seq_[p]];
        const int bit = seq_[p] & 1;
        p = (p + 1) % kPath;
        if (metric) *metric = met_[(size_t)p * nst_ + seq_[p]] - m0v;
        // keep the metrics from overflowing
        if (mc[0] > INT_MAX / 4) { for (auto& v : met_) v -= INT_MAX / 8; }
        else if (mc[0] < INT_MIN / 4) { for (auto& v : met_) v += INT_MAX / 8; }
        return bit;
    }
private:
    int nst_, trace_;
    unsigned ptr_ = 0;
    std::vector<uint8_t> out_;
    std::vector<int> met_, seq_;
    std::vector<uint16_t> hist_;
};

// ------------------------------------------------------------------ interleaver (fldigi's mfsk/interleave.cxx)
class Interleaver {
public:
    Interleaver(int size, int depth, bool forward) : size_(size), depth_(depth), fwd_(forward) {
        tab_.assign((size_t)size * size * depth, forward ? 0 : 128);
    }
    void symbols(uint8_t* p) {
        for (int k = 0; k < depth_; k++) {
            uint8_t* t = &tab_[(size_t)size_ * size_ * k];
            for (int i = 0; i < size_; i++)
                for (int j = 0; j < size_ - 1; j++) t[i * size_ + j] = t[i * size_ + j + 1];
            for (int i = 0; i < size_; i++) t[i * size_ + size_ - 1] = p[i];
            for (int i = 0; i < size_; i++) p[i] = fwd_ ? t[i * size_ + size_ - i - 1] : t[i * size_ + i];
        }
    }
    void bits(unsigned* pb) {
        uint8_t s[16];
        for (int i = 0; i < size_; i++) s[i] = (uint8_t)((*pb >> (size_ - i - 1)) & 1);
        symbols(s);
        unsigned v = 0;
        for (int i = 0; i < size_; i++) v = (v << 1) | s[i];
        *pb = v;
    }
private:
    int size_, depth_;
    bool fwd_;
    std::vector<uint8_t> tab_;
};

inline double decayAvg(double avg, double input, double weight) { return avg - avg / weight + input / weight; }

} // namespace kbd
} // namespace dect2
