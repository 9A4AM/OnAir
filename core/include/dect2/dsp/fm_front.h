// Small FM front end for the analog trunking control-channel modes (LTR, EDACS): mix the channel to 0 Hz, resample to a low working
// rate, channel filter, FM discriminator. The output is the instantaneous frequency in Hz. Header only.
#pragma once
#include "dect2/common/ring.h"
#include "dect2/dsp/exact_resampler.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace dect2 {

struct FmBiquad {   // second-order low pass (RBJ cookbook), double precision, real signals
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass(double fc, double fs, double q) {
        const double w = 2 * 3.14159265358979323846 * fc / fs, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al;
        b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
        z1 = z2 = 0;
    }
    void clear() { z1 = z2 = 0; }
    double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// Running high and low of a signal (fast attack, slow decay): the slicer threshold is the middle, the range tells whether there is a signal.
struct EyeEnvelope {
    double hi = 0, lo = 0, attack = 0.05, decay = 0.0005;
    void clear() { hi = lo = 0; }
    void push(double s) {
        hi += (s > hi ? attack : decay) * (s - hi);
        lo += (s < lo ? attack : decay) * (s - lo);
    }
    double mid() const { return 0.5 * (hi + lo); }
    double range() const { return hi - lo; }
};

class FmFront {
public:
    // workRate: sample rate of the output; chanHz: half-width of the channel filter (two cascaded sections)
    bool configure(double inRate, double workRate, double chanHz) {
        in_ = inRate; work_ = workRate;
        ok_ = inRate > 0 && workRate > 0 && inRate <= workRate * 4096 && rs_.configure(inRate, workRate, 8);
        for (int i = 0; i < 2; i++) { lr_[i].lowpass(chanHz, workRate, i ? 1.3066 : 0.5412); li_[i].lowpass(chanHz, workRate, i ? 1.3066 : 0.5412); }
        reset();
        return ok_;
    }
    bool ready() const { return ok_; }
    void setOffset(double hz) { off_ = hz; }   // where the channel sits relative to 0 Hz in the input
    void reset() {
        rs_.reset(); phase_ = 0; prev_ = cf32(0, 0); pw_ = 0;
        for (int i = 0; i < 2; i++) { lr_[i].clear(); li_[i].clear(); }
    }
    double levelDb() const { return pw_ > 0 ? 10 * std::log10(pw_ + 1e-30) : -200; }
    double workRate() const { return work_; }
    // Appends the instantaneous frequency (Hz) of the samples to out.
    void feed(const cf32* x, size_t n, std::vector<float>& out) {
        if (!ok_ || !x || !n) return;
        const double step = -2 * 3.14159265358979323846 * off_ / in_;
        mix_.resize(n);
        for (size_t i = 0; i < n; i++) {
            mix_[i] = x[i] * cf32((float)std::cos(phase_), (float)std::sin(phase_));
            phase_ += step;
            if (phase_ > 3.14159265358979323846) phase_ -= 2 * 3.14159265358979323846; else if (phase_ < -3.14159265358979323846) phase_ += 2 * 3.14159265358979323846;
        }
        rsb_.clear();
        rs_.process(mix_.data(), n, rsb_);
        const double k = work_ / (2 * 3.14159265358979323846);
        for (const cf32& v : rsb_) {
            double re = lr_[0].run(v.real()), im = li_[0].run(v.imag());
            re = lr_[1].run(re); im = li_[1].run(im);
            const cf32 y((float)re, (float)im);
            out.push_back((float)(std::arg(y * std::conj(prev_)) * k));
            prev_ = y;
            pw_ += (re * re + im * im - pw_) * 0.0005;
        }
    }
private:
    double in_ = 0, work_ = 0, off_ = 0, phase_ = 0, pw_ = 0;
    bool ok_ = false;
    ExactResampler rs_;
    FmBiquad lr_[2], li_[2];
    cf32 prev_{0, 0};
    std::vector<cf32> mix_, rsb_;
};

} // namespace dect2
