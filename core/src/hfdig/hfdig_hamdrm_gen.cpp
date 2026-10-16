// HamDRM transmitter (hfdig_hamdrm.h): the test audio of the HamDRM decoder. A file in a header segment and body segments, one data
// group per OFDM frame, built at 12 kHz with the decoder's own layout (hfdig_hamdrm_int.h), put on 1600 Hz of the audio and brought to 8 kHz.
#include "dect2/hfdig/hfdig_hamdrm.h"
#include "dect2/hfdig/hfdig_gen.h"
#include "dect2/drm/drm_fft.h"
#include "dect2/source/source.h"
#include "hfdig_hamdrm_int.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

namespace dect2 {

using namespace hamdrm;

int hamdrmSegmentBytes(int mode, int occ, int qam, int prot) {
    return layout(std::clamp(mode, 0, 2), std::clamp(occ, 0, 1), std::clamp(qam, 0, 2), std::clamp(prot, 0, 1))->payload - kGroupHeader - kGroupCrc;
}
int hamdrmBodySegments(const HamdrmTxConfig& c, size_t n) {
    const int s = hamdrmSegmentBytes(c.mode, c.occ, c.qam, c.prot);
    return s > 0 ? (int)((n + (size_t)s - 1) / (size_t)s) : 0;
}

namespace {

class Tx : public HfdigTestAudio {
public:
    Tx(const HamdrmTxConfig& c, const std::vector<uint8_t>& file) : c_(c), file_(file), down_(2, 3, kRate, 3500) {
        c_.mode = std::clamp(c_.mode, 0, 2); c_.occ = std::clamp(c_.occ, 0, 1); c_.qam = std::clamp(c_.qam, 0, 2); c_.prot = std::clamp(c_.prot, 0, 1);
        c_.repeats = std::max(1, c_.repeats);
        L_ = layout(c_.mode, c_.occ, c_.qam, c_.prot);
        fft_ = drm::DrmFft(L_->g.tu);
        segBytes_ = L_->payload - kGroupHeader - kGroupCrc;
        nSeg_ = hamdrmBodySegments(c_, file_.size());
        const int K = L_->g.kmax - L_->g.kmin + 1;
        amp_ = (float)(0.35 * std::sqrt(2.0 / K));
        silence(c_.leadSec);
    }
    void generate(float* out, size_t n) override {
        while (buf_.size() < n) next();
        for (size_t i = 0; i < n; i++) { out[i] = buf_.front(); buf_.pop_front(); }
    }

private:
    void silence(double sec) { pendingSilence_ += (int)(sec * kRate); }

    // the groups of one transmission: the header twice (a receiver may still be locking on), the body, the header again
    void plan() {
        const bool drop = c_.dropEvery > 0 && round_ == 0;
        Group h;
        h.type = kGroupHeaderSeg; h.tid = c_.transportId; h.seg = 0; h.last = 0;
        h.data = headerData(file_.size(), c_.mime, c_.name, c_.callsign);
        queue_.push_back(h); queue_.push_back(h);
        for (int i = 0; i < nSeg_; i++) {
            if (drop && i % c_.dropEvery == c_.dropEvery - 1) continue;
            Group b;
            b.type = kGroupBody; b.tid = c_.transportId; b.seg = i; b.last = nSeg_ - 1;
            const size_t a = (size_t)i * (size_t)segBytes_, e = std::min(file_.size(), a + (size_t)segBytes_);
            b.data.assign(file_.begin() + (long)a, file_.begin() + (long)e);
            queue_.push_back(b);
        }
        queue_.push_back(h);
    }

    void next() {
        std::vector<cf32> bb;
        if (pendingSilence_ > 0) {
            const int n = std::min(pendingSilence_, 1200);
            bb.assign((size_t)n, cf32(0, 0));
            pendingSilence_ -= n;
        } else {
            if (queue_.empty()) plan();
            frame(queue_.front(), bb);
            queue_.erase(queue_.begin());
            if (queue_.empty()) {   // the transmission is over
                silence(c_.pauseSec);
                if (++txInRound_ >= c_.repeats) { txInRound_ = 0; round_++; }
            }
        }
        // up to the audio frequency, then 12 -> 8 kHz
        std::vector<float> re(bb.size());
        const double w = 2 * hfdig::kPi * (kCentreHz + c_.offsetHz) / kRate;
        for (size_t i = 0; i < bb.size(); i++) {
            const double ph = std::fmod(w * (double)(n12_++), 2 * hfdig::kPi);
            re[i] = std::clamp((float)(bb[i].real() * std::cos(ph) - bb[i].imag() * std::sin(ph)), -1.f, 1.f);
        }
        std::vector<float> o;
        down_.process(re.data(), re.size(), o);
        buf_.insert(buf_.end(), o.begin(), o.end());
    }

    void frame(const Group& grp, std::vector<cf32>& bb) {
        const Geometry& g = L_->g;
        const int W = g.kmax - g.kmin + 1;
        std::vector<cf32> cells((size_t)g.ns * W, cf32(0, 0));
        auto at = [&](int s, int k) -> cf32& { return cells[(size_t)s * W + (k - g.kmin)]; };
        for (int k = g.kmin; k <= g.kmax; k++) at(0, k) = refCell(k);
        for (int s = 1; s < g.ns; s++) for (int k = g.kmin; k <= g.kmax; k++) if (isPilot(s, k)) at(s, k) = pilotValue(s, k);
        // frame information
        Fac f; f.mode = c_.mode; f.occ = c_.occ; f.qam = c_.qam; f.prot = c_.prot; f.count = (int)(count_++ & 15);
        uint8_t fb[32];
        facBits(f, fb);
        std::vector<cf32> fc(kFacCells);
        facCode().encode(fb, fc.data());
        for (int i = 0; i < kFacCells; i++) at(L_->fac[(size_t)i].first, L_->fac[(size_t)i].second) = fc[(size_t)i];
        // the data group
        const std::vector<uint8_t> bytes = packGroup(grp, L_->payload);
        std::vector<uint8_t> u((size_t)L_->code->infoBits(), 0);
        for (size_t i = 0; i < bytes.size() * 8 && i < u.size(); i++) u[i] = (uint8_t)((bytes[i / 8] >> (7 - i % 8)) & 1);
        scramble(u.data(), u.size());
        const int N = (int)L_->msc.size();
        std::vector<cf32> coded((size_t)N);
        L_->code->encode(u.data(), coded.data());
        const auto& perm = cellPerm(N);
        for (int i = 0; i < N; i++) at(L_->msc[(size_t)i].first, L_->msc[(size_t)i].second) = coded[(size_t)perm[(size_t)i]];
        // OFDM
        std::vector<cf32> x((size_t)g.tu);
        for (int s = 0; s < g.ns; s++) {
            std::fill(x.begin(), x.end(), cf32(0, 0));
            for (int k = g.kmin; k <= g.kmax; k++) x[(size_t)((k + g.tu) % g.tu)] = at(s, k);
            fft_.inverse(x.data());
            for (int i = g.tu - g.tg; i < g.tu; i++) bb.push_back(x[(size_t)i] * amp_);
            for (int i = 0; i < g.tu; i++) bb.push_back(x[(size_t)i] * amp_);
        }
    }

    HamdrmTxConfig c_;
    std::vector<uint8_t> file_;
    std::shared_ptr<const Layout> L_;
    drm::DrmFft fft_;
    Resampler down_;
    int segBytes_ = 0, nSeg_ = 0;
    float amp_ = 0;
    int pendingSilence_ = 0;
    std::vector<Group> queue_;
    int txInRound_ = 0, round_ = 0;
    uint32_t count_ = 0;
    int64_t n12_ = 0;
    std::deque<float> buf_;
};

} // namespace

std::unique_ptr<HfdigTestAudio> makeHamdrmTx(const HamdrmTxConfig& cfg, const std::vector<uint8_t>& file) {
    return std::make_unique<Tx>(cfg, file);
}

std::unique_ptr<HfdigTestAudio> makeHamdrmTestAudio(const SynthConfig& cfg) {
    HamdrmTxConfig c;
    c.mode = cfg.modeOpt[1] >= 1 && cfg.modeOpt[1] <= 3 ? cfg.modeOpt[1] - 1 : 1;   // 0: B
    c.occ = cfg.modeOpt[2] == 1 ? 0 : 1;                                            // 0: 2.5 kHz
    c.qam = cfg.modeOpt[3] >= 1 && cfg.modeOpt[3] <= 3 ? cfg.modeOpt[3] - 1 : 1;    // 0: 16-QAM
    c.dropEvery = std::max(0, cfg.modeOpt[4]);
    c.repeats = cfg.modeOpt[5] > 0 ? cfg.modeOpt[5] : 2;
    c.offsetHz = cfg.modeVal[0];
    return makeHamdrmTx(c, hamdrmTestPicture());
}

} // namespace dect2
