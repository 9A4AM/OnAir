// HFDL layers: check sequences, the interleaver, the code, the PDU builders and parsers, every rate's bit chain end to end.
#include "dect2/hfdl_proto.h"
#include <cmath>
#include <cstdio>
#include <set>
using namespace dect2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main() {
    // CRC-16/X.25 check value
    const uint8_t s9[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(hfdlFcs(s9, 9) == 0x906E, "fcs %04X", hfdlFcs(s9, 9));
    // interleaver: a permutation for every rate
    for (int m = 0; m < 8; m++) {
        const auto& p = hfdlInterleave(m);
        std::set<int> u(p.begin(), p.end());
        CHECK((int)u.size() == hfdlCodedBits(m) && *u.begin() == 0 && *u.rbegin() == hfdlCodedBits(m) - 1, "interleaver %d", m);
    }
    CHECK(hfdlPayloadOctets(0) == 66 && hfdlPayloadOctets(3) == 404, "payload %d %d", hfdlPayloadOctets(0), hfdlPayloadOctets(3));
    CHECK(hfdlBurstSyms(0) + kHfdlPrekey < kHfdlSlotSec * kHfdlBaud && hfdlBurstSyms(4) + kHfdlPrekey < 2 * kHfdlSlotSec * kHfdlBaud, "burst fits its slots");
    // M1 shifts are told apart
    for (int a = 0; a < 8; a++) for (int b = a + 1; b < 8; b++) {
        int s = 0; for (int k = 0; k < 127; k++) s += hfdlMBit(a, k) == hfdlMBit(b, k) ? 1 : -1;
        CHECK(std::abs(s) < 20, "M1 %d %d cross %d", a, b, s);
    }
    // squitter
    HfdlSquitterSpec sq; sq.gsId = 7; sq.frameIndex = 1234; sq.mask = 0x2B; sq.other[0] = {3, true, 0x5}; sq.other[1] = {17, false, 0x41};
    std::vector<HfdlMessage> out;
    const auto spdu = hfdlSpdu(sq);
    CHECK(hfdlParsePdu(spdu.data(), spdu.size(), out) && out.size() == 1, "spdu parse");
    if (out.size() == 1) {
        const HfdlMessage& m = out[0];
        CHECK(m.squitter && m.gsId == 7 && m.frameIndex == 1234 && m.utcSync, "spdu fields");
        CHECK(m.activeKhz.size() == 4 && m.activeKhz[0] == 11384 && m.activeKhz[1] == 10081, "spdu freqs %zu", m.activeKhz.size());
        CHECK(m.detail.find("Reykjavik") != std::string::npos && m.detail.find("Canarias") != std::string::npos, "other stations\n%s", m.detail.c_str());
    }
    auto bad = spdu; bad[20] ^= 4; out.clear();
    CHECK(!hfdlParsePdu(bad.data(), bad.size(), out), "bad spdu rejected");
    // downlink: logon, performance data, ACARS in one MPDU
    const auto mp = hfdlMpduDown(1, 23, {hfdlLogonRequest(0xA1B2C3), hfdlUnnumbered(hfdlPerfData("UA0901", 51.4775, -45.25, 37000, 1, 2)),
                                         hfdlUnnumbered(hfdlAcarsHfnpdu("N123UA", "H1", '2', "#DFB HELLO HFDL", true))});
    out.clear();
    CHECK(hfdlParsePdu(mp.data(), mp.size(), out) && out.size() == 3, "mpdu %zu", out.size());
    if (out.size() == 3) {
        CHECK(out[0].kind == "Logon request" && out[0].icao == 0xA1B2C3 && !out[0].uplink, "logon");
        CHECK(out[1].hfnpdu == "Performance data" && out[1].hasPos && std::fabs(out[1].lat - 51.4775) < 0.001 && std::fabs(out[1].lon + 45.25) < 0.001 && out[1].flight == "UA0901" && out[1].utcSec == 37000,
              "perf %f %f %s", out[1].lat, out[1].lon, out[1].flight.c_str());
        CHECK(out[2].acars && out[2].acarsCrcOk && out[2].reg == "N123UA" && out[2].label == "H1" && out[2].acarsText.find("HFDL") != std::string::npos, "acars %s / %s", out[2].reg.c_str(), out[2].acarsText.c_str());
    }
    // uplink logon confirm, frequency data
    const auto up = hfdlMpduUp(2, 0, {hfdlLogonConfirm(0x4CA2B1, 45)});
    out.clear();
    CHECK(hfdlParsePdu(up.data(), up.size(), out) && out.size() == 1 && out[0].uplink && out[0].icao == 0x4CA2B1 && out[0].acId == 45 && out[0].gsId == 2, "logon confirm");
    const auto fd = hfdlMpduDown(3, 9, {hfdlUnnumbered(hfdlFreqData("BA0117", -33.5, 151.25, 7200, {{3, true, 3}, {7, true, 1}}))});
    out.clear();
    CHECK(hfdlParsePdu(fd.data(), fd.size(), out) && out.size() == 1 && out[0].hfnpdu == "Frequency data" && std::fabs(out[0].lat + 33.5) < 0.001, "freq data");
    // the bit chain of every rate, noiseless
    for (int m = 0; m < 8; m++) {
        const HfdlRate& r = hfdlRate(m);
        std::vector<uint8_t> pdu = m == 0 || m == 4 ? spdu : mp;
        const auto syms = hfdlBurst(m, pdu);
        CHECK((int)syms.size() == kHfdlPrekey + hfdlBurstSyms(m), "burst length %d", m);
        std::vector<int8_t> scr;
        hfdlScramblerSigns(r.blocks * kHfdlDataLen, scr);
        std::vector<float> soft((size_t)hfdlCodedBits(m));
        int i = kHfdlPrekey + kHfdlPreLen, di = 0;
        for (int b = 0; b < r.blocks; b++, i += kHfdlTLen)
            for (int q = 0; q < kHfdlDataLen; q++, i++, di++) hfdlSoftBits(r.bitsPerSym, syms[(size_t)i] * (float)scr[(size_t)di], 1.f, &soft[(size_t)di * r.bitsPerSym]);
        const auto dec = hfdlDecodeSoft(m, soft);
        bool same = dec.size() >= pdu.size();
        for (size_t k = 0; same && k < pdu.size(); k++) same = dec[k] == pdu[k];
        CHECK(same, "chain m1 %d (%d bit/s)", m, r.bps);
    }
    printf(fails ? "hfdl proto: %d FAILED\n" : "hfdl proto: all passed\n", fails);
    return fails ? 1 : 0;
}
