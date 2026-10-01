// Pilot-aided channel estimation helpers for the T2 receiver.
#pragma once
#include "ring.h"
#include <vector>

namespace dect2 {

// Windowed-sinc interpolation of a regular pilot grid (spacing S carriers) onto every carrier.
// A delay offset is removed before interpolation and re-applied after, so the channel's delay support
// [tau0 - N/(2S), tau0 + N/(2S)] is centred in the interpolator's passband.
class GridInterpolator {
public:
    // grid[n] is the channel value at carrier k = S*n (n = 0..M-1); output H[k], k = 0..K-1.
    void run(const std::vector<cf32>& grid, int S, int K, int N, double tau0, std::vector<cf32>& H);

private:
    int S_ = 0;
    std::vector<float> taps_; // [phase][tap]
    static constexpr int kHalf = 8;
    void build(int S);
};

// Power-delay profile in dB from a channel estimate: returns |h(tau)|^2 for tau = tauMin .. tauMax-1 samples
// (negative delays wrap from the end of the IFFT). `centre` shifts the axis so the main path sits near 0.
void impulseResponse(const std::vector<cf32>& H, int N, int tauMin, int tauMax, int centre, std::vector<float>& outDb);

} // namespace dect2
