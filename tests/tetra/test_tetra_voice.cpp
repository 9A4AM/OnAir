// TETRA voice path: an unencrypted generated traffic burst reaches the speech frame sink as the right coded frame; an
// encrypted one, and one with unknown encryption state, produce "not decoded" and the sink is never called.
#include "dect2/tetra/tetra_phy.h"
#include "dect2/tetra/tetra_voice.h"
#include <cstdio>
#include <cstring>
#include <random>
using namespace dect2::tetra;
#include "../common/test_util.h"

struct Sink : SpeechFrameSink {
    int calls = 0; Bits last;
    void onCodedFrame(const uint8_t* b, size_t n) override { calls++; last.assign(b, b + n); }
};

// A generated burst: the 432 type-3 bits are interleaved and scrambled the way the transmitter does, then made soft.
static void makeBurst(const Bits& t3, uint32_t scr, std::vector<float>& l) {
    Bits t4(432), t5;
    for (int i = 1; i <= 432; i++) t4[(size_t)((103 * i) % 432)] = t3[(size_t)(i - 1)];
    t5 = t4;
    scramble(t5.data(), 432, scr);
    l.resize(432);
    for (int i = 0; i < 432; i++) l[(size_t)i] = t5[(size_t)i] ? 1.f : -1.f;
}

int main() {
    std::mt19937 g(9);
    Bits t3(432);
    for (auto& v : t3) v = (uint8_t)(g() & 1);
    const uint32_t scr = scramblerInit(262, 1, 5);
    std::vector<float> l;
    makeBurst(t3, scr, l);

    {   // unencrypted: the coded frame reaches the sink unchanged
        Sink s; CallEncryption e; e.known = true; e.encMode = 0;
        const VoiceVerdict v = processTrafficBurst(l.data(), l.data() + 216, scr, e, &s);
        CHECK(v == VoiceVerdict::Passed, "clear burst passes");
        CHECK(s.calls == 1 && s.last == t3, "clear burst: coded frame delivered intact");
        CHECK(std::strcmp(voiceVerdictText(v), "voice (clear)") == 0, "clear text");
    }
    {   // encrypted: not decoded, sink untouched
        Sink s; CallEncryption e; e.known = true; e.encMode = 1;
        const VoiceVerdict v = processTrafficBurst(l.data(), l.data() + 216, scr, e, &s);
        CHECK(v == VoiceVerdict::EncryptedNotDecoded, "encrypted burst refused");
        CHECK(s.calls == 0, "encrypted burst: sink never called");
        CHECK(std::strcmp(voiceVerdictText(v), "encrypted, not decoded") == 0, "encrypted text");
    }
    {   // cell offering air interface encryption is handled as encrypted
        Sink s; CallEncryption e; e.known = true; e.cellOffersEncryption = true;
        CHECK(processTrafficBurst(l.data(), l.data() + 216, scr, e, &s) == VoiceVerdict::EncryptedNotDecoded && s.calls == 0, "air encryption cell refused");
    }
    {   // state unknown: not decoded
        Sink s; CallEncryption e;
        CHECK(processTrafficBurst(l.data(), l.data() + 216, scr, e, &s) == VoiceVerdict::StateUnknownNotDecoded && s.calls == 0, "unknown state refused");
    }
    printf(fails ? "tetra voice: %d FAILED\n" : "tetra voice: all passed\n", fails);
    return fails ? 1 : 0;
}
