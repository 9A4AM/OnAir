// MOT object reassembly (ETSI EN 301 234), see dab_mot.h. Shared by the DAB PAD / packet mode decoders and the DRM data services.
#include "dect2/dab_mot.h"
#include "dect2/dab.h"
#include "dect2/text_charset.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace dect2 {

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

bool motParseHeader(const uint8_t* h, size_t n, MotObject& o) {
    if (n < 7) return false;
    o.bodySize = ((size_t)h[0] << 20) | ((size_t)h[1] << 12) | ((size_t)h[2] << 4) | (h[3] >> 4);
    o.headerSize = (((size_t)h[3] & 0x0F) << 9) | ((size_t)h[4] << 1) | (h[5] >> 7);
    o.contentType = (h[5] >> 1) & 0x3F;
    o.subtype = ((h[5] & 1) << 8) | h[6];
    o.params.clear();
    o.name.clear(); o.caption.clear(); o.category.clear(); o.clickUrl.clear(); o.altUrl.clear();
    o.categoryId = o.slideId = -1;
    o.hasTrigger = o.hasExpire = false;
    const size_t end = o.headerSize >= 7 ? std::min(o.headerSize, n) : std::min<size_t>(7, n);
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
        MotParam pr; pr.id = id; pr.data.assign(h + p, h + p + len);
        o.params.push_back(pr);
        const uint8_t* d = h + p;
        if (id == 0x0C && len >= 1) o.name.assign((const char*)d + 1, len - 1);            // ContentName: character set byte, then the text
        else if (id == 0x17 && len >= 1) o.caption = text::clean(text::dabCharset(d[0] >> 4, d + 1, len - 1));   // ContentDescription
        else if (id == 0x25 && len >= 2) { o.categoryId = d[0]; o.slideId = d[1]; }       // CategoryID / SlideID
        else if (id == 0x26) o.category.assign((const char*)d, len);                       // CategoryTitle
        else if (id == 0x27) o.clickUrl.assign((const char*)d, len);                       // ClickThroughURL
        else if (id == 0x28) o.altUrl.assign((const char*)d, len);                         // AlternativeLocationURL
        else if (id == 0x05 && len >= 4) { o.hasTrigger = motParseTime(d, len, o.trigger) > 0; }
        else if (id == 0x04 && len >= 4) { o.hasExpire = motParseTime(d, len, o.expire) > 0; }
        p += len;
    }
    return true;
}

const char* motContentTypeName(int type, int sub) {
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

bool motIsText(const MotObject& o) { return o.contentType == 1; }

void MotDecoder::reset() {
    objs_.clear(); order_.clear(); dir_ = Dir(); dirList_.clear(); dirEntries_.clear();
}

MotDecoder::Obj& MotDecoder::obj(uint16_t tid) {
    auto it = objs_.find(tid);
    if (it == objs_.end()) {
        if (order_.size() >= maxObjects_) { objs_.erase(order_.front()); order_.pop_front(); }
        it = objs_.emplace(tid, Obj()).first;
        order_.push_back(tid);
    }
    return it->second;
}

void MotDecoder::dataGroup(const uint8_t* d, int n) {
    if (n < 3) return;
    const bool ext = d[0] & 0x80, crc = d[0] & 0x40, seg = d[0] & 0x20, ua = d[0] & 0x10;
    const int type = d[0] & 0x0F;
    if (type != 3 && type != 4 && type != 5 && type != 6 && type != 7) return;
    stats_.groups++;
    if (crc) {
        if (n < 5 || dab::crc16(d, n - 2) != (uint16_t)((d[n - 2] << 8) | d[n - 1])) { stats_.groupsBad++; return; }
        n -= 2;
    }
    int p = 2 + (ext ? 2 : 0);
    bool last = true;
    int segNo = 0;
    if (seg) {
        if (p + 2 > n) { stats_.groupsBad++; return; }
        last = (d[p] & 0x80) != 0;
        segNo = ((d[p] & 0x7F) << 8) | d[p + 1];
        p += 2;
    }
    uint16_t tid = 0;
    if (ua) {
        if (p + 1 > n) { stats_.groupsBad++; return; }
        const bool hasTid = d[p] & 0x10;
        const int li = d[p] & 0x0F;
        p++;
        if (p + li > n) { stats_.groupsBad++; return; }
        if (hasTid && li >= 2) tid = (uint16_t)((d[p] << 8) | d[p + 1]);
        p += li;
    }
    if (type == 5) { stats_.scrambled++; return; }            // a scrambled body needs conditional access
    if (type == 7) { stats_.compressed++; return; }           // a compressed directory (zlib) is not decoded
    if (p + 2 > n) { stats_.groupsBad++; return; }
    const int size = ((d[p] & 0x1F) << 8) | d[p + 1];        // the MOT segmentation header: repetition count (3 bits), segment size (13 bits)
    p += 2;
    segment(tid, type, segNo, last, d + p, std::min(size, n - p));
}

void MotDecoder::segment(uint16_t tid, int type, int segNo, bool last, const uint8_t* d, int n) {
    if (n < 0 || segNo > 4095) return;
    if (type == 6) {                                           // directory mode: the directory is an object of its own
        stats_.directorySegments++;
        if (dir_.tid != tid || (dir_.have && segNo == 0 && !dir_.raw.empty() && dir_.parts.count(0) && dir_.parts[0] != std::vector<uint8_t>(d, d + n)))
            dir_ = Dir();
        if (dir_.have) return;                                 // this version is complete already
        dir_.tid = tid;
        dir_.parts[segNo].assign(d, d + n);
        if (last) dir_.last = segNo;
        if (dir_.last >= 0 && dir_.parts.rbegin()->first > dir_.last) { dir_ = Dir(); return; }
        if (dir_.last < 0 || (int)dir_.parts.size() != dir_.last + 1) return;
        std::vector<uint8_t> all;
        for (auto& kv : dir_.parts) all.insert(all.end(), kv.second.begin(), kv.second.end());
        if (all.size() > (4u << 20)) { dir_ = Dir(); return; }
        dir_.have = true; dir_.raw = all;
        directoryDone(all);
        return;
    }
    Obj& o = obj(tid);
    auto& parts = type == 3 ? o.head : o.body;
    int& lastNo = type == 3 ? o.headLast : o.bodyLast;
    if (type == 3) stats_.headerSegments++; else stats_.bodySegments++;
    if (type == 3 && segNo == 0) {                              // a different header under the same transport id: a new object
        auto h0 = o.head.find(0);
        if (h0 != o.head.end() && (h0->second.size() != (size_t)n || std::memcmp(h0->second.data(), d, (size_t)n) != 0)) o = Obj();
    }
    if (type == 4 && !o.haveDir) { auto de = dirEntries_.find(tid); if (de != dirEntries_.end()) { o.haveDir = true; o.dirRaw = de->second; } }
    if (o.done) { stats_.repeats++; return; }
    auto& slot = parts[segNo];
    if (!slot.empty()) stats_.repeats++;
    slot.assign(d, d + n);
    if (last) lastNo = segNo;
    if (lastNo >= 0 && parts.rbegin()->first > lastNo) { parts.clear(); lastNo = -1; return; }
    tryComplete(o, tid);
}

void MotDecoder::tryComplete(Obj& o, uint16_t tid) {
    if (o.bodyLast < 0 || (int)o.body.size() != o.bodyLast + 1) return;
    std::vector<uint8_t> h;
    if (o.haveDir) h = o.dirRaw;                                // directory mode: the header comes from the directory
    else {
        if (o.headLast < 0 || (int)o.head.size() != o.headLast + 1) return;
        for (auto& kv : o.head) h.insert(h.end(), kv.second.begin(), kv.second.end());
    }
    std::vector<uint8_t> b;
    for (auto& kv : o.body) b.insert(b.end(), kv.second.begin(), kv.second.end());
    if (h.size() < 7 || b.size() > maxBytes_) return;
    o.done = true;
    o.body.clear();                                             // the header segments stay: a different header under the same transport id is a new object
    MotObject m;
    if (!motParseHeader(h.data(), h.size(), m)) return;
    m.tid = tid; m.fromDirectory = o.haveDir;
    if (m.bodySize && m.bodySize != b.size()) return;           // the body is not what the header says
    m.body = std::move(b);
    stats_.objects++;
    if (sink_) sink_(m);
}

// The directory object (EN 301 234 clause 7.1): 32 bits compression flag and size, 16 bits number of objects, 24 bits carousel period, 16 bits
// segment size, 16 bits extension length, the extension, then per object its transport id and header (core and extension).
void MotDecoder::directoryDone(const std::vector<uint8_t>& dir) {
    stats_.directories++;
    dirList_.clear();
    if (dir.size() < 13) return;
    const size_t count = ((size_t)dir[4] << 8) | dir[5];
    const size_t extLen = ((size_t)dir[11] << 8) | dir[12];
    size_t p = 13 + extLen;
    std::map<uint16_t, std::vector<uint8_t>> entries;
    for (size_t i = 0; i < count && p + 2 + 7 <= dir.size(); i++) {
        const uint16_t tid = (uint16_t)((dir[p] << 8) | dir[p + 1]);
        const uint8_t* h = &dir[p + 2];
        const size_t hs = (((size_t)h[3] & 0x0F) << 9) | ((size_t)h[4] << 1) | (h[5] >> 7);
        if (hs < 7 || p + 2 + hs > dir.size()) break;
        std::vector<uint8_t> raw(h, h + hs);
        MotObject m;
        if (motParseHeader(raw.data(), raw.size(), m)) { m.tid = tid; m.fromDirectory = true; dirList_.push_back(m); }
        entries[tid] = raw;
        p += 2 + hs;
    }
    // a changed header under a known transport id starts that object over; bodies that arrived before the directory complete now
    dirEntries_ = entries;
    for (auto it = objs_.begin(); it != objs_.end(); ++it) {
        auto e = entries.find(it->first);
        if (e == entries.end()) continue;
        Obj& o = it->second;
        if (o.haveDir && o.dirRaw != e->second) o = Obj();
        else if (!o.haveDir && o.done) o = Obj();
        o.haveDir = true; o.dirRaw = e->second;
    }
    for (auto& e : entries) { auto it = objs_.find(e.first); if (it != objs_.end()) tryComplete(it->second, e.first); }
}

} // namespace dect2
