// The C++ implementation of drm_proto.h: the reference of the differential test against rust/onair_drm, and what DECT2_RUST=OFF builds use. The code is the
// parsing that used to sit in drm_msg.cpp (FAC, SDC, text message, super frame, tables) and drm_data.cpp (data service helpers), moved here without a change of
// behaviour apart from two bounds checks: the super frame parser refuses a higher protected part that does not fit the logical frame (and more than 16 frames).
#include "dect2/drm/drm_proto.h"
#ifndef DECT2_RUST_DRM
#include "dect2/common/crc.h"
#include <algorithm>
#include <cstring>

namespace dect2::drmp {
using namespace dect2::drm;

namespace {
struct BitWriter {
    uint8_t* p; int n = 0;
    explicit BitWriter(uint8_t* b) : p(b) {}
    void put(uint32_t v, int bits) { for (int i = bits - 1; i >= 0; i--) p[n++] = (uint8_t)((v >> i) & 1); }
};
struct BitReader {
    const uint8_t* p; int n = 0;
    explicit BitReader(const uint8_t* b) : p(b) {}
    uint32_t get(int bits) { uint32_t v = 0; for (int i = 0; i < bits; i++) v = (v << 1) | (p[n++] & 1u); return v; }
};
}

uint32_t crc8(const uint8_t* bits, size_t n) { return crc::bits(bits, n, 8, 0x1D, 0xFF, 0xFF); }        // all ones at the start, 1's complement before transmission
uint32_t crc16(const uint8_t* bits, size_t n) { return crc::bits(bits, n, 16, 0x1021, 0xFFFF, 0xFFFF); }

// ---------------------------------------------------------------- FAC
bool facParse(const uint8_t* bits, bool modeE, FacInfo& f) {
    const int total = modeE ? 116 : 72;
    uint8_t tmp[120];
    const int body = total - 8;
    std::memcpy(tmp, bits, (size_t)body);
    int cn = body;
    if (modeE) { for (int i = 0; i < 4; i++) tmp[cn++] = 0; }
    BitReader tail(bits + body);
    if (crc8(tmp, (size_t)cn) != tail.get(8)) return false;
    BitReader r(bits);
    f = FacInfo();
    f.enhancement = r.get(1) != 0; f.identity = (int)r.get(2); f.modeE = r.get(1) != 0;
    f.occupancy = (int)r.get(3); f.interleaver = (int)r.get(1); f.mscMode = (int)r.get(2); f.sdcMode = (int)r.get(1);
    f.numServicesCode = (int)r.get(4); f.reconfig = (int)r.get(3); f.toggle = (int)r.get(1); r.get(1);
    if (f.modeE != modeE) return false;
    f.nSvc = modeE ? 2 : 1;
    for (int i = 0; i < f.nSvc; i++) {
        FacService& s = f.svc[i];
        s.id = r.get(24); s.shortId = (int)r.get(2); s.audioCa = r.get(1) != 0; s.language = (int)r.get(4);
        s.data = r.get(1) != 0; s.descriptor = (int)r.get(5); s.dataCa = r.get(1) != 0; r.get(6);
    }
    return true;
}

int facAudioServices(int c) {
    static const int a[16] = {4, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, -1, 3, 3, -1, 0};
    return a[c & 15];
}
int facDataServices(int c) {
    static const int d[16] = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, -1, 0, 1, -1, 4};
    return d[c & 15];
}

std::string languageName(int c) {
    static const char* n[16] = {"", "Arabic", "Bengali", "Chinese (Mandarin)", "Dutch", "English", "French", "German", "Hindi", "Japanese", "Javanese", "Korean",
                                "Portuguese", "Russian", "Spanish", "Other"};
    return n[c & 15];
}
std::string programmeTypeName(int c) {
    static const char* n[32] = {"", "News", "Current affairs", "Information", "Sport", "Education", "Drama", "Culture", "Science", "Varied", "Pop music",
                                "Rock music", "Easy listening", "Light classical", "Serious classical", "Other music", "Weather", "Finance", "Children's",
                                "Social affairs", "Religion", "Phone-in", "Travel", "Leisure", "Jazz", "Country music", "National music", "Oldies", "Folk music",
                                "Documentary", "Alarm", "Skip"};
    return n[c & 31];
}
std::string mscModeName(bool modeE, int m) {
    if (!modeE) return m == 0 ? "64-QAM" : m == 3 ? "16-QAM" : "reserved";
    return m == 0 ? "16-QAM" : m == 3 ? "4-QAM" : "reserved";
}
std::string occupancyName(bool modeE, int o) {
    static const char* a[6] = {"4.5 kHz", "5 kHz", "9 kHz", "10 kHz", "18 kHz", "20 kHz"};
    if (modeE) return o == 0 ? "100 kHz" : "reserved";
    return o >= 0 && o < 6 ? a[o] : "reserved";
}

std::string appName(int domain, int app) {
    if (domain != 0) return domain == 1 ? "DRM domain application" : "application of another domain";
    switch (app) {
    case 0x001: return "dynamic labels";
    case 0x002: return "MOT slide show";
    case 0x003: return "MOT broadcast website";
    case 0x004: return "TPEG";
    case 0x005: return "DGPS";
    case 0x006: return "TMC";
    case 0x007: return "EPG";
    case 0x008: return "DAB Java";
    case 0x009: return "DMB";
    case 0x00A: return "IPDC services";
    case 0x00B: return "voice applications";
    case 0x00C: return "middleware";
    case 0x00D: return "filecasting";
    case 0x44A: return "Journaline";
    }
    return app < 0 ? "unknown application" : "unlisted application";
}

// ---------------------------------------------------------------- SDC
int sdcParse(const uint8_t* d, int bytes, SdcInfo& out) {
    int pos = 0, count = 0;
    while (pos + 2 <= bytes) {
        const int len = d[pos] >> 1, ver = d[pos] & 1, type = d[pos + 1] >> 4, nib = d[pos + 1] & 15;
        if (len == 0 && ver == 0 && type == 0 && nib == 0) break;            // padding
        if (pos + 2 + len > bytes) break;
        const uint8_t* b = d + pos + 2;
        count++;
        if (type == 0 && len % 3 == 0 && len >= 3 && len <= 12) {
            SdcMux m;
            m.present = true; m.protA = (nib >> 2) & 3; m.protB = nib & 3; m.nStreams = len / 3;
            for (int i = 0; i < m.nStreams; i++) {
                m.stream[i].lenA = (b[i * 3] << 4) | (b[i * 3 + 1] >> 4);
                m.stream[i].lenB = ((b[i * 3 + 1] & 15) << 8) | b[i * 3 + 2];
            }
            (ver ? out.muxNext : out.mux) = m;
        } else if (type == 1) {
            const int id = (nib >> 2) & 3;
            SdcLabel& l = out.label[id];
            l.present = true; l.shortId = id; l.textControl = 0; l.text.clear();
            int start = 0;
            if (len > 0 && b[0] >= 1 && b[0] <= 15) { l.textControl = b[0]; start = 1; }
            for (int i = start; i < len; i++) l.text.push_back((char)b[i]);
        } else if (type == 5 && len >= 1 && !ver) {
            SdcApp& a = out.app[(nib >> 2) & 3];
            a.present = true; a.shortId = (nib >> 2) & 3; a.streamId = nib & 3; a.packet = (b[0] & 0x80) != 0;
            a.domain = b[0] & 7;
            size_t at = 1;
            if (a.packet) { a.dataUnit = (b[0] & 0x40) != 0; a.packetId = (b[0] >> 4) & 3; a.packetLen = len >= 2 ? b[1] : 0; at = 2; }
            a.appData.clear();
            for (size_t i = at; i < (size_t)len; i++) a.appData.push_back(b[i]);
        } else if (type == 8 && len >= 3) {
            uint8_t bits[40];
            BitWriter w(bits);
            w.put((uint32_t)nib, 4);
            for (int i = 0; i < len && i < 4; i++) w.put(b[i], 8);
            BitReader r(bits);
            SdcTime& t = out.time;
            t.present = true; t.mjd = (int)r.get(17); t.hour = (int)r.get(5); t.minute = (int)r.get(6);
            t.hasOffset = len >= 4;
            if (t.hasOffset) { r.get(2); const int sense = (int)r.get(1); const int v = (int)r.get(5); t.offsetHalfHours = sense ? -v : v; }
        } else if (type == 9 && len >= 2 && !ver) {
            SdcAudio a;
            a.present = true; a.shortId = (nib >> 2) & 3; a.streamId = nib & 3;
            a.coding = b[0] >> 6; a.sbr = (b[0] >> 5) & 1; a.mode = (b[0] >> 3) & 3; a.rateCode = b[0] & 7;
            a.text = (b[1] & 0x80) != 0; a.enhancement = (b[1] & 0x40) != 0; a.mps = (b[1] >> 3) & 7;
            if (a.coding == 3) a.sbr = 0;
            a.config.assign(b + 2, b + len);
            out.audio[a.shortId] = a;
        } else if (type == 12 && len >= 5 && !ver) {
            SdcLang& l = out.lang[(nib >> 2) & 3];
            l.present = true; l.shortId = (nib >> 2) & 3;
            l.language.assign((const char*)b, 3); l.country.assign((const char*)b + 3, 2);
        } else {
            out.otherTypes.push_back(type);
        }
        pos += 2 + len;
    }
    out.entities = count;
    return count;
}

bool sdcCheckBits(const uint8_t* bits, int fieldBytes, int& afsIndex) {
    std::vector<uint8_t> crcIn;
    for (int i = 0; i < 4; i++) crcIn.push_back(0);
    afsIndex = 0;
    for (int i = 0; i < 4; i++) { crcIn.push_back(bits[i] & 1); afsIndex = (afsIndex << 1) | (bits[i] & 1); }
    crcIn.insert(crcIn.end(), bits + 4, bits + 4 + (size_t)fieldBytes * 8);
    uint32_t rx = 0;
    for (int j = 0; j < 16; j++) rx = (rx << 1) | (bits[4 + fieldBytes * 8 + j] & 1u);
    return crc16(crcIn.data(), crcIn.size()) == rx;
}

int dateToMjd(int y, int m, int d) {
    const int a = (14 - m) / 12;
    const int yy = y + 4800 - a, mm = m + 12 * a - 3;
    const int jdn = d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
    return jdn - 2400001;
}
void mjdToDate(int mjd, int& y, int& m, int& d) {
    const int j = mjd + 2400001 + 32044;
    const int g = j / 146097, dg = j % 146097;
    const int c = (dg / 36524 + 1) * 3 / 4, dc = dg - c * 36524;
    const int b = dc / 1461, db = dc % 1461;
    const int a = (db / 365 + 1) * 3 / 4, da = db - a * 365;
    const int yy = g * 400 + c * 100 + b * 4 + a, mm = (da * 5 + 308) / 153 - 2, dd = da - (mm + 4) * 153 / 5 + 122;
    y = yy - 4800 + (mm + 2) / 12; m = (mm + 2) % 12 + 1; d = dd + 1;
}

// ---------------------------------------------------------------- text message
int textSegmentNeed(uint8_t b0) {
    const bool command = (b0 & 0x10) != 0;
    const int n = command ? 0 : (b0 & 15) + 1;
    return 2 + n + 2;
}

TextSeg textSegment(const uint8_t* seg, size_t, int need) {
    TextSeg r;
    const int n = need - 4;
    const uint32_t rx = ((uint32_t)seg[(size_t)(2 + n)] << 8) | seg[(size_t)(3 + n)];
    if (crc::crc16Genibus(seg, (size_t)(2 + n)) != rx) return r;
    r.crcOk = true;
    r.toggle = (seg[0] & 0x80) != 0; r.first = (seg[0] & 0x40) != 0; r.last = (seg[0] & 0x20) != 0; r.command = (seg[0] & 0x10) != 0;
    r.cmd = seg[0] & 15;
    r.segnum = r.first ? 0 : (seg[1] >> 4) & 7;
    r.n = (uint32_t)n;
    return r;
}

// ---------------------------------------------------------------- AAC audio super frame
int aacNumFrames(bool modeE, int rateHz) {
    if (!modeE) return rateHz == 12000 ? 5 : rateHz == 24000 ? 10 : 0;
    return rateHz == 24000 ? 5 : rateHz == 48000 ? 10 : 0;
}
int aacHeaderBytes(int n) { return ((n - 1) * 12 + (n == 10 ? 4 : 0) + 7) / 8; }

bool aacSuperFrameParse(const uint8_t* lf, int len, int lenA, int n, AacSuperFrame& out) {
    out = AacSuperFrame();
    out.numFrames = n;
    const int hdr = aacHeaderBytes(n);
    if (n < 1 || n > 16 || len < hdr + n) return false;
    int hp = 0;
    if (lenA > 0) {
        if (lenA < hdr + n || (lenA - hdr) % n != 0) return false;
        hp = (lenA - hdr) / n - 1;
    }
    out.hpBytes = hp;
    if ((long)hdr + (long)n * (hp + 1) > len) return false;   // the higher protected part does not fit the logical frame
    // frame borders: 12 bits each, bytes counted from the start of the audio frames; they only grow, so a wrapped value is recognised
    std::vector<int> border((size_t)n, 0);
    int prev = 0;
    bool ok = true;
    for (int i = 0; i < n - 1; i++) {
        const int bit = i * 12;
        int v = (lf[bit / 8] << 8 | lf[bit / 8 + 1]);
        v = (bit % 8 == 0) ? (v >> 4) & 0xFFF : v & 0xFFF;
        while (v < prev) v += 4096;
        border[(size_t)i] = v; prev = v;
    }
    const int payload = len - hdr - n;
    border[(size_t)n - 1] = payload;
    int start = 0;
    for (int i = 0; i < n; i++) {
        if (border[(size_t)i] < start || border[(size_t)i] > payload || border[(size_t)i] - start < hp) ok = false;
        start = border[(size_t)i];
    }
    out.headerOk = ok;
    if (!ok) return false;
    // higher protected part: for every frame its first hp bytes and then its CRC byte; lower protected part: the rest of the frames in order
    const uint8_t* hpPart = lf + hdr;
    const uint8_t* lpPart = lf + hdr + (size_t)n * (size_t)(hp + 1);
    const uint8_t* end = lf + len;
    start = 0;
    out.frames.resize((size_t)n);
    for (int i = 0; i < n; i++) {
        const int fl = border[(size_t)i] - start;
        start = border[(size_t)i];
        auto& fr = out.frames[(size_t)i];
        fr.assign(hpPart + (size_t)i * (size_t)(hp + 1), hpPart + (size_t)i * (size_t)(hp + 1) + (size_t)hp);
        out.crc.push_back(hpPart[(size_t)i * (size_t)(hp + 1) + (size_t)hp]);
        const int lpn = fl - hp;
        if (lpPart + lpn > end) { out.headerOk = false; return false; }
        fr.insert(fr.end(), lpPart, lpPart + lpn);
        lpPart += lpn;
    }
    return true;
}

// ---------------------------------------------------------------- data service helpers
namespace {
bool validUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = (uint8_t)s[i];
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size()) return false;
        for (size_t j = 1; j < n; j++) if (((uint8_t)s[i + j] >> 6) != 2) return false;
        i += n;
    }
    return true;
}
const size_t kTextBytes = 16384;
}

// Text of a received object for display: control characters out, ISO 8859-1 turned into UTF-8 when the bytes are not UTF-8
std::string displayText(const uint8_t* b, size_t size) {
    std::string raw((const char*)b, std::min(size, kTextBytes));
    std::string out;
    const bool u8 = validUtf8(raw);
    for (unsigned char c : raw) {
        if (c == '\r') continue;
        if (c < 0x20 && c != '\n' && c != '\t') { out += ' '; continue; }
        if (c >= 0x80 && !u8) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); continue; }
        out += (char)c;
    }
    return out;
}

bool looksText(const uint8_t* b, size_t size) {
    const size_t n = std::min<size_t>(size, 512);
    if (n < 4) return false;
    size_t odd = 0;
    for (size_t i = 0; i < n; i++) {
        const uint8_t c = b[i];
        if (c == 0) return false;
        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') odd++;
    }
    return odd * 50 < n;
}

// The payload of an MSC data group (EN 300 401 clause 5.3.3.1): after the headers, before the CRC; false when the group is damaged
bool groupPayload(const uint8_t* d, size_t n, size_t& off, size_t& len) {
    if (n < 3) return false;
    const bool ext = d[0] & 0x80, crc = d[0] & 0x40, seg = d[0] & 0x20, ua = d[0] & 0x10;
    if (crc) {
        if (n < 5 || crc::crc16Genibus(d, n - 2) != (uint16_t)((d[n - 2] << 8) | d[n - 1])) return false;
        n -= 2;
    }
    size_t p = 2 + (ext ? 2 : 0) + (seg ? 2 : 0);
    if (ua) { if (p + 1 > n) return false; p += 1 + (d[p] & 0x0F); }
    if (p > n) return false;
    off = p; len = n - p;
    return true;
}

} // namespace dect2::drmp
#endif
