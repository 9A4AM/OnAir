#include "dect2/tv/dvbsub.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dect2 {

#ifndef DECT2_RUST_DVBSI   // with Rust (rust/onair_dvbsi) these live in dvbsub_rs.cpp
double ptsDelta(double a, double b) {
    const double wrap = 8589934592.0 / 90000.0;   // 2^33 ticks
    double d = a - b;
    if (d > wrap / 2) d -= wrap;
    else if (d < -wrap / 2) d += wrap;
    return d;
}

void dvbSubComposite(const DvbSubDisplay& d, std::vector<uint8_t>& out) {
    out.assign((size_t)d.dispW * d.dispH * 4, 0);
    for (const auto& r : d.regions) {
        for (int y = 0; y < r.h; y++) {
            const int dy = r.y + y;
            if (dy < 0 || dy >= d.dispH) continue;
            for (int x = 0; x < r.w; x++) {
                const int dx = r.x + x;
                if (dx < 0 || dx >= d.dispW) continue;
                const uint8_t* s = &r.rgba[((size_t)y * r.w + x) * 4];
                if (!s[3]) continue;
                uint8_t* o = &out[((size_t)dy * d.dispW + dx) * 4];
                const int sa = s[3], oa = o[3];
                if (sa == 255 || oa == 0) { memcpy(o, s, 4); continue; }
                const int ra = sa + oa * (255 - sa) / 255;
                for (int c = 0; c < 3; c++) o[c] = (uint8_t)((s[c] * sa * 255 + o[c] * oa * (255 - sa)) / (ra * 255));
                o[3] = (uint8_t)ra;
            }
        }
    }
}
#endif

// ------------------------------------------------------------------ colours and pixels (the pure layer: C++ here, Rust with DECT2_RUST_DVBSI)
#ifndef DECT2_RUST_DVBSI
namespace {
uint32_t ycrcb(int y, int cr, int cb, int t) {
    if (y == 0) return 0;                                   // Y = 0: fully transparent whatever T says
    const double yy = (y - 16) * 255.0 / 219.0, u = cb - 128.0, v = cr - 128.0;   // ITU-R BT.601, studio swing
    auto cl = [](double x) { return (uint8_t)std::max(0.0, std::min(255.0, std::floor(x + 0.5))); };
    const uint8_t r = cl(yy + 1.402 * v * 255.0 / 224.0), g = cl(yy - 0.344136 * u * 255.0 / 224.0 - 0.714136 * v * 255.0 / 224.0), b = cl(yy + 1.772 * u * 255.0 / 224.0);
    const uint8_t a = (uint8_t)(255 - std::min(255, std::max(0, t)));
    return r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)a << 24;
}

struct Bits {
    const uint8_t* p; int n; int pos = 0;       // pos in bits
    Bits(const uint8_t* d, int len) : p(d), n(len * 8) {}
    bool eof() const { return pos >= n; }
    int get(int k) {
        int v = 0;
        for (int i = 0; i < k; i++) { v <<= 1; if (pos < n) v |= (p[pos >> 3] >> (7 - (pos & 7))) & 1; pos++; }
        return v;
    }
    int peek(int k) { const int s = pos; const int v = get(k); pos = s; return v; }
    void align() { pos = (pos + 7) & ~7; }
};
} // namespace

uint32_t dvbSubYcrcb(int y, int cr, int cb, int t) { return ycrcb(y, cr, cb, t); }

void dvbSubDefaultClut(uint32_t e2[4], uint32_t e4[16], uint32_t e8[256]) {
    // EN 300 743 section 10: the default 2-, 4- and 8-bit colour tables
    static const uint32_t t2[4] = {0x00000000u, 0xFFFFFFFFu, 0xFF000000u, 0xFF808080u};
    memcpy(e2, t2, sizeof t2);
    for (int i = 0; i < 16; i++) {
        if (i == 0) { e4[i] = 0; continue; }
        const int lvl = i < 8 ? 255 : 127;   // 1..7 full intensity, 8..15 half
        const uint32_t r = (i & 1) ? lvl : 0, g = (i & 2) ? lvl : 0, b = (i & 4) ? lvl : 0;
        e4[i] = r | g << 8 | b << 16 | 0xFF000000u;
        if (i == 8) e4[i] = 0x000000u | 0xFF000000u;
    }
    for (int i = 0; i < 256; i++) {
        if (i == 0) { e8[i] = 0; continue; }
        if (i < 8) { e8[i] = ((i & 1) ? 255u : 0u) | ((i & 2) ? 255u : 0u) << 8 | ((i & 4) ? 255u : 0u) << 16 | 0x40000000u; continue; }   // 25 % opaque
        const int v = i & 0x88;
        if (v == 0) {
            const uint32_t r = ((i & 1) ? 85u : 0u) + ((i & 0x10) ? 170u : 0u), g = ((i & 2) ? 85u : 0u) + ((i & 0x20) ? 170u : 0u), b = ((i & 4) ? 85u : 0u) + ((i & 0x40) ? 170u : 0u);
            e8[i] = r | g << 8 | b << 16 | 0xBF000000u;
        } else if (v == 8) {
            const uint32_t r = ((i & 1) ? 85u : 0u) + ((i & 0x10) ? 170u : 0u), g = ((i & 2) ? 85u : 0u) + ((i & 0x20) ? 170u : 0u), b = ((i & 4) ? 85u : 0u) + ((i & 0x40) ? 170u : 0u);
            e8[i] = r | g << 8 | b << 16 | 0x80000000u;   // 50 % opaque
        } else {
            const uint32_t r = ((i & 1) ? 43u : 0u) + ((i & 0x10) ? 85u : 0u) + (v == 0x80 ? 127u : 0u), g = ((i & 2) ? 43u : 0u) + ((i & 0x20) ? 85u : 0u) + (v == 0x80 ? 127u : 0u), b = ((i & 4) ? 43u : 0u) + ((i & 0x40) ? 85u : 0u) + (v == 0x80 ? 127u : 0u);
            e8[i] = r | g << 8 | b << 16 | 0xFF000000u;
        }
    }
}

void dvbSubRegionRgba(const uint8_t* pix, size_t n, int depth, const uint32_t* e2, const uint32_t* e4, const uint32_t* e8, uint8_t* out) {
    for (size_t k = 0; k < n; k++) {
        const uint32_t v = depth == 2 ? e2[pix[k] & 3] : depth == 4 ? e4[pix[k] & 15] : e8[pix[k]];
        uint8_t* o = &out[k * 4];
        o[0] = v & 0xFF; o[1] = (v >> 8) & 0xFF; o[2] = (v >> 16) & 0xFF; o[3] = (uint8_t)(v >> 24);
    }
}
#endif

#ifndef DECT2_RUST_DVBSI
// ------------------------------------------------------------------ PES header, segments and segment parsers (EN 300 743 section 7)
bool dvbSubPesInfo(const uint8_t* pes, size_t n, DvbSubPesInfo& out) {
    if (n < 10 || pes[0] != 0 || pes[1] != 0 || pes[2] != 1 || pes[3] != 0xBD) return false;
    const size_t hdr = 9 + pes[8];
    if (hdr + 2 > n) return false;
    out.payloadOff = (uint32_t)(hdr + 2);
    out.hasPts = false; out.pts = -1;
    if ((pes[7] & 0x80) && pes[8] >= 5) {
        const uint8_t* q = pes + 9;
        const uint64_t t = (uint64_t)((q[0] >> 1) & 7) << 30 | (uint64_t)q[1] << 22 | (uint64_t)(q[2] >> 1) << 15 | (uint64_t)q[3] << 7 | (q[4] >> 1);
        out.hasPts = true;
        out.pts = (double)t / 90000.0;
    }
    return pes[hdr] == 0x20;   // data_identifier: DVB subtitles
}

std::vector<DvbSubSegment> dvbSubSegments(const uint8_t* d, size_t n) {
    std::vector<DvbSubSegment> out;
    size_t i = 0;
    while (i + 6 <= n && d[i] == 0x0F) {
        const int len = (d[i + 4] << 8) | d[i + 5];
        if (i + 6 + (size_t)len > n) break;
        out.push_back(DvbSubSegment{d[i + 1], (d[i + 2] << 8) | d[i + 3], (uint32_t)(i + 6), (uint32_t)len});
        i += 6 + len;
    }
    return out;
}

bool dvbSubParsePage(const uint8_t* d, int n, DvbSubPageInfo& out) {
    out = DvbSubPageInfo();
    if (n < 2) return false;
    out.timeout = d[0];
    out.version = d[1] >> 4; out.state = (d[1] >> 2) & 3;
    for (int i = 2; i + 6 <= n; i += 6) out.regions.push_back({d[i], (d[i + 2] << 8) | d[i + 3], (d[i + 4] << 8) | d[i + 5]});
    return true;
}

bool dvbSubParseRegion(const uint8_t* d, int n, DvbSubRegionInfo& out) {
    out = DvbSubRegionInfo();
    if (n < 10) return false;
    out.id = d[0]; out.version = d[1] >> 4; out.fill = (d[1] >> 3) & 1;
    out.w = (d[2] << 8) | d[3]; out.h = (d[4] << 8) | d[5];
    const int depth = (d[6] >> 2) & 7;   // 1 = 2 bit, 2 = 4 bit, 3 = 8 bit
    out.depth = depth == 1 ? 2 : depth == 3 ? 8 : 4;
    out.clut = d[7]; out.pix8 = d[8]; out.pix4 = d[9] >> 4; out.pix2 = (d[9] >> 2) & 3;
    for (int i = 10; i + 6 <= n; ) {
        DvbSubObjRef o;
        o.id = (d[i] << 8) | d[i + 1];
        o.type = d[i + 2] >> 6;
        o.x = ((d[i + 2] & 0x0F) << 8) | d[i + 3];
        o.y = ((d[i + 4] & 0x0F) << 8) | d[i + 5];
        i += 6;
        if (o.type == 1 || o.type == 2) { if (i + 2 > n) break; o.fg = d[i]; o.bg = d[i + 1]; i += 2; }
        out.objs.push_back(o);
    }
    return true;
}

bool dvbSubParseClut(const uint8_t* d, int n, int& id, std::vector<DvbSubClutEntry>& out) {
    out.clear();
    if (n < 2) return false;
    id = d[0];
    for (int i = 2; i + 2 <= n; ) {
        const int eid = d[i], fl = d[i + 1];
        const bool full = fl & 1;
        int y, cr, cb, t;
        if (full) { if (i + 6 > n) break; y = d[i + 2]; cr = d[i + 3]; cb = d[i + 4]; t = d[i + 5]; i += 6; }
        else {
            if (i + 4 > n) break;
            const int v = (d[i + 2] << 8) | d[i + 3];
            y = (v >> 10) << 2; cr = ((v >> 6) & 15) << 4; cb = ((v >> 2) & 15) << 4; t = (v & 3) * 85; i += 4;
        }
        out.push_back(DvbSubClutEntry{eid, fl, ycrcb(y, cr, cb, t)});
    }
    return true;
}

bool dvbSubParseDisplay(const uint8_t* d, int n, int& w, int& h) {
    if (n < 5) return false;
    w = ((d[1] << 8) | d[2]) + 1;
    h = ((d[3] << 8) | d[4]) + 1;
    if (w < 1 || w > 4096 || h < 1 || h > 4096) { w = 720; h = 576; }
    return true;
}

bool dvbSubParseObject(const uint8_t* d, int n, DvbSubObjectInfo& out) {
    if (n < 3) return false;
    const int method = (d[2] >> 2) & 3;
    if (method != 0 || n < 7) return false;
    const int topLen = (d[3] << 8) | d[4], botLen = (d[5] << 8) | d[6];
    if (7 + topLen + botLen > n) return false;
    out.id = (d[0] << 8) | d[1];
    out.topOff = 7; out.topLen = (uint32_t)topLen; out.botOff = (uint32_t)(7 + topLen); out.botLen = (uint32_t)botLen;
    return true;
}
#endif

DvbSubDecoder::Clut DvbSubDecoder::defaultClut() const {
    Clut c;
    dvbSubDefaultClut(c.e2, c.e4, c.e8);
    return c;
}

// ------------------------------------------------------------------ transport
void DvbSubDecoder::setStream(int pid, int comp, int anc) {
    std::lock_guard<std::mutex> lk(mu_);
    if (pid == pid_ && comp == comp_ && anc == anc_) return;
    pid_ = pid; comp_ = comp; anc_ = anc;
    pes_.clear(); inPes_ = false; cc_ = -1;
    havePage_ = false; pageRegs_.clear(); regions_.clear(); cluts_.clear(); objects_.clear(); queue_.clear();
    pageVersion_ = -1; dispW_ = 720; dispH_ = 576;
}
int DvbSubDecoder::pid() const { std::lock_guard<std::mutex> lk(mu_); return pid_; }
int DvbSubDecoder::compositionPage() const { std::lock_guard<std::mutex> lk(mu_); return comp_; }
uint64_t DvbSubDecoder::displays() const { std::lock_guard<std::mutex> lk(mu_); return seq_; }
void DvbSubDecoder::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    pes_.clear(); inPes_ = false; cc_ = -1;
    havePage_ = false; pageRegs_.clear(); regions_.clear(); cluts_.clear(); objects_.clear(); queue_.clear();
    pageVersion_ = -1; dispW_ = 720; dispH_ = 576; seq_ = 0;
}

void DvbSubDecoder::feedTs(const uint8_t* p) {
    if (p[0] != 0x47) return;
    const int pid = ((p[1] & 0x1F) << 8) | p[2];
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (pid != pid_) return;
    }
    const bool pusi = (p[1] & 0x40) != 0;
    const int afc = (p[3] >> 4) & 3;
    int off = 4;
    if (afc & 2) off += 1 + p[4];
    if (!(afc & 1) || off >= 188) return;
    std::vector<uint8_t> done;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const int cc = p[3] & 15;
        if (cc_ >= 0 && ((cc_ + 1) & 15) != cc && !pusi) { pes_.clear(); inPes_ = false; }   // lost packets: drop the partial PES
        cc_ = cc;
        if (pusi) {
            if (inPes_ && !pes_.empty()) done.swap(pes_);   // a PES of unbounded length ends here
            pes_.clear();
            inPes_ = true;
            pesLen_ = 0;
        }
        if (inPes_) {
            pes_.insert(pes_.end(), p + off, p + 188);
            if (pes_.size() >= 6 && pesLen_ == 0 && pes_[0] == 0 && pes_[1] == 0 && pes_[2] == 1) {
                const size_t l = ((size_t)pes_[4] << 8) | pes_[5];
                if (l) pesLen_ = l + 6;
            }
            if (pesLen_ && pes_.size() >= pesLen_ && done.empty()) { pes_.resize(pesLen_); done.swap(pes_); inPes_ = false; pesLen_ = 0; }
        }
    }
    if (!done.empty()) feedPes(done.data(), done.size());
}

void DvbSubDecoder::feedPes(const uint8_t* pes, size_t n) {
    DvbSubPesInfo info;
    if (!dvbSubPesInfo(pes, n, info)) return;
    feedSegments(pes + info.payloadOff, n - info.payloadOff, info.hasPts ? info.pts : -1);   // skips data_identifier and subtitle_stream_id
}

// ------------------------------------------------------------------ segments
void DvbSubDecoder::feedSegments(const uint8_t* d, size_t n, double pts) {
    const std::vector<DvbSubSegment> segs = dvbSubSegments(d, n);
    std::lock_guard<std::mutex> lk(mu_);
    for (const DvbSubSegment& s : segs) {
        if (s.page == comp_ || s.page == anc_) {
            if (s.type == 0x80) endOfDisplaySet(pts);
            else segment(s.type, s.page, d + s.off, (int)s.len);
        }
    }
}

void DvbSubDecoder::segment(int type, int page, const uint8_t* d, int n) {
    (void)page;
    switch (type) {
    case 0x10: pageComposition(d, n); break;
    case 0x11: regionComposition(d, n); break;
    case 0x12: clutDefinition(d, n); break;
    case 0x13: objectData(d, n); break;
    case 0x14: displayDefinition(d, n); break;
    default: break;
    }
}

void DvbSubDecoder::pageComposition(const uint8_t* d, int n) {
    DvbSubPageInfo pi;
    if (!dvbSubParsePage(d, n, pi)) return;
    timeout_ = pi.timeout;
    const int ver = pi.version, state = pi.state;
    if (state == 0 && havePage_ && ver == pageVersion_) { /* the same version repeated: nothing new, but regions still listed */ }
    pageVersion_ = ver; pageState_ = state;
    if (state == 1 || state == 2) { regions_.clear(); cluts_.clear(); objects_.clear(); }   // acquisition point / mode change: everything starts afresh
    pageRegs_.clear();
    for (const DvbSubPageReg& r : pi.regions) pageRegs_.push_back({r.id, r.x, r.y});
    havePage_ = true;
}

void DvbSubDecoder::regionComposition(const uint8_t* d, int n) {
    DvbSubRegionInfo ri;
    if (!dvbSubParseRegion(d, n, ri)) return;
    const int id = ri.id, ver = ri.version;
    const bool fill = ri.fill;
    Region& r = regions_[id];
    const int w = ri.w, h = ri.h;
    if (r.version != ver || r.w != w || r.h != h || r.pix.empty()) {
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { regions_.erase(id); return; }
        r.id = id; r.version = ver; r.w = w; r.h = h; r.fill = fill;
        r.depth = ri.depth;   // 1 = 2 bit, 2 = 4 bit, 3 = 8 bit
        r.clut = ri.clut; r.pix8 = ri.pix8; r.pix4 = ri.pix4; r.pix2 = ri.pix2;
        const int bg = r.depth == 2 ? r.pix2 : r.depth == 4 ? r.pix4 : r.pix8;
        r.pix.assign((size_t)w * h, fill ? (uint8_t)bg : (uint8_t)0);
        r.objs.clear();
        for (const DvbSubObjRef& oi : ri.objs) { Obj o; o.id = oi.id; o.type = oi.type; o.x = oi.x; o.y = oi.y; o.fg = oi.fg; o.bg = oi.bg; r.objs.push_back(o); }
        // objects already received (they may arrive before the region on a repeat) are drawn now
        for (const Obj& o : r.objs) { auto it = objects_.find(o.id); if (it != objects_.end()) drawObject(r, o, it->second); }
    }
}

void DvbSubDecoder::clutDefinition(const uint8_t* d, int n) {
    int id;
    std::vector<DvbSubClutEntry> entries;
    if (!dvbSubParseClut(d, n, id, entries)) return;
    auto it = cluts_.find(id);
    if (it == cluts_.end()) it = cluts_.emplace(id, defaultClut()).first;
    Clut& c = it->second;
    for (const DvbSubClutEntry& e : entries) {
        if ((e.flags & 0x80) && e.id < 4) c.e2[e.id] = e.rgba;
        if ((e.flags & 0x40) && e.id < 16) c.e4[e.id] = e.rgba;
        if ((e.flags & 0x20)) c.e8[e.id] = e.rgba;
    }
}

void DvbSubDecoder::displayDefinition(const uint8_t* d, int n) {
    int w, h;
    if (!dvbSubParseDisplay(d, n, w, h)) return;
    dispW_ = w;
    dispH_ = h;
}

void DvbSubDecoder::objectData(const uint8_t* d, int n) {
    DvbSubObjectInfo oi;
    if (!dvbSubParseObject(d, n, oi)) return;   // only bitmap objects; character-coded ones are not used by broadcasters
    const int id = oi.id;
    ObjData od;
    od.id = id;
    od.top.assign(d + oi.topOff, d + oi.topOff + oi.topLen);
    od.bottom.assign(d + oi.botOff, d + oi.botOff + oi.botLen);
    objects_[id] = od;
    for (auto& kv : regions_) for (const Obj& o : kv.second.objs) if (o.id == id) drawObject(kv.second, o, objects_[id]);
}

#ifndef DECT2_RUST_DVBSI
// Decode one field's pixel-data sub-blocks into the region at row `y0`, advancing two lines per row.
void dvbSubDecodeField(const uint8_t* f, size_t fn, int x0, int y0, int lineStep, bool doubleLines, int regW, int regH, int regDepth, uint8_t* pix) {
    uint8_t map2to4[4] = {0, 7, 8, 15}, map2to8[4] = {0, 0x77, 0x88, 0xFF}, map4to8[16];
    for (int i = 0; i < 16; i++) map4to8[i] = (uint8_t)(i * 17);
    int x = x0, y = y0;
    auto put = [&](int code, int run, int srcDepth) {
        if (run <= 0) return;
        int v = code;
        if (srcDepth != regDepth) {
            if (srcDepth == 2 && regDepth == 4) v = map2to4[code & 3];
            else if (srcDepth == 2 && regDepth == 8) v = map2to8[code & 3];
            else if (srcDepth == 4 && regDepth == 8) v = map4to8[code & 15];
            else v = code & ((1 << regDepth) - 1);   // a deeper string in a shallower region: keep the low bits
        }
        for (int k = 0; k < run; k++, x++) {
            if (x < 0 || x >= regW) continue;
            for (int l = 0; l <= (doubleLines ? 1 : 0); l++) {
                const int yy = y + l;
                if (yy >= 0 && yy < regH) pix[(size_t)yy * regW + x] = (uint8_t)v;
            }
        }
    };
    size_t i = 0;
    while (i < fn) {
        const int t = f[i++];
        if (t == 0xF0) { x = x0; y += lineStep; continue; }
        if (t == 0x20) { for (int k = 0; k < 4 && i < fn; k += 2) { if (k == 0) { map2to4[0] = f[i] >> 4; map2to4[1] = f[i] & 15; } else { map2to4[2] = f[i] >> 4; map2to4[3] = f[i] & 15; } i++; } continue; }
        if (t == 0x21) { for (int k = 0; k < 4 && i < fn; k++) map2to8[k] = f[i++]; continue; }
        if (t == 0x22) { for (int k = 0; k < 16 && i < fn; k++) map4to8[k] = f[i++]; continue; }
        if (t != 0x10 && t != 0x11 && t != 0x12) break;   // unknown block: the rest cannot be parsed
        // pixel code string up to its end marker (the string must end in its own end-of-string code; byte alignment for 2- and 4-bit)
        Bits b(f + i, (int)(fn - i));
        if (t == 0x10) {
            for (;;) {
                if (b.eof()) break;
                int c = b.get(2);
                if (c) { put(c, 1, 2); continue; }
                if (b.get(1)) { const int run = b.get(3) + 3; put(b.get(2), run, 2); continue; }
                if (b.get(1)) { put(0, 1, 2); continue; }
                const int s3 = b.get(2);
                if (s3 == 0) break;
                if (s3 == 1) { put(0, 2, 2); continue; }
                if (s3 == 2) { const int run = b.get(4) + 12; put(b.get(2), run, 2); continue; }
                const int run = b.get(8) + 29; put(b.get(2), run, 2);
            }
        } else if (t == 0x11) {
            for (;;) {
                if (b.eof()) break;
                int c = b.get(4);
                if (c) { put(c, 1, 4); continue; }
                if (!b.get(1)) {
                    const int r = b.get(3);
                    if (r == 0) break;
                    put(0, r + 2, 4); continue;
                }
                if (!b.get(1)) { const int run = b.get(2) + 4; put(b.get(4), run, 4); continue; }
                const int s3 = b.get(2);
                if (s3 == 0) { put(0, 1, 4); continue; }
                if (s3 == 1) { put(0, 2, 4); continue; }
                if (s3 == 2) { const int run = b.get(4) + 9; put(b.get(4), run, 4); continue; }
                const int run = b.get(8) + 25; put(b.get(4), run, 4);
            }
        } else {
            for (;;) {
                if (b.eof()) break;
                int c = b.get(8);
                if (c) { put(c, 1, 8); continue; }
                if (!b.get(1)) {
                    const int r = b.get(7);
                    if (r == 0) break;
                    put(0, r, 8); continue;
                }
                const int run = b.get(7); put(b.get(8), run, 8);
            }
        }
        b.align();
        i += (size_t)(b.pos / 8);
    }
}
#endif

void DvbSubDecoder::drawObject(Region& r, const Obj& o, const ObjData& od) {
    if (o.type != 0) return;   // character-coded objects are not supported
    // a missing bottom field means the top field's lines are doubled
    const bool dbl = od.bottom.empty();
    dvbSubDecodeField(od.top.data(), od.top.size(), o.x, o.y, 2, dbl, r.w, r.h, r.depth, r.pix.data());
    if (!dbl) dvbSubDecodeField(od.bottom.data(), od.bottom.size(), o.x, o.y + 1, 2, false, r.w, r.h, r.depth, r.pix.data());
}

void DvbSubDecoder::endOfDisplaySet(double pts) {
    if (!havePage_) return;
    auto disp = std::make_shared<DvbSubDisplay>();
    disp->pts = pts;
    disp->timeout = timeout_;
    disp->dispW = dispW_; disp->dispH = dispH_;
    const Clut dflt = defaultClut();
    for (const PageReg& pr : pageRegs_) {
        auto it = regions_.find(pr.id);
        if (it == regions_.end()) continue;
        const Region& r = it->second;
        auto ci = cluts_.find(r.clut);
        const Clut& c = ci != cluts_.end() ? ci->second : dflt;
        DvbSubRegionImage im;
        im.x = pr.x; im.y = pr.y; im.w = r.w; im.h = r.h;
        im.rgba.resize((size_t)r.w * r.h * 4);
        dvbSubRegionRgba(r.pix.data(), r.pix.size(), r.depth, c.e2, c.e4, c.e8, im.rgba.data());
        disp->regions.push_back(std::move(im));
    }
    disp->seq = ++seq_;
    queue_.push_back(disp);
    while (queue_.size() > 24) queue_.pop_front();
}

std::shared_ptr<const DvbSubDisplay> DvbSubDecoder::displayAt(double clock) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::shared_ptr<DvbSubDisplay> best;
    for (const auto& d : queue_) if (d->pts < 0 || ptsDelta(d->pts, clock) <= 0.0) best = d;   // the newest one already due
    if (!best || best->regions.empty()) return nullptr;
    if (best->pts >= 0 && best->timeout > 0 && ptsDelta(clock, best->pts) > best->timeout) return nullptr;
    return best;
}

} // namespace dect2
