// Differential test: the C++ DMR frame layer (generated copies dect2::dmr_ref of dmr_fec.cpp and dmr_proto.cpp, see cmake/gen_rs_ref.cmake) against the
// Rust one (dect2::dmr when DECT2_RUST is used). The same inputs go to both and every result has to be identical: the short codes over their whole word
// space, Reed-Solomon, CRCs, BPTC, embedded LC, short LC, CACH, trellis (noisy, NaN and random soft values), rate 1, sync matching, burst assembly and
// slicing, slot type / EMB, link control, CSBK, data headers and blocks, every text format and the talker alias, over ONAIR_DIFF_ITERS (default 200000)
// seeded random / mutated inputs per family.
#include "dect2/dmr/dmr_proto.h"
#include "dmr_proto_ref.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
namespace A = dect2::dmr;
namespace R = dect2::dmr_ref;

static long fails = 0, checks = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)

static std::mt19937 g(2468);
static uint32_t r32() { return g(); }
static A::Bits rndBits(size_t n) { A::Bits b(n); for (auto& x : b) x = (uint8_t)(g() & 1); return b; }
static void flips(A::Bits& b, int n) { for (int i = 0; i < n && !b.empty(); i++) b[g() % b.size()] ^= 1; }
static void rndBytes(uint8_t* p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (uint8_t)g(); }

static void fecTests(long iters) {
    // the short codes over every word
    for (unsigned i = 0; i < 256; i++) SAME(A::golay2008Parity(i) == R::golay2008Parity(i), "golay parity %u", i);
    for (unsigned i = 0; i < (1u << 20); i += (i < 4096 ? 1 : 3)) {
        unsigned a = 77, b = 77;
        const int ea = A::golay2008Decode(i, a), eb = R::golay2008Decode(i, b);
        SAME(ea == eb && a == b, "golay decode %u", i);
    }
    for (unsigned i = 0; i < 128; i++) SAME(A::qr1676Parity(i) == R::qr1676Parity(i), "qr parity %u", i);
    for (unsigned i = 0; i < (1u << 16); i++) {
        unsigned a = 77, b = 77;
        SAME(A::qr1676Decode(i, a) == R::qr1676Decode(i, b) && a == b, "qr decode %u", i);
    }
    for (unsigned i = 0; i < 16; i++) SAME(A::hamming743Parity(i) == R::hamming743Parity(i), "ham743 parity");
    for (unsigned i = 0; i < 256; i++) {
        unsigned a = 77, b = 77;
        SAME(A::hamming743Decode(i, a) == R::hamming743Decode(i, b) && a == b, "ham743 decode %u", i);
    }
    for (int k = 0; k < 4; k++) {
        const A::HammingKind ka = (A::HammingKind)k; const R::HammingKind kr = (R::HammingKind)k;
        SAME(A::hammingInfoBits(ka) == R::hammingInfoBits(kr) && A::hammingParityBits(ka) == R::hammingParityBits(kr), "hamming sizes");
        const int tot = R::hammingInfoBits(kr) + R::hammingParityBits(kr);
        for (unsigned i = 0; i < (1u << (R::hammingInfoBits(kr) + 2)); i++) SAME(A::hammingParity(ka, i) == R::hammingParity(kr, i), "hamming parity %d %u", k, i);
        for (unsigned w = 0; w < (1u << (tot + 1)); w++) {   // one bit more than the word: the high bit must not matter the same way
            unsigned a = w, b = w;
            SAME(A::hammingCorrect(ka, a) == R::hammingCorrect(kr, b) && a == b, "hamming correct %d %u", k, w);
        }
    }
    for (long i = 0; i < iters; i++) {
        uint8_t m[9], w[12], pa[3], pb[3];
        rndBytes(m, 9);
        A::rs129Parity(m, pa); R::rs129Parity(m, pb);
        SAME(!memcmp(pa, pb, 3), "rs parity");
        memcpy(w, m, 9); memcpy(w + 9, pa, 3);
        const int errs = (int)(g() % 5);
        for (int e = 0; e < errs; e++) w[g() % 12] ^= (uint8_t)(1 + g() % 255);
        if (g() % 16 == 0) rndBytes(w, 12);
        uint8_t wa[12], wb[12];
        memcpy(wa, w, 12); memcpy(wb, w, 12);
        SAME(A::rs129Correct(wa) == R::rs129Correct(wb) && !memcmp(wa, wb, 12), "rs correct");
        // checksums
        uint8_t d[40]; const size_t n = g() % 41; rndBytes(d, 40);
        SAME(A::crcCcitt(d, n) == R::crcCcitt(d, n) && A::crc32Msg(d, n) == R::crc32Msg(d, n), "crc ccitt / 32 n %zu", n);
        const uint32_t v = r32();
        SAME(A::crc8Short(v) == R::crc8Short(v) && A::crc8Short(v & 0x0FFFFFFF) == R::crc8Short(v & 0x0FFFFFFF), "crc8");
        SAME(A::crc9(d, n, v) == R::crc9(d, n, v), "crc9");
        SAME(A::checksum5(d) == R::checksum5(d), "checksum5");
    }
    SAME(A::crcCcitt(nullptr, 0) == R::crcCcitt(nullptr, 0) && A::crc32Msg(nullptr, 0) == R::crc32Msg(nullptr, 0), "empty crc");

    for (long i = 0; i < iters; i++) {
        // BPTC (196,96)
        const A::Bits info = rndBits(96);
        A::Bits ta, tr;
        A::bptc196Encode(info, ta); R::bptc196Encode(info, tr);
        SAME(ta == tr, "bptc encode");
        A::Bits tx = tr;
        const int nf = (int)(g() % 9);
        flips(tx, nf);
        if (g() % 8 == 0) tx = rndBits(196);
        A::Bits ia(96, 5), ib(96, 5);
        const int ra = A::bptc196Decode(tx, ia), rb = R::bptc196Decode(tx, ib);
        SAME(ra == rb && ia == ib, "bptc decode flips %d: %d %d", nf, ra, rb);

        // embedded LC
        uint8_t lc[9]; rndBytes(lc, 9);
        A::Bits fa[4], fr[4];
        A::embLcEncode(lc, fa); R::embLcEncode(lc, fr);
        bool same = true;
        for (int f = 0; f < 4; f++) same = same && fa[f] == fr[f];
        SAME(same, "embLc encode");
        const int nfl = (int)(g() % 6);
        for (int e = 0; e < nfl; e++) fr[g() % 4][g() % 32] ^= 1;
        if (g() % 10 == 0) for (int f = 0; f < 4; f++) fr[f] = rndBits(32);
        uint8_t la[9], lb[9];
        memset(la, 0xA5, 9); memset(lb, 0xA5, 9);
        const int ea = A::embLcDecode(fr, la), eb = R::embLcDecode(fr, lb);
        SAME(ea == eb && !memcmp(la, lb, 9), "embLc decode %d %d", ea, eb);

        // short LC
        const uint32_t s28 = r32();
        A::Bits sa[4], sr[4];
        A::shortLcEncode(s28, sa); R::shortLcEncode(s28, sr);
        same = true;
        for (int f = 0; f < 4; f++) same = same && sa[f] == sr[f];
        SAME(same, "shortLc encode");
        const int ns = (int)(g() % 5);
        for (int e = 0; e < ns; e++) sr[g() % 4][g() % 17] ^= 1;
        if (g() % 10 == 0) for (int f = 0; f < 4; f++) sr[f] = rndBits(17);
        uint32_t va = 0xDEADBEEF, vb = 0xDEADBEEF;
        const int xa = A::shortLcDecode(sr, va), xb = R::shortLcDecode(sr, vb);
        SAME(xa == xb && va == vb, "shortLc decode %d %d", xa, xb);

        // CACH
        const int at = (int)r32() - (1 << 30), tc = (int)r32(), ls = (int)r32();
        const unsigned pl = r32();
        A::Bits ca, cr;
        A::cachEncode(at, tc, ls, pl, ca); R::cachEncode(at, tc, ls, pl, cr);
        SAME(ca == cr, "cach encode");
        A::Bits in = g() % 3 ? cr : rndBits(24);
        flips(in, (int)(g() % 3));
        int a1 = 11, t1 = 12, l1 = 13, a2 = 11, t2 = 12, l2 = 13;
        unsigned p1 = 99, p2 = 99;
        const int c1 = A::cachDecode(in, a1, t1, l1, p1), c2 = R::cachDecode(in, a2, t2, l2, p2);
        SAME(c1 == c2 && a1 == a2 && t1 == t2 && l1 == l2 && p1 == p2, "cach decode %d %d", c1, c2);

        // rate 1
        uint8_t d24[24]; rndBytes(d24, 24);
        A::Bits r1a, r1b;
        A::rate1Encode(d24, r1a); R::rate1Encode(d24, r1b);
        SAME(r1a == r1b, "rate1 encode");
        A::Bits rx = g() % 2 ? r1b : rndBits(196);
        uint8_t o1[24], o2[24]; bool p1o = false, p2o = true;
        A::rate1Decode(rx, o1, &p1o); R::rate1Decode(rx, o2, &p2o);
        SAME(!memcmp(o1, o2, 24) && p1o == p2o, "rate1 decode");
        A::rate1Decode(rx, o1); R::rate1Decode(rx, o2);
    }

    // rate 3/4 trellis: encode, decode of clean / noisy / damaged / random soft symbols
    for (long i = 0; i < iters; i++) {
        uint8_t d[18]; rndBytes(d, 18);
        A::Bits ta, tr;
        A::trellis34Encode(d, ta); R::trellis34Encode(d, tr);
        SAME(ta == tr, "trellis encode");
        float sym[98];
        const int mode = (int)(g() % 6);
        const float sigma = (float)(g() % 40) / 10.f;
        for (int k = 0; k < 98; k++) {
            const float clean = (float)R::dibitToSymbol((unsigned)((tr[2 * k] << 1) | tr[2 * k + 1]));
            const float noise = ((float)(g() & 0xFFFF) / 65536.f - 0.5f) * 2.f * sigma;
            switch (mode) {
            case 0: sym[k] = clean; break;
            case 1: case 2: sym[k] = clean + noise; break;
            case 3: sym[k] = ((float)r32() / 4294967296.f - 0.5f) * 12.f; break;
            case 4: sym[k] = g() % 40 == 0 ? std::nanf("") : clean + noise; break;
            default: sym[k] = g() % 60 == 0 ? INFINITY : (g() % 30 == 0 ? -1e30f : clean + noise); break;
            }
        }
        uint8_t da[18], db[18];
        memset(da, 0x5A, 18); memset(db, 0x5A, 18);
        const int ra = A::trellis34Decode(sym, da), rb = R::trellis34Decode(sym, db);
        SAME(ra == rb && !memcmp(da, db, 18), "trellis decode mode %d: %d %d", mode, ra, rb);
    }
}

template <class T, class U> static bool lcEq(const T& a, const U& b) {
    return a.pf == b.pf && a.flco == b.flco && a.fid == b.fid && a.svc == b.svc && a.dst == b.dst && a.src == b.src && !memcmp(a.raw, b.raw, 9);
}
template <class T, class U> static bool dhEq(const T& a, const U& b) {
    return a.dpf == b.dpf && a.sap == b.sap && a.group == b.group && a.resp == b.resp && a.dst == b.dst && a.src == b.src && a.blocks == b.blocks && a.pad == b.pad &&
           a.fmf == b.fmf && a.fsn == b.fsn && a.ns == b.ns && a.dd == b.dd && a.sp == b.sp && a.dp == b.dp && a.resync == b.resync && a.sarq == b.sarq && a.status == b.status;
}
template <class H, class U> static void rndDh(H& h, U& u) {
    h.dpf = u.dpf = g() % 5 ? (int)(g() % 16) : (int)r32() % 40;
    h.sap = u.sap = (int)(g() % 20);
    h.group = u.group = g() & 1; h.resp = u.resp = g() & 1;
    h.dst = u.dst = r32() & 0xFFFFFF; h.src = u.src = r32() & 0xFFFFFF;
    h.blocks = u.blocks = (int)(g() % 130); h.pad = u.pad = (int)(g() % 300) - 20;
    h.fmf = u.fmf = g() & 1; h.fsn = u.fsn = (int)(g() % 20); h.ns = u.ns = (int)(g() % 10); h.dd = u.dd = (int)(g() % 70);
    h.sp = u.sp = (int)(g() % 9); h.dp = u.dp = (int)(g() % 9); h.resync = u.resync = g() & 1; h.sarq = u.sarq = g() & 1; h.status = u.status = (int)(g() % 1100);
}

static std::string rndText(size_t n) {   // bytes that exercise every text format: random, ASCII, valid UTF-8 / UTF-16 / UTF-32 with BOMs
    std::string s;
    switch (g() % 7) {
    case 0: for (size_t i = 0; i < n; i++) s += (char)g(); break;
    case 1: for (size_t i = 0; i < n; i++) s += (char)(32 + g() % 95); break;
    case 2: for (size_t i = 0; i < n; i++) s += "aZ \xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\r\n\t\x01"[g() % 15]; break;
    case 3: if (n >= 2 && g() % 2) { s += "\xFF\xFE"; } for (size_t i = 0; i < n; i++) s += (char)(g() % 3 ? (g() % 2 ? 0 : 0xD8 + g() % 8) : g()); break;
    case 4: if (n >= 2 && g() % 2) { s += "\xFE\xFF"; } for (size_t i = 0; i < n; i++) s += (char)(g() % 3 ? (g() % 2 ? 0 : 0xDC + g() % 4) : g()); break;
    case 5: for (size_t i = 0; i < n; i++) s += (char)(g() % 4 ? 0 : g() % 3 ? 1 : 0x11); break;
    default: for (size_t i = 0; i < n; i++) s += (char)(g() % 2 ? (g() % 4) : 0x80 + g() % 0x80); break;
    }
    return s;
}

static void protoTests(long iters) {
    // sync patterns and names
    SAME(!memcmp(A::kSyncWords, R::kSyncWords, sizeof A::kSyncWords), "sync words");
    for (int s = -3; s < 14; s++) {
        SAME(!strcmp(A::syncName(s), R::syncName(s)) && A::syncDirectSlot(s) == R::syncDirectSlot(s), "sync name %d", s);
        if (s >= 0 && s < 9) { int8_t a[24], b[24]; A::syncSymbols(s, a); R::syncSymbols(s, b); SAME(!memcmp(a, b, 24), "sync symbols"); }
    }
    for (int f = -2; f < 6; f++) for (int v = 0; v < 2; v++) SAME(A::syncOf(f, v) == R::syncOf(f, v), "syncOf");
    for (int dt = -5; dt < 40; dt++) SAME(!strcmp(A::dataTypeName(dt), R::dataTypeName(dt)) && !strcmp(A::dataTypeShort(dt), R::dataTypeShort(dt)), "dt names");
    for (int f = -2; f < 70; f++) SAME(!strcmp(A::flcoName(f), R::flcoName(f)) && !strcmp(A::dpfName(f), R::dpfName(f)) && !strcmp(A::sapName(f), R::sapName(f)) && !strcmp(A::ddName(f), R::ddName(f)), "names %d", f);
    for (int op = 0; op < 70; op++) for (int fid = 0; fid < 2; fid++) SAME(!strcmp(A::csbkName(op, fid), R::csbkName(op, fid)), "csbkName");
    for (int s = 0; s < 256; s++) SAME(A::serviceOptionsText((uint8_t)s) == R::serviceOptionsText((uint8_t)s), "service options %d", s);
    for (int dt = -2; dt < 14; dt++) for (int c = 0; c < 2; c++) SAME(A::dataBlockBytes(dt, c) == R::dataBlockBytes(dt, c), "dataBlockBytes");

    for (long i = 0; i < iters; i++) {
        // match sync on random fields and on mutated patterns
        A::Bits centre = rndBits(48);
        if (g() % 4) { A::Bits w; A::putBits(w, A::kSyncWords[g() % 9], 48); centre = w; flips(centre, (int)(g() % 14)); }
        const int md = (int)(g() % 26);
        int da = 0, db = 0;
        SAME(A::matchSync(centre, md, &da) == R::matchSync(centre, md, &db) && da == db, "matchSync");
        SAME(A::matchSync(centre, md) == R::matchSync(centre, md), "matchSync no diff");

        // bursts: data and voice, with sync or embedded, then slicing
        const int cc = (int)r32() - (1 << 30), dt = (int)(g() % 20) - 2;
        const A::Bits pay = rndBits(196);
        const int sy = (int)(g() % 11) - 2;
        const int symOk = sy >= 9 ? -1 : sy;   // an index past the table is undefined in the C++ reference
        A::Bits ba = A::makeDataBurst(cc, dt, pay, symOk), bb = R::makeDataBurst(cc, dt, pay, symOk);
        SAME(ba == bb, "makeDataBurst");
        const A::Bits vs = rndBits(216), e32 = rndBits(32);
        const int pi = (int)r32(), ls = (int)r32();
        A::Bits va = A::makeVoiceBurst(vs, symOk, cc, pi, ls, e32), vb = R::makeVoiceBurst(vs, symOk, cc, pi, ls, e32);
        SAME(va == vb, "makeVoiceBurst");
        A::Bits burst = (g() % 3) ? bb : (g() % 2 ? vb : rndBits(264));
        flips(burst, (int)(g() % 6));
        if (g() % 16 == 0) burst.resize(g() % 264);
        const A::SlotTypeInfo sa = A::burstSlotType(burst); const R::SlotTypeInfo sb = R::burstSlotType(burst);
        SAME(sa.ok == sb.ok && sa.cc == sb.cc && sa.dt == sb.dt && sa.errors == sb.errors, "burstSlotType");
        if (burst.size() >= 148) {   // the C++ reference reads the embedded bits unchecked
            const A::EmbInfo xa = A::burstEmb(burst); const R::EmbInfo xb = R::burstEmb(burst);
            SAME(xa.ok == xb.ok && xa.cc == xb.cc && xa.pi == xb.pi && xa.lcss == xb.lcss && xa.errors == xb.errors && xa.emb32 == xb.emb32, "burstEmb");
        }
        std::vector<int8_t> y1, y2;
        A::bitsToSymbols(burst, y1); R::bitsToSymbols(burst, y2);
        SAME(y1 == y2, "bitsToSymbols");

        // full link control
        uint8_t raw[9]; rndBytes(raw, 9);
        A::FullLc pa; R::FullLc pb;
        pa.src = pb.src = 5;
        A::parseFullLc(raw, pa); R::parseFullLc(raw, pb);
        SAME(lcEq(pa, pb), "parseFullLc");
        A::FullLc qa; R::FullLc qb;
        qa.pf = qb.pf = g() & 1; qa.flco = qb.flco = (int)r32(); qa.fid = qb.fid = (int)r32(); qa.svc = qb.svc = (uint8_t)g(); qa.dst = qb.dst = r32(); qa.src = qb.src = r32();
        uint8_t o1[9], o2[9];
        A::packFullLc(qa, o1); R::packFullLc(qb, o2);
        SAME(!memcmp(o1, o2, 9), "packFullLc");

        // header / terminator bursts, CRC blocks, CSBK
        const bool term = g() & 1;
        A::Bits la, lb;
        A::lcBurstInfo(raw, term, la); R::lcBurstInfo(raw, term, lb);
        SAME(la == lb, "lcBurstInfo");
        flips(lb, (int)(g() % 3));
        if (g() % 12 == 0) lb = rndBits(96);
        uint8_t c1[9], c2[9];
        memset(c1, 0x33, 9); memset(c2, 0x33, 9);
        SAME(A::lcBurstDecode(lb, term, c1) == R::lcBurstDecode(lb, term, c2) && !memcmp(c1, c2, 9), "lcBurstDecode");
        uint8_t d10[10]; rndBytes(d10, 10);
        const uint16_t mask = (uint16_t)g();
        A::Bits ia, ib;
        A::crcBlockInfo(d10, mask, ia); R::crcBlockInfo(d10, mask, ib);
        SAME(ia == ib, "crcBlockInfo");
        if (g() % 2) flips(ib, (int)(g() % 3));
        uint8_t k1[10], k2[10];
        const bool ca = A::crcBlockCheck(ib, mask, k1), cb = R::crcBlockCheck(ib, mask, k2);
        SAME(ca == cb && !memcmp(k1, k2, 10), "crcBlockCheck");
        A::Csbk xa; R::Csbk xb;
        xa.lb = xb.lb = g() & 1; xa.pf = xb.pf = g() & 1; xa.opcode = xb.opcode = (int)r32(); xa.fid = xb.fid = (int)r32();
        rndBytes(xa.data, 8); memcpy(xb.data, xa.data, 8);
        A::Bits ka, kb;
        A::csbkInfo(xa, ka); R::csbkInfo(xb, kb);
        SAME(ka == kb, "csbkInfo");
        if (g() % 2) flips(kb, (int)(g() % 3));
        A::Csbk ya; R::Csbk yb;
        ya.opcode = yb.opcode = 77;
        const bool pA = A::csbkParse(kb, ya), pB = R::csbkParse(kb, yb);
        SAME(pA == pB && ya.lb == yb.lb && ya.pf == yb.pf && ya.opcode == yb.opcode && ya.fid == yb.fid && !memcmp(ya.data, yb.data, 8), "csbkParse");

        // data headers: from fields and from random octets
        A::DataHeader ha; R::DataHeader hb;
        rndDh(ha, hb);
        uint8_t h1[10], h2[10];
        A::packDataHeader(ha, h1); R::packDataHeader(hb, h2);
        SAME(!memcmp(h1, h2, 10), "packDataHeader dpf %d", ha.dpf);
        uint8_t hin[10]; rndBytes(hin, 10);
        if (g() % 2) memcpy(hin, h2, 10);
        A::DataHeader pha; R::DataHeader phb;
        pha.fsn = phb.fsn = 9;
        const bool oa = A::parseDataHeader(hin, pha), ob = R::parseDataHeader(hin, phb);
        SAME(oa == ob && dhEq(pha, phb), "parseDataHeader");

        // data blocks
        const int bdt = (int)(g() % 14);
        const bool conf = g() & 1;
        uint8_t user[30]; rndBytes(user, 30);
        const size_t un = g() % 31;
        const unsigned dbsn = r32();
        A::Bits pa1, pa2;
        A::dataBlockEncode(bdt, user, un, conf, dbsn, pa1); R::dataBlockEncode(bdt, user, un, conf, dbsn, pa2);
        SAME(pa1 == pa2, "dataBlockEncode dt %d", bdt);
        uint8_t oct[24]; rndBytes(oct, 24);
        A::DataBlock da1; R::DataBlock db1;
        da1.dbsn = db1.dbsn = 55; da1.crcOk = db1.crcOk = false;
        const bool x1 = A::dataBlockParse(bdt, oct, conf, da1), x2 = R::dataBlockParse(bdt, oct, conf, db1);
        SAME(x1 == x2 && da1.bytes == db1.bytes && da1.dbsn == db1.dbsn && da1.crcOk == db1.crcOk && da1.confirmed == db1.confirmed, "dataBlockParse");
        // a well formed confirmed block parses with a good CRC
        if (conf && (bdt == 7 || bdt == 8 || bdt == 10)) {
            A::Bits p; R::Bits q;
            A::dataBlockEncode(bdt, user, un, true, dbsn, p);
            uint8_t ob[24]; rndBytes(ob, 24);
            const int nb = R::dataBlockBytes(bdt, true);
            uint8_t full[24] = {};
            const unsigned crc = (R::crc9(user, (size_t)nb, dbsn & 0x7F) ^ (bdt == 8 ? R::kMaskRate34 : bdt == 10 ? R::kMaskRate1 : R::kMaskRate12)) & 0x1FF;
            full[0] = (uint8_t)(((dbsn & 0x7F) << 1) | (crc >> 8)); full[1] = (uint8_t)crc; memcpy(full + 2, user, (size_t)nb);
            A::DataBlock e1; R::DataBlock e2;
            SAME(A::dataBlockParse(bdt, full, true, e1) == R::dataBlockParse(bdt, full, true, e2) && e1.crcOk == e2.crcOk && e1.bytes == e2.bytes && e1.crcOk, "good confirmed block");
        }

        // text of every format
        const std::string t = rndText(g() % 64);
        const int fmt = (int)(g() % 30) - 3;
        const bool ta = g() & 1;
        SAME(A::textFromBytes((const uint8_t*)t.data(), t.size(), fmt, ta) == R::textFromBytes((const uint8_t*)t.data(), t.size(), fmt, ta), "text fmt %d ta %d len %zu", fmt, ta, t.size());
    }

    // talker alias: random sequences of header and block LCs (also with a wrong opcode in between)
    for (long i = 0; i < iters; i++) {
        A::TalkerAlias a; R::TalkerAlias b;
        const int steps = 1 + (int)(g() % 6);
        for (int s = 0; s < steps; s++) {
            uint8_t raw[9]; rndBytes(raw, 9);
            if (g() % 3 == 0) raw[2] = (uint8_t)((g() % 4) << 6 | (g() % 32) << 1 | (g() & 1));
            A::FullLc la; R::FullLc lb;
            la.flco = lb.flco = g() % 6 ? (int)(4 + g() % 4) : (int)(g() % 64);
            memcpy(la.raw, raw, 9); memcpy(lb.raw, raw, 9);
            if (s == 0 && g() % 3) la.flco = lb.flco = 4;
            a.add(la); b.add(lb);
            SAME(a.format == b.format && a.length == b.length && a.have == b.have && !memcmp(a.data, b.data, 28), "TalkerAlias::add");
            SAME(a.complete() == b.complete() && a.text() == b.text(), "TalkerAlias text format %d length %d have %d", a.format, a.length, a.have);
        }
    }
    for (int f = 0; f < 4; f++) for (int len = 0; len < 40; len++) {
        A::FullLc pa[4]; R::FullLc pb[4];
        const std::string al(len, 'x');
        SAME(A::talkerAliasPdus(al, f, pa) == R::talkerAliasPdus(al, f, pb) && !memcmp(pa[0].raw, pb[0].raw, 9), "talkerAliasPdus");
    }
}

int main() {
    const char* it = getenv("ONAIR_DIFF_ITERS");
    const long iters = it ? atol(it) : 200000;
    printf("dmr_proto_rs: implementation under test is dect2::dmr (Rust when DECT2_RUST_DMR)\n");
    fecTests(iters);
    protoTests(iters);
    printf("dmr_proto_rs: %ld checks, %ld mismatches\n", checks, fails);
    return fails ? 1 : 0;
}
