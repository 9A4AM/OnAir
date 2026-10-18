// IFKP test signal for the engine (see ifkp_gen.h).
#include "dect2/ifkp/ifkp_gen.h"
#include "dect2/ifkp/ifkp_rx.h"

namespace dect2 {

std::vector<Kbd33GenSignal> ifkpTestSignals() {
    std::vector<Kbd33GenSignal> v(1);
    v[0].ifkp = true; v[0].speed = 1; v[0].centreHz = 1500; v[0].snrDb = 8;
    v[0].text = "cq cq de LY2ABC ifkp test k\n";
    v[0].startSec = 1.0; v[0].pauseSec = 2.0;
    return v;
}

namespace {
class IfkpSynth : public ModeSynth {
public:
    IfkpSynth(const SynthConfig& s, double rate) : rate_(rate) {
        Kbd33GenConfig c;
        c.rate = rate;
        c.dialOffsetHz = -ifkpTuning().tuneOffsetHz;
        c.cfoHz = s.cfoHz;
        c.noiseSigma = 0.05;
        c.signals = ifkpTestSignals();
        for (auto& g : c.signals) {
            g.snrDb += s.snrDb;
            if (s.modeOpt[0] > 0 && s.modeOpt[0] <= 3) g.speed = s.modeOpt[0] - 1;
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

std::unique_ptr<ModeSynth> makeIfkpSynth(const SynthConfig& cfg, double sampleRate) {
    if (sampleRate <= 0) return nullptr;
    return std::make_unique<IfkpSynth>(cfg, sampleRate);
}

} // namespace dect2
