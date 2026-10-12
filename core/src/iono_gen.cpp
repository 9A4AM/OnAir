#include "dect2/iono_gen.h"
#include <cmath>
#include <complex>
#include <vector>

namespace dect2 {

namespace {

using cd = std::complex<double>;

class IonoSynth : public ModeSynth {
public:
    IonoSynth(const SynthConfig& c, double fs) : fs_(fs), dt_(1.0 / fs) {
        fc_ = (c.modeVal[0] > 0 ? c.modeVal[0] : kIonoSynthDefMhz) * 1e6;
        noiseOnly_ = c.modeOpt[0] == 1;
        // a table of Gaussian noise, played from random places
        noise_.resize(kTab);
        const double sig = 0.2 / std::sqrt(2.0);
        for (size_t i = 0; i < kTab; i += 2) {
            double u; do { u = uni(); } while (u <= 1e-300);
            const double v = uni(), r = std::sqrt(-2 * std::log(u));
            noise_[i] = cf32((float)(sig * r * std::cos(2 * M_PI * v)), (float)(sig * r * std::sin(2 * M_PI * v)));
            do { u = uni(); } while (u <= 1e-300);
            const double v2 = uni(), r2 = std::sqrt(-2 * std::log(u));
            noise_[i + 1] = cf32((float)(sig * r2 * std::cos(2 * M_PI * v2)), (float)(sig * r2 * std::sin(2 * M_PI * v2)));
        }
        if (!noiseOnly_)
            for (const auto& s : kIonoSynth)
                for (int p = 0; p < 2; p++) {
                    Path q;
                    q.r = s.rateHz; q.P = s.periodSec; q.len = std::min(30e6 / s.rateHz, 0.95 * s.periodSec);
                    q.a = kIonoSynthAmp * s.amp[p];
                    q.u = std::fmod(-s.delaySec[p] - s.offsetSec, q.P);
                    if (q.u < 0) q.u += q.P;
                    if (q.u > q.len) q.u -= q.P;   // waiting for the next sweep
                    q.dw = std::polar(1.0, 2 * M_PI * q.r * dt_ * dt_);
                    paths_.push_back(q);
                }
    }
    double sampleRate() const override { return fs_; }
    void generate(cf32* out, size_t n) override {
        for (size_t i = 0; i < n; i++) {
            if (pos_ == kTab || (g_ & 4095) == 0) pos_ = (size_t)(next() % (kTab - 4096));
            out[i] = noise_[pos_++];
            g_++;
        }
        for (auto& q : paths_) {
            for (size_t i = 0; i < n; i++) {
                const double u = q.u;
                q.u += dt_;
                if (q.u >= q.P) q.u -= q.P;
                const double fb = q.r * u - fc_;
                const bool on = u >= 0 && u < q.len && std::fabs(fb) < 0.5 * fs_;
                if (!on) { q.on = false; continue; }
                if (!q.on || ((g_ - n + i) & 4095) == 0) {   // exact phase and step from the sweep's own time
                    const double ph = 2 * M_PI * std::fmod(q.r * u * u / 2 - fc_ * u, 1.0);
                    q.z = std::polar(q.a, ph);
                    q.w = std::polar(1.0, 2 * M_PI * (fb * dt_ + q.r * dt_ * dt_ / 2));
                    q.on = true;
                }
                out[i] += cf32((float)q.z.real(), (float)q.z.imag());
                q.z *= q.w; q.w *= q.dw;
            }
        }
    }

private:
    static constexpr size_t kTab = 1 << 18;
    struct Path { double r = 0, P = 1, len = 0, a = 0, u = 0; cd z = 0.0, w = 1.0, dw = 1.0; bool on = false; };
    double uni() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
    uint64_t next() { s_ ^= s_ << 13; s_ ^= s_ >> 7; s_ ^= s_ << 17; return s_; }
    double fs_, dt_, fc_ = 0;
    bool noiseOnly_ = false;
    std::vector<cf32> noise_;
    std::vector<Path> paths_;
    size_t pos_ = kTab;
    uint64_t g_ = 0, s_ = 0x9E3779B97F4A7C15ull;
};

} // namespace

std::unique_ptr<ModeSynth> makeIonoSynth(const SynthConfig& cfg, double sampleRate) {
    if (sampleRate <= 0) return nullptr;
    return std::make_unique<IonoSynth>(cfg, sampleRate);
}

} // namespace dect2
