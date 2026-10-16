// VDL Mode 4 test signal: see vdl4_gen.h.
#include "dect2/vdl4/vdl4_gen.h"
#include "dect2/vdl4/vdl4_proto.h"
#include "dect2/vdl4/vdl4_rx.h"
#include "dect2/dsp/gen_util.h"
#include "dect2/source/source.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include "dect2/common/mathconst.h"

namespace dect2 {

namespace {
using dect2::math::kPi;

double gfskPulse(double tau) {
    const double sigma = std::sqrt(std::log(2.0)) / (2 * kPi * 0.28);
    const double k = 1 / (std::sqrt(2.0) * sigma);
    return 0.5 * (std::erf((tau + 0.5) * k) - std::erf((tau - 0.5) * k));
}

struct Ev { double t; int ch; std::vector<uint8_t> bits; double car; double amp; bool operator>(const Ev& o) const { return t > o.t; } };

class Vdl4Synth : public ModeSynth {
public:
    Vdl4Synth(const SynthConfig& cfg, double rate) : cfg_(cfg), rate_(rate), centerHz_(vdl4Tuning().defMhz * 1e6), rng_(cfg.modeOpt[1] ? cfg.modeOpt[1] : 1), noise_(cfg.modeOpt[1] ? cfg.modeOpt[1] : 1) {
        ch_ = vdl4EuropeChannels();
        if (cfg.snrDb < 90) {
            const double n = 0.08 * 0.08 / std::pow(10.0, cfg.snrDb / 10.0);
            sigma_ = (float)std::sqrt(n * rate / 25000.0 / 2.0);
        }
        schedule(0.2);
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override {
        std::fill(out, out + n, cf32(0, 0));
        for (size_t i = 0; i < n; i++) {
            const double now = (pos_ + (int64_t)i) / rate_;
            while (!q_.empty() && q_.top().t <= now) {
                const Ev e = q_.top();
                q_.pop();
                const double f = ch_[(size_t)e.ch % ch_.size()];
                const double off = f - centerHz_;
                vdl4RenderBits(e.bits, out, (int64_t)i, n - i, rate_, e.t - now, off + e.car + cfg_.cfoHz, e.amp, 0);
            }
        }
        if (sigma_ > 0) noise_.add(out, n, sigma_);
        pos_ += (int64_t)n;
        if (q_.empty() && pos_ / rate_ > 3.0) { pos_ = 0; schedule(0.1); }
    }

private:
    void schedule(double t0) {
        const int n = std::max(1, cfg_.modeOpt[0] ? cfg_.modeOpt[0] : 4);
        for (int i = 0; i < n; i++) {
            Ev e;
            e.t = t0 + i * 0.4;
            e.ch = i % (int)ch_.size();
            e.car = std::uniform_real_distribution<double>(-150, 150)(rng_);
            e.amp = 0.08;
            const uint32_t id = 0x4ca2b1u + (uint32_t)i;
            e.bits = vdl4SynthBurstBits((i & 1) == 0, id, 1, 51.4 + 0.02 * i, -0.5 + 0.02 * i, 35000, "VDL4 TEST");
            q_.push(e);
        }
    }

    SynthConfig cfg_;
    double rate_, centerHz_;
    std::mt19937 rng_;
    genutil::NoiseSource noise_;
    float sigma_ = 0;
    std::vector<double> ch_;
    std::priority_queue<Ev, std::vector<Ev>, std::greater<Ev>> q_;
    int64_t pos_ = 0;
};
} // namespace

std::vector<uint8_t> vdl4SynthBurstBits(bool sync, uint32_t icao, int srcType, double lat, double lon, int altFt, const std::string& dlsText) {
    Vdl4BurstFields f;
    f.autonomous = true;
    f.src = (uint32_t)((srcType & 7) << 24) | (icao & 0xFFFFFF);
    f.burstId = 1;
    if (sync) {
        f.msgId = 1;
        f.info = vdl4SyncBurstInfo(lat, lon, altFt, false, f.src, srcType);
    } else {
        f.msgId = 3;
        f.info = vdl4DlsUserInfo(dlsText);
    }
    const auto frame = vdl4PackBurst(f);
    return vdl4BurstLineBits(vdl4HdlcBits({frame}));
}

void vdl4RenderBits(const std::vector<uint8_t>& lineBits, cf32* out, int64_t outStart, size_t n, double rate, double start, double carHz, double amp, double phase) {
    const double T = rate / kVdl4BitRate;
    const double s0 = start - 2 * T, s1 = start + ((double)lineBits.size() + 2) * T;
    const int64_t a = std::max<int64_t>(outStart, (int64_t)std::ceil(s0));
    const int64_t b = std::min<int64_t>(outStart + (int64_t)n, (int64_t)std::floor(s1));
    if (a >= b) return;
    for (int64_t i = a; i < b; i++) {
        const double u = ((double)i - start) / T;
        const int k0 = (int)std::floor(u);
        double f = 0;
        for (int k = k0 - 2; k <= k0 + 2; k++) {
            if (k < 0 || k >= (int)lineBits.size()) continue;
            f += (lineBits[(size_t)k] ? 1.0 : -1.0) * gfskPulse(u - k - 0.5);
        }
        const double dev = 4800.0;
        const double ph = 2 * kPi * (f * dev + carHz) * ((double)i - start) / rate + phase;
        const size_t o = (size_t)(i - outStart);
        out[o] += cf32((float)(amp * std::cos(ph)), (float)(amp * std::sin(ph)));
    }
}

std::unique_ptr<ModeSynth> makeVdl4Synth(const SynthConfig& cfg, double sampleRate) {
    if (sampleRate <= 0) return nullptr;
    return std::make_unique<Vdl4Synth>(cfg, sampleRate);
}

} // namespace dect2
