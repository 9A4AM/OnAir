#include "dect2/transcribe/segmenter.h"
#include <algorithm>
#include <cmath>

namespace dect2 {

std::vector<float> resampleMono(const float* x, size_t n, int inRate, int outRate) {
    std::vector<float> y;
    if (!n || inRate <= 0 || outRate <= 0) return y;
    if (inRate == outRate) return std::vector<float>(x, x + n);
    const double ratio = (double)inRate / outRate;               // input samples per output sample
    const size_t m = (size_t)((double)n / ratio);
    y.resize(m);
    const double fc = std::min(1.0, (double)outRate / inRate);   // cut-off relative to the input Nyquist
    const int half = 12;
    const double pi = 3.14159265358979323846;
    for (size_t k = 0; k < m; k++) {
        const double pos = (double)k * ratio;
        const long c = (long)std::floor(pos);
        const double frac = pos - (double)c;
        double acc = 0, wsum = 0;
        for (int t = -half + 1; t <= half; t++) {
            const long i = c + t;
            if (i < 0 || i >= (long)n) continue;
            const double d = (double)t - frac;
            const double s = d == 0 ? fc : std::sin(pi * fc * d) / (pi * d);
            const double wnd = 0.5 + 0.5 * std::cos(pi * d / (double)half);   // Hann
            acc += x[i] * s * wnd;
            wsum += s * wnd;
        }
        y[k] = (float)(wsum > 1e-9 ? acc / wsum : 0.0);
    }
    return y;
}

VoiceSegmenter::VoiceSegmenter(int inRate, const SegmenterOptions& o) : rate_(std::max(1000, inRate)), o_(o) {
    blk_ = std::max<size_t>(8, (size_t)(rate_ / 50));
    part_.reserve(blk_);
}

void VoiceSegmenter::push(const float* x, size_t n, int64_t wallMs, const std::function<void(VoiceSegment&&)>& out) {
    size_t i = 0;
    while (i < n) {
        const size_t take = std::min(blk_ - part_.size(), n - i);
        part_.insert(part_.end(), x + i, x + i + take);
        i += take;
        if (part_.size() == blk_) {
            const int64_t endMs = wallMs - (int64_t)((double)(n - i) * 1000.0 / rate_);
            block(part_.data(), part_.size(), endMs, out);
            part_.clear();
        }
    }
}

void VoiceSegmenter::block(const float* x, size_t n, int64_t endMs, const std::function<void(VoiceSegment&&)>& out) {
    double p = 0;
    for (size_t i = 0; i < n; i++) p += (double)x[i] * x[i];
    p /= (double)n;
    const double db = 10 * std::log10(p + 1e-12);
    lastDb_ = (float)db;
    // noise floor: follows a fall at once, a rise slowly (a long transmission must not raise it)
    if (db < floorDb_) floorDb_ = db; else floorDb_ += (db - floorDb_) * (active_ ? 0.0005 : 0.01);
    floorDb_ = std::max(floorDb_, -80.0);
    const bool voice = db > std::max(o_.gateDb, floorDb_ + o_.marginDb);
    const size_t preMax = (size_t)(o_.preRollSec * rate_);
    if (!active_) {
        if (voice) {
            active_ = true;
            seg_ = pre_;
            pre_.clear();
            seg_.insert(seg_.end(), x, x + n);
            voicedBlocks_ = 1; quietRun_ = 0; sumPow_ = p;
            startMs_ = endMs - (int64_t)((double)n * 1000.0 / rate_);
        } else {
            pre_.insert(pre_.end(), x, x + n);
            if (pre_.size() > preMax) pre_.erase(pre_.begin(), pre_.begin() + (long)(pre_.size() - preMax));
        }
        return;
    }
    seg_.insert(seg_.end(), x, x + n);
    if (voice) { voicedBlocks_++; quietRun_ = 0; sumPow_ += p; }
    else quietRun_++;
    const double blockSec = (double)n / rate_;
    if ((double)quietRun_ * blockSec >= o_.hangSec) finish(endMs, false, out);
    else if ((double)seg_.size() / rate_ >= o_.maxSec) finish(endMs, true, out);
}

void VoiceSegmenter::finish(int64_t endMs, bool cut, const std::function<void(VoiceSegment&&)>& out) {
    (void)endMs;
    const double blockSec = (double)blk_ / rate_;
    const double voiced = (double)voicedBlocks_ * blockSec;
    active_ = false;
    if (voiced >= o_.minSec && out) {
        VoiceSegment s;
        s.durSec = voiced;
        s.levelDb = (float)(10 * std::log10(sumPow_ / (double)voicedBlocks_ + 1e-12));
        s.startMs = startMs_;
        s.cut = cut;
        // drop most of the hang, keep a little tail
        const size_t tail = (size_t)(0.25 * rate_);
        const size_t quiet = (size_t)((double)quietRun_ * (double)blk_);
        size_t len = seg_.size();
        if (quiet > tail) len -= std::min(len, quiet - tail);
        s.pcm16k = resampleMono(seg_.data(), len, rate_, 16000);
        // steady level: RMS of the voiced part to about -23 dBFS, gain limited to 36 dB up and 6 dB down
        double g = std::pow(10.0, (-23.0 - s.levelDb) / 20.0);
        g = std::max(0.5, std::min(g, 63.0));
        for (auto& v : s.pcm16k) v = std::max(-1.f, std::min(1.f, (float)(v * g)));
        out(std::move(s));
    }
    seg_.clear();
    pre_.clear();
    voicedBlocks_ = quietRun_ = 0;
    sumPow_ = 0;
    if (cut) {   // a transmission longer than the limit goes on as the next one
        active_ = true;
        voicedBlocks_ = 1;
        sumPow_ = 1e-6;
        startMs_ = endMs;
    }
}

void VoiceSegmenter::flush(int64_t wallMs, const std::function<void(VoiceSegment&&)>& out) {
    if (active_) {
        quietRun_ = std::max<size_t>(quietRun_, 1);
        finish(wallMs, false, out);
    }
    part_.clear();
    pre_.clear();
}

}  // namespace dect2
