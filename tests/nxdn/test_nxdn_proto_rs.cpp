// Differential test: the C++ NXDN frame layer (a generated copy in dect2::nxdn_ref, see cmake/gen_rs_ref.cmake) against the Rust one (dect2::nxdn when
// DECT2_RUST is used). The same inputs go to both and every result has to be identical: scrambler, LICH (all 256 bytes), CRCs, the three coded channels
// (clean, with bit errors and random soft values), layer 3 build / parse and the names, over ONAIR_DIFF_ITERS (default 200000) seeded inputs per family.
#include "dect2/nxdn/nxdn_proto.h"
#include "nxdn_proto_ref.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
namespace A = dect2::nxdn;
namespace R = dect2::nxdn_ref;

static long fails = 0, checks = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)

static std::mt19937 g(4242);
static float rndSoft() {
    switch (g() % 12) {
    case 0: return 0.f;
    case 1: return (float)(g() % 7) - 3.f;
    case 2: return ((float)g() / 4294967296.f - 0.5f) * 1e9f;
    case 3: return g() % 50 == 0 ? std::nanf("") : 1.f;
    case 4: return g() % 50 == 0 ? INFINITY : -1.f;
    default: return ((float)g() / 4294967296.f - 0.5f) * 8.f;
    }
}
template <class T, class U> static bool msgEq(const T& a, const U& b) {
    return a.type == b.type && a.callType == b.callType && a.option == b.option && a.src == b.src && a.dst == b.dst && a.cipher == b.cipher &&
           a.keyId == b.keyId && a.status == b.status && a.block == b.block && a.blocks == b.blocks && a.text == b.text && a.location == b.location &&
           a.service == b.service && a.channel == b.channel;
}

int main() {
    const char* it = getenv("ONAIR_DIFF_ITERS");
    const long iters = it ? atol(it) : 200000;
    printf("nxdn_proto_rs: implementation under test is dect2::nxdn (Rust when DECT2_RUST_NXDN)\n");

    // the table and the 8-bit LICH space
    SAME(!memcmp(A::kFsw, R::kFsw, sizeof A::kFsw), "kFsw");
    for (int v = 0; v < 256; v++) {
        A::Lich a; R::Lich b;
        const bool ra = A::lichDecode((uint8_t)v, a), rb = R::lichDecode((uint8_t)v, b);
        SAME(ra == rb && (!ra || (a.rfct == b.rfct && a.fct == b.fct && a.opt == b.opt && a.dir == b.dir)), "lichDecode %d", v);
    }
    for (long i = 0; i < iters; i++) {
        const int f[4] = {(int)g() - (1 << 30), (int)g() - (1 << 30), (int)g() - (1 << 30), (int)g() - (1 << 30)};
        A::Lich a{f[0], f[1], f[2], f[3]}; R::Lich b{f[0], f[1], f[2], f[3]};
        SAME(A::lichEncode(a) == R::lichEncode(b), "lichEncode %d %d %d %d", f[0], f[1], f[2], f[3]);
    }
    for (int t = -5; t < 300; t++) {
        SAME(!strcmp(A::msgName(t), R::msgName(t)) && !strcmp(A::callTypeName(t), R::callTypeName(t)), "names %d", t);
    }

    // scrambler and CRCs
    for (long i = 0; i < iters; i++) {
        const int n = (int)(g() % 420);
        std::vector<uint8_t> a((size_t)n + 1, 0xAA), b((size_t)n + 1, 0xAA);
        A::pn9(a.data(), n); R::pn9(b.data(), n);
        SAME(a == b, "pn9 %d", n);
        std::vector<uint8_t> bits((size_t)n + 1);
        for (auto& x : bits) x = (uint8_t)(g() % 5 == 0 ? g() : (g() & 1));
        SAME(A::crc6(bits.data(), n) == R::crc6(bits.data(), n) && A::crc12(bits.data(), n) == R::crc12(bits.data(), n) && A::crc16(bits.data(), n) == R::crc16(bits.data(), n), "crc n %d", n);
    }
    SAME(A::crc6(nullptr, -3) == R::crc6(nullptr, -3) && A::crc16(nullptr, 0) == R::crc16(nullptr, 0), "crc empty");

    // the channels
    const A::Ch ach[3] = {A::Ch::Sacch, A::Ch::Facch1, A::Ch::Cac};
    const R::Ch rch[3] = {R::Ch::Sacch, R::Ch::Facch1, R::Ch::Cac};
    for (int ci = 0; ci < 3; ci++) {
        SAME(A::infoBits(ach[ci]) == R::infoBits(rch[ci]) && A::airBits(ach[ci]) == R::airBits(rch[ci]), "sizes %d", ci);
        const int ni = R::infoBits(rch[ci]), na = R::airBits(rch[ci]);
        for (long i = 0; i < iters; i++) {
            std::vector<uint8_t> info((size_t)ni), ai((size_t)na, 7), ar((size_t)na, 9);
            for (auto& x : info) x = (uint8_t)(g() % 9 == 0 ? g() : (g() & 1));
            A::encode(ach[ci], info.data(), ai.data()); R::encode(rch[ci], info.data(), ar.data());
            SAME(ai == ar, "encode ch %d", ci);
            // soft values: the clean codeword with 0..14 errors, scaled and noisy, or pure random
            std::vector<float> soft((size_t)na);
            const int mode = (int)(g() % 4);
            if (mode == 3) { for (auto& s : soft) s = rndSoft(); }
            else {
                for (int k = 0; k < na; k++) soft[(size_t)k] = (ar[(size_t)k] ? 1.f : -1.f) * (0.2f + (float)(g() % 100) / 50.f);
                const int errs = mode == 0 ? 0 : (int)(g() % 15);
                for (int e = 0; e < errs; e++) { float& s = soft[g() % (unsigned)na]; s = -s; }
            }
            std::vector<uint8_t> oa((size_t)ni, 3), ob((size_t)ni, 5);
            int ea = -1, eb = -2;
            const bool da = A::decode(ach[ci], soft.data(), oa.data(), &ea), db = R::decode(rch[ci], soft.data(), ob.data(), &eb);
            SAME(da == db && oa == ob && (!da || ea == eb), "decode ch %d ok %d/%d errs %d/%d", ci, da, db, ea, eb);
            int e1 = -1, e2 = -1;
            if (i % 8 == 0) { SAME(A::decode(ach[ci], soft.data(), oa.data()) == R::decode(rch[ci], soft.data(), ob.data()) && oa == ob, "decode no errs ptr"); (void)e1; (void)e2; }
        }
    }

    // layer 3: built from random fields, parsed from random bits (known types over-represented), every length
    static const int types[] = {A::kVcall, A::kTxRel, A::kIdle, A::kSiteInfo, A::kSrvInfo, A::kStatReq, A::kSdHeader, A::kSdData};
    for (long i = 0; i < iters; i++) {
        A::Msg a; R::Msg b;
        a.type = b.type = g() % 4 ? types[g() % 8] : (int)(g() % 80) - 5;
        a.callType = b.callType = (int)(g() % 9) - 1; a.option = b.option = (int)g(); a.src = b.src = (int)g(); a.dst = b.dst = (int)g();
        a.cipher = b.cipher = (int)g(); a.keyId = b.keyId = (int)g(); a.status = b.status = (int)g(); a.block = b.block = (int)g();
        a.blocks = b.blocks = (int)g(); a.location = b.location = g(); a.service = b.service = (int)g(); a.channel = b.channel = (int)g();
        std::string t; for (size_t k = g() % 12; k > 0; k--) t += (char)(g() % 4 ? 32 + g() % 95 : g() & 0xFF);
        a.text = b.text = t;
        const int maxBits = (int)(g() % 130) - 3;
        std::vector<uint8_t> ba((size_t)(maxBits > 0 ? maxBits : 0) + 1, 0x55), bb(ba);
        const int na = A::buildMsg(a, ba.data(), maxBits), nb = R::buildMsg(b, bb.data(), maxBits);
        SAME(na == nb && ba == bb, "buildMsg type %d max %d n %d/%d", a.type, maxBits, na, nb);
        // parse the result and random bits
        std::vector<uint8_t> bits(g() % 200);
        for (auto& x : bits) x = (uint8_t)(g() % 7 == 0 ? g() : (g() & 1));
        if (!bits.empty() && g() % 4) {   // a known type in the first byte
            const int ty = types[g() % 8];
            for (int k = 0; k < 8 && k < (int)bits.size(); k++) bits[(size_t)k] = (uint8_t)((ty >> (7 - k)) & 1);
        }
        const int nb2 = (int)bits.size() + (int)(g() % 5) - 2;
        A::Msg pa; R::Msg pb;
        pa.src = pb.src = 77;   // must be reset
        const bool qa = A::parseMsg(bits.data(), nb2 > (int)bits.size() ? (int)bits.size() : nb2, pa), qb = R::parseMsg(bits.data(), nb2 > (int)bits.size() ? (int)bits.size() : nb2, pb);
        SAME(qa == qb && msgEq(pa, pb), "parseMsg n %d ok %d/%d type %d/%d", nb2, qa, qb, pa.type, pb.type);
        const int nn = na < maxBits ? na : maxBits;
        if (nn > 0) {
            A::Msg ra; R::Msg rb;
            const bool xa = A::parseMsg(ba.data(), nn, ra), xb = R::parseMsg(ba.data(), nn, rb);
            SAME(xa == xb && msgEq(ra, rb), "parse of built");
        }
    }
    for (long i = 0; i < iters / 4; i++) {
        uint8_t bits[72]; for (auto& x : bits) x = (uint8_t)(g() & 0xFF);
        char fa[4][24], fb[4][24];
        A::ambeFrame(bits, fa); R::ambeFrame(bits, fb);
        SAME(!memcmp(fa, fb, sizeof fa), "ambeFrame");
    }
    printf("nxdn_proto_rs: %ld checks, %ld mismatches\n", checks, fails);
    return fails ? 1 : 0;
}
