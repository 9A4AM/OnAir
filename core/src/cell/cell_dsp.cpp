// Cell towers: resampler, mixer and spectrum (cell_dsp.h).
#include "dect2/cell_dsp.h"
#include "dect2/fftutil.h"
#include <algorithm>
#include <cmath>

namespace dect2::cell {

Resampler::Resampler(double cutoff, int halfLen) : fc_(std::max(0.005, std::min(0.5, cutoff))) {
    H_ = halfLen > 0 ? halfLen : std::max(8, (int)std::ceil(4.0 / fc_));   // about 8 zero crossings of the sinc each side
    const int n = 2 * H_ * kOver + 1;
    tab_.resize((size_t)n + 1);
    for (int i = 0; i <= n; i++) {
        const double t = (double)(i - H_ * kOver) / kOver;
        const double a = 2 * fc_ * t;
        const double s = std::fabs(a) < 1e-9 ? 1.0 : std::sin(M_PI * a) / (M_PI * a);
        const double u = t / H_;
        const double w = std::fabs(u) >= 1 ? 0.0 : 0.42 + 0.5 * std::cos(M_PI * u) + 0.08 * std::cos(2 * M_PI * u);   // Blackman
        tab_[(size_t)i] = (float)(2 * fc_ * s * w);
    }
}

void Resampler::run(const cf32* x, size_t n, double start, double step, cf32* out, size_t nOut) const {
    const float* tb = tab_.data();
    for (size_t o = 0; o < nOut; o++) {
        const double tau = start + (double)o * step;
        const long c = (long)std::floor(tau);
        const double fr = tau - (double)c;
        long i0 = c - H_ + 1, i1 = c + H_;
        float re = 0, im = 0;
        if (i1 < 0 || i0 >= (long)n) { out[o] = 0; continue; }
        i0 = std::max(i0, 0L); i1 = std::min(i1, (long)n - 1);
        // tap for input i: h(tau - i) = tab[(tau - i + H) * kOver]
        double pos = (tau - (double)i0 + H_) * kOver;
        for (long i = i0; i <= i1; i++, pos -= kOver) {
            const int p = (int)pos;
            if (p < 0 || p >= (int)tab_.size() - 1) continue;
            const float f = (float)(pos - p);
            const float h = tb[p] + f * (tb[p + 1] - tb[p]);
            re += h * x[i].real(); im += h * x[i].imag();
        }
        (void)fr;
        out[o] = cf32(re, im);
    }
}

double mixDown(cf32* x, size_t n, double hz, double rate, double phase0) {
    const double w = -2 * M_PI * hz / rate;
    double ph = phase0;
    // re-anchor the oscillator every 1024 samples so rounding never builds up
    for (size_t i = 0; i < n; i += 1024) {
        const size_t m = std::min<size_t>(1024, n - i);
        const std::complex<double> st(std::cos(w), std::sin(w));
        std::complex<double> o(std::cos(ph), std::sin(ph));
        for (size_t k = 0; k < m; k++) { x[i + k] *= cf32((float)o.real(), (float)o.imag()); o *= st; }
        ph = std::fmod(ph + w * (double)m, 2 * M_PI);
    }
    return ph;
}

std::vector<cf32> resampleBlock(const cf32* x, size_t n, double fsIn, double fsOut, double shiftHz, double bwHz, double start, size_t nOut, double clockScale) {
    const double step = fsIn / fsOut * clockScale;
    // only the part of the input that is needed is mixed
    const long a = std::max(0L, (long)std::floor(start) - 200), b = std::min((long)n, (long)std::ceil(start + step * (double)nOut) + 200);
    std::vector<cf32> m;
    if (b > a) m.assign(x + a, x + b);
    if (shiftHz != 0 && !m.empty()) mixDown(m.data(), m.size(), shiftHz, fsIn, -2 * M_PI * shiftHz / fsIn * (double)a);
    const Resampler rs(std::min(0.5, 0.5 * bwHz / fsIn), 0);
    std::vector<cf32> out(nOut);
    if (!m.empty()) rs.run(m.data(), m.size(), start - (double)a, step, out.data(), nOut);
    return out;
}

std::vector<float> welch(const cf32* x, size_t n, int nfft, int maxAvg) {
    std::vector<float> p((size_t)nfft, 0.f), w((size_t)nfft);
    for (int i = 0; i < nfft; i++) w[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2 * M_PI * i / nfft));
    Fft f(nfft);
    std::vector<cf32> b((size_t)nfft);
    const size_t segs = n / (size_t)nfft;
    if (segs == 0) return p;
    const size_t stride = std::max<size_t>(1, segs / (size_t)maxAvg);
    int cnt = 0;
    for (size_t s = 0; s < segs; s += stride) {
        for (int i = 0; i < nfft; i++) b[(size_t)i] = x[s * (size_t)nfft + (size_t)i] * w[(size_t)i];
        f.forward(b.data());
        for (int i = 0; i < nfft; i++) p[(size_t)((i + nfft / 2) % nfft)] += std::norm(b[(size_t)i]);
        cnt++;
    }
    for (auto& v : p) v /= (float)std::max(1, cnt) * (float)nfft;
    return p;
}

int pow2At(int n) { int p = 1; while (p < n) p <<= 1; return p; }

} // namespace dect2::cell
