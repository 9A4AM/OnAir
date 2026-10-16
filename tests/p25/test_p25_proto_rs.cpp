// Differential test: the C++ P25 frame layer (a generated copy in dect2::p25_ref, see cmake/gen_rs_ref.cmake) against the Rust one (dect2::p25 when
// DECT2_RUST is used). The same inputs go to both and every result has to be identical (floating point bit for bit): NID over all messages and noisy
// words, Reed-Solomon of every shape with 0..t+3 errors, Golay / Hamming over their word space, CRC, trellis with noisy / NaN / random levels, TSBK, link control,
// encryption sync and header words, the IMBE interleave and the names, over ONAIR_DIFF_ITERS (default 200000) seeded inputs per family.
#include "dect2/p25/p25_proto.h"
#include "p25_proto_ref.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
namespace A = dect2::p25;
namespace R = dect2::p25_ref;

static long fails = 0, checks = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)

static std::mt19937 g(1357);
static uint32_t r32() { return g(); }
static uint64_t r64() { return ((uint64_t)g() << 32) | g(); }
static bool fEq(float a, float b) { return memcmp(&a, &b, sizeof a) == 0; }

int main() {
    const char* it = getenv("ONAIR_DIFF_ITERS");
    const long iters = it ? atol(it) : 200000;
    printf("p25_proto_rs: implementation under test is dect2::p25 (Rust when DECT2_RUST_P25)\n");

    // tables and names
    SAME(!memcmp(A::kSync, R::kSync, sizeof A::kSync), "kSync");
    for (int d = -3; d < 20; d++) SAME(!strcmp(A::duidName(d), R::duidName(d)) && A::frameLen(d) == R::frameLen(d), "duid %d", d);
    for (int o = -2; o < 70; o++) SAME(!strcmp(A::opcodeName(o), R::opcodeName(o)), "opcode %d", o);
    for (int a = -2; a < 260; a++) SAME(!strcmp(A::algName(a), R::algName(a)), "alg %d", a);
    for (long i = 0; i < iters; i++) { const int ch = (int)r32(); SAME(A::channelText(ch) == R::channelText(ch), "channelText %d", ch); }

    // NID: every message, then noisy and random words
    for (int m = 0; m < 65536; m++) {
        uint8_t a[64], b[64];
        A::nidEncode(m >> 4, m & 15, a); R::nidEncode(m >> 4, m & 15, b);
        SAME(!memcmp(a, b, 64), "nidEncode %d", m);
    }
    for (long i = 0; i < iters / 2; i++) {
        uint8_t w[64];
        if (g() % 4 == 0) { for (auto& x : w) x = (uint8_t)(g() & 1); }
        else {
            R::nidEncode((int)r32(), (int)r32(), w);
            const int nf = (int)(g() % 16);
            for (int e = 0; e < nf; e++) w[g() % 64] ^= 1;
        }
        int na = 7, da = 8, ea = -1, nb = 7, db = 8, eb = -2;
        const bool oa = A::nidDecode(w, na, da, &ea), ob = R::nidDecode(w, nb, db, &eb);
        SAME(oa == ob && ea == eb && na == nb && da == db, "nidDecode %d/%d errs %d/%d", oa, ob, ea, eb);
        if (i % 16 == 0) SAME(A::nidDecode(w, na, da) == R::nidDecode(w, nb, db), "nidDecode no errs");
    }

    // Reed-Solomon: the three shapes of the standard and random shapes
    static const int shapes[][2] = {{36, 20}, {24, 12}, {24, 16}, {63, 51}, {63, 0}, {10, 10}, {1, 0}, {2, 1}};
    for (long i = 0; i < iters; i++) {
        int n, k;
        if (g() % 3 == 0) { n = 1 + (int)(g() % 63); k = (int)(g() % (unsigned)(n + 1)); }
        else { const int s = (int)(g() % 8); n = shapes[s][0]; k = shapes[s][1]; }
        uint8_t a[64], b[64];
        for (int j = 0; j < 64; j++) a[j] = b[j] = (uint8_t)(g() % 64);
        A::rsEncode(a, n, k); R::rsEncode(b, n, k);
        SAME(!memcmp(a, b, 64), "rsEncode n %d k %d", n, k);
        const int t = (n - k) / 2;
        const int ne = (int)(g() % (unsigned)(t + 4));
        for (int e = 0; e < ne; e++) b[g() % (unsigned)n] ^= (uint8_t)(1 + g() % 63);
        if (g() % 12 == 0) for (int j = 0; j < n; j++) b[j] = (uint8_t)(g() % 64);
        memcpy(a, b, 64);
        int ea = 5, eb = 6;
        const bool oa = A::rsDecode(a, n, k, &ea), ob = R::rsDecode(b, n, k, &eb);
        SAME(oa == ob && ea == eb && !memcmp(a, b, 64), "rsDecode n %d k %d errs injected %d: %d/%d %d/%d", n, k, ne, oa, ob, ea, eb);
        if (i % 8 == 0) { uint8_t c[64], d[64]; memcpy(c, b, 64); memcpy(d, b, 64); SAME(A::rsDecode(c, n, k) == R::rsDecode(d, n, k) && !memcmp(c, d, 64), "rsDecode no errs"); }
    }

    // Hamming, Golay, CRC
    for (int h = -2; h < 70; h++) SAME(A::hamming10Encode(h) == R::hamming10Encode(h) && A::golay18Encode(h) == R::golay18Encode(h), "encode small %d", h);
    for (int d = -2; d < 4200; d++) SAME(A::golay24Encode(d) == R::golay24Encode(d), "golay24Encode %d", d);
    for (long i = 0; i < iters; i++) {
        const uint32_t w = r32();
        int ea = -1, eb = -2;
        const int h1 = A::hamming10Decode((uint16_t)w, &ea), h2 = R::hamming10Decode((uint16_t)w, &eb);
        SAME(h1 == h2 && ea == eb, "hamming10Decode %u", w & 0xFFFF);
        const uint32_t g24 = g() % 3 ? (R::golay24Encode((int)(w & 0xFFF)) ^ (r32() & r32() & r32() & 0xFFFFFF)) : w;
        const int d1 = A::golay24Decode(g24, &ea), d2 = R::golay24Decode(g24, &eb);
        SAME(d1 == d2 && ea == eb, "golay24Decode %u", g24);
        const uint32_t g18 = g() % 3 ? (R::golay18Encode((int)(w & 63)) ^ (r32() & r32() & r32() & 0x3FFFF)) : w;
        const int e1 = A::golay18Decode(g18, &ea), e2 = R::golay18Decode(g18, &eb);
        SAME(e1 == e2 && ea == eb, "golay18Decode %u", g18);
        SAME(A::golay24Decode(g24) == R::golay24Decode(g24) && A::hamming10Decode((uint16_t)w) == R::hamming10Decode((uint16_t)w) && A::golay18Decode(g18) == R::golay18Decode(g18), "decode no errs");
        uint8_t bits[100]; for (auto& x : bits) x = (uint8_t)(g() % 5 ? (g() & 1) : g());
        const int n = (int)(g() % 101) - (g() % 20 == 0 ? 3 : 0);
        SAME(A::crcCcitt(bits, n) == R::crcCcitt(bits, n), "crc n %d", n);
    }

    // trellis
    for (long i = 0; i < iters; i++) {
        uint8_t bits[96]; for (auto& x : bits) x = (uint8_t)(g() & 1);
        uint8_t da[98], db[98];
        A::trellisEncode(bits, da); R::trellisEncode(bits, db);
        SAME(!memcmp(da, db, 98), "trellisEncode");
        float lv[98];
        const int mode = (int)(g() % 6);
        const float sigma = (float)(g() % 40) / 10.f;
        for (int k = 0; k < 98; k++) {
            const float clean = (float)R::dibitToSym(db[k]);
            const float noise = ((float)(g() & 0xFFFF) / 65536.f - 0.5f) * 2.f * sigma;
            switch (mode) {
            case 0: lv[k] = clean; break;
            case 1: case 2: lv[k] = clean + noise; break;
            case 3: lv[k] = ((float)r32() / 4294967296.f - 0.5f) * 12.f; break;
            case 4: lv[k] = g() % 40 == 0 ? std::nanf("") : clean + noise; break;
            default: lv[k] = g() % 60 == 0 ? INFINITY : (g() % 30 == 0 ? -1e30f : clean + noise); break;
            }
        }
        uint8_t oa[96], ob[96];
        memset(oa, 0, 96); memset(ob, 0, 96);
        float ma = -1, mb = -2;
        const bool ra = A::trellisDecode(lv, oa, &ma), rb = R::trellisDecode(lv, ob, &mb);
        SAME(ra == rb && fEq(ma, mb) && (!ra || !memcmp(oa, ob, 96)), "trellisDecode mode %d ok %d/%d metric %g/%g", mode, ra, rb, ma, mb);
        if (i % 8 == 0) SAME(A::trellisDecode(lv, oa) == R::trellisDecode(lv, ob), "trellisDecode no metric");
    }

    // TSBK, link control, encryption sync, header word, IMBE
    for (long i = 0; i < iters; i++) {
        A::Tsbk ta; R::Tsbk tb;
        ta.last = tb.last = g() & 1; ta.prot = tb.prot = g() & 1; ta.opcode = tb.opcode = (int)r32(); ta.mfid = tb.mfid = (int)r32(); ta.args = tb.args = r64();
        uint8_t ba[96], bb[96];
        A::tsbkBits(ta, ba); R::tsbkBits(tb, bb);
        SAME(!memcmp(ba, bb, 96), "tsbkBits");
        if (g() % 2) bb[g() % 96] ^= 1;
        if (g() % 10 == 0) for (auto& x : bb) x = (uint8_t)(g() & 1);
        A::Tsbk pa; R::Tsbk pb;
        pa.mfid = pb.mfid = 99;
        const bool oa = A::tsbkParse(bb, pa), ob = R::tsbkParse(bb, pb);
        SAME(oa == ob && pa.last == pb.last && pa.prot == pb.prot && pa.opcode == pb.opcode && pa.mfid == pb.mfid && pa.args == pb.args, "tsbkParse");

        A::Lc la; R::Lc lb;
        const int lcos[] = {A::kLcGroup, A::kLcUnit, A::kLcTerm, (int)(g() % 64), (int)r32()};
        la.lco = lb.lco = lcos[g() % 5]; la.mfid = lb.mfid = g() % 3 ? (int)(g() % 3) : (int)r32(); la.prot = lb.prot = g() & 1;
        la.svcOpts = lb.svcOpts = (int)r32(); la.tg = lb.tg = g() % 4 ? (int)(g() % 70000) : -1; la.src = lb.src = (int)r32(); la.dst = lb.dst = g() % 4 ? (int)(g() % 20000000) : -(int)(g() % 5); la.raw = lb.raw = r64();
        uint8_t ha[12], hb[12];
        A::lcHexbits(la, ha); R::lcHexbits(lb, hb);
        SAME(!memcmp(ha, hb, 12), "lcHexbits lco %d", la.lco);
        uint8_t hin[12];
        for (auto& x : hin) x = (uint8_t)(g() % 20 == 0 ? g() : g() % 64);
        if (g() % 2) memcpy(hin, hb, 12);
        A::Lc xa; R::Lc xb;
        xa.tg = xb.tg = 4242;
        const bool qa = A::lcParse(hin, xa), qb = R::lcParse(hin, xb);
        SAME(qa == qb && xa.lco == xb.lco && xa.mfid == xb.mfid && xa.prot == xb.prot && xa.svcOpts == xb.svcOpts && xa.tg == xb.tg && xa.src == xb.src && xa.dst == xb.dst && xa.raw == xb.raw, "lcParse");

        A::Crypto ca; R::Crypto cb;
        for (int k = 0; k < 9; k++) ca.mi[k] = cb.mi[k] = (uint8_t)g();
        ca.algId = cb.algId = (int)r32(); ca.keyId = cb.keyId = (int)r32(); ca.mfid = cb.mfid = (int)r32(); ca.tg = cb.tg = g() % 4 ? (int)(g() % 70000) : -1;
        uint8_t e1[16], e2[16], u1[20], u2[20];
        A::esHexbits(ca, e1); R::esHexbits(cb, e2);
        A::hduHexbits(ca, u1); R::hduHexbits(cb, u2);
        SAME(!memcmp(e1, e2, 16) && !memcmp(u1, u2, 20), "es / hdu hexbits");
        uint8_t hx[20]; for (auto& x : hx) x = (uint8_t)(g() % 30 == 0 ? g() : g() % 64);
        A::Crypto sa; R::Crypto sb;
        sa.mfid = sb.mfid = 17; sa.tg = sb.tg = 18;
        A::esParse(hx, sa); R::esParse(hx, sb);
        SAME(!memcmp(sa.mi, sb.mi, 9) && sa.algId == sb.algId && sa.keyId == sb.keyId && sa.mfid == sb.mfid && sa.tg == sb.tg, "esParse");
        A::hduParse(hx, sa); R::hduParse(hx, sb);
        SAME(!memcmp(sa.mi, sb.mi, 9) && sa.algId == sb.algId && sa.keyId == sb.keyId && sa.mfid == sb.mfid && sa.tg == sb.tg, "hduParse");

        uint8_t d72[72]; for (auto& x : d72) x = (uint8_t)(g() % 7 == 0 ? g() : g() & 3);
        char fa[8][23], fb[8][23];
        A::imbeDeinterleave(d72, fa); R::imbeDeinterleave(d72, fb);
        SAME(!memcmp(fa, fb, sizeof fa), "imbeDeinterleave");
        uint8_t i1[72], i2[72];
        char m[8][23]; for (auto& row : m) for (auto& c : row) c = (char)g();
        A::imbeInterleave(m, i1); R::imbeInterleave(m, i2);
        SAME(!memcmp(i1, i2, 72), "imbeInterleave");
        uint8_t f1[144], f2[144];
        A::imbeFlatten(m, f1); R::imbeFlatten(m, f2);
        SAME(!memcmp(f1, f2, 144), "imbeFlatten");
        char u1c[8][23], u2c[8][23];
        uint8_t bits144[144]; for (auto& x : bits144) x = (uint8_t)g();
        A::imbeUnflatten(bits144, u1c); R::imbeUnflatten(bits144, u2c);
        SAME(!memcmp(u1c, u2c, sizeof u1c), "imbeUnflatten");
    }
    printf("p25_proto_rs: %ld checks, %ld mismatches\n", checks, fails);
    return fails ? 1 : 0;
}
