#include "dect2/tetra/tetra_voice_codec.h"

namespace dect2::tetra {

void TetraVoiceSink::setPcmOut(PcmOut f) { std::lock_guard<std::mutex> lk(mu_); out_ = std::move(f); }
void TetraVoiceSink::setFrameDecoder(TrafficFrameDecoder f) { std::lock_guard<std::mutex> lk(mu_); dec_ = std::move(f); }
void TetraVoiceSink::setCodec(std::unique_ptr<codec::SpeechCodec> c) { std::lock_guard<std::mutex> lk(mu_); codec_ = std::move(c); }
bool TetraVoiceSink::hasCodec() const { std::lock_guard<std::mutex> lk(mu_); return codec_ != nullptr; }
uint64_t TetraVoiceSink::framesDecoded() const { std::lock_guard<std::mutex> lk(mu_); return frames_; }

std::string TetraVoiceSink::status() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (!codec_) return "speech codec not installed: no audio";
    if (!dec_) return "traffic channel decoder not set: no audio";
    return "speech codec ready";
}

void TetraVoiceSink::onCodedFrame(const uint8_t* bits, size_t n) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!codec_ || !dec_ || !out_ || !bits) { dropped_++; return; }
    const std::vector<uint8_t> frames = dec_(bits, n);
    if (frames.empty()) { dropped_++; return; }
    constexpr size_t kFrame = 137;   // kTchSFrameBits: a TCH/S burst holds two frames
    const bool split = frames.size() > kFrame && frames.size() % kFrame == 0;
    const size_t step = split ? kFrame : frames.size();
    for (size_t off = 0; off < frames.size(); off += step) {
        const std::vector<uint8_t> frame(frames.begin() + (std::ptrdiff_t)off, frames.begin() + (std::ptrdiff_t)(off + step));
        const std::vector<int16_t> pcm = codec_->decode(frame);
        if (pcm.empty()) { dropped_++; continue; }
        frames_++;
        out_(pcm.data(), pcm.size(), codec_->sampleRate());
    }
}

} // namespace dect2::tetra
