// Differential test: the C++ DVB service information layers (generated copies in dect2::text_ref, dect2::dvbsi_ref and dect2_ref, see cmake/gen_rs_ref.cmake)
// against the ones the program uses (the Rust crate onair_dvbsi when DECT2_RUST is on). The same inputs go to both and every result has to be identical:
// broadcast character sets and the DVB text field, Hamming codes, G0 subsets, teletext data units, the page renderer and a whole TeletextDecoder fed with
// PES packets, DVB subtitle segment parsers, colours, run-length pixel strings, blending and a whole DvbSubDecoder fed with PES packets, over ONAIR_DIFF_ITERS
// (default 200000) seeded inputs per family.
#include "dect2/common/text_charset.h"
#include "dect2/tv/dvb_text.h"
#include "dect2/tv/dvbsub.h"
#include "dect2/tv/teletext.h"
#include "dvb_text_ref.h"
#include "dvbsub_ref.h"
#include "teletext_ref.h"
#include "text_charset_ref.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
namespace A = dect2;
namespace R = dect2_ref;
namespace AT = dect2::text;
namespace RT = dect2::text_ref;

static long fails = 0, checks = 0;
static long stat[16];
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)
static std::mt19937 g(777);
static uint32_t r32() { return g(); }
using Buf = std::vector<uint8_t>;
static void rnd(Buf& b, size_t n) { for (size_t i = 0; i < n; i++) b.push_back((uint8_t)r32()); }
static void put(Buf& b, std::initializer_list<int> v) { for (int x : v) b.push_back((uint8_t)x); }
static void cat(Buf& a, const Buf& b) { a.insert(a.end(), b.begin(), b.end()); }

// An exactly sized heap copy, so a sanitizer build sees reads past the end.
template <class F> static void exact(const Buf& s, F f) {
    uint8_t* p = s.empty() ? nullptr : (uint8_t*)malloc(s.size());
    if (p) memcpy(p, s.data(), s.size());
    f(p, s.size());
    free(p);
}

// ---- character sets --------------------------------------------------------------------------------------------------------------------------------------
static Buf textBytes(bool prefixed) {
    Buf b;
    if (prefixed) {
        static const int pre[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x1F, 0x20, 0x41, 0xA1, 0xC1, 0xFF};
        b.push_back((uint8_t)pre[r32() % 26]);
        if (b[0] == 0x10) put(b, {0x00, (int)(r32() % 16)});
    }
    const size_t n = r32() % 60;
    for (size_t i = 0; i < n; i++) {
        switch (r32() % 8) {
        case 0: b.push_back((uint8_t)(0xC1 + r32() % 15)); b.push_back((uint8_t)(0x20 + r32() % 96)); break;           // accent + letter
        case 1: b.push_back((uint8_t)(0xA1 + r32() % 94)); b.push_back((uint8_t)(0xA1 + r32() % 94)); break;           // GB 2312 pair
        case 2: b.push_back(0x8A); break;
        case 3: put(b, {0xC2, 0x80 + (int)(r32() % 32)}); break;                                                        // C1 control in UTF-8
        case 4: b.push_back((uint8_t)(r32() % 3 ? 0x20 : 0x0A)); break;
        case 5: { const uint32_t c = r32() % 6; static const Buf u[] = {{0xE2, 0x80, 0x8B}, {0xEF, 0xBB, 0xBF}, {0xC2, 0xA0}, {0xF0, 0x9F, 0x98, 0x80}, {0xC0, 0x80}, {0xED, 0xA0, 0x80}}; cat(b, u[c]); break; }
        default: b.push_back((uint8_t)r32());
        }
    }
    return b;
}

static void testText() {
    for (int c = 0; c < 256; c++) {
        const uint8_t one[2] = {(uint8_t)c, (uint8_t)(c ^ 0x55)};
        SAME(AT::ebuLatin(one, 2) == RT::ebuLatin(one, 2) && AT::iso6937(one, 2) == RT::iso6937(one, 2) && AT::ucs2be(one, 2) == RT::ucs2be(one, 2), "byte %d", c);
    }
    SAME(AT::ebuLatin(nullptr, 0) == RT::ebuLatin(nullptr, 0) && AT::clean("") == RT::clean(""), "empty");
    long iters = getenv("ONAIR_DIFF_ITERS") ? atol(getenv("ONAIR_DIFF_ITERS")) : 200000;
    for (long i = 0; i < iters; i++) {
        const Buf b = textBytes(false);
        exact(b, [&](const uint8_t* p, size_t n) {
            const std::string e1 = AT::ebuLatin(p, n), e2 = RT::ebuLatin(p, n);
            SAME(e1 == e2, "ebuLatin n=%zu", n);
            SAME(AT::iso6937(p, n) == RT::iso6937(p, n), "iso6937 n=%zu", n);
            SAME(AT::ucs2be(p, n) == RT::ucs2be(p, n), "ucs2be n=%zu", n);
            for (int cs : {0, 4, 6, 15, 3, 99, -1}) SAME(AT::dabCharset(cs, p, n) == RT::dabCharset(cs, p, n), "dabCharset %d n=%zu", cs, n);
            const std::string s((const char*)p, n);
            const std::string c1 = AT::clean(s), c2 = RT::clean(s);
            SAME(c1 == c2, "clean n=%zu", n);
            if (!c1.empty()) stat[0]++;
        });
        const Buf t = textBytes(true);
        exact(t, [&](const uint8_t* p, size_t n) {
            for (bool gb : {false, true}) {
                const std::string a = A::dvbsi::dvbText(p, (int)n, gb), r = A::dvbsi_ref::dvbText(p, (int)n, gb);
                SAME(a == r, "dvbText first=%02x n=%zu gb=%d: '%s' vs '%s'", n ? p[0] : 0, n, gb, a.c_str(), r.c_str());
                if (!a.empty()) stat[1]++;
            }
        });
    }
    SAME(A::dvbsi::dvbText(nullptr, -3, false) == A::dvbsi_ref::dvbText(nullptr, -3, false), "negative length");
}

// ---- teletext --------------------------------------------------------------------------------------------------------------------------------------------
static uint8_t hammEnc(int nib) {   // a transmitted Hamming 8/4 byte, found through the decoder
    for (int b = 0; b < 256; b++) { bool ok = A::ttxHamming84((uint8_t)b) == nib; for (int k = 0; ok && k < 8; k++) ok = A::ttxHamming84((uint8_t)(b ^ (1 << k))) == nib; if (ok) return (uint8_t)b; }
    return 0;
}
static uint8_t flip(uint8_t b) { return r32() % 6 == 0 ? (uint8_t)(b ^ (1 << (r32() % 8))) : r32() % 40 == 0 ? (uint8_t)r32() : b; }
// A 44 byte data unit (not yet bit-reversed fields: built in the logical order, then reversed as in the PES).
static Buf teletextUnit(int mag, int row, bool header, int page) {
    uint8_t b[42];
    const int addr = (mag & 7) | row << 3;
    b[0] = flip(hammEnc(addr & 15)); b[1] = flip(hammEnc(addr >> 4));
    if (header) {
        b[2] = flip(hammEnc(page % 10)); b[3] = flip(hammEnc(page / 10 % 10));
        for (int i = 4; i < 10; i++) b[i] = flip(hammEnc((int)(r32() % 16)));
        for (int i = 10; i < 42; i++) b[i] = (uint8_t)r32();
    } else if (row == 27) {
        b[2] = flip(hammEnc(r32() % 6 ? 0 : (int)(r32() % 4)));
        for (int i = 3; i < 42; i++) b[i] = flip(hammEnc((int)(r32() % 16)));
        if (r32() % 4 == 0) { const int l = (int)(r32() % 6); b[3 + l * 6] = hammEnc(15); b[4 + l * 6] = hammEnc(15); }
    } else {
        for (int i = 2; i < 42; i++) b[i] = (uint8_t)(r32() % 3 ? 0x20 + r32() % 96 : r32());
    }
    Buf u{(uint8_t)(r32() % 3 ? 0xC0 | (r32() % 32) : r32()), (uint8_t)(r32() % 20 ? 0xE4 : r32() % 2 ? 0x27 : r32())};
    for (int i = 0; i < 42; i++) u.push_back(A::ttxReverse8(b[i]));
    return u;
}
template <class T, class U> static bool unitEq(const T& a, const U& b) {
    bool e = a.kind == b.kind && a.serial == b.serial && a.pageOk == b.pageOk && a.erase == b.erase && a.newsflash == b.newsflash && a.subtitle == b.subtitle && a.inhibit == b.inhibit &&
             a.mag == b.mag && a.row == b.row && a.number == b.number && a.subcode == b.subcode && a.national == b.national && !memcmp(a.head, b.head, 32) && !memcmp(a.rowData, b.rowData, 40);
    for (int i = 0; i < 6; i++) e = e && a.linkAct[i] == b.linkAct[i] && a.linkPage[i] == b.linkPage[i] && a.linkSub[i] == b.linkSub[i];
    return e;
}
static Buf teletextPes(const std::vector<Buf>& units) {
    Buf p; put(p, {0, 0, 1, 0xBD, 0, 0, 0x80, 0x00, 0}); p[8] = (uint8_t)(r32() % 4 ? 0 : r32() % 8); rnd(p, p[8]);
    p.push_back((uint8_t)(r32() % 20 ? 0x10 + r32() % 16 : r32()));
    for (const Buf& u : units) { p.push_back((uint8_t)(r32() % 10 ? 0x02 + (r32() % 2) : r32())); p.push_back((uint8_t)(r32() % 20 ? 44 : r32() % 60)); cat(p, u); }
    return p;
}

static void testTeletext() {
    for (int b = 0; b < 256; b++) SAME(A::ttxHamming84((uint8_t)b) == R::ttxHamming84((uint8_t)b) && A::ttxReverse8((uint8_t)b) == R::ttxReverse8((uint8_t)b), "ham %d", b);
    for (int c = 0; c < 256; c++) for (int n = -2; n < 10; n++) SAME(A::ttxG0((uint8_t)c, n) == R::ttxG0((uint8_t)c, n), "g0 %d %d", c, n);
    const long iters = getenv("ONAIR_DIFF_ITERS") ? atol(getenv("ONAIR_DIFF_ITERS")) : 200000;
    for (long i = 0; i < iters; i++) {
        uint8_t w[3] = {(uint8_t)r32(), (uint8_t)r32(), (uint8_t)r32()};
        SAME(A::ttxHamming2418(w) == R::ttxHamming2418(w), "2418 %02x%02x%02x", w[0], w[1], w[2]);
        if (A::ttxHamming2418(w) >= 0) stat[2]++;
        // data units
        const Buf u = (r32() % 4 == 0) ? [&] { Buf x; rnd(x, 44); return x; }() : teletextUnit((int)(r32() % 9), r32() % 3 == 0 ? 0 : r32() % 4 == 0 ? 27 : (int)(r32() % 32), false, 0);
        const Buf hu = teletextUnit((int)(r32() % 9), 0, true, (int)(r32() % 100));
        for (const Buf* x : {&u, &hu}) {
            exact(*x, [&](const uint8_t* p, size_t) {
                const A::TtxUnit a = A::ttxDecodeUnit(p); const R::TtxUnit r = R::ttxDecodeUnit(p);
                SAME(unitEq(a, r), "unit kind %d vs %d", a.kind, r.kind);
                if (a.kind >= 1 && a.kind <= 3) stat[3 + a.kind - 1]++;
            });
        }
        // PES walk
        std::vector<Buf> units; const int nu = (int)(r32() % 4);
        for (int k = 0; k < nu; k++) units.push_back(r32() % 3 ? teletextUnit((int)(r32() % 9), (int)(r32() % 30), r32() % 3 == 0, (int)(r32() % 100)) : Buf(44, (uint8_t)r32()));
        Buf pes = teletextPes(units);
        if (r32() % 4 == 0) pes.resize(r32() % (pes.size() + 1));
        exact(pes, [&](const uint8_t* p, size_t n) {
            uint32_t oa[16], orr[16];
            const size_t ca = A::ttxPesUnits(p, n, oa, 16), cr = R::ttxPesUnits(p, n, orr, 16);
            SAME(ca == cr && !memcmp(oa, orr, sizeof(uint32_t) * (ca < 16 ? ca : 16)), "pes units n=%zu %zu vs %zu", n, ca, cr);
            if (ca) stat[6]++;
        });
        // renderer
        A::TtxPage pa; R::TtxPage pr;
        pa.national = pr.national = (int)(r32() % 9) - 1;
        pa.rowMask = pr.rowMask = r32() % 5 ? r32() : 0xFFFFFFFFu;
        for (int r = 0; r < 25; r++) for (int c = 0; c < 40; c++) {
            const uint8_t v = (uint8_t)(r32() % 3 == 0 ? r32() % 32 : r32() % 5 == 0 ? r32() : 0x20 + r32() % 96);
            pa.rows[r][c] = pr.rows[r][c] = v;
        }
        A::TtxGrid ga; R::TtxGrid gr;
        A::ttxRender(pa, ga); R::ttxRender(pr, gr);
        bool eq = true;
        for (int r = 0; r < 25 && eq; r++) {
            for (int c = 0; c < 40; c++) { const auto &x = ga[r][c]; const auto &y = gr[r][c]; if (x.ch != y.ch || x.mosaic != y.mosaic || x.sextants != y.sextants || x.separated != y.separated || x.fg != y.fg || x.bg != y.bg || x.dh != y.dh || x.flash != y.flash || x.conceal != y.conceal || x.boxed != y.boxed) { eq = false; break; } }
            if (A::ttxRowText(ga, r) != R::ttxRowText(gr, r)) eq = false;
        }
        SAME(eq, "render national=%d", pa.national);
        SAME(A::ttxRowText(ga, -1) == R::ttxRowText(gr, -1) && A::ttxRowText(ga, 25) == R::ttxRowText(gr, 25), "row text bounds");
    }
    // whole decoders on the same PES stream
    for (long i = 0; i < iters / 40; i++) {
        A::TeletextDecoder da; R::TeletextDecoder dr;
        da.setPid(100); dr.setPid(100);
        const int np = (int)(1 + r32() % 12);
        for (int k = 0; k < np; k++) {
            std::vector<Buf> units;
            const int mag = (int)(r32() % 3), pg = (int)(r32() % 10);
            if (r32() % 3) units.push_back(teletextUnit(mag, 0, true, pg));
            const int nr = (int)(r32() % 8);
            for (int q = 0; q < nr; q++) units.push_back(teletextUnit(mag, r32() % 5 == 0 ? 27 : (int)(1 + r32() % 24), false, 0));
            if (r32() % 5 == 0) units.push_back(teletextUnit((int)(r32() % 8), 0, true, (int)(r32() % 100)));
            Buf pes = teletextPes(units);
            if (r32() % 10 == 0) pes.resize(r32() % (pes.size() + 1));
            da.feedPes(pes.data(), pes.size()); dr.feedPes(pes.data(), pes.size());
        }
        SAME(da.packets() == dr.packets() && da.pages() == dr.pages(), "decoder pages %zu vs %zu, packets %llu vs %llu", da.pages().size(), dr.pages().size(), (unsigned long long)da.packets(), (unsigned long long)dr.packets());
        for (int n : da.pages()) {
            SAME(da.subpages(n) == dr.subpages(n), "subpages %d", n);
            for (int sub : da.subpages(n)) {
                A::TtxPage x; R::TtxPage y;
                const bool ba = da.page(n, x, sub), br = dr.page(n, y, sub);
                SAME(ba == br && x.number == y.number && x.subcode == y.subcode && x.national == y.national && x.subtitle == y.subtitle && x.newsflash == y.newsflash && x.inhibit == y.inhibit &&
                     x.rows == y.rows && x.rowMask == y.rowMask && x.version == y.version && x.serial == y.serial && x.complete == y.complete && !memcmp(x.fastext, y.fastext, sizeof x.fastext) &&
                     !memcmp(x.fastextSub, y.fastextSub, sizeof x.fastextSub), "decoder page %d/%d", n, sub);
                stat[7]++;
            }
        }
    }
}

// ---- DVB subtitles ---------------------------------------------------------------------------------------------------------------------------------------
static Buf pixelBlock() {   // sub-blocks of pixel data: random code strings, map tables, line ends
    Buf f;
    const int n = (int)(r32() % 6);
    for (int k = 0; k < n; k++) {
        switch (r32() % 7) {
        case 0: f.push_back(0xF0); break;
        case 1: put(f, {0x20, (int)(r32() & 0xFF), (int)(r32() & 0xFF)}); break;
        case 2: f.push_back(0x21); rnd(f, 4); break;
        case 3: f.push_back(0x22); rnd(f, 16); break;
        case 4: if (r32() % 15 == 0) f.push_back((uint8_t)r32()); break;
        default: { f.push_back((uint8_t)(0x10 + r32() % 3)); rnd(f, r32() % 30); if (r32() % 2) put(f, {0, 0}); }
        }
    }
    return f;
}
static Buf segment(int type, int page, const Buf& d) { Buf s{0x0F, (uint8_t)type, (uint8_t)(page >> 8), (uint8_t)page, (uint8_t)(d.size() >> 8), (uint8_t)d.size()}; cat(s, d); return s; }
static Buf pageSeg() { Buf d{(uint8_t)(r32() % 40), (uint8_t)(((r32() % 16) << 4) | ((r32() % 4) << 2))}; const int n = (int)(r32() % 4); for (int i = 0; i < n; i++) put(d, {(int)(r32() % 3), 0, (int)(r32() % 2), (int)(r32() & 0xFF), 0, (int)(r32() % 20)}); if (r32() % 8 == 0) rnd(d, r32() % 5); return d; }
static Buf regionSeg() {
    Buf d{(uint8_t)(r32() % 3), (uint8_t)(((r32() % 16) << 4) | ((r32() % 2) << 3)), 0, (uint8_t)(1 + r32() % 40), 0, (uint8_t)(1 + r32() % 20), (uint8_t)(((r32() % 4) << 2)), (uint8_t)(r32() % 3), (uint8_t)r32(), (uint8_t)r32()};
    if (r32() % 12 == 0) { d[2] = (uint8_t)r32(); d[4] = (uint8_t)r32(); }
    const int n = (int)(r32() % 4);
    for (int i = 0; i < n; i++) { put(d, {0, (int)(r32() % 3), (int)((r32() % 3) << 6 | (r32() % 3)), (int)(r32() % 30), 0, (int)(r32() % 10)}); if ((d[d.size() - 4] >> 6) != 0 && (d[d.size() - 4] >> 6) != 3) rnd(d, 2); }
    return d;
}
static Buf clutSeg() { Buf d{(uint8_t)(r32() % 3), (uint8_t)(r32() % 16 << 4)}; const int n = (int)(r32() % 8); for (int i = 0; i < n; i++) { const bool full = r32() % 2; put(d, {(int)(r32() % 20), (int)((r32() % 8) << 5 | (r32() % 2 ? 0x1 : 0) * full | 0)}); if (full) { d[d.size() - 1] |= 1; rnd(d, 4); } else rnd(d, 2); } return d; }
static Buf objectSeg() { Buf top = pixelBlock(), bot = r32() % 3 ? pixelBlock() : Buf(); Buf d{0, (uint8_t)(r32() % 3), (uint8_t)(r32() % 8 ? 0 : (r32() % 4) << 2), (uint8_t)(top.size() >> 8), (uint8_t)top.size(), (uint8_t)(bot.size() >> 8), (uint8_t)bot.size()}; cat(d, top); cat(d, bot); return d; }
static Buf subtitlePes(int page) {
    Buf p; put(p, {0, 0, 1, 0xBD, 0, 0, 0x80, r32() % 4 ? 0x80 : 0x00, 5}); p.push_back((uint8_t)(0x21 | ((r32() % 8) << 1))); rnd(p, 4); p[p.size() - 4] &= 0xFE; p[p.size() - 2] |= 1;
    if (r32() % 5 == 0) { p[8] = (uint8_t)(r32() % 4); p.resize(9 + p[8]); }
    put(p, {r32() % 30 ? 0x20 : (int)r32(), 0});
    const int n = (int)(1 + r32() % 7);
    for (int i = 0; i < n; i++) {
        const int pg = r32() % 6 ? page : (int)(r32() % 4);
        switch (r32() % 7) {
        case 0: cat(p, segment(0x10, pg, pageSeg())); break;
        case 1: cat(p, segment(0x11, pg, regionSeg())); break;
        case 2: cat(p, segment(0x12, pg, clutSeg())); break;
        case 3: case 4: cat(p, segment(0x13, pg, objectSeg())); break;
        case 5: cat(p, segment(0x14, pg, Buf{0, 0, (uint8_t)(r32() % 3 ? 2 : r32()), 0, (uint8_t)(r32() % 3 ? 0xFF : r32())})); break;
        default: cat(p, segment(0x80, pg, Buf())); break;
        }
    }
    if (r32() % 6 == 0) cat(p, segment(0x80, page, Buf()));
    if (r32() % 8 == 0) p.resize(r32() % (p.size() + 1));
    return p;
}

static void testSubs() {
    const long iters = getenv("ONAIR_DIFF_ITERS") ? atol(getenv("ONAIR_DIFF_ITERS")) : 200000;
    { uint32_t a2[4], a4[16], a8[256], b2[4], b4[16], b8[256]; A::dvbSubDefaultClut(a2, a4, a8); R::dvbSubDefaultClut(b2, b4, b8); SAME(!memcmp(a2, b2, sizeof a2) && !memcmp(a4, b4, sizeof a4) && !memcmp(a8, b8, sizeof a8), "default clut"); }
    for (int y = 0; y < 256; y += 1) for (int cr = 0; cr < 256; cr += 17) for (int cb = 0; cb < 256; cb += 15) { const int t = (y * 7 + cr) & 0xFF; SAME(A::dvbSubYcrcb(y, cr, cb, t) == R::dvbSubYcrcb(y, cr, cb, t), "ycrcb %d %d %d %d", y, cr, cb, t); }
    for (long i = 0; i < iters; i++) {
        const int yy = (int)(r32() % 256), cr = (int)(r32() % 256), cb = (int)(r32() % 256), tt = (int)(r32() % 300) - 20;
        SAME(A::dvbSubYcrcb(yy, cr, cb, tt) == R::dvbSubYcrcb(yy, cr, cb, tt), "ycrcb %d %d %d %d", yy, cr, cb, tt);
        // PES + segments + parsers
        Buf pes = subtitlePes(1);
        exact(pes, [&](const uint8_t* p, size_t n) {
            A::DvbSubPesInfo a; R::DvbSubPesInfo r;
            const bool ba = A::dvbSubPesInfo(p, n, a), br = R::dvbSubPesInfo(p, n, r);
            SAME(ba == br && (!ba || (a.payloadOff == r.payloadOff && a.hasPts == r.hasPts && memcmp(&a.pts, &r.pts, 8) == 0)), "pes info n=%zu", n);
            if (!ba) return;
            stat[8]++;
            const uint8_t* d = p + a.payloadOff; const size_t dn = n - a.payloadOff;
            const auto sa = A::dvbSubSegments(d, dn); const auto sr = R::dvbSubSegments(d, dn);
            bool eq = sa.size() == sr.size();
            for (size_t k = 0; eq && k < sa.size(); k++) eq = sa[k].type == sr[k].type && sa[k].page == sr[k].page && sa[k].off == sr[k].off && sa[k].len == sr[k].len;
            SAME(eq, "segments %zu vs %zu", sa.size(), sr.size());
            for (size_t k = 0; eq && k < sa.size(); k++) {
                const uint8_t* s = d + sa[k].off; const int sl = (int)sa[k].len;
                if (sa[k].type == 0x10) { A::DvbSubPageInfo x; R::DvbSubPageInfo y; const bool u = A::dvbSubParsePage(s, sl, x), v = R::dvbSubParsePage(s, sl, y);
                    bool e = u == v && (!u || (x.timeout == y.timeout && x.version == y.version && x.state == y.state && x.regions.size() == y.regions.size()));
                    for (size_t q = 0; e && u && q < x.regions.size(); q++) e = x.regions[q].id == y.regions[q].id && x.regions[q].x == y.regions[q].x && x.regions[q].y == y.regions[q].y;
                    SAME(e, "page comp"); stat[9]++; }
                if (sa[k].type == 0x11) { A::DvbSubRegionInfo x; R::DvbSubRegionInfo y; const bool u = A::dvbSubParseRegion(s, sl, x), v = R::dvbSubParseRegion(s, sl, y);
                    bool e = u == v && (!u || (x.id == y.id && x.version == y.version && x.fill == y.fill && x.w == y.w && x.h == y.h && x.depth == y.depth && x.clut == y.clut && x.pix8 == y.pix8 && x.pix4 == y.pix4 && x.pix2 == y.pix2 && x.objs.size() == y.objs.size()));
                    for (size_t q = 0; e && u && q < x.objs.size(); q++) e = x.objs[q].id == y.objs[q].id && x.objs[q].type == y.objs[q].type && x.objs[q].x == y.objs[q].x && x.objs[q].y == y.objs[q].y && x.objs[q].fg == y.objs[q].fg && x.objs[q].bg == y.objs[q].bg;
                    SAME(e, "region comp"); stat[10]++; }
                if (sa[k].type == 0x12) { int ia = -1, ir = -1; std::vector<A::DvbSubClutEntry> x; std::vector<R::DvbSubClutEntry> y; const bool u = A::dvbSubParseClut(s, sl, ia, x), v = R::dvbSubParseClut(s, sl, ir, y);
                    bool e = u == v && (!u || (ia == ir && x.size() == y.size()));
                    for (size_t q = 0; e && u && q < x.size(); q++) e = x[q].id == y[q].id && x[q].flags == y[q].flags && x[q].rgba == y[q].rgba;
                    SAME(e, "clut"); stat[11]++; }
                if (sa[k].type == 0x13) { A::DvbSubObjectInfo x; R::DvbSubObjectInfo y; const bool u = A::dvbSubParseObject(s, sl, x), v = R::dvbSubParseObject(s, sl, y);
                    SAME(u == v && (!u || (x.id == y.id && x.topOff == y.topOff && x.topLen == y.topLen && x.botOff == y.botOff && x.botLen == y.botLen)), "object"); if (u) stat[12]++; }
                if (sa[k].type == 0x14) { int w1 = -1, h1 = -1, w2 = -1, h2 = -1; const bool u = A::dvbSubParseDisplay(s, sl, w1, h1), v = R::dvbSubParseDisplay(s, sl, w2, h2); SAME(u == v && (!u || (w1 == w2 && h1 == h2)), "display"); }
            }
        });
        // the pixel decoder, region conversion and blending
        const Buf f = pixelBlock();
        static const int depths[3] = {2, 4, 8};
        const int depth = depths[r32() % 3], w = (int)(1 + r32() % 30), h = (int)(1 + r32() % 12);
        std::vector<uint8_t> pa((size_t)w * h), pr;
        for (auto& v : pa) v = (uint8_t)(r32() % 3 ? 0 : r32());
        pr = pa;
        const int x0 = (int)(r32() % 40) - 8, y0 = (int)(r32() % 16) - 4;
        const bool dbl = r32() % 2;
        exact(f, [&](const uint8_t* p, size_t n) { A::dvbSubDecodeField(p, n, x0, y0, 2, dbl, w, h, depth, pa.data()); R::dvbSubDecodeField(p, n, x0, y0, 2, dbl, w, h, depth, pr.data()); });
        SAME(pa == pr, "decode field depth=%d", depth);
        uint32_t c2[4], c4[16], c8[256];
        for (auto& v : c2) v = r32(); for (auto& v : c4) v = r32(); for (auto& v : c8) v = r32();
        std::vector<uint8_t> ra(pa.size() * 4), rr(pa.size() * 4);
        A::dvbSubRegionRgba(pa.data(), pa.size(), depth, c2, c4, c8, ra.data()); R::dvbSubRegionRgba(pa.data(), pa.size(), depth, c2, c4, c8, rr.data());
        SAME(ra == rr, "region rgba");
        A::DvbSubDisplay da; R::DvbSubDisplay dr;
        da.dispW = dr.dispW = (int)(8 + r32() % 40); da.dispH = dr.dispH = (int)(4 + r32() % 20);
        const int nreg = (int)(r32() % 4);
        for (int q = 0; q < nreg; q++) {
            A::DvbSubRegionImage ia; R::DvbSubRegionImage ir; ia.x = ir.x = (int)(r32() % 50) - 10; ia.y = ir.y = (int)(r32() % 25) - 5; ia.w = ir.w = w; ia.h = ir.h = h;
            ia.rgba.resize((size_t)w * h * 4); for (auto& v : ia.rgba) v = (uint8_t)(r32() % 3 ? 255 : r32() % 2 ? 0 : r32()); ir.rgba = ia.rgba;
            for (size_t k = 3; k < ia.rgba.size(); k += 4) if (r32() % 4 == 0) ia.rgba[k] = ir.rgba[k] = (uint8_t)r32();
            da.regions.push_back(ia); dr.regions.push_back(ir);
        }
        std::vector<uint8_t> oa, orr;
        A::dvbSubComposite(da, oa); R::dvbSubComposite(dr, orr);
        SAME(oa == orr, "composite");
        const double pa1 = (double)(r32() % 100000000) / 1000.0, pb1 = (double)(r32() % 100000000) / 1000.0;
        const double d1 = A::ptsDelta(pa1, pb1), d2 = R::ptsDelta(pa1, pb1);
        SAME(memcmp(&d1, &d2, 8) == 0, "ptsDelta");
    }
    // whole decoders on the same PES stream
    for (long i = 0; i < iters / 40; i++) {
        A::DvbSubDecoder da; R::DvbSubDecoder dr;
        da.setStream(1, 1, 2); dr.setStream(1, 1, 2);
        const int np = (int)(1 + r32() % 8);
        for (int k = 0; k < np; k++) { const Buf pes = subtitlePes(r32() % 4 ? 1 : 2); da.feedPes(pes.data(), pes.size()); dr.feedPes(pes.data(), pes.size()); }
        SAME(da.displays() == dr.displays(), "displays %llu vs %llu", (unsigned long long)da.displays(), (unsigned long long)dr.displays());
        for (double clk : {-1.0, 0.0, 1.0, 1000.0, 90000.0, 20000.5}) {
            const auto x = da.displayAt(clk); const auto y = dr.displayAt(clk);
            bool eq = (x == nullptr) == (y == nullptr);
            if (eq && x) {
                eq = x->seq == y->seq && memcmp(&x->pts, &y->pts, 8) == 0 && x->timeout == y->timeout && x->dispW == y->dispW && x->dispH == y->dispH && x->regions.size() == y->regions.size();
                for (size_t q = 0; eq && q < x->regions.size(); q++) eq = x->regions[q].x == y->regions[q].x && x->regions[q].y == y->regions[q].y && x->regions[q].w == y->regions[q].w && x->regions[q].h == y->regions[q].h && x->regions[q].rgba == y->regions[q].rgba;
                if (x->regions.size()) stat[13]++;
            }
            SAME(eq, "displayAt %.1f", clk);
        }
    }
}

int main() {
    testText();
    testTeletext();
    testSubs();
    printf("dvbsi proto: %ld checks, %ld mismatches (non-empty text %ld %ld, 2418 ok %ld, units %ld %ld %ld, pes %ld, decoder pages %ld, sub pes %ld page %ld region %ld clut %ld object %ld, decoder displays with regions %ld)\n", checks, fails,
           stat[0], stat[1], stat[2], stat[3], stat[4], stat[5], stat[6], stat[7], stat[8], stat[9], stat[10], stat[11], stat[12], stat[13]);
    return fails ? 1 : 0;
}
