#include "cdr_audio.h"
#include "dradec.h"
#include <algorithm>

namespace dect2 {

namespace {
constexpr int kStuckUnits = 80;      // units in (about 1.7 s of sound) without a single sound frame out: the decoder is rebuilt
constexpr int kDeviceRetry = 40;     // sub-frames between attempts to open a sound device that failed
}

CdrAudio::CdrAudio(bool headless) : headless_(headless) {}
CdrAudio::~CdrAudio() { if (dra_) DRADecDestroy(dra_); }

void CdrAudio::resetDecoder() {
    if (dra_) { DRADecDestroy(dra_); dra_ = nullptr; }
    playingId_ = -1;
    unitsSincePcm_ = 0;
}
void CdrAudio::resetAll() { resetDecoder(); lastListen_ = -1; flush(); }
void CdrAudio::flush() { if (out_) out_->flush(); }

void CdrAudio::play(int want, uint16_t id, const std::vector<cdr::AudioUnit>& units, float volume, bool muted, CdrTelemetry& tel) {
    if (want == 0 || (int)id != want || units.empty()) return;
    if (lastListen_ != want || playingId_ != (int)id) {      // another service: nothing of the old one may reach the new sound
        resetDecoder();
        playingId_ = (int)id;
        lastListen_ = want;
        flush();
    }
    if (!dra_) dra_ = DRADecCreate();
    if (!dra_) return;
    for (const auto& u : units) {
        if (u.data.empty()) continue;
        tel.draUnitsIn++;
        if (DRADecSendData(dra_, const_cast<unsigned char*>(u.data.data()), (int)u.data.size()) != 0) { tel.draUnitsBad++; continue; }
        if (++unitsSincePcm_ > kStuckUnits) {                // fed and fed, nothing comes out: a latched decoder (bad header, odd frame size)
            const int keep = playingId_;
            resetDecoder();
            playingId_ = keep;
            dra_ = DRADecCreate();
            if (!dra_) return;
            tel.draResets++;
            continue;
        }
        for (;;) {
            unsigned char* pcm = nullptr;
            int len = 0;
            if (DRADecRecvFrame(dra_, &pcm, &len) != 0) break;
            DRAFrameInfo info;
            if (DRADecGetFrameInfo(dra_, &info) != 0 || info.nSampleRate < 8000 || info.nChannels < 1 || !pcm || len < 4) continue;
            // the decoder returns one frame of 1024 samples, mono or an interleaved stereo pair (a 5.1 stream is mixed down to the pair)
            const int ch = std::min(info.nChannels, 2);
            const int frames = std::min(len / (2 * ch), 1024);
            if (frames < 1) continue;
            unitsSincePcm_ = 0;
            tel.draPcmFrames += (uint64_t)frames;
            tel.draRateHz = info.nSampleRate;
            if (headless_) continue;
            if (!out_ || rate_ != info.nSampleRate) {
                out_ = std::make_unique<AudioOut>();
                out_->start(info.nSampleRate);
                out_->setStartThreshold(info.nSampleRate / 10);
                rate_ = info.nSampleRate;
            } else if (!out_->running() && ++deviceTries_ >= kDeviceRetry) {      // the device was busy or went away: try again
                deviceTries_ = 0;
                out_->start(info.nSampleRate);
            }
            out_->setVolume(volume);
            out_->setMuted(muted);
            // DRA+ PCM arrives in ~640 ms logical-frame bursts; do not flush around 0.5 s or playback drops every frame.
            if (!out_->playing()) out_->setStartThreshold(rate_ / 10);
            if (out_->bufferedFrames() > rate_ * 3 / 2) out_->flush();   // catch up when the radio clock runs ahead of the sound card
            std::vector<float> st((size_t)frames * 2);
            const int16_t* s = reinterpret_cast<const int16_t*>(pcm);
            for (int i = 0; i < frames; i++) {
                const float L = s[i * ch] / 32768.f;
                st[(size_t)i * 2] = L;
                st[(size_t)i * 2 + 1] = ch > 1 ? s[i * ch + 1] / 32768.f : L;
            }
            out_->write(st.data(), frames);
        }
    }
}

} // namespace dect2
