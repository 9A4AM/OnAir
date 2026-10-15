// Spectrum analyser test signal: plays a HackRF native sweep over a made-up band scene. Every block of 8184 samples is tagged with its
// tuning (as the radio's sweep header does) and posted to SaSweepTap; the same samples go to the engine. The scene: carriers at
// 100 MHz (-30 dBFS), 433.92 MHz (-40), 1090 MHz (-35) and 2440 MHz (-25), over a noise floor. modeVal[0] / [1]: the range in MHz.
#include "dect2/specan_gen.h"
#include "dect2/specan_dsp.h"
#include <cmath>
#include <random>

namespace dect2 {

const SpecanTone* specanTestTones(int& count) {
    static const SpecanTone kT[] = {{100e6, -30}, {433.92e6, -40}, {1090e6, -35}, {2440e6, -25}};
    count = 4;
    return kT;
}

namespace {

class SpecanSynth : public ModeSynth {
public:
    SpecanSynth(const SynthConfig& c, double rate) : rate_(rate) {
        lo_ = c.modeVal[0] > 0 ? c.modeVal[0] : 1;
        hi_ = c.modeVal[1] > lo_ ? c.modeVal[1] : 6000;
        for (double f = std::floor(lo_); f < hi_; f += 20) { tags_.push_back(f * 1e6); tags_.push_back((f + 5) * 1e6); }
        sigma_ = (float)(0.25 * std::pow(10.0, -c.snrDb / 20.0));
        SaSweepTap::get().setActive(true, rate_, 7.5e6);
        block_.resize(kBlock);
        newBlock();
    }
    ~SpecanSynth() override { SaSweepTap::get().setActive(false); }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override {
        for (size_t i = 0; i < n; i++) {
            if (pos_ >= kBlock) { SaSweepTap::get().post(tags_[t_], block_.data(), kBlock); t_ = (t_ + 1) % tags_.size(); newBlock(); }
            out[i] = block_[pos_++];
        }
    }
private:
    static constexpr size_t kBlock = 8184;
    void newBlock() {
        pos_ = 0;
        const double loHz = tags_[t_] + 7.5e6;
        int nt = 0;
        const SpecanTone* tn = specanTestTones(nt);
        std::normal_distribution<float> g(0.f, sigma_ / std::sqrt(2.f));
        for (size_t i = 0; i < kBlock; i++) block_[i] = cf32(g(rng_), g(rng_));
        for (int k = 0; k < nt; k++) {
            const double off = tn[k].hz - loHz;
            if (std::fabs(off) > rate_ * 0.45) continue;
            const float a = (float)std::pow(10.0, tn[k].dbfs / 20.0);
            const double w = 2 * M_PI * off / rate_;
            for (size_t i = 0; i < kBlock; i++) block_[i] += a * cf32((float)std::cos(w * i), (float)std::sin(w * i));
        }
        block_[0] += cf32(0.02f, 0);   // a little LO leak
        for (size_t i = 0; i < kBlock; i++) block_[i] += cf32(0.01f, 0.f);
    }
    double rate_, lo_, hi_;
    float sigma_;
    std::vector<double> tags_;
    size_t t_ = 0, pos_ = 0;
    std::vector<cf32> block_;
    std::mt19937 rng_{7};
};

} // namespace

std::unique_ptr<ModeSynth> makeSpecanSynth(const SynthConfig& cfg, double sampleRate) { return std::make_unique<SpecanSynth>(cfg, sampleRate); }

} // namespace dect2
