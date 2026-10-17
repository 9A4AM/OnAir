#include "dect2/lbj/lbj_modem.h"
#include "dect2/common/bits.h"
#include <algorithm>
#include <cmath>

namespace dect2::lbj {

namespace {
constexpr double kPi = 3.14159265358979323846;
inline int pop(uint32_t v) { return dect2::bits::popcount(v); }
constexpr int kMaxBatches = 6;
constexpr double kChannelHz = 6000.0, kSmoothHz = 1500.0;
constexpr double kDcTau = 0.03;     // seconds: the slicer threshold follows the carrier offset (a preamble is balanced)
constexpr int kAvg = 2;             // bit value = mean of 2*kAvg+1 samples around the bit centre
}

void Modem::Biquad::lowpass(double fc, double fs, double q) {
    const double w = 2 * kPi * fc / fs, c = std::cos(w), s = std::sin(w), al = s / (2 * q);
    const double a0 = 1 + al;
    b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0;
    a1 = -2 * c / a0; a2 = (1 - al) / a0;
}

void Modem::configure(double inputRate) {
    inputRate_ = inputRate;
    ready_ = inputRate >= kWork * 4 - 1 || std::fabs(inputRate - kWork) < 1;
    if (ready_) rs_.configure(inputRate, kWork, 8);
    lp1_.lowpass(kChannelHz, kWork, 0.5412);
    lp2_.lowpass(kChannelHz, kWork, 1.3066);
    post_.lowpass(kSmoothHz, kWork, 0.7071);
    reset();
}

void Modem::setOffset(double hz) { offsetHz_ = hz; }

void Modem::reset() {
    rs_.reset();
    lp1_.clear(); lp2_.clear(); post_.clear();
    prev_ = cf32(0, 0);
    std::fill(ring_.begin(), ring_.end(), 0.0);
    n_ = 0; inCount_ = 0; phase_ = 0;
    dc_ = pw_ = floor_ = 0;
    levelDb_ = -200; snrDb_ = 0; cfoHz_ = 0;
    for (auto& r : reg_) r = 0;
    pendActive_ = lockActive_ = false;
    hits_.clear();
    cur_ = Burst();
}

void Modem::feed(const cf32* x, size_t n, std::vector<Burst>& out) {
    if (!ready_ || !x || !n) return;
    const double step = -2 * kPi * offsetHz_ / inputRate_;
    std::vector<cf32>& mixed = mixBuf_;
    mixed.resize(n);
    for (size_t i = 0; i < n; i++) {
        mixed[i] = x[i] * cf32((float)std::cos(phase_), (float)std::sin(phase_));
        phase_ += step;
        if (phase_ > kPi) phase_ -= 2 * kPi; else if (phase_ < -kPi) phase_ += 2 * kPi;
    }
    inCount_ += (double)n;
    rsBuf_.clear();
    rs_.process(mixed.data(), n, rsBuf_);
    for (const cf32& v : rsBuf_) process(v, out);
}

void Modem::process(cf32 v, std::vector<Burst>& out) {
    double re = lp1_.run(v.real(), lp1_.z1r, lp1_.z2r), im = lp1_.run(v.imag(), lp1_.z1i, lp1_.z2i);
    re = lp2_.run(re, lp2_.z1r, lp2_.z2r); im = lp2_.run(im, lp2_.z1i, lp2_.z2i);
    const cf32 y((float)re, (float)im);
    const double d = std::arg(y * std::conj(prev_));
    prev_ = y;
    const double s = post_.run(d, post_.z1r, post_.z2r);
    ring_[n_ & kMask] = s;
    const double a = 1.0 / (kDcTau * kWork);
    dc_ += (s - dc_) * a;
    const double p = re * re + im * im;
    pw_ += (p - pw_) * (1.0 / 2400.0);
    if (floor_ <= 0 || pw_ < floor_) floor_ = pw_; else floor_ += (pw_ - floor_) * 1e-5;
    if ((n_ & 0x3FF) == 0) {
        levelDb_ = pw_ > 0 ? 10 * std::log10(pw_ + 1e-30) : -200;
        snrDb_ = floor_ > 0 ? (float)(10 * std::log10((pw_ + 1e-30) / (floor_ + 1e-30))) : 0;
    }
    // slicing: one shift register per sample phase, on the mean of 5 samples (centred kAvg samples back)
    double sum5 = 0;
    for (int k = 0; k < 2 * kAvg + 1; k++) sum5 += soft(n_ - k);
    sum5 /= (2 * kAvg + 1);
    const int ph = (int)(n_ % kSps);
    prevWord_[ph] = (prevWord_[ph] << 1) | (reg_[ph] >> 31);
    reg_[ph] = (reg_[ph] << 1) | (sum5 > dc_ ? 1u : 0u);
    if (!lockActive_ && n_ > 4000) {
        const uint32_t xx = reg_[ph] ^ kSyncWord;
        const int e = pop(xx);
        const int ee = std::min(e, 32 - e);
        bool accept = ee <= 2;
        if (!accept && ee <= 5) {   // a weaker sync is only believed after a preamble (alternating bits) in front of it
            const int pe = std::min(pop(prevWord_[ph] ^ 0xAAAAAAAAu), pop(prevWord_[ph] ^ 0x55555555u));
            accept = pe <= 6;
        }
        if (accept) {
            const bool inv = e >= 16;
            if (!pendActive_) { pendActive_ = true; pendUntil_ = n_ + 12; hits_.clear(); }
            hits_.push_back({n_ - kAvg, ee, inv});
        }
        if (pendActive_ && n_ >= pendUntil_) finishPending();
    }
    n_++;
    if (lockActive_) serviceLock(out);
}

void Modem::finishPending() {
    pendActive_ = false;
    if (hits_.empty()) return;
    // the best hits (fewest errors, one polarity); the median of their positions is the bit centre
    int best = 99;
    for (const Hit& h : hits_) best = std::min(best, h.errs);
    bool inv = false;
    for (const Hit& h : hits_) if (h.errs == best) { inv = h.inv; break; }
    std::vector<uint64_t> pos;
    for (const Hit& h : hits_) if (h.errs == best && h.inv == inv) pos.push_back(h.n);
    std::sort(pos.begin(), pos.end());
    end_ = pos[pos.size() / 2];
    inv_ = inv;
    lockDc_ = dc_;
    lockPw_ = pw_;
    batches_ = 0;
    cur_ = Burst();
    cur_.syncErrors = best;
    cur_.timeSec = (double)end_ / kWork;
    cur_.levelDb = (float)(lockPw_ > 0 ? 10 * std::log10(lockPw_ + 1e-30) : -200);
    cur_.cfoHz = (float)(lockDc_ * kWork / (2 * kPi));
    lockActive_ = true;
    syncs_++;
}

bool Modem::serviceLock(std::vector<Burst>& out) {
    // one batch (512 bits) plus the next sync word (32 bits) plus the re-timing margin must be in the ring
    const uint64_t need = end_ + (uint64_t)kSps * 545 + 4 + kAvg;
    if (n_ < need) return false;
    auto avgAt = [&](uint64_t c) {
        double acc = 0;
        for (int k = -kAvg; k <= kAvg; k++) acc += soft(c + k);
        return acc / (2 * kAvg + 1);
    };
    // the batch's own slicer threshold: the midpoint of the two clusters (the carrier offset moves both)
    std::vector<double> v(512);
    for (int i = 0; i < 512; i++) v[i] = avgAt(end_ + (uint64_t)kSps * (i + 1));
    double thr = lockDc_, hi = 1, lo = -1;
    for (int it = 0; it < 3; it++) {
        double sh = 0, sl = 0; int nh = 0, nl = 0;
        for (double x : v) { if (x > thr) { sh += x; nh++; } else { sl += x; nl++; } }
        if (nh < 20 || nl < 20) break;
        hi = sh / nh; lo = sl / nl;
        thr = (hi + lo) / 2;
    }
    const double amp = std::max(1e-6, (hi - lo) / 2);
    if (batches_ == 0) cur_.cfoHz = (float)(thr * kWork / (2 * kPi));
    for (int w = 0; w < 16; w++) {
        uint32_t cw = 0;
        for (int b = 0; b < 32; b++) {
            const double x = v[w * 32 + b] - thr;
            cw = (cw << 1) | (((x > 0) != inv_) ? 1u : 0u);
            cur_.rel.push_back((float)std::min(2.0, std::fabs(x) / amp));
        }
        cur_.words.push_back(cw);
    }
    // the next sync word, searched +-3 samples around its expected place (this also tracks the clock)
    int bestErr = 99, bestD = 0;
    for (int dlt = -3; dlt <= 3; dlt++) {
        const uint64_t e2 = end_ + (uint64_t)kSps * 544 + dlt;
        uint32_t cw = 0;
        for (int b = 0; b < 32; b++) cw = (cw << 1) | (((avgAt(e2 - (uint64_t)kSps * (31 - b)) > thr) != inv_) ? 1u : 0u);
        const int err = pop(cw ^ kSyncWord);
        if (err < bestErr) { bestErr = err; bestD = dlt; }
    }
    batches_++;
    if (bestErr <= 6 && batches_ < kMaxBatches) {
        end_ += (uint64_t)kSps * 544 + bestD;
        lockDc_ = thr;
        return true;
    }
    lockActive_ = false;
    for (auto& r : reg_) r = 0;
    out.push_back(std::move(cur_));
    cur_ = Burst();
    return true;
}

} // namespace dect2::lbj
