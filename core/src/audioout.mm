#include "dect2/audioout.h"
#import <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace dect2 {

struct AudioOut::Impl {
    AudioUnit unit = nullptr;
    std::vector<float> ring;          // interleaved stereo
    size_t frames = 0;                // ring capacity in frames (power of two)
    std::atomic<uint64_t> w{0}, r{0}; // frame counters
    std::atomic<uint64_t> written{0}, played{0};
    std::atomic<float> volume{1.f};
    std::atomic<bool> muted{false};
    std::atomic<int> underruns{0};
    std::atomic<int> startThreshold{14400};
    std::atomic<bool> playing{false};
    std::atomic<bool> flushReq{false};
};

static OSStatus renderCb(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 nFrames, AudioBufferList* io) {
    auto* I = static_cast<AudioOut::Impl*>(ref);
    float* out = (float*)io->mBuffers[0].mData;
    if (I->flushReq.exchange(false)) { I->r.store(I->w.load()); I->playing = false; }
    uint64_t w = I->w.load(std::memory_order_acquire), r = I->r.load(std::memory_order_relaxed);
    uint64_t avail = w - r;
    if (!I->playing) {
        if ((int)avail >= I->startThreshold.load()) { I->playing = true; I->startThreshold = 16800; } // resume sooner after the first start
    }
    uint32_t n = 0;
    if (I->playing) {
        n = (uint32_t)std::min<uint64_t>(avail, nFrames);
        const float vol = I->muted.load() ? 0.f : I->volume.load();
        const size_t mask = I->frames - 1;
        for (uint32_t i = 0; i < n; i++) {
            size_t k = (size_t)((r + i) & mask) * 2;
            out[2 * i] = I->ring[k] * vol;
            out[2 * i + 1] = I->ring[k + 1] * vol;
        }
        I->r.store(r + n, std::memory_order_release);
        I->played.fetch_add(n);
        if (n < nFrames) { I->underruns++; I->playing = false; }
    }
    if (n < nFrames) memset(out + 2 * n, 0, (size_t)(nFrames - n) * 2 * sizeof(float));
    return noErr;
}

AudioOut::AudioOut() : p_(new Impl) {
    p_->frames = 1 << 17; // 131072 frames ~ 2.7 s at 48 kHz
    p_->ring.assign(p_->frames * 2, 0.f);
}
AudioOut::~AudioOut() { stop(); }

bool AudioOut::start(int rate) {
    if (running_) return true;
    rate_ = rate;
    AudioComponentDescription d = {kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput, kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent c = AudioComponentFindNext(nullptr, &d);
    if (!c || AudioComponentInstanceNew(c, &p_->unit) != noErr) return false;
    AudioStreamBasicDescription f = {};
    f.mSampleRate = rate; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    f.mFramesPerPacket = 1; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
    f.mBytesPerFrame = 8; f.mBytesPerPacket = 8;
    AudioUnitSetProperty(p_->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
    AURenderCallbackStruct cb = {renderCb, p_.get()};
    AudioUnitSetProperty(p_->unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    if (AudioUnitInitialize(p_->unit) != noErr || AudioOutputUnitStart(p_->unit) != noErr) {
        AudioComponentInstanceDispose(p_->unit); p_->unit = nullptr;
        return false;
    }
    running_ = true;
    return true;
}

void AudioOut::stop() {
    if (!running_) return;
    AudioOutputUnitStop(p_->unit);
    AudioUnitUninitialize(p_->unit);
    AudioComponentInstanceDispose(p_->unit);
    p_->unit = nullptr;
    running_ = false;
}

int AudioOut::write(const float* s, int frames) {
    uint64_t w = p_->w.load(std::memory_order_relaxed), r = p_->r.load(std::memory_order_acquire);
    int space = (int)(p_->frames - (w - r));
    int n = std::min(frames, space);
    const size_t mask = p_->frames - 1;
    for (int i = 0; i < n; i++) {
        size_t k = (size_t)((w + i) & mask) * 2;
        p_->ring[k] = s[2 * i]; p_->ring[k + 1] = s[2 * i + 1];
    }
    p_->w.store(w + n, std::memory_order_release);
    p_->written.fetch_add(n);
    return n;
}

void AudioOut::flush() { p_->flushReq = true; }
int AudioOut::bufferedFrames() const { return (int)(p_->w.load() - p_->r.load()); }
uint64_t AudioOut::writtenFrames() const { return p_->written.load(); }
uint64_t AudioOut::playedFrames() const { return p_->played.load(); }
void AudioOut::setVolume(float v) { p_->volume = v; }
void AudioOut::setMuted(bool m) { p_->muted = m; }
int AudioOut::underruns() const { return p_->underruns.load(); }
bool AudioOut::playing() const { return p_->playing.load(); }
void AudioOut::setStartThreshold(int f) { p_->startThreshold = f; }

} // namespace dect2
