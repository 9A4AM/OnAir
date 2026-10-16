// DECT A-field: the CRC, the header fields, the RFPI split, the carrier raster.
#include "dect2/dect/dect_proto.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
int main() {
    using namespace dect;
    // the sync words of a base station and a handset are each other's complement
    CHECK((kSyncRfp ^ kSyncPp) == 0xFFFFFFFFu, "sync words");
    const uint64_t tail = 0x0100A5C3D1ULL;
    const uint64_t f = makeAField(TaNt, 1, 0, 0, tail);
    AField a = parseAField(f);
    CHECK(a.crcOk && !a.crcInverted && a.ta == TaNt && a.q1 == 1 && a.tail == tail, "parse: ta %d tail %llx crc %d", a.ta, (unsigned long long)a.tail, a.crcOk);
    a = parseAField(makeAField(TaQt, 0, 3, 1, 0x1122334455ULL, true));
    CHECK(a.crcOk && a.crcInverted && a.ta == TaQt && a.ba == 3 && a.q2 == 1, "complemented CRC form");
    for (int bit = 0; bit < 64; bit++) { a = parseAField(f ^ (1ULL << bit)); CHECK(!a.crcOk, "a single flipped bit (%d) passes the CRC", bit); }
    // the CRC is linear: crc(a ^ b) == crc(a) ^ crc(b) with a zero start value
    CHECK((rcrc(0x123456789ABCULL) ^ rcrc(0x0F0F0F0F0F0FULL)) == rcrc(0x123456789ABCULL ^ 0x0F0F0F0F0F0FULL), "linearity");
    const Rfpi r = rfpiOf(((uint64_t)0 << 35) | ((uint64_t)0x0123 << 19) | ((uint64_t)0x1ABCD << 2));
    CHECK(r.arc == 0 && r.emc == 0x0123 && r.fpn == 0x1ABCD, "RFPI split: emc %x fpn %x", r.emc, r.fpn);
    CHECK(carrierOf(1897.344) == 0 && carrierOf(1881.792) == 9 && carrierOf(1890.0) == -1 && carrierOf(1896.0) == -1 && carrierOf(1895.616) == 1, "carrier raster");
    CHECK(carrierMhz(9) > 1881.79 && carrierMhz(9) < 1881.80, "carrier 9 %.3f", carrierMhz(9));
    if (!fails) printf("dect proto ok\n");
    return fails ? 1 : 0;
}
