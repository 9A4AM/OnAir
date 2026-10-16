// Sound-card IQ source: WebSDR, VAC, or a radio's speaker/line into the computer (not a replacement for an SDR when you need MHz of bandwidth).
// Reuses AudioIn (CoreAudio / miniaudio). The microphone is never opened until start().
#include "dect2/audio/audioin.h"
#include "dect2/source/source.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace dect2 {

namespace {
using Clock = std::chrono::steady_clock;
constexpr double kPi = 3.14159265358979323846;
constexpr int kHilN = AudioHilbert::N;
constexpr int kHilDelay = (kHilN - 1) / 2;

const float* hilbertTaps() {
    static float h[kHilN];
    static bool once = false;
    if (once) return h;
    for (int i = 0; i < kHilN; i++) {
        const int t = i - kHilDelay;
        if (t % 2 == 0) { h[i] = 0.f; continue; }   // even lags (and the centre) are zero
        const double w = 0.54 + 0.46 * std::cos(kPi * t / (kHilDelay + 1));   // Hamming
        h[i] = (float)(2.0 / (kPi * t) * w);
    }
    once = true;
    return h;
}

AudioIqMode modeOf(const TuneSettings& s) {
    const int v = (int)radioNumber(s, "audio_iq", 0);
    if (v < 0 || v > 3) return AudioIqMode::StereoIq;
    return (AudioIqMode)v;
}

class AudioSource : public IqSource {
public:
    explicit AudioSource(DeviceInfo d) : dev_(std::move(d)) {}
    ~AudioSource() override { stop(); }

    bool start(const TuneSettings& s, IqRing& ring, std::string& err) override {
        stop();
        mode_ = modeOf(s);
        in_ = std::make_unique<AudioIn>();
        AudioIn::Kind k = AudioIn::Mic;
        std::string arg = dev_.serial;
        double toneHz = 1000;
        if (dev_.serial.rfind("tone", 0) == 0) {
            k = AudioIn::Tone;
            if (dev_.serial.size() > 5 && dev_.serial[4] == ':') toneHz = atof(dev_.serial.c_str() + 5);
            arg.clear();
        } else if (dev_.serial == "silence") {
            k = AudioIn::Silence;
            arg.clear();
        }
        if (!in_->start(k, 48000, arg, toneHz, err)) {
            in_.reset();
            return false;
        }
        live_ = k == AudioIn::Mic;
        rate_ = in_->sampleRate() > 0 ? in_->sampleRate() : 48000;
        hil_ = AudioHilbert();
        ring_ = &ring;
        run_ = true;
        th_ = std::thread([this] { loop(); });
        return true;
    }

    void stop() override {
        run_ = false;
        if (th_.joinable()) th_.join();
        if (in_) { in_->stop(); in_.reset(); }
        ring_ = nullptr;
    }

    bool retune(const TuneSettings& s, std::string&) override {
        mode_ = modeOf(s);   // the sound card is not tuned; only the IQ mapping and the dial the decoder assumes change
        return true;
    }
    double sampleRate() const override { return rate_; }
    bool realtimeHardware() const override { return true; }

private:
    void loop() {
        std::vector<float> st(2048 * 2);
        std::vector<cf32> iq(2048);
        auto t0 = Clock::now();
        uint64_t sent = 0;
        while (run_ && in_) {
            const int n = 1024;
            in_->read(st.data(), n);
            audioStereoToIq(st.data(), n, mode_.load(), iq.data(), hil_);
            if (live_) {
                ring_->write(iq.data(), (size_t)n);   // a live tap drops when the receiver is slow (the capture ring would overflow anyway)
            } else {
                while (run_ && ring_->capacity() - ring_->available() < (size_t)n) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (!run_) break;
                ring_->write(iq.data(), (size_t)n);
            }
            sent += (uint64_t)n;
            auto due = t0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(sent / rate_));
            std::this_thread::sleep_until(due);
        }
    }

    DeviceInfo dev_;
    std::unique_ptr<AudioIn> in_;
    IqRing* ring_ = nullptr;
    double rate_ = 48000;
    bool live_ = true;
    AudioHilbert hil_;
    std::atomic<AudioIqMode> mode_{AudioIqMode::StereoIq};
    std::atomic<bool> run_{false};
    std::thread th_;
};
} // namespace

void audioStereoToIq(const float* stereo, int n, AudioIqMode mode, cf32* out, AudioHilbert& h) {
    if (n <= 0) return;
    const float* ht = hilbertTaps();
    for (int i = 0; i < n; i++) {
        const float l = stereo[2 * i], r = stereo[2 * i + 1];
        if (mode == AudioIqMode::StereoIq) { out[i] = cf32(l, r); continue; }
        const float reIn = mode == AudioIqMode::MonoReal ? 0.5f * (l + r) : l;
        if (mode == AudioIqMode::MonoReal) { out[i] = cf32(reIn, 0.f); continue; }
        h.x[h.p] = reIn;
        if (++h.p >= kHilN) h.p = 0;
        float im = 0;
        int idx = h.p;   // oldest first (the slot about to be overwritten)
        for (int k = kHilN - 1; k >= 0; k--) {
            im += ht[k] * h.x[idx];
            if (++idx >= kHilN) idx = 0;
        }
        const float re = h.x[(h.p + kHilN - 1 - kHilDelay) % kHilN];
        out[i] = mode == AudioIqMode::Lsb ? cf32(re, -im) : cf32(re, im);
    }
}

std::vector<DeviceInfo> listAudioDevices() {
    std::vector<DeviceInfo> out;
    for (const AudioInputDevice& a : listAudioInputs()) {
        DeviceInfo d;
        d.kind = DeviceInfo::Audio;
        d.serial = a.id;
        d.board = "audio";
        d.name = "Sound card: " + a.name;
        d.minRateHz = 8000;
        d.maxRateHz = 48000;
        d.steadyRateHz = 48000;
        out.push_back(std::move(d));
    }
    return out;
}

std::unique_ptr<IqSource> makeAudioSource(const DeviceInfo& d) { return std::make_unique<AudioSource>(d); }

} // namespace dect2
