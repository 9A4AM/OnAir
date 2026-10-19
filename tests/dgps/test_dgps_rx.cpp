// DGPS beacons: a generated RTCM message is MSK-modulated at 200 bit/s, received and its header recovered.
// Covers both channel polarities, a carrier offset, a corrupted word and a stream with leading noise bits.
#include "dect2/dgps/dgps_rx.h"
#include <cstdio>
#include <cstdlib>
using namespace dect2;
static int fails = 0;
#define CHECK(c, fmt, ...) do { if (!(c)) { printf("FAIL line %d: " fmt "\n", __LINE__ __VA_OPT__(,) __VA_ARGS__); fails++; } } while (0)   // Flawfinder: ignore (the format is a literal)

static const double kFs = 2400.0;   // 12 samples per bit

static dgps::Stats runBits(const std::vector<uint8_t>& bits, double offsetHz) {
    auto iq = dgps::modulate(bits, kFs, offsetHz, 0.5f);
    dgps::Receiver rx;
    rx.process(iq.data(), iq.size(), kFs);
    return rx.stats();
}

int main() {
    dgps::Header h;
    h.type = 1; h.stationId = 0x123; h.zcount = 1234; h.seq = 3; h.length = 2; h.health = 0;
    const std::vector<uint32_t> extra = {0xA5C3F0u, 0x0F0F0Fu};

    for (int pol = 0; pol < 2; pol++) {
        auto msg = dgps::encodeMessage(h, extra, pol);
        CHECK(msg.size() == 4 * dgps::kWordBits, "message is four words, got %zu bits", msg.size());
        // Leading noise-free padding so the bit timing has to be found, then the message, then padding.
        std::vector<uint8_t> bits(37, 0);
        bits.insert(bits.end(), msg.begin(), msg.end());
        bits.insert(bits.end(), 90, 1);
        dgps::Stats s = runBits(bits, 7.0);
        CHECK(s.framesOk >= 1, "pol %d: header found (framesOk %d)", pol, s.framesOk);
        CHECK(s.typesSeen.count(1) == 1, "pol %d: message type 1 seen", pol);
        CHECK(s.last.stationId == 0x123 && s.last.zcount == 1234 && s.last.seq == 3 && s.last.length == 2 &&
              s.last.health == 0, "pol %d: header fields (station %d, zcount %d)", pol, s.last.stationId, s.last.zcount);
        CHECK(s.carrierOffsetHz > 5.0 && s.carrierOffsetHz < 9.0, "pol %d: carrier offset %.1f Hz", pol, s.carrierOffsetHz);
        CHECK(s.levelDb < 0 && s.levelDb > -10, "pol %d: level %.1f dB", pol, s.levelDb);
    }

    // A corrupted header word fails the parity check: no header from this message.
    {
        auto msg = dgps::encodeMessage(h, extra, 0);
        msg[10] ^= 1;   // a data bit of the first word
        msg[11] ^= 1;
        dgps::Stats s = runBits(msg, 0.0);
        CHECK(s.typesSeen.count(1) == 0, "corrupted first word is rejected");
    }

    // Two messages back to back: both recovered, the second with another station ID.
    {
        dgps::Header h2 = h;
        h2.type = 9; h2.stationId = 0x3FF; h2.zcount = 77; h2.health = 5;
        auto a = dgps::encodeMessage(h, extra, 0);
        auto b = dgps::encodeMessage(h2, extra, 0);
        std::vector<uint8_t> bits(a);
        bits.insert(bits.end(), b.begin(), b.end());
        dgps::Stats s = runBits(bits, -12.0);
        CHECK(s.typesSeen.count(1) == 1 && s.typesSeen.count(9) == 1, "both message types seen");
        CHECK(s.last.stationId == 0x3FF && s.last.health == 5, "last header is the second message");
    }

    // A noise-only block gives no header.
    {
        std::vector<uint8_t> noise;
        unsigned seed = 12345u;
        for (int i = 0; i < 4000; i++) { seed = seed * 1103515245u + 12345u; noise.push_back((seed >> 16) & 1); }
        dgps::Stats s = runBits(noise, 0.0);
        CHECK(s.framesOk == 0, "random bits give no header (got %d)", s.framesOk);
    }

    printf(fails ? "dgps: %d failure(s)\n" : "dgps: all checks passed\n", fails);
    return fails ? 1 : 0;
}
