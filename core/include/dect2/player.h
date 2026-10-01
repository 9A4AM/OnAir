// Live player: decodes the selected service of the transport stream (video with VideoToolbox when possible, audio
// through CoreAudio) and keeps audio and video in sync using the stream's timestamps.
#pragma once
#include "audioout.h"
#include "ts.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dect2 {

struct VideoFrame {
    double pts = 0;
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;           // RGBA8, only when a subtitle was burnt in; otherwise empty
    std::vector<uint8_t> y, uv;          // NV12 planes (w*h and w*(h/2) bytes, tightly packed); converted to RGB on the GPU
    bool bt709 = true, fullRange = false;
};

struct AudioTrackInfo { int pid = 0; std::string codec, lang; int channels = 0; };

struct PlayerStats {
    bool active = false;
    int serviceId = -1;
    std::string status;
    std::string videoCodec, audioCodec;
    int width = 0, height = 0;
    double fps = 0;
    bool hardware = false;
    uint64_t decoded = 0, shown = 0, late = 0, errors = 0;
    double avOffsetMs = 0;
    double audioBufferMs = 0;
    int videoQueue = 0;
    int audioChannels = 0;
    int underruns = 0;
    bool hasVideo = false, hasAudio = false;
};

class Player {
public:
    Player();
    ~Player();
    void select(int serviceId);            // -1 stops
    int selected() const { return sid_; }
    void push(const uint8_t* packets, size_t nPackets, const TsSnapshot& snap);
    // Newest frame whose presentation time has arrived; null if nothing new since `seq`.
    std::shared_ptr<const VideoFrame> videoFrame(uint64_t& seq);
    PlayerStats stats() const;
    std::vector<AudioTrackInfo> audioTracks() const;
    void setVolume(float v) { audio_.setVolume(v); }
    void setMuted(bool m) { audio_.setMuted(m); }
    void setAudioTrack(int index);         // index into audioTracks()
    void setSubtitles(bool on) { subsOn_ = on; }
    bool subtitlesAvailable() const { return subsAvail_; }

private:
    struct Impl;
    friend struct Impl;
    bool clockNow(double& clock);
    void threadMain();
    std::unique_ptr<Impl> p_;
    std::atomic<int> sid_{-1};
    AudioOut audio_;
    std::thread th_;
    std::atomic<bool> stop_{false}, restart_{false};
    std::atomic<bool> subsOn_{true}, subsAvail_{false};
    ServiceFilter filter_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<uint8_t> in_;
};

} // namespace dect2
