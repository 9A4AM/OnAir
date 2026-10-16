// CCSDS shared pieces (ccsds.h): CRC-16 check value, and the CCSDS 121 Rice coder: round trips over sample widths, block sizes,
// reference intervals, with and without the predictor, flat / smooth / noisy / zero data (zero block runs and the rest-of-segment
// code), a padded last block, truncated input.
#include "dect2/ccsds/ccsds.h"
#include <cstdio>
#include <random>
#include <vector>
using namespace dect2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static bool roundTrip(const std::vector<uint16_t>& v, const ccsds::RiceParams& p, size_t* bytes = nullptr) {
    const std::vector<uint8_t> c = ccsds::riceEncode(v.data(), v.size(), p);
    if (bytes) *bytes = c.size();
    std::vector<uint16_t> o(v.size(), 0xFFFF);
    const size_t n = ccsds::riceDecode(c.data(), c.size(), p, o.data(), o.size());
    return n == v.size() && o == v;
}

int main() {
    const uint8_t chk[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(ccsds::crc16(chk, 9) == 0x29B1, "CRC-16 check value %04x", ccsds::crc16(chk, 9));

    std::mt19937 rng(3);
    int cases = 0;
    for (int bps : {8, 10, 12, 16, 4})
        for (int J : {8, 16, 32})
            for (int rsi : {1, 7, 64, 200})
                for (int pre = 0; pre < 2; pre++)
                    for (int kind = 0; kind < 5; kind++) {
                        ccsds::RiceParams p;
                        p.bitsPerSample = bps; p.blockSize = J; p.rsiBlocks = rsi; p.preprocess = pre != 0;
                        const uint32_t mx = (1u << bps) - 1;
                        const size_t n = (size_t)(rsi * J * 2 + 5);   // two intervals and a partial block
                        std::vector<uint16_t> v(n);
                        for (size_t i = 0; i < n; i++) {
                            switch (kind) {
                            case 0: v[i] = (uint16_t)(rng() & mx); break;                                          // noise
                            case 1: v[i] = (uint16_t)((mx / 2 + (int)(mx / 3 * std::sin(i * 0.01))) & mx); break;  // smooth
                            case 2: v[i] = 0; break;                                                                // all zero
                            case 3: v[i] = (uint16_t)((i / 300) % 2 ? mx : (rng() % 3)); break;                    // flat with jumps to the top
                            default: v[i] = (uint16_t)(i % 97 < 60 ? 17 & mx : (rng() % 5)); break;                // zero-like runs
                            }
                        }
                        size_t bytes = 0;
                        const bool ok = roundTrip(v, p, &bytes);
                        CHECK(ok, "bps %d J %d rsi %d pre %d kind %d (%zu bytes)", bps, J, rsi, pre, kind, bytes);
                        cases++;
                    }
    // compression is real on smooth data, and an all-zero interval is a handful of bytes
    {
        ccsds::RiceParams p;
        std::vector<uint16_t> v(2048);
        for (size_t i = 0; i < v.size(); i++) v[i] = (uint16_t)(128 + 60 * std::sin(i * 0.02));
        size_t b = 0;
        CHECK(roundTrip(v, p, &b) && b < 800, "smooth line: %zu bytes", b);
        std::vector<uint16_t> z(2048, 0);
        CHECK(roundTrip(z, p, &b) && b < 40, "zero line: %zu bytes", b);
    }
    // cut data: fewer samples, no crash
    {
        ccsds::RiceParams p;
        std::vector<uint16_t> v(1000);
        for (auto& s : v) s = (uint16_t)(rng() & 255);
        std::vector<uint8_t> c = ccsds::riceEncode(v.data(), v.size(), p);
        c.resize(c.size() / 2);
        std::vector<uint16_t> o(v.size());
        const size_t n = ccsds::riceDecode(c.data(), c.size(), p, o.data(), o.size());
        CHECK(n < v.size(), "truncated input decoded %zu", n);
        std::vector<uint8_t> junk(300);
        for (auto& b : junk) b = (uint8_t)rng();
        ccsds::riceDecode(junk.data(), junk.size(), p, o.data(), o.size());
    }
    printf("  %d Rice round trips\n", cases);
    printf(fails ? "ccsds rice: %d FAILED\n" : "ccsds rice: all passed\n", fails);
    return fails ? 1 : 0;
}
