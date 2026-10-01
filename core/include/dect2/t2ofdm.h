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
    // `cutoff` (0..1) narrows the delay passband to cutoff * N/(2S) samples around tau0 (noise reduction).
    void run(const std::vector<cf32>& grid, int S, int K, int N, double tau0, std::vector<cf32>& H, double cutoff = 1.0);

private:
    int S_ = 0, half_ = 8;
    double cut_ = 0;
    std::vector<float> taps_; // [phase][tap]
    void build(int S, double cutoff);
};

// Delay extent of the channel: samples (relative to the main path) where the power-delay profile is significant.
// Returns false if no clear profile was found.
bool delaySpan(const std::vector<cf32>& H, int N, int searchLo, int searchHi, int centre, int& tMin, int& tMax);

// Power-delay profile in dB from a channel estimate: returns |h(tau)|^2 for tau = tauMin .. tauMax-1 samples
// (negative delays wrap from the end of the IFFT). `centre` shifts the axis so the main path sits near 0.
void impulseResponse(const std::vector<cf32>& H, int N, int tauMin, int tauMax, int centre, std::vector<float>& outDb);

} // namespace dect2
