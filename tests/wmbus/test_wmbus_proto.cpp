// Wireless M-Bus link layer: CRC, 3-of-6, frame formats A and B, manufacturer code, DIF / VIF records, encrypted payloads.
#include "dect2/wmbus/wmbus_gen.h"
#include "dect2/wmbus/wmbus_proto.h"
#include <cstring>
using namespace dect2;
using namespace dect2::wmbus;
#include "../common/test_util.h"

int main() {
    const uint8_t chk[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(crc16(chk, 9) == 0xC2B7, "CRC-16/EN-13757 check value %04X", crc16(chk, 9));
    for (int n = 0; n < 16; n++) { const int s = encode36((unsigned)n); CHECK(decode36((unsigned)s) == n && __builtin_popcount((unsigned)s) == 3, "3of6 %d", n); }
    CHECK(decode36(0x3F) == -1 && decode36(0) == -1, "invalid 3of6 words");
    CHECK(mfrCodeToString(mfrStringToCode("KAM")) == "KAM" && mfrStringToCode("KAM") == 0x2C2D, "manufacturer code %04X", mfrStringToCode("KAM"));
    for (int m = 0; m < WmbusGenerator::meterCount(); m++) {
        Phy ph;
        auto air = WmbusGenerator::meterFrame(m, 5, ph);
        const bool fb = ph == Phy::C1B;
        CHECK(airLength(air[0], fb) == air.size(), "meter %d: air length %zu vs %zu", m, airLength(air[0], fb), air.size());
        Frame f;
        CHECK(parseFrame(air.data(), air.size(), fb, f) && f.crcOk, "meter %d parse", m);
        CHECK(f.mfr.size() == 3 && f.id.size() == 8, "meter %d fields", m);
        if (m == 2) CHECK(f.encrypted && f.records.empty(), "encrypted meter must not be decoded");
        else CHECK(!f.encrypted && !f.records.empty(), "meter %d records %zu", m, f.records.size());
        auto bad = air; bad[bad.size() / 2] ^= 0x10;
        Frame g;
        CHECK(!parseFrame(bad.data(), bad.size(), fb, g), "meter %d: damaged frame passes", m);
    }
    {
        Phy ph;
        auto air = WmbusGenerator::meterFrame(0, 0, ph);
        Frame f; parseFrame(air.data(), air.size(), false, f);
        CHECK(f.mfr == "KAM" && f.id == "12345678" && f.version == 0x1B && f.type == 0x07 && f.typeName == "water" && f.ci == 0x7A, "link layer %s %s", f.mfr.c_str(), f.id.c_str());
        bool vol = false;
        for (auto& r : f.records) if (r.quantity == "Volume" && r.value == "123.456 m3") vol = true;
        CHECK(vol, "volume record 123.456 m3 (records %zu, first %s)", f.records.size(), f.records.empty() ? "-" : f.records[0].value.c_str());
    }
    {
        Phy ph;
        auto air = WmbusGenerator::meterFrame(4, 1, ph);
        CHECK(ph == Phy::C1B && air.size() > 128, "long B frame %zu", air.size());
        Frame f; CHECK(parseFrame(air.data(), air.size(), true, f) && f.records.size() > 20, "B records %zu", f.records.size());
    }
    {   // hand-made records: types, extension bytes, negative BCD, date, variable length, special DIFs
        const uint8_t d[] = {0x01, 0x13, 0x7F,  0x82, 0x01, 0x13, 0x10, 0x00,  0x0A, 0x5A, 0x34, 0x12,  0x0B, 0x2D, 0x99, 0x99, 0xF9,  0x02, 0x6C, 0x21, 0x3A,  0x0D, 0x78, 0x03, 'A', 'B', 'C',  0x2F, 0x2F, 0x0F, 0x01, 0x02};
        std::vector<Record> r; bool mfr = false;
        CHECK(parseRecords(d, sizeof d, r, mfr) && mfr, "records parse");
        CHECK(r.size() == 6, "record count %zu", r.size());
        if (r.size() == 6) {
            CHECK(r[0].value == "0.127 m3", "int8 %s", r[0].value.c_str());
            CHECK(r[1].storage == 2 && r[1].value == "0.016 m3", "dife %d %s", r[1].storage, r[1].value.c_str());
            CHECK(r[2].value == "123.4 C", "bcd %s", r[2].value.c_str());
            CHECK(r[3].numeric && r[3].number < 0, "negative bcd %s", r[3].value.c_str());
            CHECK(r[4].value == "2025-10-01", "date %s", r[4].value.c_str());
            CHECK(r[5].value == "CBA", "string %s", r[5].value.c_str());
        }
    }
    {   // garbage never crashes
        uint32_t x = 7;
        for (int k = 0; k < 3000; k++) {
            uint8_t b[64]; for (auto& v : b) { x = x * 1664525u + 1013904223u; v = (uint8_t)(x >> 24); }
            Frame f; parseFrame(b, 1 + (size_t)(x % 64), (x & 0x100) != 0, f);
            std::vector<Record> r; bool m; parseRecords(b, sizeof b, r, m);
        }
    }
    return testSummary("wmbus proto");
}
