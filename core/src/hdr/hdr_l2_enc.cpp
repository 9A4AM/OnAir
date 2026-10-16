// HD Radio Layer 2, the encoder side (for the test signal): transfer frames, PDUs and the SIS encoder, the reverse of the decoder in
// hdr_l2.cpp. Adapted from nrsc5 (GPL-3.0, github.com/theori-io/nrsc5).
#include "dect2/hdr/hdr_l2.h"
#include "dect2/hdr/hdr_fec.h"
#include "hdr_l2_internal.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>


namespace dect2 {

const char* hdrProgramTypeName(int t) {
    static const char* const names[] = {"None", "News", "Information", "Sports", "Talk", "Rock", "Classic rock", "Adult hits", "Soft rock", "Top 40",
                                        "Country", "Oldies", "Soft", "Nostalgia", "Jazz", "Classical", "Rhythm and blues", "Soft rhythm and blues",
                                        "Foreign language", "Religious music", "Religious talk", "Personality", "Public", "College", "Spanish talk",
                                        "Spanish music", "Hip hop", "", "", "Weather", "Emergency test", "Emergency"};
    if (t >= 0 && t < 32) return names[t];
    if (t == 65) return "Traffic";
    if (t == 76) return "Special reading services";
    return "";
}

// the data service types of the SIS and the SIG (the values nrsc5.h lists)
const char* hdrDataTypeName(int t) {
    switch (t) {
    case 0: return "non-specific"; case 1: return "News"; case 3: return "Sports"; case 29: return "Weather"; case 31: return "Emergency";
    case 65: return "Traffic"; case 66: return "Image maps"; case 80: return "Text"; case 256: return "Advertising"; case 257: return "Financial";
    case 258: return "Stock ticker"; case 259: return "Navigation"; case 260: return "Program guide"; case 261: return "Audio";
    case 262: return "Private data network"; case 263: return "Service maintenance"; case 264: return "HD Radio system services";
    case 265: return "Audio-related data"; case 511: return "Special tests";
    default: return "";
    }
}

namespace hdr {

const char* mimeName(uint32_t m) {
    switch (m) {
    case kMimePrimaryImage: return "album art (primary image)";
    case kMimeStationLogo: return "station logo";
    case kMimeHdc: return "HDC audio";
    case kMimeText: return "text";
    case kMimeJpeg: return "JPEG";
    case kMimePng: return "PNG";
    case 0x2D42AC3E: return "NAVTEQ traffic";
    case 0x82F03DFC: return "HERE TPEG traffic";
    case 0xB7F03DFC: return "HERE traffic images";
    case 0xEECB55B6: return "HD TMC traffic";
    case 0xB39EBEB2: case 0x4EB03469: case 0x52103469: return "TTN TPEG traffic";
    case kMimeTtnTraffic: return "TTN traffic";
    case kMimeTtnWeather: return "TTN weather";
    default: return "";
    }
}

// ---------------------------------------------------------------- transfer frames (nrsc5 frame.c frame_push())

bool frameGeom(int len, FrameGeom& g) {
    switch (len) {
    case 146176: g = {len, 146176 - 30000, 1248, 24}; return true;   // FM P1
    case 4608: g = {len, 120, 184, 24}; return true;                  // FM P3 (MP3, MP11)
    case 2304: g = {len, 120, 88, 24}; return true;                   // FM P3 (MP2)
    case 3750: g = {len, 120, 160, 22}; return true;                  // AM P1
    case 24000: g = {len, 120, 992, 24}; return true;                 // AM P3 (MA1)
    case 30000: g = {len, 120, 1240, 24}; return true;                // AM P3 (MA3)
    default: return false;
    }
}

int pduBytes(int len) {
    FrameGeom g;
    if (!frameGeom(len, g)) return 0;
    return (len - g.pciLen) / 8;
}

namespace {
// the bit of the L1 stream that carries bit i of the frame (the bytes are sent least significant bit first)
inline int l1Index(int i, int len) {
    const int bs = (i >> 3) << 3;
    const int bl = std::min(8, len - bs);
    return bs + bl - 1 - (i & 7);
}
}

void packTransfer(const std::vector<uint8_t>& pdu, uint32_t pci, int len, uint8_t* bits) {
    FrameGeom g;
    if (!frameGeom(len, g)) return;
    int h = 0;
    size_t bit = 0;
    for (int i = 0; i < len; i++) {
        uint8_t v;
        if (i >= g.start && ((i - g.start) % g.offset) == 0 && h < g.pciLen) { v = (uint8_t)((pci >> (23 - h)) & 1); h++; }
        else { v = bit / 8 < pdu.size() ? (uint8_t)((pdu[bit / 8] >> (7 - (bit & 7))) & 1) : 0; bit++; }
        bits[l1Index(i, len)] = v;
    }
}

void unpackTransfer(const uint8_t* bits, int len, std::vector<uint8_t>& pdu, uint32_t& pci) {
    FrameGeom g;
    pci = 0;
    pdu.clear();
    if (!frameGeom(len, g)) return;
    pdu.reserve((size_t)(len / 8));
    int h = 0, j = 0;
    unsigned val = 0;
    for (int i = 0; i < len; i++) {
        const unsigned b = bits[l1Index(i, len)] & 1;
        if (i >= g.start && ((i - g.start) % g.offset) == 0 && h < g.pciLen) { pci |= b << (23 - h); h++; }
        else {
            val |= b << (7 - j);
            if (++j == 8) { pdu.push_back((uint8_t)val); val = 0; j = 0; }
        }
    }
}

int matchPci(uint32_t pci, int pciLen, uint32_t& match) {
    static const uint32_t cw[8] = {kPciAudio, kPciAudioOpp, kPciAudioFixed, kPciAudioFixedOpp, kPciFixed, 0x8D338D, 0xD8D338, 0x634CE3};
    for (uint32_t c : cw) {
        const int s = __builtin_popcount((pci ^ c) >> (24 - pciLen));
        if (s <= 4) { match = c; return s; }
    }
    return -1;
}

// ---------------------------------------------------------------- encoder pieces

void hdlcAppend(std::vector<uint8_t>& out, const std::vector<uint8_t>& frame) {
    for (uint8_t b : frame) {
        if (b == 0x7E || b == 0x7D) { out.push_back(0x7D); out.push_back((uint8_t)(b ^ 0x20)); }
        else out.push_back(b);
    }
    out.push_back(0x7E);
}

std::vector<uint8_t> aasFrame(uint16_t port, uint16_t seq, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    f.reserve(payload.size() + 7);
    f.push_back(0x21);
    f.push_back((uint8_t)(port & 0xFF)); f.push_back((uint8_t)(port >> 8));
    f.push_back((uint8_t)(seq & 0xFF)); f.push_back((uint8_t)(seq >> 8));
    f.insert(f.end(), payload.begin(), payload.end());
    appendFcs(f);
    return f;
}

std::vector<uint8_t> id3Tag(const std::string& title, const std::string& artist, const std::string& album, const std::string& genre, int xhdrLot, uint32_t xhdrMime) {
    std::vector<uint8_t> fr;
    auto frame = [&](const char* id, const std::vector<uint8_t>& data) {
        fr.insert(fr.end(), id, id + 4);
        const uint32_t n = (uint32_t)data.size();
        fr.push_back((uint8_t)(n >> 24)); fr.push_back((uint8_t)(n >> 16)); fr.push_back((uint8_t)(n >> 8)); fr.push_back((uint8_t)n);
        fr.push_back(0); fr.push_back(0);
        fr.insert(fr.end(), data.begin(), data.end());
    };
    auto text = [&](const char* id, const std::string& s) {
        if (s.empty()) return;
        std::vector<uint8_t> d{0};                  // ISO-8859-1
        d.insert(d.end(), s.begin(), s.end());
        frame(id, d);
    };
    text("TIT2", title);
    text("TPE1", artist);
    text("TALB", album);
    text("TCON", genre);
    if (xhdrLot >= 0) {
        std::vector<uint8_t> d = {(uint8_t)xhdrMime, (uint8_t)(xhdrMime >> 8), (uint8_t)(xhdrMime >> 16), (uint8_t)(xhdrMime >> 24), 0, 2,
                                  (uint8_t)(xhdrLot & 0xFF), (uint8_t)(xhdrLot >> 8)};
        frame("XHDR", d);
    }
    std::vector<uint8_t> t = {'I', 'D', '3', 3, 0, 0};
    const uint32_t n = (uint32_t)fr.size();
    t.push_back((uint8_t)((n >> 21) & 0x7F)); t.push_back((uint8_t)((n >> 14) & 0x7F)); t.push_back((uint8_t)((n >> 7) & 0x7F)); t.push_back((uint8_t)(n & 0x7F));
    t.insert(t.end(), fr.begin(), fr.end());
    return t;
}

std::vector<uint8_t> sigTable(const std::vector<SigService>& services) {
    std::vector<uint8_t> o;
    for (const SigService& s : services) {
        o.push_back(s.audio ? 0x40 : 0x41);
        o.push_back((uint8_t)(s.number & 0xFF)); o.push_back((uint8_t)(s.number >> 8)); o.push_back(0);
        if (!s.name.empty()) {
            o.push_back(0x69);
            o.push_back((uint8_t)(s.name.size() + 2));
            o.push_back(0);
            o.insert(o.end(), s.name.begin(), s.name.end());
        }
        for (const SigService::Comp& c : s.comps) {
            if (c.audio) {
                o.push_back(0x66); o.push_back(12);
                o.push_back((uint8_t)c.id); o.push_back((uint8_t)c.port); o.push_back((uint8_t)c.type);
                o.push_back(0); o.push_back(0); o.push_back(0); o.push_back(0);
            } else {
                o.push_back(0x67); o.push_back(13);
                o.push_back((uint8_t)c.id); o.push_back((uint8_t)(c.port & 0xFF)); o.push_back((uint8_t)(c.port >> 8));
                o.push_back((uint8_t)(c.sdType & 0xFF)); o.push_back((uint8_t)(c.sdType >> 8)); o.push_back((uint8_t)c.type);
                o.push_back(0); o.push_back(0);
            }
            o.push_back((uint8_t)c.mime); o.push_back((uint8_t)(c.mime >> 8)); o.push_back((uint8_t)(c.mime >> 16)); o.push_back((uint8_t)(c.mime >> 24));
        }
    }
    return o;
}

std::vector<std::vector<uint8_t>> lotFragments(const LotFile& f, int repeat) {
    std::vector<std::vector<uint8_t>> out;
    const size_t nf = std::max<size_t>(1, (f.bytes.size() + 255) / 256);
    for (size_t i = 0; i < nf; i++) {
        std::vector<uint8_t> p;
        const bool head = i == 0;
        const size_t name = std::min<size_t>(f.name.size(), 200);
        p.push_back((uint8_t)(8 + (head ? 16 + name : 0)));
        p.push_back((uint8_t)repeat);
        p.push_back((uint8_t)(f.lot & 0xFF)); p.push_back((uint8_t)(f.lot >> 8));
        p.push_back((uint8_t)i); p.push_back((uint8_t)(i >> 8)); p.push_back((uint8_t)(i >> 16)); p.push_back((uint8_t)(i >> 24));
        if (head) {
            p.push_back(1); p.push_back(0); p.push_back(0); p.push_back(0);      // version 1
            const int year = 2030, mon = 1, mday = 1, hour = 0, min = 0;        // expiry, UTC
            p.push_back((uint8_t)(((hour & 3) << 6) | min));
            p.push_back((uint8_t)((mday << 3) | (hour >> 2)));
            p.push_back((uint8_t)(((year & 0xF) << 4) | mon));
            p.push_back((uint8_t)(year >> 4));
            const uint32_t sz = (uint32_t)f.bytes.size();
            p.push_back((uint8_t)sz); p.push_back((uint8_t)(sz >> 8)); p.push_back((uint8_t)(sz >> 16)); p.push_back((uint8_t)(sz >> 24));
            p.push_back((uint8_t)f.mime); p.push_back((uint8_t)(f.mime >> 8)); p.push_back((uint8_t)(f.mime >> 16)); p.push_back((uint8_t)(f.mime >> 24));
            p.insert(p.end(), f.name.begin(), f.name.begin() + (long)name);
        }
        const size_t a = i * 256, b = std::min(f.bytes.size(), a + 256);
        p.insert(p.end(), f.bytes.begin() + (long)a, f.bytes.begin() + (long)b);
        out.push_back(std::move(p));
    }
    return out;
}


int audioPduHeaderBytes(const AudioPduSpec& s) {
    const int nop = (int)s.packetBytes.size();
    return 14 + (locBits(s.codecMode, s.streamId) * nop + 4) / 8 + 3;
}

std::vector<uint8_t> audioPdu(const AudioPduSpec& s, std::vector<uint8_t>& psd) {
    const int nop = (int)s.packetBytes.size();
    const int lcb = locBits(s.codecMode, s.streamId);
    const int hdr = audioPduHeaderBytes(s);
    const int la = hdr + s.psdRoom - 1;                 // last byte of the PSD area
    std::vector<uint8_t> o((size_t)hdr, 0);
    o[8] = (uint8_t)((s.codecMode & 0xF) | ((s.streamId & 3) << 4) | ((s.pduSeq & 3) << 6));
    o[9] = (uint8_t)(((s.pduSeq >> 2) & 1) | (0 << 1) | (0 << 3));     // blend control 0, digital gain 0
    o[10] = (uint8_t)(0 | ((s.latency & 3) << 6));
    o[11] = (uint8_t)(((s.latency >> 2) & 1) | (0 << 1) | (0 << 2) | ((s.seq & 0x1F) << 3));   // whole packets only (pfirst = plast = 0)
    o[12] = (uint8_t)(((s.seq >> 5) & 1) | ((nop & 0x3F) << 1) | 0x80);    // with a header expansion
    o[13] = (uint8_t)la;
    // packet locators: the offset of the last byte (the CRC) of every packet
    std::vector<int> loc;
    int end = la;
    for (int b : s.packetBytes) { end += b + 1; loc.push_back(end); }
    uint8_t* L = &o[14];
    for (int j = 0; j < nop; j++) {
        const int v = loc[(size_t)j];
        if (lcb == 16) { L[2 * j] = (uint8_t)(v & 0xFF); L[2 * j + 1] = (uint8_t)(v >> 8); }
        else if (j % 2 == 0) { L[j / 2 * 3] = (uint8_t)(v & 0xFF); L[j / 2 * 3 + 1] = (uint8_t)((L[j / 2 * 3 + 1] & 0xF0) | ((v >> 8) & 0xF)); }
        else { L[j / 2 * 3 + 1] = (uint8_t)((L[j / 2 * 3 + 1] & 0x0F) | ((v & 0xF) << 4)); L[j / 2 * 3 + 2] = (uint8_t)(v >> 4); }
    }
    // header expansion: program number, then access and program type
    uint8_t* H = &o[(size_t)hdr - 3];
    H[0] = (uint8_t)(0x80 | 0x10 | ((s.program & 7) << 1));
    H[1] = (uint8_t)(0x80 | 0x20 | ((s.access & 1) << 3) | ((s.progType >> 7) & 1));
    H[2] = (uint8_t)(s.progType & 0x7F);
    // the PSD stream, padded with flags
    const size_t take = std::min(psd.size(), (size_t)s.psdRoom);
    o.insert(o.end(), psd.begin(), psd.begin() + (long)take);
    psd.erase(psd.begin(), psd.begin() + (long)take);
    o.insert(o.end(), (size_t)s.psdRoom - take, 0x7E);
    for (int b : s.packetBytes) {
        const size_t st = o.size();
        o.insert(o.end(), (size_t)b, s.fill);
        o.push_back(crc8(&o[st], (size_t)b));
    }
    while (o.size() < 96) o.push_back(0);              // never happens with real packets: the RS word needs 96 bytes
    rsEncodeHeader(o.data());
    return o;
}

void fixedBlock(std::vector<uint8_t>& hdlc, std::vector<uint8_t>& out) {
    static const uint8_t bbm[4] = {0x7D, 0x3A, 0xE2, 0x42};
    out.insert(out.end(), bbm, bbm + 4);
    const size_t n = std::min<size_t>(255, hdlc.size());
    out.insert(out.end(), hdlc.begin(), hdlc.begin() + (long)n);
    hdlc.erase(hdlc.begin(), hdlc.begin() + (long)n);
    for (size_t i = n; i < 255; i++) out.push_back(0x7E);
}

void fixedRegion(const FixedSpec& f, std::vector<uint8_t>& sub, std::vector<uint8_t>& out) {
    // subchannel bytes, then the CCC (channel configuration) and the sync byte that gives its width
    const size_t take = std::min(sub.size(), (size_t)f.subLen);
    out.insert(out.end(), sub.begin(), sub.begin() + (long)take);
    sub.erase(sub.begin(), sub.begin() + (long)take);
    out.insert(out.end(), (size_t)f.subLen - take, 0x7E);
    std::vector<uint8_t> ccc = {0x00, 0x00, 0x00, (uint8_t)(f.subLen & 0xFF), (uint8_t)(f.subLen >> 8)};
    appendFcs(ccc);
    std::vector<uint8_t> w = {0x7E};
    hdlcAppend(w, ccc);
    int width = std::max(f.cccWidth, (int)w.size() + 1);
    width = std::min(30, (width + 1) & ~1);
    while ((int)w.size() < width) w.push_back(0x7E);
    w.resize((size_t)width);
    out.insert(out.end(), w.begin(), w.end());
    const int n = width / 2;
    out.push_back((uint8_t)((n << 4) | n));
}

// ---------------------------------------------------------------- SIS encoder (the reverse of nrsc5 pids.c)

namespace {
void putBits(std::vector<uint8_t>& v, unsigned x, int n) { for (int i = n - 1; i >= 0; i--) v.push_back((uint8_t)((x >> i) & 1)); }
int char5(char c) {
    for (int i = 0; i < 31; i++) if (kChars5[i] == c) return i;
    return 26;
}
}

SisEncoder::SisEncoder(const SisConfig& c) {
    auto add = [&](int id, std::vector<uint8_t> b) { msgs_.push_back({id, std::move(b)}); };
    {   // station id
        std::vector<uint8_t> b;
        for (int i = 0; i < 2; i++) putBits(b, (unsigned)char5(i < (int)c.country.size() ? (char)std::toupper((unsigned char)c.country[(size_t)i]) : ' '), 5);
        putBits(b, 0, 3);
        putBits(b, (unsigned)c.facilityId & 0x7FFFF, 19);
        add(0, b);
    }
    {   // short name (call sign)
        std::vector<uint8_t> b;
        for (int i = 0; i < 4; i++) putBits(b, (unsigned)char5(i < (int)c.callSign.size() ? c.callSign[(size_t)i] : ' '), 5);
        putBits(b, c.fmSuffix ? 1 : 0, 2);
        add(1, b);
    }
    if (!c.name.empty()) {   // universal short station name: up to two frames of 6 characters
        const std::string n = c.name.substr(0, 12);
        const int frames = n.size() > 6 ? 2 : 1;
        for (int f = 0; f < frames; f++) {
            std::vector<uint8_t> b;
            putBits(b, (unsigned)f, 4); putBits(b, 0, 1);
            if (f == 0) { putBits(b, 0, 3); putBits(b, c.fmSuffix ? 1 : 0, 1); putBits(b, (unsigned)(frames - 1), 1); }
            else putBits(b, 0, 5);
            for (int j = 0; j < 6; j++) { const size_t k = (size_t)(f * 6 + j); putBits(b, k < n.size() ? (uint8_t)n[k] : 0, 8); }
            add(8, b);
        }
    }
    if (!c.slogan.empty()) {   // slogan: 5 characters in the first frame, 6 in the others
        const std::string s = c.slogan.substr(0, 95);
        const int len = (int)s.size(), frames = (len + 6) / 6;
        for (int f = 0; f < frames; f++) {
            std::vector<uint8_t> b;
            putBits(b, (unsigned)f, 4); putBits(b, 1, 1);
            int first;
            if (f == 0) { putBits(b, 0, 3); putBits(b, 0, 3); putBits(b, (unsigned)len, 7); first = 0; }
            else { putBits(b, 0, 5); first = f * 6 - 1; }
            const int cnt = f == 0 ? 5 : 6;
            for (int j = 0; j < cnt; j++) { const int k = first + j; putBits(b, k < len ? (uint8_t)s[(size_t)k] : 0, 8); }
            add(8, b);
        }
    }
    if (!c.longName.empty()) {   // long station name: 7-bit characters, 7 per frame, up to 8 frames
        const std::string s = c.longName.substr(0, 56);
        const int frames = std::max(1, ((int)s.size() + 6) / 7);
        for (int f = 0; f < frames; f++) {
            std::vector<uint8_t> b;
            putBits(b, (unsigned)(frames - 1), 3); putBits(b, (unsigned)f, 3);
            for (int j = 0; j < 7; j++) { const size_t k = (size_t)(f * 7 + j); putBits(b, k < s.size() ? (uint8_t)s[k] & 0x7F : 0, 7); }
            putBits(b, 0, 3);   // sequence
            add(2, b);
        }
    }
    if (c.location) {
        const int lat = (int)std::lround(c.lat * 8192), lon = (int)std::lround(c.lon * 8192);
        std::vector<uint8_t> b;
        putBits(b, 1, 1); putBits(b, (unsigned)lat & 0x3FFFFF, 22); putBits(b, (unsigned)(c.altM >> 8) & 0xF, 4);
        add(4, b);
        b.clear();
        putBits(b, 0, 1); putBits(b, (unsigned)lon & 0x3FFFFF, 22); putBits(b, (unsigned)(c.altM >> 4) & 0xF, 4);
        add(4, b);
    }
    for (const auto& a : c.audio) {
        std::vector<uint8_t> b;
        putBits(b, 0, 2); putBits(b, (unsigned)a.access, 1); putBits(b, (unsigned)a.program, 6); putBits(b, (unsigned)a.type, 8);
        putBits(b, 0, 5); putBits(b, (unsigned)a.soundExp, 5);
        add(6, b);
    }
    for (const auto& d : c.data) {
        std::vector<uint8_t> b;
        putBits(b, 1, 2); putBits(b, (unsigned)d.access, 1); putBits(b, (unsigned)d.type, 9); putBits(b, 0, 3); putBits(b, (unsigned)d.mime, 12);
        add(6, b);
    }
    if (!c.message.empty()) {   // station message: 4 characters in frame 0, 6 in the others
        const std::string s = c.message.substr(0, 190);
        const int len = (int)s.size();
        unsigned sum = 0;
        for (char ch : s) sum += (uint8_t)ch;
        const unsigned ck = (((sum >> 8) & 0x7F) + (sum & 0xFF)) & 0x7F;
        const int frames = (len + 7) / 6;
        for (int f = 0; f < frames; f++) {
            std::vector<uint8_t> b;
            putBits(b, (unsigned)f, 5); putBits(b, 0, 2);
            int first, cnt;
            if (f == 0) { putBits(b, 0, 1); putBits(b, 0, 3); putBits(b, (unsigned)len, 8); putBits(b, ck, 7); first = 0; cnt = 4; }
            else { putBits(b, 0, 3); first = f * 6 - 2; cnt = 6; }
            for (int j = 0; j < cnt; j++) { const int k = first + j; putBits(b, k < len ? (uint8_t)s[(size_t)k] : 0, 8); }
            add(5, b);
        }
    }
}

void SisEncoder::next(uint8_t* bits) {
    memset(bits, 0, 80);
    if (msgs_.empty()) { putCrc12(bits); return; }
    const Msg& a = msgs_[pos_ % msgs_.size()];
    const Msg& b = msgs_[(pos_ + 1) % msgs_.size()];
    const bool two = msgs_.size() > 1 && a.bits.size() + b.bits.size() <= 54;
    int off = 2;                       // bit 0: SIS frame, bit 1: one or two messages
    bits[0] = 0;
    bits[1] = two ? 1 : 0;
    auto put = [&](const Msg& m) {
        for (int i = 3; i >= 0; i--) bits[off++] = (uint8_t)((m.id >> i) & 1);
        for (uint8_t x : m.bits) bits[off++] = x;
    };
    put(a);
    if (two) put(b);
    pos_ += two ? 2 : 1;
    putCrc12(bits);
}

}} // namespace dect2::hdr
