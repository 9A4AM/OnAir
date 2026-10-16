// Analog voice for TX: NFM, AM, USB/LSB of a microphone, a file, a 1 kHz tone or silence. Broadcast FM (stereo + RDS) lives in fm_gen.cpp.
#include "dect2/tx/tx.h"
#include "dect2/engine/mode_tuning.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include "dect2/common/mathconst.h"

namespace dect2 {

namespace {
using dect2::math::kPi;

// 48 kHz stereo from the session → one sample of mono at `rate` (linear).
struct PullMono {
    std::function<int(float*, int)> src;
    int srcRate = 48000;
    double pos = 1;
    float prev = 0, next = 0;
    float peek() {
        while (pos >= 1.0) {
            pos -= 1.0;
            prev = next;
            float st[2] = {0, 0};
            if (src) src(st, 1);
            next = 0.5f * (st[0] + st[1]);
        }
        const float y = prev + (next - prev) * (float)pos;
        return y;
    }
    void advance(double adv) { pos += adv; }
};

class AnalogVoice : public ModeSynth {
public:
    AnalogVoice(double rate, double offHz, int kind, const TxRequest& r)
        : rate_(rate), off_(offHz), kind_(kind) {
        pull_.src = r.pullAudio;
        tone_ = r.toneHz > 0 ? r.toneHz : 1000;
        dev_ = r.deviationHz > 0 ? r.deviationHz : (kind == 1 ? 5000.0 : 2500.0);
        pre_ = r.preemphUs > 0 ? r.preemphUs : 50;
        if (!r.pullAudio) {
            if (r.audioSrc == 3) local_ = 3;
            else local_ = 2;   // tone (or silence-as-tone with amp 0 if audioSrc 3)
        }
        // Hilbert taps (odd, zeros on even): delay of N samples on the real path
        hil_.assign((size_t)(2 * kHilN + 1), 0.f);
        for (int k = -kHilN; k <= kHilN; k++) {
            if (k % 2) hil_[(size_t)(k + kHilN)] = (float)(2.0 / (kPi * k));
        }
        hist_.assign(hil_.size(), 0.f);
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override {
        const double w0 = 2 * kPi * off_ / rate_;
        const double wt = 2 * kPi * tone_ / rate_;
        const double adv = pull_.src ? 48000.0 / rate_ : 0;
        const double kpre = pre_ * 1e-6 * (pull_.src ? 48000.0 : rate_);
        for (size_t i = 0; i < n; i++) {
            float a;
            if (pull_.src) {
                a = pull_.peek();
                pull_.advance(adv);
                const float p = (float)(a + kpre * (a - preX_));
                preX_ = a;
                a = std::max(-1.f, std::min(1.f, p));
            } else if (local_ == 3) a = 0;
            else {
                a = 0.5f * (float)std::sin(tph_);
                tph_ += wt;
                if (tph_ > 2 * kPi) tph_ -= 2 * kPi;
            }
            if (kind_ == 2) {   // AM
                const float m = 0.55f * (1.f + 0.85f * a);
                out[i] = cf32(m * (float)std::cos(cph_), m * (float)std::sin(cph_));
                cph_ += w0;
            } else if (kind_ == 3 || kind_ == 4) {   // USB / LSB, suppressed carrier at `off`
                hist_[hpos_] = a;
                float im = 0;
                for (size_t k = 0; k < hil_.size(); k++) im += hil_[k] * hist_[(hpos_ + k) % hist_.size()];
                const float re = hist_[(hpos_ + (size_t)kHilN) % hist_.size()];
                if (++hpos_ >= hist_.size()) hpos_ = 0;
                const float sg = kind_ == 4 ? -1.f : 1.f;
                const float I = 0.4f * re, Q = 0.4f * sg * im;
                const float cs = (float)std::cos(cph_), sn = (float)std::sin(cph_);
                out[i] = cf32(I * cs - Q * sn, I * sn + Q * cs);
                cph_ += w0;
            } else {   // NFM (kind 1) or generic FM
                cph_ += w0 + 2 * kPi * (dev_ * a) / rate_;
                out[i] = cf32(0.40f * (float)std::cos(cph_), 0.40f * (float)std::sin(cph_));
            }
            if (cph_ > kPi) cph_ -= 2 * kPi; else if (cph_ < -kPi) cph_ += 2 * kPi;
        }
    }
private:
    static constexpr int kHilN = 31;
    double rate_, off_, tone_, dev_, pre_, cph_ = 0, tph_ = 0;
    int kind_ = 1, local_ = 2;
    PullMono pull_;
    float preX_ = 0;
    std::vector<float> hil_, hist_;
    size_t hpos_ = 0;
};

class MixSynth : public ModeSynth {
public:
    MixSynth(std::unique_ptr<ModeSynth> a, std::unique_ptr<ModeSynth> b, double rate)
        : a_(std::move(a)), b_(std::move(b)), rate_(rate) {}
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override {
        if (a_) a_->generate(out, n);
        else for (size_t i = 0; i < n; i++) out[i] = cf32(0.f, 0.f);
        if (!b_) return;
        tmp_.resize(n);
        b_->generate(tmp_.data(), n);
        for (size_t i = 0; i < n; i++) out[i] += tmp_[i];
    }
private:
    std::unique_ptr<ModeSynth> a_, b_;
    double rate_;
    std::vector<cf32> tmp_;
};
}

std::unique_ptr<ModeSynth> makeTxVoiceSynth(const TxModeSpec& m, const TxRequest& r, double rate) {
    const std::string id = m.id ? m.id : "";
    if (id == "airband" || m.stdMode == 28) {
        const ModeTuning* mt = modeTuning(28);
        return std::make_unique<AnalogVoice>(rate, mt ? -mt->tuneOffsetHz : 0, 2, r);
    }
    if (id == "fm" || m.stdMode == 7) return nullptr;   // FmGenerator in tx_payload
    if (id == "atv" || m.stdMode == 10) {
        const int k = r.kind == 2 ? 3 : r.kind == 3 ? 4 : r.kind == 1 ? 2 : 1;
        return std::make_unique<AnalogVoice>(rate, 5.5e6, k, r);
    }
    if (r.analogVoice || r.kind == 1 || r.kind == 2 || r.kind == 3) {
        const int k = r.kind == 2 ? 3 : r.kind == 3 ? 4 : r.kind == 1 ? 2 : 1;
        return std::make_unique<AnalogVoice>(rate, 0, k, r);
    }
    if (id == "qo100") return std::make_unique<AnalogVoice>(rate, 0, 3, r);
    (void)id;
    return std::make_unique<AnalogVoice>(rate, 0, 1, r);
}

std::unique_ptr<ModeSynth> makeTxMixSynth(std::unique_ptr<ModeSynth> a, std::unique_ptr<ModeSynth> b, double rate) {
    return std::make_unique<MixSynth>(std::move(a), std::move(b), rate);
}

void txPullPcm8k(const TxRequest& r, int16_t* pcm, int n) {
    static thread_local PullMono p;
    p.src = r.pullAudio;
    const double adv = r.pullAudio ? 48000.0 / 8000.0 : 0;
    for (int i = 0; i < n; i++) {
        float a = 0;
        if (r.pullAudio) { a = p.peek(); p.advance(adv); }
        else if (r.audioSrc != 3) a = 0.5f * (float)std::sin(2 * kPi * (r.toneHz > 0 ? r.toneHz : 1000) * i / 8000.0);
        pcm[i] = (int16_t)std::max(-32767, std::min(32767, (int)lrint(a * 20000)));
    }
}

} // namespace dect2
