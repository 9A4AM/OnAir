#include "dect2/t2ofdm.h"
#include <Accelerate/Accelerate.h>
#include <algorithm>
#include <cmath>

namespace dect2 {

void GridInterpolator::build(int S) {
    S_ = S;
    const int T = 2 * kHalf;
    taps_.assign((size_t)S * T, 0.f);
    auto i0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 30; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
    const double beta = 5.0;
    for (int p = 0; p < S; p++) {
        double frac = (double)p / S;
        double sum = 0;
        for (int t = 0; t < T; t++) {
            double x = frac - (t - (kHalf - 1)); // distance from the sample at index (t-(kHalf-1))
            double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
            double r = x / kHalf;
            double w = std::fabs(r) >= 1 ? 0 : i0(beta * std::sqrt(1 - r * r)) / i0(beta);
            taps_[(size_t)p * T + t] = (float)(sinc * w);
            sum += sinc * w;
        }
        for (int t = 0; t < T; t++) taps_[(size_t)p * T + t] = (float)(taps_[(size_t)p * T + t] / sum);
    }
}

void GridInterpolator::run(const std::vector<cf32>& grid, int S, int K, int N, double tau0, std::vector<cf32>& H) {
    if (S != S_) build(S);
    const int M = (int)grid.size();
    const int T = 2 * kHalf;
    // remove the delay offset so delays are centred
    std::vector<cf32> g(M);
    for (int n = 0; n < M; n++) {
        double a = 2 * M_PI * (double)(S * n) * tau0 / N;
        g[n] = grid[n] * cf32((float)std::cos(a), (float)std::sin(a));
    }
    auto at = [&](int n) -> cf32 { // odd reflection about the edges keeps linear trends
        if (n < 0) return 2.0f * g[0] - g[std::min(M - 1, -n)];
        if (n >= M) return 2.0f * g[M - 1] - g[std::max(0, 2 * (M - 1) - n)];
        return g[n];
    };
    H.resize(K);
    for (int k = 0; k < K; k++) {
        int n0 = k / S, p = k % S;
        const float* h = &taps_[(size_t)p * T];
        cf32 acc = 0;
        for (int t = 0; t < T; t++) acc += h[t] * at(n0 + t - (kHalf - 1));
        double a = -2 * M_PI * (double)k * tau0 / N;
        H[k] = acc * cf32((float)std::cos(a), (float)std::sin(a));
    }
}

void impulseResponse(const std::vector<cf32>& H, int N, int tauMin, int tauMax, int centre, std::vector<float>& outDb) {
    int lg = (int)std::lround(std::log2((double)N));
    static FFTSetup setup[17] = {};
    if (!setup[lg]) setup[lg] = vDSP_create_fftsetup(lg, kFFTRadix2);
    std::vector<float> re(N, 0.f), im(N, 0.f);
    int K = (int)H.size();
    for (int k = 0; k < K; k++) {
        int f = k - (K - 1) / 2;
        int b = (f + N) % N;
        re[b] = H[k].real();
        im[b] = H[k].imag();
    }
    DSPSplitComplex sc{re.data(), im.data()};
    vDSP_fft_zip(setup[lg], &sc, 1, lg, FFT_INVERSE);
    outDb.assign(std::max(0, tauMax - tauMin), -120.f);
    double peak = 1e-30;
    for (int n = 0; n < N; n++) peak = std::max(peak, (double)re[n] * re[n] + (double)im[n] * im[n]);
    for (int t = tauMin; t < tauMax; t++) {
        int idx = ((t + centre) % N + N) % N;
        double p = ((double)re[idx] * re[idx] + (double)im[idx] * im[idx]) / peak;
        outDb[t - tauMin] = (float)(10 * std::log10(std::max(p, 1e-12)));
    }
}

} // namespace dect2
