// COSPAS-SARSAT message layer: every test beacon survives encode -> decode with all its fields, the BCH codes repair up to 3 / 2 damaged bits and refuse
// more, the fixed fields land where the format puts them, Baudot packing, positions in all four quadrants and the position default.
#include "dect2/sarsat/sarsat_gen.h"
#include "dect2/sarsat/sarsat_proto.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace dect2;
using namespace dect2::sarsat;
#include "../common/test_util.h"

static bool dividesXnPlus1(uint32_t g, int deg, int n) {   // g(x) | x^n + 1 : the generator of a (shortened) cyclic code
    std::vector<uint8_t> w((size_t)n + 1, 0);
    w[0] = 1; w[(size_t)n] = 1;
    return bchRemainder(w, 1, n + 1, g, deg) == 0;
}

int main() {
    CHECK(dividesXnPlus1(0x26D9E3, 21, 127), "BCH-1 generator");
    CHECK(dividesXnPlus1(0x1539, 12, 63), "BCH-2 generator");
    // every test beacon
    for (const Spec& s : testBeacons()) {
        std::vector<uint8_t> b = encode(s);
        CHECK(b.size() == (size_t)(s.longMsg ? 144 : 112), "length %zu", b.size());
        Beacon o;
        CHECK(decode(b, o), "decode failed");
        CHECK(o.valid && o.corrected == 0 && o.longMsg == s.longMsg && o.userProto == s.userProto && o.mid == s.mid && o.protoCode == s.protoCode, "fields");
        CHECK(o.hexId.size() == 15, "hex id %s", o.hexId.c_str());
        CHECK(!o.country.empty(), "country of %d", o.mid);
        CHECK(o.homing == s.homing, "homing");
        if (s.longMsg) CHECK(o.bch2Present && o.bch2Ok, "second check");
        if (s.hasPos && !s.userProto) {
            CHECK(o.hasPos, "position missing");
            const double tol = s.longMsg && s.fine ? 0.0012 : 0.126;
            CHECK(std::fabs(o.lat - s.lat) <= tol && std::fabs(o.lon - s.lon) <= tol, "position %.4f %.4f vs %.4f %.4f", o.lat, o.lon, s.lat, s.lon);
            CHECK(o.fine == (s.longMsg && s.fine), "fine flag");
        } else CHECK(!o.hasPos, "a position that is not there");
        printf("  %s %s %s | %s | %s\n", o.hexId.c_str(), o.beaconType.c_str(), o.country.c_str(), o.idText.c_str(), o.hasPos ? "pos" : "-");
        // up to 3 damaged bits in bits 25-106 are repaired
        for (int nerr = 1; nerr <= 3; nerr++) {
            std::vector<uint8_t> d = encode(s);
            const int pos[3] = {30, 67, 100};
            for (int k = 0; k < nerr; k++) d[(size_t)pos[k]] ^= 1;
            Beacon r;
            CHECK(decode(d, r) && r.valid && r.corrected == nerr && r.hexId == o.hexId, "repair of %d bits", nerr);
        }
        if (s.longMsg) {   // two in the second field
            std::vector<uint8_t> d = encode(s);
            d[112] ^= 1; d[130] ^= 1;
            Beacon r;
            CHECK(decode(d, r) && r.bch2Ok && r.corrected == 2 && std::fabs(r.lat - o.lat) < 1e-9, "repair in the second field");
            d = encode(s);
            d[110] ^= 1; d[115] ^= 1; d[140] ^= 1;
            CHECK(decode(d, r) && r.valid && !r.bch2Ok, "three errors in the second field must fail its check");
        }
    }
    // four damaged bits are beyond the code: refused (or, rarely, a different word; never the original with a clean report)
    {
        int refused = 0, total = 0;
        for (const Spec& s : testBeacons()) {
            for (int off = 0; off < 6; off++) {
                std::vector<uint8_t> d = encode(s);
                for (int k = 0; k < 4; k++) d[(size_t)(26 + off * 3 + k * 17)] ^= 1;
                Beacon r;
                total++;
                refused += !decode(d, r);
            }
        }
        CHECK(refused * 10 >= total * 9, "4 damaged bits refused in %d of %d", refused, total);
    }
    // quadrants and the default
    const double pts[4][2] = {{55.7041, 21.0}, {-33.9249, 18.4241}, {40.7128, -74.0060}, {-12.0464, -77.0428}};
    for (auto& p : pts) {
        Spec s; s.protoCode = 3; s.lat = p[0]; s.lon = p[1]; s.ident = 12345;
        std::vector<uint8_t> b = encode(s);
        Beacon o;
        CHECK(decode(b, o) && o.hasPos && std::fabs(o.lat - p[0]) < 0.0012 && std::fabs(o.lon - p[1]) < 0.0012, "quadrant %.2f %.2f -> %.4f %.4f", p[0], p[1], o.lat, o.lon);
    }
    {
        Spec s; s.protoCode = 3; s.hasPos = false;
        std::vector<uint8_t> b = encode(s);
        Beacon o;
        CHECK(decode(b, o) && !o.hasPos, "default position is no position");
    }
    // sync bits and Baudot
    {
        const std::vector<uint8_t> sb = syncBits(false);
        CHECK(sb.size() == 24 && sb[0] == 1 && sb[14] == 1 && sb[15] == 0 && sb[18] == 1 && sb[19] == 0 && sb[23] == 1, "sync bits");
        const std::vector<uint8_t> st = syncBits(true);
        CHECK(st[16] == 1 && st[17] == 1 && st[18] == 0 && st[19] == 1 && st[23] == 0, "self-test sync");
        CHECK(baudotUnpack(baudotPack("OXTEST", 6), 6) == "OXTEST" && baudotUnpack(baudotPack("N123-9", 6), 6) == "N123-9", "Baudot");
        Spec s; s.longMsg = false; s.selfTest = true; s.protoCode = 14; s.ident = 1;
        std::vector<uint8_t> b = encode(s);
        Beacon o;
        CHECK(decode(b, o) && o.selfTest && o.beaconType == "Test", "self-test");
    }
    CHECK(countryOfMid(277) == "Lithuania" && countryOfMid(366) == "USA" && countryOfMid(1).empty(), "countries");
    return testSummary("test_sarsat_proto");
}
