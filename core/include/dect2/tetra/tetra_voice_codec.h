// Connects the TETRA unencrypted voice path (tetra_voice.h) to a user-installed speech codec (codec/speech_codec.h).
// OnAir bundles no speech codec: the codec is a plug-in the user installs, loaded only after the legal notice is accepted
// (that gate runs in the app before setCodec() is ever called). Encrypted or unknown-state calls never reach this class,
// because processTrafficBurst() refuses them before it touches a burst.
#pragma once
#include "dect2/codec/speech_codec.h"
#include "dect2/tetra/tetra_voice.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dect2::tetra {

// Turns the 432 descrambled type-4 bits of a traffic burst (one bit per byte, still interleaved and channel coded) into the speech frames the codec
// plug-in takes: one or more frames of kTchSFrameBits (137) bytes each, one bit per byte in the order B1..B137 of EN 300 395-2 table 4, frames
// back to back (see tetra_traffic.h: makeTrafficFrameDecoder() is the decoder written from clause 5.5). Empty result: the burst is dropped.
// A result whose size is a multiple of 137 is split into frames and the plug-in is called once per frame; any other size is passed as one frame.
using TrafficFrameDecoder = std::function<std::vector<uint8_t>(const uint8_t* bits, size_t n)>;

class TetraVoiceSink final : public SpeechFrameSink {
public:
    using PcmOut = std::function<void(const int16_t* pcm, size_t n, int sampleRate)>;

    void setPcmOut(PcmOut f);
    void setFrameDecoder(TrafficFrameDecoder f);
    // The loaded codec (null: none; frames are then dropped and no audio is made). Call only after the notice gate said Ready.
    void setCodec(std::unique_ptr<codec::SpeechCodec> c);
    bool hasCodec() const;

    // Text for the TETRA panel about the voice output.
    std::string status() const;
    uint64_t framesDecoded() const;

    void onCodedFrame(const uint8_t* bits, size_t n) override;

private:
    mutable std::mutex mu_;
    std::unique_ptr<codec::SpeechCodec> codec_;
    PcmOut out_;
    TrafficFrameDecoder dec_;
    uint64_t frames_ = 0;
    uint64_t dropped_ = 0;
};

} // namespace dect2::tetra
