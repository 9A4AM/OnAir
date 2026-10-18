// FSQ test signal for the engine (see fsq_gen.h).
#include "dect2/fsq/fsq_gen.h"
#include "dect2/fsq/fsq_rx.h"

namespace dect2 {

std::vector<Kbd33GenSignal> fsqTestSignals() {
    std::vector<Kbd33GenSignal> v(1);
    v[0].ifkp = false; v[0].speed = 2; v[0].centreHz = 1500; v[0].snrDb = 8;
    v[0].text = fsqFrame("LY2ABC", "LY2XYZ? snr please", true);
    v[0].startSec = 1.0; v[0].pauseSec = 2.0;
    return v;
}

namespace {
class FsqSynth : public ModeSynth {
public:
    FsqSynth(const SynthConfig& s, double rate) : rate_(rate) {
        Kbd33GenConfig c;
        c.rate = rate;
        c.dialOffsetHz = -fsqTuning().tuneOffsetHz;
        c.cfoHz = s.cfoHz;
        c.noiseSigma = 0.05;
        c.signals = fsqTestSignals();
        for (auto& g : c.signals) {
            g.snrDb += s.snrDb;
            if (s.modeOpt[0] > 0 && s.modeOpt[0] <= 5) g.speed = s.modeOpt[0] - 1;
        }
        gen_ = std::make_unique<Kbd33Generator>(c);
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override { gen_->generate(out, n); }
private:
    double rate_;
    std::unique_ptr<Kbd33Generator> gen_;
};
} // namespace

std::unique_ptr<ModeSynth> makeFsqSynth(const SynthConfig& cfg, double sampleRate) {
    if (sampleRate <= 0) return nullptr;
    return std::make_unique<FsqSynth>(cfg, sampleRate);
}

} // namespace dect2
