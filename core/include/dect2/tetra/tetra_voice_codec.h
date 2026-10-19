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

// Turns the 432 type-3 bits of a traffic burst (one bit per byte, channel coding NOT removed) into the coded speech frame the
// codec takes (bytes). Empty result: the frame is dropped.
// TODO(ETSI EN 300 395-2): the traffic channel coding (RCPC decoding and the ACELP frame layout of the full slot traffic channel)
// is NOT implemented. The exact clause of the public standard could not be read, and the coding parameters must not be guessed.
// Until a decoder written from that clause is set here, no frame reaches the codec and no audio is produced.
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
