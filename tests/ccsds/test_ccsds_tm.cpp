// CCSDS telemetry codeblocks: the randomiser sequence of 131.0-B, the Reed-Solomon codes (255,223) and (255,239) in both bases with errors, interleaving, the codeblock
// layout found from a few blocks, and the TM and AOS headers. The tests follow those of DecSAT (Arclamp, GPL-3.0-or-later), whose code this is adapted from.
#include "dect2/ccsds/ccsds_tm.h"
#include <cstdio>
using namespace dect2::ccsds;
#include "../common/test_util.h"

int main() {
    const auto r = randomiser(8);
    const uint8_t want[8] = {0xFF, 0x48, 0x0E, 0xC0, 0x9A, 0x0D, 0x70, 0xBC};
    bool ok = true;
    for (int i = 0; i < 8; i++) if (r[(size_t)i] != want[i]) ok = false;
    CHECK(ok, "the randomiser starts FF 48 0E C0 9A 0D 70 BC");
    // Reed-Solomon: round trip and error correction up to t, in both bases, full and shortened
    for (int e : {16, 8})
        for (int dual = 0; dual < 2; dual++)
            for (int n : {255, 200}) {
                Rs rs(e, n);
                std::vector<uint8_t> cw((size_t)n);
                for (int i = 0; i < rs.k(); i++) cw[(size_t)i] = (uint8_t)(i * 37 + 11);
                rs.encode(cw.data(), dual != 0);
                std::vector<uint8_t> orig = cw;
                CHECK(rs.decode(cw.data(), dual != 0) == 0 && cw == orig, "E=%d n=%d dual=%d: a clean word", e, n, dual);
                for (int k = 0; k < e; k++) cw[(size_t)(k * 11 % n)] ^= (uint8_t)(0x55 + k);
                const int fixed = rs.decode(cw.data(), dual != 0);
                CHECK(fixed == e && cw == orig, "E=%d n=%d dual=%d: %d errors corrected (got %d)", e, n, dual, e, fixed);
                for (int k = 0; k < e + 3; k++) cw[(size_t)(k * 7 % n)] ^= (uint8_t)(0x31 + k);
                CHECK(rs.decode(cw.data(), dual != 0) < 0 || cw != orig, "E=%d n=%d dual=%d: too many errors are not passed as good", e, n, dual);
            }
    // the codeblock layouts
    std::vector<std::vector<uint8_t>> frames;
    {
        Format f;
        for (int rs = 0; rs < 2; rs++)
            for (int rnd = 0; rnd < 2; rnd++)
                for (int dual = 0; dual < 2; dual++) {
                    if (!rs && dual) continue;
                    Format g;
                    g.rs = rs != 0; g.randomised = rnd != 0; g.layout.e = 16; g.layout.n = 255; g.layout.dual = dual != 0; g.layout.interleave = 2;
                    g.blockBytes = rs ? 2 * 255 : 596;
                    std::vector<std::vector<uint8_t>> blocks;
                    for (int k = 0; k < 4; k++) {
                        std::vector<uint8_t> frame = rs ? std::vector<uint8_t>(g.frameBytes()) : buildTmFrame(596, 0x2DF, 3, k, k * 2, {1, 2, 3});
                        if (rs) { frame = buildTmFrame(g.frameBytes(), 0x2DF, 3, k, k, {9, 8, 7}); }
                        blocks.push_back(encodeBlock(g, frame));
                    }
                    Format found;
                    const bool got = findFormat(blocks, found);
                    CHECK(got && found.rs == g.rs && found.randomised == g.randomised && (!g.rs || found.layout.dual == g.layout.dual), "layout rs=%d rnd=%d dual=%d found: %s", rs, rnd, dual, got ? found.describe().c_str() : "none");
                    if (got && g.rs) CHECK(found.layout.interleave == 2 && found.layout.e == 16, "interleave 2: %s", found.describe().c_str());
                    // errors in a block are corrected
                    if (got && g.rs) {
                        auto b = blocks[0];
                        for (int k = 0; k < 20; k++) b[(size_t)(k * 13)] ^= 0xA5;
                        std::vector<uint8_t> fr; int c;
                        const bool good = found.decode(b.data(), b.size(), fr, c);
                        std::vector<uint8_t> clean; int c0;
                        found.decode(blocks[0].data(), blocks[0].size(), clean, c0);
                        CHECK(good && fr == clean && c == 20, "20 byte errors over two words corrected (%d)", c);
                    }
                }
    }
    // headers
    {
        const auto f = buildTmFrame(100, 0x2DF, 5, 17, 200, {1});
        Header h;
        CHECK(parseHeader(f.data(), f.size(), h) && h.version == 0 && h.scid == 0x2DF && h.vcid == 5 && h.mcCount == 17 && h.vcCount == 200 && h.firstHeader == 0 && !h.idle(), "TM header");
        std::vector<uint8_t> aos{0x40 | 0x03, 0xC5, 0x01, 0x02, 0x03, 0, 0, 0};
        CHECK(parseHeader(aos.data(), aos.size(), h) && h.version == 1 && h.scid == 0x0F && h.vcid == 5 && h.vcCount == 0x010203, "AOS header: scid %d vcid %d count %u", h.scid, h.vcid, h.vcCount);
    }
    return testSummary("ccsds tm");
}
