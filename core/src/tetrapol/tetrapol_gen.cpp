// TETRAPOL test signal: one UHF control-channel carrier of GMSK 8 kbit/s data frames (D_SYSTEM_INFO).
#include "dect2/tetrapol/tetrapol_gen.h"
#include "dect2/tetrapol/tetrapol_codec.h"
#include "dect2/tetrapol/tetrapol_rx.h"
#include "dect2/dsp/gen_util.h"
#include "dect2/gsmr/gsmr_phy.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace dect2 {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kBaud = 8000.0;
constexpr int kSps = 8;
constexpr double kWork = kBaud * kSps;   // 64 kHz
constexpr double kChanBw = 12500.0;

std::vector<cf32> frameIq(int scramble) {
    tetrapol::DataFrame f;
    f.scramble = scramble;
    tetrapol::packSysinfo(f.payload, kTetrapolTestCountry, kTetrapolTestSystem, kTetrapolTestBn, kTetrapolTestCell,
                          kTetrapolTestLa, kTetrapolTestChannel, 2);
    uint8_t bits[tetrapol::kFrameBits];
    tetrapol::encodeDataFrame(f, bits);
    // one GMSK burst of several frames so the Gaussian pulse is continuous (no ramp between frames)
    gsmr::Bits b;
    b.reserve((size_t)tetrapol::kFrameBits * 8);
    for (int k = 0; k < 8; k++) b.insert(b.end(), bits, bits + tetrapol::kFrameBits);
    return gsmr::gmskBurst(b, kSps);
}

class TetrapolSynth : public ModeSynth {
public:
    TetrapolSynth(const SynthConfig& cfg, double rate) : rate_(rate), noise_(9) {
        const int scr = cfg.modeOpt[0] > 0 ? std::min(127, cfg.modeOpt[0]) : kTetrapolTestScramble;
        iq_ = frameIq(scr);
        const double pn = rate / kChanBw / std::pow(10.0, cfg.snrDb / 10.0);
        sigma_ = cfg.snrDb < -100 ? 0.f : (float)std::sqrt(pn / 2);
        scale_ = sigma_ > 0.15f ? 0.15f / sigma_ : 1.f;
        if (sigma_ == 0.f) scale_ = 0.35f;
        const double w = 2 * kPi * cfg.cfoHz / rate;
        step_ = cf32((float)std::cos(w), (float)std::sin(w));
        rot_ = cf32(1.f, 0.f);
        adv_ = kWork / rate;
        // clock error: stretch the work-rate playback
        adv_ *= (1.0 + cfg.sroPpm * 1e-6);
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override {
        const size_t len = iq_.size();
        for (size_t i = 0; i < n; i++) {
            const size_t i0 = (size_t)pos_;
            const float fr = (float)(pos_ - (double)i0);
            const cf32 a = iq_[i0 % len], b = iq_[(i0 + 1) % len];
            out[i] = (a + (b - a) * fr) * rot_ * scale_;
            rot_ *= step_;
            pos_ += adv_;
            if (pos_ >= (double)len) pos_ -= (double)len;
        }
        rot_ /= std::abs(rot_);
        if (sigma_ > 0.f) noise_.add(out, n, sigma_ * scale_);
    }
private:
    double rate_, pos_ = 0, adv_ = 1;
    std::vector<cf32> iq_;
    genutil::NoiseSource noise_;
    float sigma_ = 0, scale_ = 1;
    cf32 step_{1.f, 0.f}, rot_{1.f, 0.f};
};
} // namespace

std::unique_ptr<ModeSynth> makeTetrapolSynth(const SynthConfig& cfg, double sampleRate) {
    if (sampleRate < tetrapolTuning().minSampleRate - 1) return nullptr;
    return std::make_unique<TetrapolSynth>(cfg, sampleRate);
}

} // namespace dect2
