#pragma once
#include "dect2/audio/videoin.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dect2 {

struct VideoIn::Impl {
    void* backend = nullptr;
    std::mutex mu;
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;          // packed RGB24, latest frame
    std::atomic<uint64_t> seq{0};
    std::atomic<bool> stop{false};
    std::thread fileTh;
    void setRgb(int nw, int nh, const uint8_t* packed);
};

bool videoInCameraStart(VideoIn::Impl* impl, const std::string& deviceId, std::string& err);
void videoInCameraStop(VideoIn::Impl* impl);
std::vector<VideoInputDevice> videoInListCameras();

} // namespace dect2
