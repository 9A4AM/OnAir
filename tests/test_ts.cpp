// BB frame <-> transport stream round trips (normal and high-efficiency mode, with lost frames) and PSI/SI parsing.
#include "dect2/bbunpack.h"
#include "dect2/ts.h"
#include <cstdio>
#include <cstring>
#include <random>

using namespace dect2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static uint8_t crc8(const uint8_t* p, int n) {
    unsigned crc = 0;
    for (int i = 0; i < n; i++) for (int b = 7; b >= 0; b--) { unsigned fb = ((crc >> 7) & 1) ^ ((p[i] >> b) & 1); crc = (crc << 1) & 0xff; if (fb) crc ^= 0xD5; }
    return crc;
}

// Pack a TS (sequence of 188-byte packets) into BB frames the way a transmitter does.
struct Packer {
    bool hem;
    int dfBytes;
    size_t pos = 0;           // next byte of the continuous user-packet stream
    std::vector<uint8_t> stream;
    uint8_t crc = 0;
    // build the byte stream of user packets
    void load(const std::vector<uint8_t>& ts) {
        stream.clear();
        for (size_t i = 0; i + 188 <= ts.size(); i += 188) {
            if (hem) stream.insert(stream.end(), ts.begin() + i + 1, ts.begin() + i + 188);
            else { stream.push_back(crc); stream.insert(stream.end(), ts.begin() + i + 1, ts.begin() + i + 188); crc = crc8(&ts[i + 1], 187); }
        }
    }
    bool next(std::vector<uint8_t>& bits, BbHeader& h) {
        if (pos + dfBytes > stream.size()) return false;
        const int stride = hem ? 187 : 188;
        h = BbHeader();
        h.hem = hem; h.dfl = dfBytes * 8; h.upl = 1504; h.issyi = hem ? 1 : 0;
        size_t rem = pos % stride;
        h.syncd = rem == 0 ? 0 : (int)((stride - rem) * 8);
        std::vector<uint8_t> hdr(80);
        buildBbHeader(h, hdr.data());
        if (hem) { // HEM: CRC mode bit
            hdr[79] ^= 1;
        }
        bits.assign(80 + dfBytes * 8, 0);
        std::copy(hdr.begin(), hdr.end(), bits.begin());
        for (int i = 0; i < dfBytes; i++) for (int b = 0; b < 8; b++) bits[80 + i * 8 + b] = (stream[pos + i] >> (7 - b)) & 1;
        pos += dfBytes;
        h.crcOk = true;
        return true;
    }
};

static std::vector<uint8_t> makeTs(int n, std::mt19937& rng) {
    std::vector<uint8_t> ts;
    for (int i = 0; i < n; i++) {
        uint8_t p[188];
        p[0] = 0x47; p[1] = 0x01; p[2] = (uint8_t)(0x00 + (i % 3)); p[3] = 0x10 | (i & 15);
        for (int k = 4; k < 188; k++) p[k] = rng() & 0xFF;
        ts.insert(ts.end(), p, p + 188);
    }
    return ts;
}

static void roundTrip(bool hem, std::mt19937& rng) {
    auto ts = makeTs(400, rng);
    Packer pk; pk.hem = hem; pk.dfBytes = 3000 + (hem ? 3 : 7);
    pk.load(ts);
    BbUnpacker u;
    std::vector<uint8_t> got;
    u.setSink([&](const uint8_t* p) { got.insert(got.end(), p, p + 188); });
    std::vector<uint8_t> bits; BbHeader h;
    int frame = 0, lostFrame = 7;
    std::vector<int> lostAt;
    while (pk.next(bits, h)) {
        if (frame == lostFrame) u.lost(); else u.push(bits, h);
        frame++;
    }
    // everything except the packets overlapping the lost frame must come out identical, in order
    size_t matched = 0;
    for (size_t i = 0; i + 188 <= got.size(); i += 188) {
        bool found = false;
        for (size_t j = matched; j + 188 <= ts.size(); j += 188) if (!memcmp(&got[i], &ts[j], 188)) { matched = j + 188; found = true; break; }
        CHECK(found, "%s: output packet %zu does not appear in order in the input", hem ? "HEM" : "NM", i / 188);
        if (!found) break;
    }
    const int stride = hem ? 187 : 188;
    size_t sentBytes = (pk.stream.size() / pk.dfBytes) * pk.dfBytes;
    size_t expectedMin = sentBytes / stride - (pk.dfBytes / stride + 3);
    CHECK(got.size() / 188 >= expectedMin, "%s: only %zu packets recovered (expected at least %zu)", hem ? "HEM" : "NM", got.size() / 188, expectedMin);
    CHECK(u.stats().crcErrors == 0, "%s: unexpected CRC-8 errors %llu", hem ? "HEM" : "NM", (unsigned long long)u.stats().crcErrors);
    printf("%s round trip: %zu/%zu packets recovered around one lost frame\n", hem ? "HEM   " : "normal", got.size() / 188, ts.size() / 188);
}

static std::vector<uint8_t> section(std::vector<uint8_t> body) { // body starts at table_id, CRC appended
    uint32_t crc = mpegCrc32(body.data(), (int)body.size());
    body.push_back(crc >> 24); body.push_back(crc >> 16); body.push_back(crc >> 8); body.push_back(crc);
    return body;
}
static void feedSection(TsDemux& d, int pid, const std::vector<uint8_t>& sec, int& cc) {
    uint8_t p[188]; memset(p, 0xFF, 188);
    p[0] = 0x47; p[1] = 0x40 | (pid >> 8); p[2] = pid & 0xFF; p[3] = 0x10 | (cc++ & 15); p[4] = 0;
    memcpy(p + 5, sec.data(), sec.size());
    d.feed(p);
}

int main() {
    std::mt19937 rng(4);
    roundTrip(true, rng);
    roundTrip(false, rng);
    // PSI/SI
    TsDemux d;
    int cc = 0;
    // PAT: program 100 -> PMT 0x100
    std::vector<uint8_t> pat = {0x00, 0xB0, 13, 0x00, 0x07, 0xC1, 0, 0, 0x00, 100, 0xE1, 0x00};
    feedSection(d, 0, section(pat), cc);
    // PMT: PCR 0x101, H.264 on 0x101, AAC on 0x102 with language
    std::vector<uint8_t> pmt = {0x02, 0xB0, 0, 0x00, 100, 0xC1, 0, 0, 0xE1, 0x01, 0xF0, 0x00,
                                0x1B, 0xE1, 0x01, 0xF0, 0x00,
                                0x0F, 0xE1, 0x02, 0xF0, 6, 0x0A, 4, 'e', 'n', 'g', 0};
    pmt[2] = (uint8_t)(pmt.size() - 3 + 4);
    feedSection(d, 0x100, section(pmt), cc);
    // SDT: service 100 "News One" by "Provider"
    std::vector<uint8_t> sdt = {0x42, 0xF0, 0, 0x00, 0x07, 0xC1, 0, 0, 0x23, 0x10, 0xFF,
                                0x00, 100, 0xFC, 0x80, 0, // service entry (running, free)
                                0x48, 0, 0x01, 8, 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 8, 'N', 'e', 'w', 's', ' ', 'O', 'n', 'e'};
    sdt[17] = (uint8_t)(sdt.size() - 18); // descriptor length field of the entry (bytes after it)
    sdt[15] = 0x80 | 0; sdt[16] = (uint8_t)(sdt.size() - 17);
    sdt[2] = (uint8_t)(sdt.size() - 3 + 4);
    // descriptor 0x48 length
    sdt[17 - 1 + 1] = 0; // placeholder overwritten below
    // rebuild the entry cleanly
    std::vector<uint8_t> desc = {0x48, (uint8_t)(3 + 8 + 8), 0x01, 8, 'P', 'r', 'o', 'v', 'i', 'd', 'e', 'r', 8, 'N', 'e', 'w', 's', ' ', 'O', 'n', 'e'};
    std::vector<uint8_t> sdt2 = {0x42, 0xF0, 0, 0x00, 0x07, 0xC1, 0, 0, 0x23, 0x10, 0xFF, 0x00, 100, 0xFC, (uint8_t)(0x80 | (desc.size() >> 8)), (uint8_t)(desc.size() & 0xFF)};
    sdt2.insert(sdt2.end(), desc.begin(), desc.end());
    sdt2[2] = (uint8_t)(sdt2.size() - 3 + 4);
    feedSection(d, 0x11, section(sdt2), cc);
    TsSnapshot sn = d.snapshot();
    CHECK(sn.services.size() == 1, "service count %zu", sn.services.size());
    if (!sn.services.empty()) {
        auto& sv = sn.services[0];
        CHECK(sv.id == 100 && sv.name == "News One" && sv.provider == "Provider", "SDT parse: id %d name '%s' provider '%s'", sv.id, sv.name.c_str(), sv.provider.c_str());
        CHECK(sv.pmtPid == 0x100 && sv.pcrPid == 0x101 && sv.streams.size() == 2, "PMT parse: pmt %x pcr %x streams %zu", sv.pmtPid, sv.pcrPid, sv.streams.size());
        if (sv.streams.size() == 2) CHECK(sv.streams[0].codec == "H.264" && sv.streams[1].codec == "AAC" && sv.streams[1].lang == "eng", "stream parse: %s / %s %s", sv.streams[0].codec.c_str(), sv.streams[1].codec.c_str(), sv.streams[1].lang.c_str());
    }
    CHECK(sn.onid == 0x2310 && sn.tsid == 7, "ids onid %x tsid %x", sn.onid, sn.tsid);
    // text decoding
    const uint8_t arabic[] = {0x02, 0xE5, 0xD1, 0xCD, 0xC8, 0xC7}; // ISO-8859-6: "مرحبا"
    CHECK(dvbText(arabic, 6) == "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7", "ISO-8859-6 decode");
    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
