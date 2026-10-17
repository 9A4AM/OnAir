// macOS backend: an AudioUnit on the default output device.
#include "audioout_impl.h"
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#include <string>
#include <vector>

namespace dect2 {

static OSStatus renderCb(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 nFrames, AudioBufferList* io) {
    static_cast<AudioOut::Impl*>(ref)->render((float*)io->mBuffers[0].mData, nFrames);
    return noErr;
}

// Output devices: (id, name) of every device that has output streams.
static std::vector<std::pair<AudioObjectID, std::string>> outputDevices() {
    std::vector<std::pair<AudioObjectID, std::string>> v;
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &sz) != noErr || !sz) return v;
    std::vector<AudioObjectID> ids(sz / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &sz, ids.data()) != noErr) return v;
    for (AudioObjectID id : ids) {
        AudioObjectPropertyAddress st = {kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain};
        UInt32 ssz = 0;
        if (AudioObjectGetPropertyDataSize(id, &st, 0, nullptr, &ssz) != noErr || !ssz) continue;   // no output streams
        AudioObjectPropertyAddress nm = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        CFStringRef cf = nullptr; UInt32 csz = sizeof cf;
        if (AudioObjectGetPropertyData(id, &nm, 0, nullptr, &csz, &cf) != noErr || !cf) continue;
        char buf[256] = {};
        CFStringGetCString(cf, buf, sizeof buf, kCFStringEncodingUTF8);
        CFRelease(cf);
        v.emplace_back(id, buf);
    }
    return v;
}

std::vector<std::string> audioListOutputs() {
    std::vector<std::string> r;
    for (auto& d : outputDevices()) r.push_back(d.second);
    return r;
}

bool audioBackendStart(AudioOut::Impl* I, int rate) {
    AudioObjectID dev = 0;
    if (!I->device.empty()) for (auto& d : outputDevices()) if (d.second == I->device) { dev = d.first; break; }
    AudioComponentDescription d = {kAudioUnitType_Output, dev ? kAudioUnitSubType_HALOutput : kAudioUnitSubType_DefaultOutput, kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent c = AudioComponentFindNext(nullptr, &d);
    AudioUnit unit = nullptr;
    if (!c || AudioComponentInstanceNew(c, &unit) != noErr) return false;
    if (dev) {   // a named device: the HAL unit, output only
        UInt32 off = 0, on = 1;
        AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &off, sizeof off);
        AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &on, sizeof on);
        AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev, sizeof dev);
    }
    AudioStreamBasicDescription f = {};
    f.mSampleRate = rate; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    f.mFramesPerPacket = 1; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 32;
    f.mBytesPerFrame = 8; f.mBytesPerPacket = 8;
    AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
    AURenderCallbackStruct cb = {renderCb, I};
    AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    if (AudioUnitInitialize(unit) != noErr || AudioOutputUnitStart(unit) != noErr) {
        AudioComponentInstanceDispose(unit);
        return false;
    }
    I->backend = unit;
    return true;
}

void audioBackendStop(AudioOut::Impl* I) {
    AudioUnit unit = (AudioUnit)I->backend;
    if (!unit) return;
    AudioOutputUnitStop(unit);
    AudioUnitUninitialize(unit);
    AudioComponentInstanceDispose(unit);
    I->backend = nullptr;
}

} // namespace dect2
