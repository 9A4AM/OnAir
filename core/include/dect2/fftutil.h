// Small complex FFT helper on top of Accelerate (power-of-two sizes).
#pragma once
#include "ring.h"
#include <Accelerate/Accelerate.h>
#include <vector>

namespace dect2 {

class Fft {
public:
    explicit Fft(int n) : n_(n), re_(n), im_(n) {
        log2n_ = 0;
        while ((1 << log2n_) < n) log2n_++;
        setup_ = vDSP_create_fftsetup(log2n_, kFFTRadix2);
    }
    ~Fft() { if (setup_) vDSP_destroy_fftsetup(setup_); }
    Fft(const Fft&) = delete;
    Fft& operator=(const Fft&) = delete;
    int size() const { return n_; }
    // In-place; the inverse is unnormalised (sum over carriers), like the textbook IDFT without the 1/N
    void forward(cf32* x) { run(x, false); }
    void inverse(cf32* x) { run(x, true); }

private:
    void run(cf32* x, bool inv) {
        DSPSplitComplex sc{re_.data(), im_.data()};
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(x), 2, &sc, 1, n_);
        vDSP_fft_zip(setup_, &sc, 1, log2n_, inv ? FFT_INVERSE : FFT_FORWARD);
        vDSP_ztoc(&sc, 1, reinterpret_cast<DSPComplex*>(x), 2, n_);
    }
    int n_, log2n_;
    FFTSetup setup_ = nullptr;
    std::vector<float> re_, im_;
};

} // namespace dect2
