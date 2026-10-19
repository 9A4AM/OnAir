// TETRA unencrypted voice path (receive only). Frame and burst structure follow the public ETSI EN 300 392-2 descriptions
// (full slot traffic channel: two 216 bit blocks, scrambled and block interleaved over 432 bits).
//
// Rules, all mandatory:
//  1. Encrypted calls are never decoded. The encryption state is checked FIRST, before any bit of the burst is touched.
//     Unknown encryption state counts as encrypted. No key work, no cipher work of any kind.
//  2. No speech codec is part of this program. The ACELP codec is a user-installed add-on loaded at run time. This code
//     only hands the descrambled, deinterleaved coded frame (432 type-3 bits, channel coding NOT removed) to a SpeechFrameSink.
//  3. A speech frame that is passed on is never stored by this code.
#pragma once
#include "dect2/tetra/tetra_phy.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dect2::tetra {

// Where coded speech frames go: the codec loader (written elsewhere). bits: one bit per byte, 432 of them.
class SpeechFrameSink {
public:
    virtual ~SpeechFrameSink() = default;
    virtual void onCodedFrame(const uint8_t* bits, size_t n) = 0;
};

enum class VoiceVerdict { Passed, EncryptedNotDecoded, StateUnknownNotDecoded };

// What the signalling of the call or cell says about air interface encryption: encMode is the MAC-RESOURCE encryption mode
// (0 clear, anything else encrypted), known is false when no clear signalling of this call or cell was seen.
struct CallEncryption {
    bool known = false;
    int encMode = 0;
    bool cellOffersEncryption = false;   // the cell advertises the air interface encryption service: handled as encrypted
};
VoiceVerdict voiceVerdict(const CallEncryption& e);
const char* voiceVerdictText(VoiceVerdict v);   // "voice (clear)", "encrypted, not decoded", "encryption state unknown, not decoded"

// One full slot traffic burst: bkn1 and bkn2 are the two 216 soft bit blocks (llr > 0 means a 1). The verdict is taken first;
// only a Passed burst is descrambled and deinterleaved and given to the sink (which may be null: the frame is then dropped).
VoiceVerdict processTrafficBurst(const float* bkn1, const float* bkn2, uint32_t scramblerInit, const CallEncryption& e, SpeechFrameSink* sink);

} // namespace dect2::tetra
