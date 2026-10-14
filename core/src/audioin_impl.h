#pragma once
#include "dect2/audioin.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace dect2 {

struct AudioIn::Impl {
    void* backend = nullptr;
    std::vector<float> ring;          // interleaved stereo
    size_t frames = 0;
    std::atomic<uint64_t> w{0}, r{0};
    std::atomic<float> peak{0};
    std::atomic<float> gain{1.f};
    std::atomic<bool> muted{false};
    std::vector<float> scratch;
    std::vector<float> file;          // looped file, interleaved stereo at the session rate
    size_t filePos = 0;
    double toneHz = 1000, tph = 0;
    // Capture callback (real-time): nFrames of interleaved stereo float.
    void push(const float* in, uint32_t nFrames);
};

bool audioInMicStart(AudioIn::Impl* impl, int sampleRate, const std::string& deviceId, std::string& err);
void audioInMicStop(AudioIn::Impl* impl);
std::vector<AudioInputDevice> audioInListMic();

} // namespace dect2
