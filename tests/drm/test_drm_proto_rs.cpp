// Differential test: the C++ DRM signalling and data layers (a generated copy in dect2::drmp_ref, see cmake/gen_rs_ref.cmake) against the one the program uses
// (dect2::drmp: the Rust crate onair_drm when DECT2_RUST is on). The same inputs go to both and every result has to be identical: the CRCs, FAC blocks (both
// robustness mode families, good and corrupted), SDC data fields (every entity type, parsed twice into the same record so the in-place refinement is compared),
// SDC block checks, the audio super frame split (consistent and corrupt borders), the text message segment check, the tables, MJD conversion and the data
// service helpers, over ONAIR_DIFF_ITERS (default 200000) seeded inputs per family. Buffers are exactly as long as the input (sanitizer builds see overruns).
#include "dect2/drm/drm_proto.h"
#include "drm_proto_ref.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
namespace A = dect2::drmp;
namespace R = dect2::drmp_ref;
namespace D = dect2::drm;

static long fails = 0, checks = 0;
static long okc[16];
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)
static std::mt19937 g(31415);
static uint32_t r32() { return g(); }
using Buf = std::vector<uint8_t>;
static void rnd(Buf& b, size_t n) { for (size_t i = 0; i < n; i++) b.push_back((uint8_t)r32()); }
static void putBits(Buf& b, uint32_t v, int n) { for (int i = n - 1; i >= 0; i--) b.push_back((uint8_t)((v >> i) & 1)); }
template <class F> static void exact(const Buf& s, F f) {
    uint8_t* p = s.empty() ? nullptr : (uint8_t*)malloc(s.size());
    if (p) memcpy(p, s.data(), s.size());
    f(p, s.size());
    free(p);
}
static Buf flipBits(Buf b, int maxFlips) { const int k = (int)(r32() % (maxFlips + 1)); for (int i = 0; i < k && !b.empty(); i++) b[r32() % b.size()] ^= 1; return b; }

// ---- FAC -------------------------------------------------------------------------------------------------------------------------------------------------
static Buf genFac(bool modeE, bool goodCrc) {
    Buf b;
    putBits(b, r32() % 2, 1); putBits(b, r32() % 4, 2); putBits(b, modeE ? (r32() % 8 ? 1 : 0) : (r32() % 8 ? 0 : 1), 1);
    putBits(b, r32() % 8, 3); putBits(b, r32() % 2, 1); putBits(b, r32() % 4, 2); putBits(b, r32() % 2, 1); putBits(b, r32() % 16, 4); putBits(b, r32() % 8, 3); putBits(b, r32() % 2, 1); putBits(b, 0, 1);
    for (int i = 0; i < (modeE ? 2 : 1); i++) { putBits(b, r32() & 0xFFFFFF, 24); putBits(b, r32() % 4, 2); putBits(b, r32() % 2, 1); putBits(b, r32() % 16, 4); putBits(b, r32() % 2, 1); putBits(b, r32() % 32, 5); putBits(b, r32() % 2, 1); putBits(b, 0, 6); }
    Buf crcIn = b; if (modeE) crcIn.resize(crcIn.size() + 4, 0);
    uint32_t c = A::crc8(crcIn.data(), crcIn.size()); if (!goodCrc) c ^= 1 + r32() % 255;
    putBits(b, c, 8);
    return b;
}
static void testFac() {
    for (int mode = 0; mode < 2; mode++) {
        const bool me = mode == 1;
        for (int rm = 0; rm < 2; rm++) {   // rm: the flag in the data says mode E, the parameter may disagree
            Buf b = genFac(me, r32() % 5 != 0);
            if (r32() % 4 == 0) b = flipBits(b, 3);
            const bool askE = rm ? !me : me;
            const size_t need = askE ? 116 : 72;
            if (b.size() < need) b.resize(need, 0); else if (b.size() > need) b.resize(need);
            exact(b, [&](const uint8_t* p, size_t) {
                D::FacInfo fa, fr; const bool ba = A::facParse(p, askE, fa), br = R::facParse(p, askE, fr);
                bool e = ba == br;
                if (e && ba) { e = fa.enhancement == fr.enhancement && fa.identity == fr.identity && fa.modeE == fr.modeE && fa.occupancy == fr.occupancy && fa.interleaver == fr.interleaver && fa.mscMode == fr.mscMode &&
                    fa.sdcMode == fr.sdcMode && fa.numServicesCode == fr.numServicesCode && fa.reconfig == fr.reconfig && fa.toggle == fr.toggle && fa.nSvc == fr.nSvc;
                    for (int i = 0; i < fa.nSvc; i++) e = e && fa.svc[i].id == fr.svc[i].id && fa.svc[i].shortId == fr.svc[i].shortId && fa.svc[i].audioCa == fr.svc[i].audioCa && fa.svc[i].dataCa == fr.svc[i].dataCa &&
                        fa.svc[i].language == fr.svc[i].language && fa.svc[i].data == fr.svc[i].data && fa.svc[i].descriptor == fr.svc[i].descriptor; }
                SAME(e, "fac modeE=%d ask=%d ok %d/%d", me, askE, ba, br);
                okc[0] += ba;
            });
        }
    }
}

// ---- SDC -------------------------------------------------------------------------------------------------------------------------------------------------
static void entity(Buf& f, int len, int ver, int type, int nib, bool randBody = true) { f.push_back((uint8_t)(len << 1 | ver)); f.push_back((uint8_t)(type << 4 | nib)); if (randBody) rnd(f, len); }
static Buf genSdc() {
    Buf f; const int n = (int)(r32() % 7);
    for (int i = 0; i < n; i++) {
        switch (r32() % 10) {
        case 0: entity(f, 3 * (1 + r32() % 4), r32() % 3 == 0, 0, (int)(r32() % 16)); break;
        case 1: { const int len = (int)(r32() % 30); entity(f, len, r32() % 5 == 0, 1, (int)(r32() % 16), false); if (len) f.push_back((uint8_t)(r32() % 4 ? r32() % 18 : 0x41 + r32() % 20)); for (int k = 1; k < len; k++) f.push_back((uint8_t)(0x20 + r32() % 90)); break; }
        case 2: { const int len = (int)(1 + r32() % 20); entity(f, len, r32() % 5 == 0, 5, (int)(r32() % 16), false); f.push_back((uint8_t)(r32() & 0xFF)); for (int k = 1; k < len; k++) f.push_back((uint8_t)r32()); break; }
        case 3: entity(f, 3 + r32() % 3, 0, 8, (int)(r32() % 16)); break;
        case 4: entity(f, 2 + r32() % 6, r32() % 5 == 0, 9, (int)(r32() % 16)); break;
        case 5: entity(f, 5 + (r32() % 3 == 0 ? r32() % 3 : 0), r32() % 5 == 0, 12, (int)(r32() % 16)); break;
        default: entity(f, (int)(r32() % 10), r32() % 3 == 0, (int)(r32() % 16), (int)(r32() % 16)); break;
        }
    }
    if (r32() % 3 == 0) f.resize(f.size() + r32() % 6, 0);
    return f;
}
static void fillSdc(D::SdcInfo& s) {   // arbitrary previous state, so the in-place refinement matters
    if (r32() % 2) return;
    s.mux.present = true; s.mux.protA = 1; s.mux.nStreams = 1; s.mux.stream[0].lenA = 7;
    for (int i = 0; i < 4; i++) {
        if (r32() % 2) { s.label[i].present = true; s.label[i].shortId = i; s.label[i].text = "old"; }
        if (r32() % 2) { s.app[i].present = true; s.app[i].shortId = i; s.app[i].packet = true; s.app[i].dataUnit = true; s.app[i].packetId = 2; s.app[i].packetLen = 55; s.app[i].appData = {1, 2, 3}; }
        if (r32() % 2) { s.audio[i].present = true; s.audio[i].config = {9, 9}; s.audio[i].coding = 3; }
        if (r32() % 2) { s.lang[i].present = true; s.lang[i].language = "abc"; s.lang[i].country = "de"; }
    }
    if (r32() % 2) { s.time.present = true; s.time.hasOffset = true; s.time.offsetHalfHours = -7; s.time.mjd = 1; }
    s.otherTypes = {3, 4};
}
static bool sdcEq(const D::SdcInfo& a, const D::SdcInfo& b) {
    auto mux = [](const D::SdcMux& x, const D::SdcMux& y) { bool e = x.present == y.present && x.protA == y.protA && x.protB == y.protB && x.nStreams == y.nStreams; for (int i = 0; i < 4; i++) e = e && x.stream[i].lenA == y.stream[i].lenA && x.stream[i].lenB == y.stream[i].lenB; return e; };
    bool e = a.afsIndex == b.afsIndex && a.entities == b.entities && mux(a.mux, b.mux) && mux(a.muxNext, b.muxNext) && a.otherTypes == b.otherTypes &&
             a.time.present == b.time.present && a.time.mjd == b.time.mjd && a.time.hour == b.time.hour && a.time.minute == b.time.minute && a.time.hasOffset == b.time.hasOffset && a.time.offsetHalfHours == b.time.offsetHalfHours;
    for (int i = 0; i < 4; i++) {
        const auto &x = a.audio[i], &y = b.audio[i]; const auto &l = a.label[i], &m = b.label[i]; const auto &p = a.lang[i], &q = b.lang[i]; const auto &s = a.app[i], &t = b.app[i];
        e = e && x.present == y.present && x.shortId == y.shortId && x.streamId == y.streamId && x.coding == y.coding && x.sbr == y.sbr && x.mode == y.mode && x.rateCode == y.rateCode && x.text == y.text && x.enhancement == y.enhancement && x.mps == y.mps && x.config == y.config;
        e = e && l.present == m.present && l.shortId == m.shortId && l.textControl == m.textControl && l.text == m.text;
        e = e && p.present == q.present && p.shortId == q.shortId && p.language == q.language && p.country == q.country;
        e = e && s.present == t.present && s.shortId == t.shortId && s.streamId == t.streamId && s.packet == t.packet && s.domain == t.domain && s.dataUnit == t.dataUnit && s.packetId == t.packetId && s.packetLen == t.packetLen && s.appData == t.appData;
    }
    return e;
}
static void testSdc() {
    Buf f = genSdc();
    if (r32() % 3 == 0) { const int k = (int)(r32() % 3); for (int i = 0; i < k && !f.empty(); i++) f[r32() % f.size()] = (uint8_t)r32(); }
    if (r32() % 5 == 0) f.resize(r32() % (f.size() + 1));
    exact(f, [&](const uint8_t* p, size_t n) {
        D::SdcInfo sa, sr; fillSdc(sa); sr = sa;
        const int ca = A::sdcParse(p, (int)n, sa), cr = R::sdcParse(p, (int)n, sr);
        SAME(ca == cr && sdcEq(sa, sr), "sdcParse n=%zu %d vs %d", n, ca, cr);
        const int c2 = A::sdcParse(p, (int)n, sa), c3 = R::sdcParse(p, (int)n, sr);   // a second field refines the first
        SAME(c2 == c3 && sdcEq(sa, sr), "sdcParse twice n=%zu", n);
        okc[1] += ca > 0; okc[2] += !sa.otherTypes.empty();
    });
    // CRC block
    const int fb = (int)(r32() % 20);
    Buf blk; putBits(blk, r32() % 16, 4); for (int i = 0; i < fb; i++) putBits(blk, r32() & 0xFF, 8);
    Buf crcIn(4, 0); crcIn.insert(crcIn.end(), blk.begin(), blk.end());
    uint32_t c = A::crc16(crcIn.data(), crcIn.size()); if (r32() % 4 == 0) c ^= 1 + r32() % 65535;
    putBits(blk, c, 16);
    if (r32() % 8 == 0) blk = flipBits(blk, 2);
    exact(blk, [&](const uint8_t* p, size_t) { int aa = -1, ar = -2; const bool ba = A::sdcCheckBits(p, fb, aa), br = R::sdcCheckBits(p, fb, ar); SAME(ba == br && aa == ar, "sdcCheckBits"); okc[3] += ba; });
}

// ---- audio super frame -----------------------------------------------------------------------------------------------------------------------------------
static void testAac() {
    for (int rate : {0, 12000, 24000, 48000, 16000}) for (int me = 0; me < 2; me++) SAME(A::aacNumFrames(me, rate) == R::aacNumFrames(me, rate), "numFrames");
    for (int n = -2; n < 20; n++) SAME(A::aacHeaderBytes(n) == R::aacHeaderBytes(n), "headerBytes %d", n);
    const int nf = (int)(r32() % 14 < 12 ? (r32() % 2 ? 5 : 10) : (int)(r32() % 20) - 2);
    const int hdr = A::aacHeaderBytes(nf), hp = (int)(r32() % 6), len = (int)(r32() % 3 == 0 ? r32() % 300 : 60 + r32() % 200);
    Buf lf; rnd(lf, (size_t)len);
    if (nf >= 2 && nf <= 16 && len > hdr + nf * (hp + 2) && r32() % 3) {   // consistent borders
        const int payload = len - hdr - nf;
        std::fill(lf.begin(), lf.begin() + hdr, 0);
        int b = 0;
        for (int i = 0; i < nf - 1; i++) { b += hp + (int)(r32() % std::max(1, (payload - b - (nf - i) * hp) / (nf - i) + 1)); const int bit = i * 12; for (int k = 0; k < 12; k++) if (((std::min(b, 4095)) >> (11 - k)) & 1) lf[(size_t)((bit + k) / 8)] |= (uint8_t)(0x80 >> ((bit + k) % 8)); }
    }
    const int lenA = r32() % 3 == 0 ? 0 : r32() % 4 == 0 ? (int)(r32() % 400) - 20 : hdr + nf * (hp + 1);
    exact(lf, [&](const uint8_t* p, size_t) {
        D::AacSuperFrame a, r; const bool ba = A::aacSuperFrameParse(p, len, lenA, nf, a), br = R::aacSuperFrameParse(p, len, lenA, nf, r);
        bool e = ba == br && a.numFrames == r.numFrames && a.headerOk == r.headerOk && a.hpBytes == r.hpBytes;
        if (e && ba) { e = a.frames == r.frames && a.crc == r.crc; }
        SAME(e, "aacParse n=%d len=%d lenA=%d ok %d/%d", nf, len, lenA, ba, br);
        okc[4 + (ba ? 1 : 0)]++;
    });
}

// ---- tables, dates, data service helpers, text message --------------------------------------------------------------------------------------------------
static Buf genGroup() {
    Buf d; const int ext = (int)(r32() % 3 == 0), crc = (int)(r32() % 4 != 0), seg = (int)(r32() % 3 == 0), ua = (int)(r32() % 3 == 0);
    d.push_back((uint8_t)(ext << 7 | crc << 6 | seg << 5 | ua << 4 | (r32() % 16))); d.push_back((uint8_t)r32());
    if (ext) rnd(d, 2); if (seg) rnd(d, 2); if (ua) { d.push_back((uint8_t)(r32() % 16)); rnd(d, d.back() & 0x0F); } rnd(d, r32() % 30);
    if (crc) { uint16_t c = 0xFFFF; for (uint8_t x : d) { c ^= (uint16_t)(x << 8); for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1); } c = (uint16_t)~c; if (r32() % 4 == 0) c ^= (uint16_t)(1 + r32() % 65534); d.push_back(c >> 8); d.push_back(c & 0xFF); }
    return d;
}
static Buf genText() {
    Buf t; const size_t n = r32() % 5 == 0 ? 16000 + r32() % 2000 : r32() % 80;
    for (size_t i = 0; i < n; i++) {
        switch (r32() % 12) {
        case 0: t.push_back((uint8_t)(r32() % 32)); break;
        case 1: t.push_back('\r'); break;
        case 2: t.push_back('\n'); break;
        case 3: t.push_back((uint8_t)(0x80 + r32() % 128)); break;
        case 4: { const uint8_t u[2] = {0xC3, (uint8_t)(0x80 + r32() % 64)}; t.insert(t.end(), u, u + 2); break; }
        case 5: if (r32() % 20 == 0) t.push_back(0); break;
        default: t.push_back((uint8_t)(0x20 + r32() % 95));
        }
    }
    return t;
}
static void testTables() {   // the tables are small: all values, once
    for (int c = -3; c < 40; c++) {
        SAME(A::languageName(c) == R::languageName(c) && A::programmeTypeName(c) == R::programmeTypeName(c) && A::facAudioServices(c) == R::facAudioServices(c) && A::facDataServices(c) == R::facDataServices(c), "names %d", c);
        for (int me = 0; me < 2; me++) SAME(A::mscModeName(me, c) == R::mscModeName(me, c) && A::occupancyName(me, c) == R::occupancyName(me, c), "mode names %d", c);
    }
    for (int dom = -1; dom < 4; dom++) for (int app = -2; app < 0x460; app++) SAME(A::appName(dom, app) == R::appName(dom, app), "appName %d %d", dom, app);
}

static void testMisc() {
    for (int i = 0; i < 20; i++) {
        const int mjd = (int)(r32() % 200000) - 10000;
        int y1, m1, d1, y2, m2, d2; A::mjdToDate(mjd, y1, m1, d1); R::mjdToDate(mjd, y2, m2, d2);
        SAME(y1 == y2 && m1 == m2 && d1 == d2 && A::dateToMjd(y1, m1, d1) == R::dateToMjd(y1, m1, d1), "mjd %d", mjd);
    }
    SAME(A::dateToMjd(2026, 10, 17) == 61330 && R::dateToMjd(2026, 10, 17) == 61330, "mjd value");
    Buf bits; rnd(bits, r32() % 300); for (auto& x : bits) x &= 1;
    exact(bits, [&](const uint8_t* p, size_t n) { SAME(A::crc8(p, n) == R::crc8(p, n) && A::crc16(p, n) == R::crc16(p, n), "crc bits n=%zu", n); });
    const Buf grp = genGroup();
    exact(grp, [&](const uint8_t* p, size_t n) { size_t oa = 9, la = 9, orr = 8, lr = 8; const bool ba = A::groupPayload(p, n, oa, la), br = R::groupPayload(p, n, orr, lr); SAME(ba == br && (!ba || (oa == orr && la == lr)), "groupPayload n=%zu", n); okc[6] += ba; });
    const Buf txt = genText();
    exact(txt, [&](const uint8_t* p, size_t n) {
        SAME(A::displayText(p, n) == R::displayText(p, n), "displayText n=%zu", n);
        const bool la = A::looksText(p, n), lr = R::looksText(p, n); SAME(la == lr, "looksText n=%zu", n); okc[7] += la;
    });
    // text message segments
    Buf seg; seg.push_back((uint8_t)(r32() & 0xFF)); const int need = A::textSegmentNeed(seg[0]);
    SAME(need == R::textSegmentNeed(seg[0]), "textSegmentNeed");
    rnd(seg, (size_t)need - 1);
    { uint16_t c = 0xFFFF; for (int i = 0; i < need - 2; i++) { c ^= (uint16_t)(seg[(size_t)i] << 8); for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1); } c = (uint16_t)~c; if (r32() % 4 == 0) c ^= (uint16_t)(1 + r32() % 65534); seg[(size_t)need - 2] = (uint8_t)(c >> 8); seg[(size_t)need - 1] = (uint8_t)c; }
    exact(seg, [&](const uint8_t* p, size_t n) { const auto a = A::textSegment(p, n, need); const auto r = R::textSegment(p, n, need);
        SAME(a.crcOk == r.crcOk && (!a.crcOk || (a.toggle == r.toggle && a.first == r.first && a.last == r.last && a.command == r.command && a.cmd == r.cmd && a.segnum == r.segnum && a.n == r.n)), "textSegment"); okc[8] += a.crcOk; });
}

int main() {
    const long iters = getenv("ONAIR_DIFF_ITERS") ? atol(getenv("ONAIR_DIFF_ITERS")) : 200000;
    testTables();
    for (long i = 0; i < iters; i++) { testFac(); testSdc(); testAac(); testMisc(); }
    printf("drm proto: %ld checks, %ld mismatches (ok: fac %ld sdc %ld sdc-other %ld sdc-crc %ld aac bad %ld good %ld group %ld text %ld segment %ld)\n", checks, fails, okc[0], okc[1], okc[2], okc[3], okc[4], okc[5], okc[6], okc[7], okc[8]);
    return fails ? 1 : 0;
}
