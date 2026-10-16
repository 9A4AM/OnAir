// Elektro-L HRIT test signal (elektro_gen.h): GOES xRIT products at 665.4 ksym/s.
#include "dect2/elektro/elektro_gen.h"
#include "dect2/goes/goes_gen.h"

namespace dect2 {

namespace {
class ElektroSynth : public ModeSynth {
public:
    ElektroSynth(const SynthConfig& s, double rate) : rate_(rate) {
        GoesGenConfig c;
        c.rate = rate;
        c.symbolRate = 665400.0;
        c.snrDb = s.snrDb;
        c.cfoHz = s.cfoHz;
        c.sroPpm = s.sroPpm;
        gen_ = std::make_unique<GoesGenerator>(c);
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override { gen_->generate(out, n); }
private:
    double rate_;
    std::unique_ptr<GoesGenerator> gen_;
};
} // namespace

std::unique_ptr<ModeSynth> makeElektroSynth(const SynthConfig& cfg, double sampleRate) {
    if (sampleRate <= 0) return nullptr;
    return std::make_unique<ElektroSynth>(cfg, sampleRate);
}

} // namespace dect2
