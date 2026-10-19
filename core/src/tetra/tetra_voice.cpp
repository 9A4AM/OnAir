#include "dect2/tetra/tetra_voice.h"
#include <algorithm>

namespace dect2::tetra {

VoiceVerdict voiceVerdict(const CallEncryption& e) {
    if (!e.known) return VoiceVerdict::StateUnknownNotDecoded;
    if (e.encMode != 0 || e.cellOffersEncryption) return VoiceVerdict::EncryptedNotDecoded;
    return VoiceVerdict::Passed;
}

const char* voiceVerdictText(VoiceVerdict v) {
    switch (v) {
        case VoiceVerdict::Passed: return "voice (clear)";
        case VoiceVerdict::EncryptedNotDecoded: return "encrypted, not decoded";
        default: return "encryption state unknown, not decoded";
    }
}

VoiceVerdict processTrafficBurst(const float* bkn1, const float* bkn2, uint32_t scr, const CallEncryption& e, SpeechFrameSink* sink) {
    const VoiceVerdict v = voiceVerdict(e);   // first, before the burst is read
    if (v != VoiceVerdict::Passed) return v;
    if (!sink || !bkn1 || !bkn2) return v;
    float f[432], t3[432];
    std::copy(bkn1, bkn1 + 216, f);
    std::copy(bkn2, bkn2 + 216, f + 216);
    descramble(f, 432, scr);
    deinterleave(f, t3, 432, 103);
    uint8_t bits[432];
    for (int i = 0; i < 432; i++) bits[i] = t3[i] > 0.f ? 1 : 0;
    sink->onCodedFrame(bits, 432);
    return v;
}

} // namespace dect2::tetra
