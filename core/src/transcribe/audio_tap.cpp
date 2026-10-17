#include "dect2/transcribe/audio_tap.h"
#include <chrono>

namespace dect2 {

AudioTap& AudioTap::instance() {
    static AudioTap t;
    return t;
}

void AudioTap::setSink(Sink s) {
    std::lock_guard<std::mutex> lk(mu_);
    sink_ = std::move(s);
    owner_ = nullptr;
    on_.store(static_cast<bool>(sink_), std::memory_order_release);
}

void AudioTap::push(const void* src, const float* stereo, int frames, int rate) {
    if (frames <= 0) return;
    std::unique_lock<std::mutex> lk(mu_, std::try_to_lock);
    if (!lk.owns_lock() || !sink_) return;
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (owner_ != src) {
        if (owner_ && now - lastMs_ < 2000) return;
        owner_ = src;
    }
    lastMs_ = now;
    mono_.resize((size_t)frames);
    for (int i = 0; i < frames; i++) mono_[(size_t)i] = 0.5f * (stereo[2 * i] + stereo[2 * i + 1]);
    sink_(mono_.data(), (size_t)frames, rate);
}

}  // namespace dect2
