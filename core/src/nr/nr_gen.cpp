// 5G NR cells test signal (nr_gen.h).
#include "dect2/nr/nr_gen.h"
#include "dect2/common/stable_random.h"
#include "dect2/cell/cell_dsp.h"
#include "dect2/source/source.h"
#include <cmath>
#include <random>

namespace dect2 {

std::vector<NrTestCell> nrTestScene(int scene) {
    std::vector<NrTestCell> v;
    auto cellOf = [](int pci, int scs, int lmax, int sfn, int cs0, int ss0, int kSsb, bool barred) {
        nr::GenCell g;
        g.pci = pci; g.scsKhz = scs; g.lmax = lmax; g.ssbMask = lmax == 8 ? 0xFF : 0x0F;
        for (int i = 0; i < lmax; i++) g.beamDb.push_back(-1.5f * (float)std::abs(i - 2));   // beam 2 points at the receiver
        g.mib.sfn = sfn; g.mib.scs30 = scs == 30; g.mib.kSsb = kSsb; g.mib.dmrsTypeAPos = 2; g.mib.coreset0 = cs0; g.mib.ss0 = ss0;
        g.mib.barred = barred; g.mib.intraFreqResel = true;
        g.seed = 5 + (uint32_t)pci;
        return g;
    };
    if (scene == 1) v.push_back({cellOf(222, 15, 4, 100, 3, 0, 6, false), 0});
    else if (scene == 2) v.push_back({cellOf(17, 30, 8, 900, 0, 0, 31, true), 0});
    else {
        v.push_back({cellOf(501, 30, 8, 512, 12, 2, 4, false), -4.32e6});   // GSCN 7872 at the default 3541.44 MHz
        v.push_back({cellOf(1007, 30, 8, 33, 6, 0, 18, false), 4.32e6});    // GSCN 7878
        v.back().g.beamDb = {-12, -9, -6, -3, 0, -3, -6, -9};
    }
    return v;
}

namespace {

class NrSynth : public ModeSynth {
public:
    NrSynth(const SynthConfig& c, double rate) : out_(rate), rng_(17) {
        for (const auto& tc : nrTestScene(c.modeOpt[0])) {
            Src s;
            s.g = tc.g; s.off = tc.offsetHz;
            s.native = 256e3 * tc.g.scsKhz;
            s.step = s.native / out_;
            s.rs.reset(new cell::Resampler(std::min(0.5, 0.5 * out_ / s.native) * 0.97));
            src_.push_back(std::move(s));
        }
        cfo_ = c.cfoHz;
        const double nat = src_.empty() ? out_ : src_[0].native;
        // the generator's rms is 0.2 over its native band: noise over the whole output band for snrDb inside the SSB
        noise_ = (float)std::sqrt(0.04 * std::max(1.0, out_ / nat) / std::pow(10.0, c.snrDb / 10) / 2);
        if (c.snrDb >= 99) noise_ = 0;
    }
    double sampleRate() const override { return out_; }
    void generate(cf32* o, size_t n) override {
        for (size_t i = 0; i < n; i++) o[i] = 0;
        std::vector<cf32> tmp(n);
        for (auto& s : src_) {
            size_t done = 0;
            const int H = s.rs->halfLen();
            while (done < n) {
                if (s.pos + H + 2 >= (double)s.buf.size()) { more(s); continue; }
                const size_t fit = (size_t)std::floor(((double)s.buf.size() - H - 2 - s.pos) / s.step) + 1;
                const size_t m = std::min(n - done, fit);
                s.rs->run(s.buf.data(), s.buf.size(), s.pos, s.step, tmp.data() + done, m);
                s.pos += s.step * (double)m;
                done += m;
            }
            const double w = 2 * M_PI * (s.off + cfo_) / out_;
            for (size_t i = 0; i < n; i++) {
                o[i] += tmp[i] * cf32((float)std::cos(s.ph), (float)std::sin(s.ph));
                s.ph = std::fmod(s.ph + w, 2 * M_PI);
            }
        }
        dect2::normal_distribution<float> nd(0.f, 1.f);
        if (noise_ > 0) for (size_t i = 0; i < n; i++) o[i] += cf32{noise_ * nd(rng_), noise_ * nd(rng_)};
    }

private:
    struct Src {
        nr::GenCell g;
        double off = 0, native = 1, step = 1, pos = 0, ph = 0;
        int half = 0;   // half frames made so far
        std::unique_ptr<cell::Resampler> rs;
        std::vector<cf32> buf;
    };
    void more(Src& s) {
        const long keep = std::max(0L, (long)std::floor(s.pos) - s.rs->halfLen() - 2);
        if (keep > 0) { s.buf.erase(s.buf.begin(), s.buf.begin() + keep); s.pos -= (double)keep; }
        // the next 20 ms (four half frames), SFN running on
        nr::GenCell g = s.g;
        const int hf0 = (s.g.mib.hrf ? 1 : 0) + s.half;
        g.mib.sfn = (s.g.mib.sfn + hf0 / 2) & 1023;
        g.mib.hrf = (hf0 & 1) != 0;
        g.seed = s.g.seed + (uint32_t)s.half;
        const std::vector<cf32> f = nr::generate(g, 4);
        s.half += 4;
        s.buf.insert(s.buf.end(), f.begin(), f.end());
    }
    double out_, cfo_ = 0;
    float noise_ = 0;
    std::vector<Src> src_;
    std::mt19937 rng_;
};

} // namespace

std::unique_ptr<ModeSynth> makeNrSynth(const SynthConfig& cfg, double sampleRate) { return std::unique_ptr<ModeSynth>(new NrSynth(cfg, sampleRate)); }

} // namespace dect2
