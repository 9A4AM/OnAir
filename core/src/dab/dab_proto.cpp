// The C++ implementation of dab_proto.h: the reference of the differential test against rust/onair_dab, and what DECT2_RUST=OFF builds use. The code is
// the parsing that used to sit inside DabReceiver (dab_rx.cpp), MotDecoder (dab_mot.cpp), DabPadDecoder (dab_pad.cpp), JournalineDecoder and the packet
// demultiplexers (dab_journaline.cpp, drm_journaline.cpp), moved here without a change of behaviour.
#include "dect2/dab/dab_proto.h"
#ifndef DECT2_RUST_DAB
#include "dect2/common/text_charset.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace dect2::dabp {
namespace textns = dect2::text;

namespace {
uint32_t getBits(const uint8_t* d, int pos, int n) {   // packed MSB first
    uint32_t v = 0;
    for (int i = 0; i < n; i++) { const int p = pos + i; v = (v << 1) | ((d[p >> 3] >> (7 - (p & 7))) & 1u); }
    return v;
}
constexpr int kSubfieldLen[8] = {4, 6, 8, 12, 16, 24, 32, 48};   // EN 300 401 table 30: the length index of a content indicator
inline uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
}

uint16_t crc16(const uint8_t* d, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(d[i] << 8);
        for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return (uint16_t)~c;
}

// ------------------------------------------------------------------ FIB and FIG
std::vector<FigRef> fibWalk(const uint8_t* fib, size_t, int& used) {
    std::vector<FigRef> out;
    int i = 0;
    while (i < 30 && fib[i] != 0xFF) {
        const int type = fib[i] >> 5, len = fib[i] & 31;
        if (i + 1 + len > 30) break;
        const int off = i + 1;
        i += 1 + len;
        if (len < 1) continue;
        out.push_back(FigRef{type, (uint32_t)off, (uint32_t)len});
    }
    used = std::min(i, 30);
    return out;
}

FigHead figHead(int type, const uint8_t* d, size_t) {
    FigHead h;
    if (type == 0) { h.cn = d[0] >> 7; h.oe = (d[0] >> 6) & 1; h.pd = (d[0] >> 5) & 1; h.ext = d[0] & 31; }
    else if (type == 1) { h.oe = (d[0] >> 3) & 1; h.ext = d[0] & 7; }
    else if (type == 2 || type == 5) h.ext = d[0] & 7;
    return h;
}

bool fig0Eid(const uint8_t* d, size_t len, int& eid) {
    if (len < 3) return false;
    eid = (uint16_t)((d[1] << 8) | d[2]);
    return true;
}

std::vector<Fig01Sub> fig0Subchannels(const uint8_t* d, size_t len) {
    std::vector<Fig01Sub> out;
    const uint8_t* b = d + 1;
    const int n = (int)len - 1;
    int p = 0;
    while (p + 3 <= n) {
        Fig01Sub s;
        s.id = b[p] >> 2;
        s.start = ((b[p] & 3) << 8) | b[p + 1];
        if (b[p + 2] & 0x80) {
            if (p + 4 > n) break;
            s.eep = true;
            s.option = (b[p + 2] >> 4) & 7;
            s.level = (b[p + 2] >> 2) & 3;
            s.size = ((b[p + 2] & 3) << 8) | b[p + 3];
            p += 4;
        } else {
            s.uepIndex = b[p + 2] & 0x3F;
            s.uepTable = (b[p + 2] & 0x40) != 0;
            p += 3;
        }
        out.push_back(s);
    }
    return out;
}

std::vector<Fig03Packet> fig0Packet(const uint8_t* d, size_t len) {   // packet mode service components: SCId -> sub-channel, address (clause 6.3.2)
    std::vector<Fig03Packet> out;
    const uint8_t* b = d + 1;
    const int n = (int)len - 1;
    int p = 0;
    while (p + 5 <= n) {
        Fig03Packet pc;
        pc.scid = (b[p] << 4) | (b[p + 1] >> 4);
        pc.dg = !(b[p + 2] & 0x80);   // the DG flag set means NO data groups are used
        pc.dscty = b[p + 2] & 0x3F;
        pc.subId = b[p + 3] >> 2;
        pc.address = ((b[p + 3] & 3) << 8) | b[p + 4];
        const bool ca = b[p + 1] & 1;
        out.push_back(pc);
        p += 5 + (ca ? 2 : 0);
    }
    return out;
}

std::vector<Fig02Service> fig0Services(const uint8_t* d, size_t len) {
    std::vector<Fig02Service> out;
    const int pd = (d[0] >> 5) & 1;
    const uint8_t* b = d + 1;
    const int n = (int)len - 1;
    int p = 0;
    while (p + 3 <= n) {
        Fig02Service sv;
        if (pd == 0) { sv.sid = (uint32_t)((b[p] << 8) | b[p + 1]); p += 2; }
        else { if (p + 5 > n) break; sv.sid = ((uint32_t)b[p] << 24) | ((uint32_t)b[p + 1] << 16) | ((uint32_t)b[p + 2] << 8) | b[p + 3]; p += 4; }
        const int nc = b[p] & 15;
        p++;
        for (int c = 0; c < nc && p + 2 <= n; c++, p += 2) {
            Fig02Comp dc;
            dc.tmid = b[p] >> 6;
            if (dc.tmid == 0) { dc.ascty = b[p] & 63; dc.subId = b[p + 1] >> 2; dc.primary = (b[p + 1] >> 1) & 1; }
            else if (dc.tmid == 3) { dc.scid = ((b[p] & 0x3F) << 6) | (b[p + 1] >> 2); dc.primary = (b[p + 1] >> 1) & 1; }
            else if (dc.tmid == 1) { dc.dscty = b[p] & 63; dc.subId = b[p + 1] >> 2; dc.primary = (b[p + 1] >> 1) & 1; }
            sv.comps.push_back(dc);
        }
        out.push_back(sv);
    }
    return out;
}

std::vector<Fig013App> fig0UserApps(const uint8_t* d, size_t len) {   // user applications (TS 101 756 table 16)
    std::vector<Fig013App> out;
    const int pd = (d[0] >> 5) & 1;
    const uint8_t* b = d + 1;
    const int n = (int)len - 1;
    int p = 0;
    while (p + (pd ? 5 : 3) <= n) {
        uint32_t sid;
        if (pd == 0) { sid = (uint32_t)((b[p] << 8) | b[p + 1]); p += 2; }
        else { sid = ((uint32_t)b[p] << 24) | ((uint32_t)b[p + 1] << 16) | ((uint32_t)b[p + 2] << 8) | b[p + 3]; p += 4; }
        const int scids = b[p] >> 4, nua = b[p] & 15;
        p++;
        for (int u = 0; u < nua && p + 2 <= n; u++) {
            const int type = (b[p] << 3) | (b[p + 1] >> 5), alen = b[p + 1] & 31;
            p += 2;
            if (p + alen > n) break;
            out.push_back(Fig013App{sid, scids, u, type, (uint32_t)(p + 1), (uint32_t)alen});
            p += alen;
        }
    }
    return out;
}

std::vector<Fig017Pty> fig0Pty(const uint8_t* d, size_t len) {   // programme type: S/D flag, P/S flag, language, programme type, complementary type
    std::vector<Fig017Pty> out;
    const uint8_t* b = d + 1;
    const int n = (int)len - 1;
    int p = 0;
    while (p + 3 <= n) {
        Fig017Pty e;
        e.sid = (uint32_t)((b[p] << 8) | b[p + 1]);
        e.sd = b[p + 2] & 0x80; e.primary = !(b[p + 2] & 0x40);
        const bool l = b[p + 2] & 0x20, cc = b[p + 2] & 0x10;
        p += 3;
        if (l && p < n) { e.lang = b[p]; p++; }
        if (p < n) { e.pty = b[p] & 0x1F; p++; }
        if (cc) p++;
        out.push_back(e);
    }
    return out;
}

std::vector<Fig018Support> fig0AnnSupport(const uint8_t* d, size_t len) {   // announcement support: SId, ASu flags, clusters
    std::vector<Fig018Support> out;
    const uint8_t* b = d + 1;
    const int n = (int)len - 1;
    int p = 0;
    while (p + 5 <= n) {
        Fig018Support as;
        as.sid = (uint32_t)((b[p] << 8) | b[p + 1]);
        as.asu = (uint32_t)((b[p + 2] << 8) | b[p + 3]);
        const int nc = b[p + 4] & 0x1F;
        p += 5;
        for (int c = 0; c < nc && p < n; c++, p++) as.clusters.push_back(b[p]);
        out.push_back(as);
    }
    return out;
}

std::vector<Fig019Switch> fig0AnnSwitch(const uint8_t* d, size_t len) {   // cluster, ASw flags, new flag, region flag, sub-channel
    std::vector<Fig019Switch> out;
    const uint8_t* b = d + 1;
    const int n = (int)len - 1;
    int p = 0;
    while (p + 4 <= n) {
        Fig019Switch sw;
        sw.cluster = b[p];
        sw.asw = (uint32_t)((b[p + 1] << 8) | b[p + 2]);
        sw.newFlag = b[p + 3] & 0x80;
        sw.region = b[p + 3] & 0x40;
        sw.subId = b[p + 3] & 0x3F;
        p += 4 + (sw.region ? 1 : 0);
        out.push_back(sw);
    }
    return out;
}

bool fig0Time(const uint8_t* d, size_t len, Fig010Time& t) {
    const uint8_t* b = d + 1;
    const int n = (int)len - 1;
    if (n < 4) return false;
    t.mjd = getBits(b, 1, 17);
    const int flag = (int)getBits(b, 20, 1);
    t.hour = (int)getBits(b, 21, 5); t.minute = (int)getBits(b, 26, 6);
    t.second = 0;
    if (flag && n >= 6) t.second = (int)getBits(b, 32, 6);
    t.utc = t.mjd > 40587 ? (int64_t)(t.mjd - 40587) * 86400 + t.hour * 3600 + t.minute * 60 + t.second : 0;
    return true;
}

bool fig1(const uint8_t* d, size_t len, Fig1Label& l) {
    const int ext = d[0] & 7, cs = d[0] >> 4;
    const uint8_t* b = d + 1;
    const int n = (int)len - 1;
    l = Fig1Label();
    l.ext = ext; l.charset = cs;
    if (ext == 0 && n >= 18) l.off = 3;
    else if (ext == 1 && n >= 18) { l.sid = (uint32_t)((b[0] << 8) | b[1]); l.off = 3; }
    else if (ext == 5 && n >= 20) { l.sid = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; l.off = 5; }   // data service label (32-bit SId)
    else if (ext == 4 && n >= 20) {   // service component label: P/D, SCIdS, SId, label
        const bool pd = b[0] & 0x80;
        l.scids = b[0] & 0x0F;
        const int il = pd ? 4 : 2;
        if (n < 1 + il + 18) return false;
        l.sid = pd ? (((uint32_t)b[1] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 8) | b[4]) : (uint32_t)((b[1] << 8) | b[2]);
        l.off = (uint32_t)(2 + il);
    } else return false;
    return true;
}

// FIG 2: labels in UTF-8 or UCS-2 of up to 8 segments (EN 300 401 8.1.13, TS 103 176)
bool fig2(const uint8_t* d, size_t length, Fig2Seg& r) {
    const int len = (int)length;
    r = Fig2Seg();
    const int toggle = d[0] >> 7, seg = (d[0] >> 4) & 7, rfu = (d[0] >> 3) & 1, ext = d[0] & 7;
    int idl = 0;
    switch (ext) {
        case 0: case 1: idl = 2; break;
        case 4: idl = (len > 1 && (d[1] & 0x80)) ? 5 : 3; break;
        case 5: idl = 4; break;
        default: return false;
    }
    if (len <= 1 + idl) return false;
    r.ext = ext; r.toggle = toggle; r.seg = seg; r.rfu = rfu;
    if (ext == 1) r.sid = (uint32_t)((d[1] << 8) | d[2]);
    else if (ext == 5) r.sid = ((uint32_t)d[1] << 24) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 8) | d[4];
    else if (ext == 4) {
        r.scids = d[1] & 0x0F;
        r.sid = idl == 5 ? (((uint32_t)d[2] << 24) | ((uint32_t)d[3] << 16) | ((uint32_t)d[4] << 8) | d[5]) : (uint32_t)((d[2] << 8) | d[3]);
    }
    int p = 1 + idl, n = len - 1 - idl;
    if (seg == 0) {
        r.ucs2 = d[p] >> 7;
        r.total = ((d[p] >> 4) & 7) + 1;
        const int skip = rfu ? 1 : 3;   // text control, or Rfa and the character flag field
        if (n <= skip) { r.valid = false; r.textOff = (uint32_t)p; r.textLen = 0; return true; }
        p += skip; n -= skip;
    }
    r.valid = true; r.textOff = (uint32_t)p; r.textLen = (uint32_t)n;
    return true;
}

// ------------------------------------------------------------------ MOT
size_t motParseTime(const uint8_t* d, size_t n, MotTime& t) {
    t = MotTime();
    if (n < 4) return 0;
    const uint32_t v = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
    t.valid = (v >> 31) & 1;
    if (!t.valid) { t.now = true; return 4; }
    t.mjd = (int)((v >> 14) & 0x1FFFF);
    t.longForm = (v >> 11) & 1;                  // bit 13 = LSI, bit 12 = confidence, bit 11 = UTC flag
    t.hour = (int)((v >> 6) & 0x1F);
    t.minute = (int)(v & 0x3F);
    if (!t.longForm) return 4;
    if (n < 6) return 0;
    const uint32_t w = ((uint32_t)d[4] << 8) | d[5];   // seconds (6 bits), milliseconds (10 bits)
    t.second = (int)((w >> 10) & 0x3F);
    t.ms = (int)(w & 0x3FF);
    return 6;
}

static void mjdDate(int mjd, int& y, int& m, int& d) {
    // MJD to the calendar (EN 300 401 annex B)
    const long j = (long)mjd + 2400001 + 68569;
    const long c = 4 * j / 146097;
    long e = j - (146097 * c + 3) / 4;
    const long yy = 4000 * (e + 1) / 1461001;
    e = e - 1461 * yy / 4 + 31;
    const long mm = 80 * e / 2447;
    d = (int)(e - 2447 * mm / 80);
    const long k = mm / 11;
    m = (int)(mm + 2 - 12 * k);
    y = (int)(100 * (c - 49) + yy + k);
}

std::string motTimeText(const MotTime& t) {
    if (t.now) return "now";
    if (!t.valid) return "-";
    int y, m, d;
    mjdDate(t.mjd, y, m, d);
    char b[64];
    if (t.longForm) snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d", y, m, d, t.hour, t.minute, t.second);
    else snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", y, m, d, t.hour, t.minute);
    return b;
}

bool motParseHeader(const uint8_t* h, size_t n, MotHeader& o) {
    if (n < 7) return false;
    o = MotHeader();
    o.bodySize = ((size_t)h[0] << 20) | ((size_t)h[1] << 12) | ((size_t)h[2] << 4) | (h[3] >> 4);
    o.headerSize = (((size_t)h[3] & 0x0F) << 9) | ((size_t)h[4] << 1) | (h[5] >> 7);
    o.contentType = (h[5] >> 1) & 0x3F;
    o.subtype = ((h[5] & 1) << 8) | h[6];
    const size_t end = o.headerSize >= 7 ? std::min<size_t>(o.headerSize, n) : std::min<size_t>(7, n);
    for (size_t p = 7; p < end;) {                                   // the header extension: parameters with a 2 bit length class
        const int pli = h[p] >> 6, id = h[p] & 0x3F;
        p++;
        size_t len = pli == 0 ? 0 : pli == 1 ? 1 : pli == 2 ? 4 : 0;
        if (pli == 3) {
            if (p >= end) break;
            if (h[p] & 0x80) { if (p + 1 >= end) break; len = ((size_t)(h[p] & 0x7F) << 8) | h[p + 1]; p += 2; }
            else len = h[p++] & 0x7F;
        }
        if (p + len > end) break;
        o.params.push_back(MotParamRef{id, Range{(uint32_t)p, (uint32_t)len}});
        const uint8_t* d = h + p;
        const Range rg{(uint32_t)p, (uint32_t)len};
        if (id == 0x0C && len >= 1) { o.hasName = true; o.name = Range{(uint32_t)p + 1, (uint32_t)len - 1}; }   // ContentName: character set byte, then the text
        else if (id == 0x17 && len >= 1) o.caption = textns::clean(textns::dabCharset(d[0] >> 4, d + 1, len - 1));   // ContentDescription
        else if (id == 0x25 && len >= 2) { o.hasSlide = true; o.categoryId = d[0]; o.slideId = d[1]; }          // CategoryID / SlideID
        else if (id == 0x26) o.category = rg;                                                                    // CategoryTitle
        else if (id == 0x27) o.clickUrl = rg;                                                                    // ClickThroughURL
        else if (id == 0x28) o.altUrl = rg;                                                                      // AlternativeLocationURL
        else if (id == 0x05 && len >= 4) o.hasTrigger = motParseTime(d, len, o.trigger) > 0;
        else if (id == 0x04 && len >= 4) o.hasExpire = motParseTime(d, len, o.expire) > 0;
        p += len;
    }
    return true;
}

std::string motContentTypeName(int type, int sub) {
    switch (type) {
    case 0: return sub == 1 ? "MIME / HTTP header" : "general data";
    case 1: return sub == 0 ? "text/ASCII" : sub == 1 ? "text/ISO-8859-1" : sub == 2 ? "text/HTML" : "text";
    case 2: return sub == 0 ? "image/GIF" : sub == 1 ? "image/JPEG" : sub == 2 ? "image/BMP" : sub == 3 ? "image/PNG" : "image";
    case 3: return "audio";
    case 4: return "video";
    case 5: return "MOT transport";
    case 6: return sub == 0 ? "MHEG" : sub == 1 ? "Java" : "system";
    case 7: return "application";
    case 8: return "proprietary";
    }
    return "unknown";
}

MotGroup motParseGroup(const uint8_t* d, size_t length) {
    MotGroup g;
    int n = (int)length;
    if (n < 3) return g;
    const bool ext = d[0] & 0x80, crc = d[0] & 0x40, seg = d[0] & 0x20, ua = d[0] & 0x10;
    const int type = d[0] & 0x0F;
    if (type != 3 && type != 4 && type != 5 && type != 6 && type != 7) return g;
    g.status = MotGroup::Bad;
    if (crc) {
        if (n < 5 || crc16(d, (size_t)n - 2) != (uint16_t)((d[n - 2] << 8) | d[n - 1])) return g;
        n -= 2;
    }
    int p = 2 + (ext ? 2 : 0);
    bool last = true;
    int segNo = 0;
    if (seg) {
        if (p + 2 > n) return g;
        last = (d[p] & 0x80) != 0;
        segNo = ((d[p] & 0x7F) << 8) | d[p + 1];
        p += 2;
    }
    int tid = 0;
    if (ua) {
        if (p + 1 > n) return g;
        const bool hasTid = d[p] & 0x10;
        const int li = d[p] & 0x0F;
        p++;
        if (p + li > n) return g;
        if (hasTid && li >= 2) tid = (uint16_t)((d[p] << 8) | d[p + 1]);
        p += li;
    }
    if (type == 5) { g.status = MotGroup::Scrambled; return g; }       // a scrambled body needs conditional access
    if (type == 7) { g.status = MotGroup::Compressed; return g; }      // a compressed directory (zlib) is not decoded
    if (p + 2 > n) return g;
    const int size = ((d[p] & 0x1F) << 8) | d[p + 1];                  // the MOT segmentation header: repetition count (3 bits), segment size (13 bits)
    p += 2;
    g.status = MotGroup::Segment; g.last = last; g.type = type; g.segNo = segNo; g.tid = tid; g.off = (uint32_t)p; g.len = (uint32_t)std::min(size, n - p);
    return g;
}

// The directory object (EN 301 234 clause 7.1): 32 bits compression flag and size, 16 bits number of objects, 24 bits carousel period, 16 bits
// segment size, 16 bits extension length, the extension, then per object its transport id and header (core and extension).
bool motParseDirectory(const uint8_t* dir, size_t n, std::vector<DirEntry>& out) {
    out.clear();
    if (n < 13) return false;
    const size_t count = ((size_t)dir[4] << 8) | dir[5];
    const size_t extLen = ((size_t)dir[11] << 8) | dir[12];
    size_t p = 13 + extLen;
    for (size_t i = 0; i < count && p + 2 + 7 <= n; i++) {
        const int tid = (uint16_t)((dir[p] << 8) | dir[p + 1]);
        const uint8_t* h = &dir[p + 2];
        const size_t hs = (((size_t)h[3] & 0x0F) << 9) | ((size_t)h[4] << 1) | (h[5] >> 7);
        if (hs < 7 || p + 2 + hs > n) break;
        out.push_back(DirEntry{tid, (uint32_t)(p + 2), (uint32_t)hs});
        p += 2 + hs;
    }
    return true;
}

// ------------------------------------------------------------------ PAD
bool padFromAu(const uint8_t* au, size_t n, PadBytes& out) {
    out = PadBytes();
    if (n < 4 || (au[0] >> 5) != 4) return false;           // the data stream element (id_syn_ele 4) comes first when there is PAD
    int pos = 2, cnt = au[1];
    if (cnt == 255) { cnt += au[2]; pos = 3; }
    if (cnt < 2 || pos + cnt > (int)n) return false;
    const uint8_t* d = au + pos;
    out.fpad[0] = d[cnt - 2]; out.fpad[1] = d[cnt - 1];
    out.xpad.resize((size_t)cnt - 2);
    for (int k = 0; k < cnt - 2; k++) out.xpad[(size_t)k] = d[cnt - 3 - k];
    return true;
}

bool padFromMp2(const uint8_t* f, size_t length, int& lastCont, PadBytes& out) {
    out = PadBytes();
    const int len = (int)length;
    if (len < 8) return false;
    const uint8_t fpad[2] = {f[len - 2], f[len - 1]};
    if ((fpad[0] >> 6) != 0) return false;
    const int ind = (fpad[0] >> 4) & 3;
    const bool ci = (fpad[1] & 2) != 0;
    int xlen = 0;
    if (ind == 1) { xlen = 4; if (ci) lastCont = 3; }
    else if (ind == 2) {
        if (ci) {   // the sizes of the subfields are in the content indicators, the bytes next to the F-PAD
            int i = 0, tot = 0;
            for (; i < 4 && i < len - 3; i++) {
                const int t = f[len - 3 - i] & 0x1F;
                if (t == 0) { i++; break; }
                lastCont = kSubfieldLen[f[len - 3 - i] >> 5];
                tot += lastCont;
            }
            xlen = i + tot;
        } else xlen = lastCont;
    }
    xlen = std::min(xlen, len - 6);
    if (xlen <= 0) return false;
    out.fpad[0] = fpad[0]; out.fpad[1] = fpad[1];
    out.xpad.resize((size_t)xlen);
    for (int k = 0; k < xlen; k++) out.xpad[(size_t)k] = f[len - 3 - k];
    return true;
}

std::vector<PadSub> padSplit(const uint8_t* xp, size_t length, const uint8_t fpad[2], int& lastType) {
    std::vector<PadSub> out;
    int xlen = (int)length;
    if ((fpad[0] >> 6) != 0) return out;                    // F-PAD type 0 only
    const int ind = (fpad[0] >> 4) & 3;
    const bool ci = (fpad[1] & 2) != 0;
    if ((ind != 1 && ind != 2) || xlen <= 0) return out;
    if (ind == 1) xlen = std::min(xlen, 4);                // short X-PAD: 4 bytes
    struct Sub { int type, len; };
    std::vector<Sub> subs;
    int pos = 0;
    if (ci) {
        if (ind == 1) { subs.push_back({xp[0] & 0x1F, 3}); pos = 1; }
        else {
            int i = 0;
            for (; i < 4 && i < xlen; i++) {
                const int t = xp[i] & 0x1F;
                if (t == 0) { i++; break; }                 // end marker (also uses up a byte)
                subs.push_back({t, kSubfieldLen[xp[i] >> 5]});
            }
            pos = i;
        }
        if (!subs.empty()) lastType = subs.back().type;
    } else {
        const int t = lastType == 12 ? 13 : lastType == 2 ? 3 : lastType;   // the data continue the application of the last indicator
        if (t == 0) return out;
        subs.push_back({t, xlen});
    }
    for (const Sub& s : subs) {
        const int len = std::min(s.len, xlen - pos);
        if (len <= 0) break;
        out.push_back(PadSub{s.type, (uint32_t)pos, (uint32_t)len});
        pos += len;
    }
    return out;
}

int padDgLength(const uint8_t* d, size_t n, int& len) {   // type 1: data group length indicator: 14 bit length, CRC-16
    if (n < 2) return 0;
    if (n >= 4 && crc16(d, 2) != (uint16_t)((d[2] << 8) | d[3])) return 1;
    len = ((d[0] & 0x3F) << 8) | d[1];
    return 2;
}

// A dynamic label segment: a 2 byte prefix, up to 16 characters and a CRC-16. A command: the clear command has no data, the others (DL Plus) a few bytes:
// the length is the one the CRC agrees with.
DlsStep dlsStep(const uint8_t* dls, size_t n) {
    DlsStep s;
    if (n < 2) return s;
    auto crcOk = [&](size_t total) { return n >= total && crc16(dls, total - 2) == (uint16_t)((dls[total - 2] << 8) | dls[total - 1]); };
    if (!(dls[0] & 0x10)) {
        const size_t total = 2 + (dls[0] & 0x0F) + 1 + 2;
        if (n >= total) { if (crcOk(total)) { s.action = 2; s.segLen = (uint32_t)(total - 2); } else s.action = 1; }
        return s;
    }
    int found = -1;
    const int most = (dls[0] & 0x0F) == 1 ? 0 : 16;
    for (int l = 0; l <= most && found < 0; l++) { const size_t total = (size_t)(2 + l + 2); if (crcOk(total)) found = (int)total; }
    if (found > 0) { s.action = 2; s.segLen = (uint32_t)(found - 2); }
    else if ((int)n >= 2 + most + 2) s.action = 1;
    return s;
}

// One dynamic label segment without its CRC: prefix = toggle, first, last, command flag, 4 bit field 1 (length - 1, or the command);
// second byte: field 2 (character set in the first segment, else the segment number in bits 6..4)
DlSegment dlSegment(const uint8_t* d, size_t n) {
    DlSegment s;
    const int b0 = n > 0 ? d[0] : 0, b1 = n > 1 ? d[1] : 0;
    s.toggle = b0 >> 7; s.first = b0 & 0x40; s.last = b0 & 0x20; s.cmd = b0 & 0x10; s.code = b0 & 0x0F;
    s.segNo = s.first ? 0 : (b1 >> 4) & 7;
    s.charset = b1 >> 4;
    return s;
}

// DL Plus tags command (TS 102 980 clause 5.3): item toggle, item running, number of tags - 1, then per tag the content type, the start and the
// length - 1 in characters of the label, 7 bits each
bool dlPlusTags(const uint8_t* d, size_t n, bool& running, std::vector<DlpTag>& out) {
    out.clear();
    if (n < 1) return false;
    const int nt = (d[0] & 3) + 1;
    if (n < (size_t)(1 + 3 * nt)) return false;
    for (int i = 0; i < nt; i++) out.push_back(DlpTag{d[1 + 3 * i] & 0x7F, d[2 + 3 * i] & 0x7F, (d[3 + 3 * i] & 0x7F) + 1});
    running = (d[0] >> 2) & 1;
    return true;
}

// ------------------------------------------------------------------ Journaline framing and packet mode
JlGroup jlDataGroup(const uint8_t* d, size_t n) {
    JlGroup g;
    if (n < 4) return g;
    // extension flag 0, CRC flag 1, segment flag 0, user access flag 0
    if ((d[0] & 0xF0) != 0x40) return g;
    const int t = d[0] & 0x0F;
    if (t != 0 && t != 6) return g;
    if (n - 4 > 4092) return g;
    if (crc16(d, n - 2) != be16(d + n - 2)) { g.status = 2; return g; }
    g.status = 0; g.type = t; g.fieldLen = (uint32_t)(n - 4);
    return g;
}

bool jlParseToc(const uint8_t* d, size_t n, JlTocInfo& info, std::vector<JlTocEntry>& entries) {
    entries.clear();
    if (n < 13 || d[0] != 0x54) return false;
    info.revision = d[1]; info.blockCount = d[2]; info.blockIndex = d[3];
    info.objectCount = be16(d + 6); info.timeoutMin = be16(d + 8);
    const size_t entLen = d[10], extLen = be16(d + 11);
    if (entLen < 3 || info.blockCount < 1 || info.blockIndex >= info.blockCount) return false;
    size_t p = 13 + extLen;
    for (; p + entLen <= n && entries.size() < 2048; p += entLen) entries.push_back({be16(d + p), d[p + 2]});
    return !entries.empty();
}

bool jlUserAppData(const uint8_t* d, size_t n, int& version, int& extHeader, int& entry) {
    if (n < 2) return false;
    version = d[0]; extHeader = d[1];
    entry = n >= 4 ? be16(d + 2) : -1;
    return true;
}

DabPacket dabPacket(const uint8_t* p, size_t len) {
    DabPacket k;
    if (len < 5) return k;
    k.crcOk = crc16(p, len - 2) == be16(p + len - 2);
    k.ci = (p[0] >> 4) & 3;
    k.first = p[0] & 8; k.last = p[0] & 4;
    k.addr = ((p[0] & 3) << 8) | p[1];
    k.cmd = p[2] & 0x80;
    k.ul = p[2] & 0x7F;
    return k;
}

DrmPacket drmPacket(const uint8_t* p, size_t sz) {
    DrmPacket k;
    if (sz < 4) return k;
    k.crcOk = crc16(p, sz - 2) == be16(p + sz - 2);
    k.first = p[0] & 0x80; k.last = p[0] & 0x40;
    k.id = (p[0] >> 4) & 3; k.ci = p[0] & 7;
    k.n = (uint32_t)(sz - 3); k.at = 1;
    if (p[0] & 0x08) { k.n = p[1]; k.at = 2; if (k.n > sz - 4) k.lenBad = true; }   // the useful length does not fit: a damaged packet
    return k;
}

} // namespace dect2::dabp
#endif
