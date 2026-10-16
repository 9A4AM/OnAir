// Capture through miniaudio (WASAPI / ALSA / Pulse / PipeWire). MINIAUDIO_IMPLEMENTATION lives in audioout_miniaudio.cpp.
#include "audioin_impl.h"
#include "miniaudio.h"
#include <cstring>
#include <string>
#include <vector>

namespace dect2 {

namespace {
struct Cap {
    ma_context ctx{};
    ma_device dev{};
    bool haveCtx = false, haveDev = false;
};

void dataCb(ma_device* d, void*, const void* in, ma_uint32 frames) {
    static_cast<AudioIn::Impl*>(d->pUserData)->push(static_cast<const float*>(in), frames);
}
}

bool audioInMicStart(AudioIn::Impl* I, int rate, const std::string& deviceId, std::string& err) {
    auto* b = new Cap;
    ma_context_config cc = ma_context_config_init();
    if (ma_context_init(nullptr, 0, &cc, &b->ctx) != MA_SUCCESS) {
        delete b;
        err = "no sound system for capture";
        return false;
    }
    b->haveCtx = true;
    ma_device_config dc = ma_device_config_init(ma_device_type_capture);
    dc.capture.format = ma_format_f32;
    dc.capture.channels = 2;
    dc.sampleRate = (ma_uint32)rate;
    dc.dataCallback = dataCb;
    dc.pUserData = I;
    ma_device_id chosen{};
    if (!deviceId.empty()) {
        ma_device_info *play = nullptr, *cap = nullptr;
        ma_uint32 nPlay = 0, nCap = 0;
        if (ma_context_get_devices(&b->ctx, &play, &nPlay, &cap, &nCap) == MA_SUCCESS) {
            const unsigned idx = (unsigned)strtoul(deviceId.c_str(), nullptr, 10);
            if (idx < nCap) {
                chosen = cap[idx].id;
                dc.capture.pDeviceID = &chosen;
            }
        }
    }
    if (ma_device_init(&b->ctx, &dc, &b->dev) != MA_SUCCESS || ma_device_start(&b->dev) != MA_SUCCESS) {
        if (b->haveDev) ma_device_uninit(&b->dev);
        ma_context_uninit(&b->ctx);
        delete b;
        err = "the microphone could not be started";
        return false;
    }
    b->haveDev = true;
    I->backend = b;
    return true;
}

void audioInMicStop(AudioIn::Impl* I) {
    auto* b = static_cast<Cap*>(I->backend);
    if (!b) return;
    if (b->haveDev) ma_device_uninit(&b->dev);
    if (b->haveCtx) ma_context_uninit(&b->ctx);
    delete b;
    I->backend = nullptr;
}

std::vector<AudioInputDevice> audioInListMic() {
    std::vector<AudioInputDevice> v;
    ma_context ctx;
    ma_context_config cc = ma_context_config_init();
    if (ma_context_init(nullptr, 0, &cc, &ctx) != MA_SUCCESS) return v;
    ma_device_info *play = nullptr, *cap = nullptr;
    ma_uint32 nPlay = 0, nCap = 0;
    if (ma_context_get_devices(&ctx, &play, &nPlay, &cap, &nCap) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < nCap; i++) v.push_back({std::to_string(i), cap[i].name});
    }
    ma_context_uninit(&ctx);
    return v;
}

} // namespace dect2
