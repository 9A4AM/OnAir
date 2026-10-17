// Differential test: the C++ DAB data layers (a generated copy in dect2::dabp_ref, see cmake/gen_rs_ref.cmake) against the one the program uses (dect2::dabp:
// the Rust crate onair_dab when DECT2_RUST is on). The same inputs go to both and every result has to be identical: the FIB walk and the FIG 0/0, 0/1, 0/2, 0/3,
// 0/10, 0/13, 0/17, 0/18, 0/19, FIG 1 and FIG 2 parsers, MOT time, header (parameters, caption, slide show fields), data group and directory, the PAD split of
// access units and MP2 frames, X-PAD subfields, dynamic label framing and DL Plus, the Journaline data group and TOC, the packet mode headers, built from random
// descriptors, mutated, truncated and with corrupt length fields, over ONAIR_DIFF_ITERS (default 200000) seeded inputs per family. Buffers are exactly as long
// as the input so a sanitizer build sees any read past the end.
#include "dect2/dab/dab_proto.h"
#include "dab_proto_ref.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
namespace A = dect2::dabp;
namespace R = dect2::dabp_ref;

static long fails = 0, checks = 0;
static long okc[32];
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)
static std::mt19937 g(1234567);
static uint32_t r32() { return g(); }
using Buf = std::vector<uint8_t>;
static void rnd(Buf& b, size_t n) { for (size_t i = 0; i < n; i++) b.push_back((uint8_t)r32()); }
static void put(Buf& b, std::initializer_list<int> v) { for (int x : v) b.push_back((uint8_t)x); }
static void cat(Buf& a, const Buf& b) { a.insert(a.end(), b.begin(), b.end()); }
static Buf mutate(Buf s) {
    const int k = (int)(r32() % 4);
    for (int i = 0; i < k; i++) {
        switch (r32() % 6) {
        case 0: if (!s.empty()) s[r32() % s.size()] = (uint8_t)r32(); break;
        case 1: if (!s.empty()) s[r32() % s.size()] ^= (uint8_t)(1u << (r32() % 8)); break;
        case 2: s.resize(r32() % (s.size() + 1)); break;
        case 3: rnd(s, r32() % 10); break;
        case 4: if (s.size() > 2) s.erase(s.begin() + r32() % (s.size() - 1)); break;
        default: if (s.size() > 4) s[r32() % 5] = (uint8_t)(r32() % 3 ? 0xFF : r32()); break;
        }
    }
    return s;
}
static Buf pick(Buf (*gen)()) { Buf s = gen(); const uint32_t r = r32() % 10; if (r < 3) return s; if (r < 9) return mutate(s); Buf t; rnd(t, 1 + r32() % 60); return t; }
template <class F> static void exact(const Buf& s, F f) {   // an exactly sized heap copy
    uint8_t* p = s.empty() ? nullptr : (uint8_t*)malloc(s.size());
    if (p) memcpy(p, s.data(), s.size());
    f(p, s.size());
    free(p);
}
static void crcAppend(Buf& b, bool good) { uint16_t c = A::crc16(b.data(), b.size()); if (!good) c ^= (uint16_t)(1 + r32() % 0xFFFE); put(b, {c >> 8, c & 0xFF}); }

// ---- FIG generators --------------------------------------------------------------------------------------------------------------------------------------
static Buf fig0(int ext) {
    Buf d; const int pd = (int)(r32() % 2);
    put(d, {(int)((r32() % 4 ? 0 : 0x80) | (r32() % 4 ? 0 : 0x40) | pd << 5 | ext)});
    switch (ext) {
    case 0: rnd(d, 2 + r32() % 3); break;
    case 1: { const int n = (int)(r32() % 5); for (int i = 0; i < n; i++) { put(d, {(int)(r32() % 64) << 2 | (int)(r32() % 4), (int)(r32() & 0xFF)}); if (r32() % 2) { put(d, {0x80 | (int)(r32() & 0x7F)}); rnd(d, 1); } else put(d, {(int)(r32() & 0x7F)}); } break; }
    case 2: { const int n = (int)(r32() % 4); for (int i = 0; i < n; i++) { if (pd) rnd(d, 4); else rnd(d, 2); const int nc = (int)(r32() % 5); d.push_back((uint8_t)(r32() % 16 << 4 | nc)); for (int c = 0; c < nc; c++) { put(d, {(int)(r32() % 4) << 6 | (int)(r32() % 64), (int)(r32() & 0xFF)}); } } break; }
    case 3: { const int n = (int)(r32() % 4); for (int i = 0; i < n; i++) { const int ca = (int)(r32() % 2); put(d, {(int)(r32() & 0xFF), (int)((r32() & 0xF0) | ca), (int)(r32() & 0xFF), (int)(r32() & 0xFF), (int)(r32() & 0xFF)}); if (ca) rnd(d, 2); } break; }
    case 10: put(d, {(int)(0x80 | (r32() & 0x7F)), (int)(r32() & 0xFF), (int)(r32() & 0xFF), (int)(r32() & 0xFF)}); if (r32() % 2) rnd(d, 2); break;
    case 13: { const int n = (int)(r32() % 3); for (int i = 0; i < n; i++) { if (pd) rnd(d, 4); else rnd(d, 2); const int nua = (int)(r32() % 4); d.push_back((uint8_t)(r32() % 16 << 4 | nua)); for (int u = 0; u < nua; u++) { const int l = (int)(r32() % 6); put(d, {(int)(r32() & 0xFF), (int)((r32() & 0xE0) | l)}); rnd(d, l); } } break; }
    case 17: { const int n = (int)(r32() % 4); for (int i = 0; i < n; i++) { const int fl = (int)(r32() & 0xFF); rnd(d, 2); d.push_back((uint8_t)fl); if (fl & 0x20) rnd(d, 1); rnd(d, 1); if (fl & 0x10) rnd(d, 1); } break; }
    case 18: { const int n = (int)(r32() % 3); for (int i = 0; i < n; i++) { rnd(d, 4); const int nc = (int)(r32() % 6); d.push_back((uint8_t)(r32() & 0xE0 | nc)); rnd(d, nc); } break; }
    case 19: { const int n = (int)(r32() % 4); for (int i = 0; i < n; i++) { rnd(d, 4); if (d.back() & 0x40) rnd(d, 1); } break; }
    default: rnd(d, r32() % 12);
    }
    return d;
}
static Buf genFig0() { static const int exts[] = {0, 1, 2, 3, 10, 13, 17, 18, 19, 5, 8, 31}; return fig0(exts[r32() % 12]); }
static Buf genFig1() {
    static const int exts[] = {0, 1, 4, 5, 2, 7};
    const int ext = exts[r32() % 6], pd = (int)(r32() % 2);
    Buf d; put(d, {(int)((r32() % 16) << 4 | (r32() % 2) << 3 | ext)});
    if (ext == 1) rnd(d, 2); else if (ext == 5) rnd(d, 4); else if (ext == 4) { d.push_back((uint8_t)(pd << 7 | (r32() % 16))); rnd(d, pd ? 4 : 2); }
    rnd(d, 16 + r32() % 4); if (r32() % 4 == 0) rnd(d, 2);
    return d;
}
static Buf genFig2() {
    static const int exts[] = {0, 1, 4, 5, 3};
    const int ext = exts[r32() % 5];
    Buf d; put(d, {(int)((r32() % 2) << 7 | (r32() % 8) << 4 | (r32() % 2) << 3 | ext)});
    if (ext == 0) rnd(d, 2); else if (ext == 1) rnd(d, 2); else if (ext == 5) rnd(d, 4); else if (ext == 4) { d.push_back((uint8_t)((r32() % 2) << 7 | (r32() % 16))); rnd(d, 2 + (d.back() & 0x80 ? 3 : 1)); }
    rnd(d, r32() % 20);
    return d;
}
static Buf genFib() {
    Buf f;
    while (f.size() < 30 && r32() % 6) {
        Buf d = r32() % 2 ? genFig0() : r32() % 2 ? genFig1() : genFig2();
        if (d.size() > 31) d.resize(31);
        const int type = d[0] < 0x20 ? 0 : (int)(r32() % 3);
        f.push_back((uint8_t)(type << 5 | d.size())); cat(f, d);
    }
    if (f.size() < 30 && r32() % 2) f.push_back(0xFF);
    f.resize(30, r32() % 2 ? 0xFF : 0x00);
    return f;
}

template <class T, class U> static bool spanEq(const T& a, const U& b) { return a.off == b.off && a.len == b.len; }
static void testFig(const Buf& s) {
    exact(s, [&](const uint8_t* p, size_t n) {
        if (n == 0) return;
        const auto ha = A::figHead(0, p, n);
        const auto hr = R::figHead(0, p, n);
        SAME(ha.cn == hr.cn && ha.oe == hr.oe && ha.pd == hr.pd && ha.ext == hr.ext, "figHead0");
        for (int ty : {1, 2, 5, 3}) { const auto a = A::figHead(ty, p, n); const auto r = R::figHead(ty, p, n); SAME(a.cn == r.cn && a.oe == r.oe && a.pd == r.pd && a.ext == r.ext, "figHead %d", ty); }
        int ea = -1, er = -1; const bool ba = A::fig0Eid(p, n, ea), br = R::fig0Eid(p, n, er); SAME(ba == br && (!ba || ea == er), "eid"); if (ba) okc[0]++;
        { const auto a = A::fig0Subchannels(p, n); const auto r = R::fig0Subchannels(p, n); bool e = a.size() == r.size();
          for (size_t i = 0; e && i < a.size(); i++) e = a[i].id == r[i].id && a[i].start == r[i].start && a[i].eep == r[i].eep && a[i].uepTable == r[i].uepTable && a[i].option == r[i].option && a[i].level == r[i].level && a[i].size == r[i].size && a[i].uepIndex == r[i].uepIndex;
          SAME(e, "subchannels n=%zu %zu vs %zu", n, a.size(), r.size()); if (!a.empty()) okc[1]++; }
        { const auto a = A::fig0Packet(p, n); const auto r = R::fig0Packet(p, n); bool e = a.size() == r.size();
          for (size_t i = 0; e && i < a.size(); i++) e = a[i].scid == r[i].scid && a[i].dg == r[i].dg && a[i].dscty == r[i].dscty && a[i].subId == r[i].subId && a[i].address == r[i].address;
          SAME(e, "packet"); if (!a.empty()) okc[2]++; }
        { const auto a = A::fig0Services(p, n); const auto r = R::fig0Services(p, n); bool e = a.size() == r.size();
          for (size_t i = 0; e && i < a.size(); i++) { e = a[i].sid == r[i].sid && a[i].comps.size() == r[i].comps.size();
              for (size_t k = 0; e && k < a[i].comps.size(); k++) { const auto& x = a[i].comps[k]; const auto& y = r[i].comps[k]; e = x.tmid == y.tmid && x.ascty == y.ascty && x.dscty == y.dscty && x.subId == y.subId && x.scid == y.scid && x.primary == y.primary; } }
          SAME(e, "services n=%zu", n); if (!a.empty()) okc[3]++; }
        { const auto a = A::fig0UserApps(p, n); const auto r = R::fig0UserApps(p, n); bool e = a.size() == r.size();
          for (size_t i = 0; e && i < a.size(); i++) e = a[i].sid == r[i].sid && a[i].scids == r[i].scids && a[i].index == r[i].index && a[i].type == r[i].type && a[i].off == r[i].off && a[i].len == r[i].len;
          SAME(e, "userapps"); if (!a.empty()) okc[4]++; }
        { const auto a = A::fig0Pty(p, n); const auto r = R::fig0Pty(p, n); bool e = a.size() == r.size();
          for (size_t i = 0; e && i < a.size(); i++) e = a[i].sid == r[i].sid && a[i].sd == r[i].sd && a[i].primary == r[i].primary && a[i].lang == r[i].lang && a[i].pty == r[i].pty;
          SAME(e, "pty"); if (!a.empty()) okc[5]++; }
        { const auto a = A::fig0AnnSupport(p, n); const auto r = R::fig0AnnSupport(p, n); bool e = a.size() == r.size();
          for (size_t i = 0; e && i < a.size(); i++) e = a[i].sid == r[i].sid && a[i].asu == r[i].asu && a[i].clusters == r[i].clusters;
          SAME(e, "ann support"); if (!a.empty()) okc[6]++; }
        { const auto a = A::fig0AnnSwitch(p, n); const auto r = R::fig0AnnSwitch(p, n); bool e = a.size() == r.size();
          for (size_t i = 0; e && i < a.size(); i++) e = a[i].cluster == r[i].cluster && a[i].asw == r[i].asw && a[i].newFlag == r[i].newFlag && a[i].region == r[i].region && a[i].subId == r[i].subId;
          SAME(e, "ann switch"); if (!a.empty()) okc[7]++; }
        { A::Fig010Time a; R::Fig010Time r; const bool ba2 = A::fig0Time(p, n, a), br2 = R::fig0Time(p, n, r);
          SAME(ba2 == br2 && (!ba2 || (a.mjd == r.mjd && a.hour == r.hour && a.minute == r.minute && a.second == r.second && a.utc == r.utc)), "time"); if (ba2) okc[8]++; }
        { A::Fig1Label a; R::Fig1Label r; const bool ba2 = A::fig1(p, n, a), br2 = R::fig1(p, n, r);
          SAME(ba2 == br2 && (!ba2 || (a.ext == r.ext && a.charset == r.charset && a.scids == r.scids && a.sid == r.sid && a.off == r.off)), "fig1"); if (ba2) okc[9]++; }
        { A::Fig2Seg a; R::Fig2Seg r; const bool ba2 = A::fig2(p, n, a), br2 = R::fig2(p, n, r);
          SAME(ba2 == br2 && (!ba2 || (a.ext == r.ext && a.toggle == r.toggle && a.seg == r.seg && a.rfu == r.rfu && a.scids == r.scids && a.sid == r.sid && a.ucs2 == r.ucs2 && a.total == r.total && a.valid == r.valid && a.textOff == r.textOff && a.textLen == r.textLen)), "fig2"); if (ba2) okc[10]++; }
    });
}
static void testFib(const Buf& s) {
    Buf f = s; f.resize(30, 0);
    exact(f, [&](const uint8_t* p, size_t n) {
        int ua = -1, ur = -2; const auto a = A::fibWalk(p, n, ua); const auto r = R::fibWalk(p, n, ur);
        bool e = ua == ur && a.size() == r.size();
        for (size_t i = 0; e && i < a.size(); i++) e = a[i].type == r[i].type && a[i].off == r[i].off && a[i].len == r[i].len;
        SAME(e, "fibWalk"); okc[11] += !a.empty();
        for (const auto& fr : a) { const Buf d(p + fr.off, p + fr.off + fr.len); if (fr.type == 0) testFig(d); }
    });
}

// ---- MOT -------------------------------------------------------------------------------------------------------------------------------------------------
static Buf motTime() { Buf t; const uint32_t v = (r32() % 6 ? 1u << 31 : 0) | (r32() & 0x1FFFF) << 14 | (r32() % 2) << 11 | (r32() % 24) << 6 | (r32() % 60); put(t, {(int)(v >> 24), (int)(v >> 16 & 0xFF), (int)(v >> 8 & 0xFF), (int)(v & 0xFF)}); if (r32() % 2) rnd(t, 2); return t; }
static Buf motParam(int id, const Buf& d) {
    Buf p; const size_t len = d.size();
    if (len == 0) p.push_back((uint8_t)(id & 0x3F));
    else if (len == 1) p.push_back((uint8_t)(0x40 | id));
    else if (len == 4) p.push_back((uint8_t)(0x80 | id));
    else { p.push_back((uint8_t)(0xC0 | id)); if (len < 128) p.push_back((uint8_t)len); else { p.push_back((uint8_t)(0x80 | (len >> 8))); p.push_back((uint8_t)len); } }
    cat(p, d); return p;
}
static Buf genMotHeader() {
    Buf ext;
    const int n = (int)(r32() % 6);
    for (int i = 0; i < n; i++) {
        Buf d;
        switch (r32() % 9) {
        case 0: { d.push_back((uint8_t)(r32() % 16)); for (size_t k = r32() % 12; k; k--) d.push_back((uint8_t)(0x20 + r32() % 90)); cat(ext, motParam(0x0C, d)); break; }
        case 1: { d.push_back((uint8_t)((r32() % 16) << 4)); for (size_t k = r32() % 20; k; k--) d.push_back((uint8_t)(r32() % 3 ? 0x20 + r32() % 90 : r32())); cat(ext, motParam(0x17, d)); break; }
        case 2: put(d, {(int)(r32() & 0xFF), (int)(r32() & 0xFF)}); cat(ext, motParam(0x25, d)); break;
        case 3: for (size_t k = r32() % 15; k; k--) d.push_back((uint8_t)(0x20 + r32() % 90)); cat(ext, motParam(0x26 + (r32() % 3), d)); break;
        case 4: d = motTime(); d.resize(r32() % 3 ? d.size() : r32() % 8); cat(ext, motParam(r32() % 2 ? 0x05 : 0x04, d)); break;
        case 5: for (size_t k = r32() % 5; k; k--) d.push_back((uint8_t)r32()); cat(ext, motParam((int)(r32() % 64), d)); break;
        case 6: rnd(d, 130 + r32() % 20); cat(ext, motParam(0x30, d)); break;
        default: rnd(d, r32() % 5); cat(ext, motParam(0x01, d));
        }
    }
    Buf h; const size_t body = r32() % 5 ? r32() % 100000 : 0, hs = 7 + ext.size();
    put(h, {(int)(body >> 20 & 0xFF), (int)(body >> 12 & 0xFF), (int)(body >> 4 & 0xFF), (int)((body & 0xF) << 4 | (hs >> 9 & 0xF)), (int)(hs >> 1 & 0xFF), (int)((hs & 1) << 7 | (r32() % 9) << 1 | (r32() % 2)), (int)(r32() & 0xFF)});
    cat(h, ext);
    if (r32() % 10 == 0) { const size_t w = r32() % 40; h[3] = (uint8_t)((h[3] & 0xF0) | (w >> 9 & 0xF)); h[4] = (uint8_t)(w >> 1); h[5] = (uint8_t)((h[5] & 0x7F) | (w & 1) << 7); }
    return h;
}
static Buf genMotGroup() {
    Buf d; const int type = (int)(r32() % 10 < 8 ? 3 + r32() % 5 : r32() % 16);
    const int ext = (int)(r32() % 4 == 0), crc = (int)(r32() % 5 != 0), seg = (int)(r32() % 4 != 0), ua = (int)(r32() % 3 != 0);
    put(d, {ext << 7 | crc << 6 | seg << 5 | ua << 4 | type, (int)(r32() & 0xFF)});
    if (ext) rnd(d, 2);
    if (seg) put(d, {(int)((r32() % 2) << 7 | (r32() & 0x7F)), (int)(r32() & 0xFF)});
    if (ua) { const int li = (int)(r32() % 5); d.push_back((uint8_t)((r32() % 2) << 4 | li)); rnd(d, li); }
    const int size = (int)(r32() % 30);
    put(d, {(int)((r32() % 8) << 5 | (size >> 8)), size & 0xFF}); rnd(d, (r32() % 3) ? size : r32() % 40);
    if (crc) crcAppend(d, r32() % 4 != 0);
    return d;
}
static Buf genMotDir() {
    Buf d; rnd(d, 4); const int cnt = (int)(r32() % 5); put(d, {0, cnt}); rnd(d, 5); const size_t el = r32() % 3 == 0 ? r32() % 8 : 0; put(d, {(int)(el >> 8), (int)(el & 0xFF)}); rnd(d, el);
    for (int i = 0; i < cnt; i++) { rnd(d, 2); const Buf h = genMotHeader(); cat(d, h); }
    return d;
}
static void testMot(int kind, const Buf& s) {
    exact(s, [&](const uint8_t* p, size_t n) {
        if (kind == 0) {
            A::MotTime a; R::MotTime r; const size_t ua = A::motParseTime(p, n, a), ur = R::motParseTime(p, n, r);
            SAME(ua == ur && a.valid == r.valid && a.now == r.now && a.longForm == r.longForm && a.mjd == r.mjd && a.hour == r.hour && a.minute == r.minute && a.second == r.second && a.ms == r.ms, "motTime n=%zu", n);
            SAME(A::motTimeText(a) == R::motTimeText(r), "motTimeText '%s' vs '%s'", A::motTimeText(a).c_str(), R::motTimeText(r).c_str());
            if (ua) okc[12]++;
        } else if (kind == 1) {
            A::MotHeader a; R::MotHeader r; const bool ba = A::motParseHeader(p, n, a), br = R::motParseHeader(p, n, r);
            SAME(ba == br, "motHeader ok n=%zu", n);
            if (!ba || !br) return;
            okc[13]++;
            bool e = false;
            e = a.bodySize == r.bodySize && a.headerSize == r.headerSize && a.contentType == r.contentType && a.subtype == r.subtype && a.params.size() == r.params.size() && a.hasName == r.hasName &&
                a.hasSlide == r.hasSlide && a.hasTrigger == r.hasTrigger && a.hasExpire == r.hasExpire && a.categoryId == r.categoryId && a.slideId == r.slideId && spanEq(a.name, r.name) &&
                spanEq(a.category, r.category) && spanEq(a.clickUrl, r.clickUrl) && spanEq(a.altUrl, r.altUrl) && a.caption == r.caption &&
                a.trigger.valid == r.trigger.valid && a.trigger.mjd == r.trigger.mjd && a.trigger.hour == r.trigger.hour && a.trigger.second == r.trigger.second && a.trigger.ms == r.trigger.ms && a.trigger.longForm == r.trigger.longForm && a.trigger.now == r.trigger.now &&
                a.expire.valid == r.expire.valid && a.expire.mjd == r.expire.mjd && a.expire.minute == r.expire.minute && a.expire.second == r.expire.second && a.expire.longForm == r.expire.longForm && a.expire.now == r.expire.now;
            for (size_t i = 0; e && i < a.params.size(); i++) e = a.params[i].id == r.params[i].id && spanEq(a.params[i].data, r.params[i].data);
            SAME(e, "motHeader n=%zu", n);
            if (!a.caption.empty()) okc[14]++;
        } else if (kind == 2) {
            const A::MotGroup a = A::motParseGroup(p, n); const R::MotGroup r = R::motParseGroup(p, n);
            SAME(a.status == r.status && a.last == r.last && a.type == r.type && a.segNo == r.segNo && a.tid == r.tid && a.off == r.off && a.len == r.len, "motGroup n=%zu status %d vs %d", n, a.status, r.status);
            okc[15 + (a.status == A::MotGroup::Segment ? 1 : 0)]++;
        } else {
            std::vector<A::DirEntry> a; std::vector<R::DirEntry> r; const bool ba = A::motParseDirectory(p, n, a), br = R::motParseDirectory(p, n, r);
            bool e = ba == br && a.size() == r.size();
            for (size_t i = 0; e && i < a.size(); i++) e = a[i].tid == r[i].tid && a[i].off == r[i].off && a[i].size == r[i].size;
            SAME(e, "motDir n=%zu", n); if (!a.empty()) okc[17]++;
        }
    });
}
static const char* kNames[] = {"general", "text", "image"};
static void testContentNames() { for (int t = -1; t < 12; t++) for (int s = -1; s < 5; s++) SAME(A::motContentTypeName(t, s) == R::motContentTypeName(t, s), "content type %d/%d", t, s); (void)kNames; }

// ---- PAD -------------------------------------------------------------------------------------------------------------------------------------------------
static Buf genAu() { Buf a; put(a, {(int)(0x80 | (r32() & 0x1F)), 0}); const int cnt = (int)(r32() % 3 == 0 ? 250 + r32() % 20 : 2 + r32() % 30); if (cnt >= 255) { a[1] = 255; a.push_back((uint8_t)(cnt - 255)); } else a[1] = (uint8_t)cnt; rnd(a, cnt + (r32() % 5 == 0 ? 0 : 3)); return a; }
static Buf genMp2() { Buf f; rnd(f, 8 + r32() % 40); const int ind = (int)(r32() % 4); f[f.size() - 2] = (uint8_t)((r32() % 8 ? 0 : 0x40) | ind << 4 | (r32() & 0x0F)); f[f.size() - 1] = (uint8_t)((r32() & 0xFD) | (r32() % 2) << 1); return f; }
static void testPad() {
    const long iters = getenv("ONAIR_DIFF_ITERS") ? atol(getenv("ONAIR_DIFF_ITERS")) : 200000;
    int lcA = 0, lcR = 0, ltA = 0, ltR = 0;
    for (long i = 0; i < iters; i++) {
        { const Buf s = pick(genAu); exact(s, [&](const uint8_t* p, size_t n) { A::PadBytes a; R::PadBytes r; const bool ba = A::padFromAu(p, n, a), br = R::padFromAu(p, n, r);
            SAME(ba == br && (!ba || (a.xpad == r.xpad && a.fpad[0] == r.fpad[0] && a.fpad[1] == r.fpad[1])), "padFromAu n=%zu", n); okc[18] += ba; }); }
        { const Buf s = pick(genMp2); lcA = lcR = (int)(r32() % 50); exact(s, [&](const uint8_t* p, size_t n) { A::PadBytes a; R::PadBytes r; const bool ba = A::padFromMp2(p, n, lcA, a), br = R::padFromMp2(p, n, lcR, r);
            SAME(ba == br && lcA == lcR && (!ba || (a.xpad == r.xpad && a.fpad[0] == r.fpad[0] && a.fpad[1] == r.fpad[1])), "padFromMp2 n=%zu lc %d vs %d", n, lcA, lcR); okc[19] += ba; }); }
        { Buf xp; rnd(xp, r32() % 60); const int ind = (int)(r32() % 4); uint8_t fp[2] = {(uint8_t)((r32() % 8 ? 0 : 0x40) | ind << 4), (uint8_t)((r32() & 0xFD) | (r32() % 2) << 1)};
          if (!xp.empty() && r32() % 2) for (size_t k = 0; k < 4 && k < xp.size(); k++) xp[k] = (uint8_t)(r32() % 3 ? (r32() % 32) | (r32() % 8) << 5 : 0);
          ltA = ltR = (int)(r32() % 16);
          exact(xp, [&](const uint8_t* p, size_t n) { const auto a = A::padSplit(p, n, fp, ltA); const auto r = R::padSplit(p, n, fp, ltR); bool e = a.size() == r.size() && ltA == ltR;
            for (size_t k = 0; e && k < a.size(); k++) e = a[k].type == r[k].type && a[k].off == r[k].off && a[k].len == r[k].len; SAME(e, "padSplit n=%zu", n); okc[20] += !a.empty(); }); }
        { Buf d; rnd(d, r32() % 8); if (d.size() >= 4 && r32() % 2) { const uint16_t c = A::crc16(d.data(), 2); d[2] = (uint8_t)(c >> 8); d[3] = (uint8_t)c; }
          exact(d, [&](const uint8_t* p, size_t n) { int la = 7, lr = 7; const int ra = A::padDgLength(p, n, la), rr = R::padDgLength(p, n, lr); SAME(ra == rr && la == lr, "dgLength"); okc[21] += ra == 2; }); }
        { Buf d; const int isCmd = (int)(r32() % 3 == 0); put(d, {(int)((r32() & 0xE0) | isCmd << 4 | (isCmd ? (r32() % 3 ? 1 + r32() % 2 : r32() % 16) : r32() % 16)), (int)(r32() & 0xFF)});
          rnd(d, r32() % 20); if (r32() % 4) { const size_t t = isCmd ? (size_t)(2 + (r32() % 3 ? 0 : r32() % 17)) : (size_t)(2 + (d[0] & 0x0F) + 1); if (d.size() >= t) { d.resize(t); crcAppend(d, r32() % 5 != 0); } }
          if (r32() % 5 == 0) d.resize(r32() % (d.size() + 1));
          exact(d, [&](const uint8_t* p, size_t n) { const auto a = A::dlsStep(p, n); const auto r = R::dlsStep(p, n); SAME(a.action == r.action && a.segLen == r.segLen, "dlsStep n=%zu %d/%u vs %d/%u", n, a.action, a.segLen, r.action, r.segLen); okc[22 + a.action]++;
            const auto sa = A::dlSegment(p, n); const auto sr = R::dlSegment(p, n); SAME(sa.toggle == sr.toggle && sa.first == sr.first && sa.last == sr.last && sa.cmd == sr.cmd && sa.code == sr.code && sa.segNo == sr.segNo && sa.charset == sr.charset, "dlSegment");
            bool ra = false, rr = false; std::vector<A::DlpTag> ta; std::vector<R::DlpTag> tr; const bool ba = A::dlPlusTags(p, n, ra, ta), br = R::dlPlusTags(p, n, rr, tr);
            bool e = ba == br && (!ba || (ra == rr && ta.size() == tr.size())); for (size_t k = 0; e && ba && k < ta.size(); k++) e = ta[k].type == tr[k].type && ta[k].start == tr[k].start && ta[k].len == tr[k].len; SAME(e, "dlPlusTags"); okc[25] += ba; }); }
    }
}

// ---- Journaline and packets --------------------------------------------------------------------------------------------------------------------------------
static Buf genJlGroup() { Buf d; put(d, {r32() % 8 ? 0x40 | (r32() % 3 ? 0 : 6) : (int)(r32() & 0xFF), 0}); rnd(d, r32() % 20); if (r32() % 20 == 0) rnd(d, 4100 + r32() % 20); crcAppend(d, r32() % 5 != 0); return d; }
static Buf genToc() { Buf d; put(d, {0x54, (int)(r32() & 0xFF), (int)(1 + r32() % 4), (int)(r32() % 5), 0, 0, 0, (int)(r32() % 100), 0, (int)(r32() % 100)}); const size_t el = r32() % 6; put(d, {(int)(r32() % 8 ? 3 + r32() % 3 : r32() % 3), 0, (int)el}); rnd(d, el); rnd(d, r32() % 40); return d; }
static Buf genDabPkt() { Buf p; const size_t len = 24 * (1 + r32() % 4); rnd(p, len - 2); p[2] = (uint8_t)((r32() % 4 ? 0 : 0x80) | (r32() % (len - 4))); crcAppend(p, r32() % 4 != 0); return p; }
static Buf genDrmPkt() { Buf p; const size_t len = 4 + r32() % 60; rnd(p, len - 2); if (p[0] & 0x08) p[1] = (uint8_t)(r32() % 4 ? r32() % (len > 4 ? len - 4 : 1) : r32()); crcAppend(p, r32() % 4 != 0); return p; }
static void testJl(int kind, const Buf& s) {
    exact(s, [&](const uint8_t* p, size_t n) {
        if (kind == 0) { const auto a = A::jlDataGroup(p, n); const auto r = R::jlDataGroup(p, n); SAME(a.status == r.status && (a.status != 0 || (a.type == r.type && a.fieldLen == r.fieldLen)), "jlGroup n=%zu", n); okc[26 + a.status]++; }
        else if (kind == 1) { A::JlTocInfo a; R::JlTocInfo r; std::vector<A::JlTocEntry> ea; std::vector<R::JlTocEntry> er; const bool ba = A::jlParseToc(p, n, a, ea), br = R::jlParseToc(p, n, r, er);
            bool e = ba == br; if (e && ba) e = a.revision == r.revision && a.blockCount == r.blockCount && a.blockIndex == r.blockIndex && a.objectCount == r.objectCount && a.timeoutMin == r.timeoutMin && ea.size() == er.size();
            for (size_t i = 0; e && ba && i < ea.size(); i++) e = ea[i].id == er[i].id && ea[i].desc == er[i].desc; SAME(e, "jlToc n=%zu", n); okc[29] += ba;
            int va = 0, xa = 0, na = 0, vr = 0, xr = 0, nr = 0; const bool ua = A::jlUserAppData(p, n, va, xa, na), ur = R::jlUserAppData(p, n, vr, xr, nr); SAME(ua == ur && (!ua || (va == vr && xa == xr && na == nr)), "jlUserApp"); }
        else if (kind == 2) { if (n < 5) return; const auto a = A::dabPacket(p, n); const auto r = R::dabPacket(p, n); SAME(a.crcOk == r.crcOk && a.first == r.first && a.last == r.last && a.cmd == r.cmd && a.ci == r.ci && a.addr == r.addr && a.ul == r.ul, "dabPacket"); okc[30] += a.crcOk; }
        else { if (n < 4) return; const auto a = A::drmPacket(p, n); const auto r = R::drmPacket(p, n); SAME(a.crcOk == r.crcOk && a.first == r.first && a.last == r.last && a.lenBad == r.lenBad && a.id == r.id && a.ci == r.ci && a.n == r.n && a.at == r.at, "drmPacket"); okc[31] += a.crcOk; }
    });
}

int main() {
    const long iters = getenv("ONAIR_DIFF_ITERS") ? atol(getenv("ONAIR_DIFF_ITERS")) : 200000;
    SAME(A::crc16((const uint8_t*)"123456789", 9) == 0xD64E && R::crc16((const uint8_t*)"123456789", 9) == 0xD64E, "crc check value");
    testContentNames();
    for (long i = 0; i < iters; i++) {
        testFig(pick(genFig0)); testFig(pick(genFig1)); testFig(pick(genFig2));
        testFib(pick(genFib));
        testMot(0, [] { Buf t = motTime(); return r32() % 6 ? t : mutate(t); }());
        testMot(1, pick(genMotHeader)); testMot(2, pick(genMotGroup)); testMot(3, pick(genMotDir));
        testJl(0, pick(genJlGroup)); testJl(1, pick(genToc)); testJl(2, pick(genDabPkt)); testJl(3, pick(genDrmPkt));
    }
    testPad();
    printf("dab proto: %ld checks, %ld mismatches (ok: eid %ld sub %ld pkt %ld svc %ld apps %ld pty %ld asup %ld asw %ld time %ld fig1 %ld fig2 %ld fib %ld mottime %ld hdr %ld caption %ld group %ld/%ld dir %ld au %ld mp2 %ld split %ld dg %ld dls %ld %ld %ld dlp %ld jl %ld %ld %ld toc %ld dabpkt %ld drmpkt %ld)\n", checks, fails,
           okc[0], okc[1], okc[2], okc[3], okc[4], okc[5], okc[6], okc[7], okc[8], okc[9], okc[10], okc[11], okc[12], okc[13], okc[14], okc[15], okc[16], okc[17], okc[18], okc[19], okc[20], okc[21], okc[22], okc[23], okc[24], okc[25], okc[26], okc[27], okc[28], okc[29], okc[30], okc[31]);
    return fails ? 1 : 0;
}
