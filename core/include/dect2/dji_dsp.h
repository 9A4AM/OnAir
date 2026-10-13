// Sample-rate conversion for the DJI mode: a windowed-sinc polyphase table (256 phases, linearly interpolated), used by the receiver to bring a burst from the
// radio's rate to 15.36 Msps (centred on the burst) and by the test signal to go the other way.
#pragma once
#include "ring.h"
#include <cmath>
#include <vector>

namespace dect2::dji {

class Resampler {
public:
    // cutHz: the pass band edge (half the width of what is kept)
    Resampler(double inRate, double outRate, double cutHz) : inRate_(inRate), outRate_(outRate) {
        const double fc = cutHz / inRate;                       // cycles per input sample
        hw_ = (int)std::ceil(6.0 / (2 * fc));                   // six zero crossings each side
        taps_ = 2 * hw_;
        table_.assign((size_t)(kPhases + 1) * (size_t)taps_, 0.f);
        for (int p = 0; p <= kPhases; p++) {
            const double frac = (double)p / kPhases;            // the output sits frac after input sample j0
            for (int t = 0; t < taps_; t++) {
                const double d = (double)(t - hw_ + 1) - frac;  // distance of input sample (j0 + t - hw + 1) from the output
                const double a = 2 * fc * d;
                const double sinc = std::abs(a) < 1e-9 ? 1.0 : std::sin(3.14159265358979323846 * a) / (3.14159265358979323846 * a);
                const double w = std::abs(d) >= hw_ ? 0.0 : 0.42 + 0.5 * std::cos(3.14159265358979323846 * d / hw_) + 0.08 * std::cos(2 * 3.14159265358979323846 * d / hw_);
                table_[(size_t)p * (size_t)taps_ + (size_t)t] = (float)(2 * fc * sinc * w);
            }
        }
    }
    // Output m is taken at input position start + m * inRate / outRate (input samples; may be fractional). Positions outside the input read as zero.
    std::vector<cf32> run(const cf32* in, size_t nIn, double start, size_t nOut) const {
        std::vector<cf32> out(nOut);
        const double step = inRate_ / outRate_;
        for (size_t m = 0; m < nOut; m++) {
            const double pos = start + (double)m * step;
            const double fl = std::floor(pos);
            const long j0 = (long)fl;
            const double ph = (pos - fl) * kPhases;
            const int p0 = (int)ph;
            const float a = (float)(ph - p0);
            const float* h0 = &table_[(size_t)p0 * (size_t)taps_];
            const float* h1 = h0 + taps_;
            float re = 0, im = 0;
            const long first = j0 - hw_ + 1;
            const int lo = first < 0 ? (int)(-first) : 0;
            const int hi = first + taps_ > (long)nIn ? (int)((long)nIn - first) : taps_;
            for (int t = lo; t < hi; t++) {
                const float h = h0[t] + a * (h1[t] - h0[t]);
                const cf32 v = in[first + t];
                re += h * v.real(); im += h * v.imag();
            }
            out[m] = cf32(re, im);
        }
        return out;
    }
    std::vector<cf32> run(const std::vector<cf32>& in, double start, size_t nOut) const { return run(in.data(), in.size(), start, nOut); }
private:
    static constexpr int kPhases = 256;
    double inRate_, outRate_;
    int hw_ = 0, taps_ = 0;
    std::vector<float> table_;
};

// multiply by exp(j 2 pi f n / rate) starting at sample n0
inline void mixInPlace(cf32* x, size_t n, double f, double rate, double n0 = 0) {
    const double step = 2 * 3.14159265358979323846 * f / rate;
    double ph = std::fmod(step * n0, 2 * 3.14159265358979323846);
    cf32 p((float)std::cos(ph), (float)std::sin(ph));
    const cf32 inc((float)std::cos(step), (float)std::sin(step));
    for (size_t i = 0; i < n; i++) {
        x[i] *= p;
        p *= inc;
        if ((i & 1023) == 1023) p /= std::abs(p);
    }
}

} // namespace dect2::dji
