// Differential test of the VDL Mode 2 layers: the Rust-backed dect2::vdl2* functions (DECT2_RUST_VDL2) against the frozen C++ copy dect2_vdl2_ref::
// (cmake/gen_rs_ref.cmake) on identical inputs: the scrambler, every one of the 2^25 header words, every burst length, Reed-Solomon with random errors and
// erasures, burst bodies (built by the generator, with bit errors), the FCS, the HDLC deframer on built / damaged / random bit streams, addresses, and AVLC frame
// decoding of built frames (ACARS, X.25 with CLNP / COTP, call set-up, XID GSIF / link establishment), random X.25 packets and random frames. Doubles bit for bit.
// ONAIR_DIFF_ITERS (default 200000) inputs per family.
#include "dect2/vdl2/vdl2_proto.h"
#include "vdl2_proto_ref.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>
namespace R = dect2_vdl2_ref;

static long fails = 0, checks = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 25) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)
static std::mt19937 g(1029);
static uint32_t r32() { return g(); }
static uint8_t rb() { return (uint8_t)g(); }
static bool dEq(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }

static bool msgEq(const dect2::Vdl2Message& a, const dect2::Vdl2Message& b) {
    return a.src == b.src && a.dst == b.dst && a.srcType == b.srcType && a.dstType == b.dstType && a.fromAir == b.fromAir && a.fromGround == b.fromGround &&
           a.onGround == b.onGround && a.response == b.response && a.frameKind == b.frameKind && a.frameName == b.frameName && a.ns == b.ns && a.nr == b.nr && a.pf == b.pf &&
           a.proto == b.proto && a.summary == b.summary && a.detail == b.detail && a.acars == b.acars && a.reg == b.reg && a.flight == b.flight && a.label == b.label &&
           a.labelText == b.labelText && a.msgNum == b.msgNum && a.acarsText == b.acarsText && a.blockId == b.blockId && a.acarsCrcOk == b.acarsCrcOk && a.hasPos == b.hasPos &&
           dEq(a.lat, b.lat) && dEq(a.lon, b.lon) && a.hasAlt == b.hasAlt && a.altFt == b.altFt && a.posKind == b.posKind && a.hasKin == b.hasKin &&
           dEq(a.kinTrackDeg, b.kinTrackDeg) && dEq(a.groundKt, b.groundKt) && a.vsFpm == b.vsFpm;
}

static std::vector<uint8_t> mutate(std::vector<uint8_t> v) {
    for (int k = (int)(g() % 4); k > 0; k--) {
        if (v.empty()) { v.push_back(rb()); continue; }
        switch (g() % 5) {
        case 0: v[g() % v.size()] = rb(); break;
        case 1: v[g() % v.size()] ^= (uint8_t)(1u << (g() % 8)); break;
        case 2: v.erase(v.begin() + (long)(g() % v.size())); break;
        case 3: v.insert(v.begin() + (long)(g() % (v.size() + 1)), rb()); break;
        default: v.resize(g() % (v.size() + 1)); break;
        }
    }
    return v;
}
static std::vector<uint8_t> rndBytes(size_t n) { std::vector<uint8_t> v(n); for (auto& b : v) b = rb(); return v; }
static std::string rndStr(size_t n, const char* a, size_t al) { std::string s; for (size_t i = 0; i < n; i++) s += a[g() % al]; return s; }
#define RS(n, lit) rndStr((n), lit, sizeof(lit) - 1)

static void testBitLayer(long iters) {
    // header words: every 25 bit word, with preset outputs to catch what is (not) written
    for (uint32_t w = 0; w < (1u << 25); w++) {
        uint32_t la = 0xDEADBEEF, lb = 0xDEADBEEF;
        bool ca = false, cb = true;
        const bool oa = dect2::vdl2HeaderDecode(w, la, ca), ob = R::vdl2HeaderDecode(w, lb, cb);
        if (oa != ob || la != lb || ca != cb) SAME(false, "header decode %07x: %d/%d len %x/%x corr %d/%d", w, oa, ob, la, lb, ca, cb);
    }
    checks += 1u << 25;
    for (uint32_t len = 0; len < 140000; len++) {
        SAME(dect2::vdl2HeaderWord(len) == R::vdl2HeaderWord(len), "header word %u", len);
        dect2::Vdl2Layout a; R::Vdl2Layout b;
        const bool oa = dect2::vdl2Layout(len, a), ob = R::vdl2Layout(len, b);
        SAME(oa == ob && a.dataOct == b.dataOct && a.fecOct == b.fecOct && a.blocks == b.blocks && a.lastLen == b.lastLen && a.lastFec == b.lastFec, "layout %u", len);
    }
    for (long i = 0; i < iters; i++) {
        const uint32_t len = r32();
        dect2::Vdl2Layout a; R::Vdl2Layout b;
        const bool oa = dect2::vdl2Layout(len, a), ob = R::vdl2Layout(len, b);
        SAME(oa == ob && a.dataOct == b.dataOct && a.fecOct == b.fecOct && a.blocks == b.blocks && a.lastLen == b.lastLen && a.lastFec == b.lastFec, "layout %u", len);
        SAME(dect2::vdl2HeaderWord(len) == R::vdl2HeaderWord(len), "header word");
    }
    for (int n = -5; n < 400; n++) SAME(dect2::vdl2LastFec(n) == R::vdl2LastFec(n), "last fec %d", n);
    // scrambler
    for (long i = 0; i < iters; i++) {
        std::vector<uint8_t> a(g() % 3000);
        for (auto& b : a) b = g() % 2;
        auto b = a;
        const size_t from = g() % (a.size() + 3);
        dect2::vdl2Scramble(a, from); R::vdl2Scramble(b, from);
        SAME(a == b, "scramble from %zu of %zu", from, a.size());
    }
}

static void testRs(long iters) {
    long corrected = 0, refused = 0;
    for (long i = 0; i < iters; i++) {
        uint8_t data[249];
        for (auto& b : data) b = rb();
        uint8_t pa[6], pb[6];
        dect2::vdl2RsEncode(data, pa); R::vdl2RsEncode(data, pb);
        SAME(!memcmp(pa, pb, 6), "rs encode");
        uint8_t cw[255];
        memcpy(cw, data, 249); memcpy(cw + 249, pa, 6);
        const int np = (int)(g() % 7);
        if (np < 6) memset(cw + 249 + np, 0, (size_t)(6 - np));      // erased parity octets are zero (as the burst gatherer leaves them)
        const int errs = g() % 3 == 0 ? (int)(g() % 9) : (int)(g() % 4);
        for (int e = 0; e < errs; e++) cw[g() % 255] ^= (uint8_t)(1 + g() % 255);
        uint8_t a[255], b[255];
        memcpy(a, cw, 255); memcpy(b, cw, 255);
        const int ra = dect2::vdl2RsDecode(a, np), rr = R::vdl2RsDecode(b, np);
        SAME(ra == rr && !memcmp(a, b, 255), "rs decode np %d errs %d: %d vs %d", np, errs, ra, rr);
        (ra < 0 ? refused : corrected)++;
    }
    printf("  reed-solomon: %ld decoded, %ld refused\n", corrected, refused);
}

static void testBurst(long iters) {
    long ok = 0;
    for (long i = 0; i < iters; i++) {
        const size_t nbits = 8 + g() % 4000 + (g() % 40 == 0 ? g() % 20000 : 0);
        std::vector<uint8_t> data(nbits);
        for (auto& b : data) b = g() % 2;
        std::vector<uint8_t> burst = R::vdl2BurstBits(data);       // header, data, FEC, scrambled
        if (burst.size() < 25) continue;
        R::vdl2Scramble(burst, 0);                                 // descramble the lot
        const size_t body = burst.size() - 25;
        const int flips = g() % 4 == 0 ? (int)(g() % 40) : (int)(g() % 5);
        for (int k = 0; k < flips && body; k++) burst[25 + g() % body] ^= 1;
        if (g() % 12 == 0) for (size_t k = 25; k < burst.size(); k++) burst[k] = g() % 2;   // noise
        dect2::Vdl2Layout la; R::Vdl2Layout lr;
        const uint32_t len = (uint32_t)data.size();
        dect2::vdl2Layout(len, la); R::vdl2Layout(len, lr);
        std::vector<uint8_t> da{1, 2, 3}, dr{1, 2, 3};
        int fa = -7, fr = -7;
        const bool oa = dect2::vdl2BurstData(burst.data() + 25, la, len, da, fa), orr = R::vdl2BurstData(burst.data() + 25, lr, len, dr, fr);
        SAME(oa == orr && da == dr && fa == fr, "burst data len %u flips %d: %d/%d fixed %d/%d", len, flips, oa, orr, fa, fr);
        ok += oa;
    }
    printf("  burst bodies: %ld corrected\n", ok);
}

static void testHdlc(long iters) {
    long framesSeen = 0;
    for (long i = 0; i < iters; i++) {
        const size_t nf = 1 + g() % 3;
        std::vector<std::vector<uint8_t>> frames;
        for (size_t k = 0; k < nf; k++) {
            std::vector<uint8_t> f = rndBytes(g() % 12 == 0 ? g() % 3 : 2 + g() % 40);
            if (g() % 3) for (auto& b : f) if (g() % 4 == 0) b = g() % 2 ? 0x7E : 0xFF;
            if (g() % 2) { const uint16_t c = dect2::vdl2Fcs(f.data(), f.size()); f.push_back((uint8_t)c); f.push_back((uint8_t)(c >> 8)); }
            frames.push_back(f);
        }
        std::vector<uint8_t> bits = dect2::vdl2HdlcBits(frames);
        if (g() % 3 == 0) for (int k = (int)(g() % 5); k > 0 && !bits.empty(); k--) bits[g() % bits.size()] ^= 1;
        if (g() % 8 == 0) bits.resize(g() % (bits.size() + 1));
        if (g() % 10 == 0) { bits.clear(); const size_t n = g() % 300; for (size_t k = 0; k < n; k++) bits.push_back(g() % 5 ? 1 : 0); }
        const auto a = dect2::vdl2HdlcFrames(bits), b = R::vdl2HdlcFrames(bits);
        SAME(a == b, "hdlc frames %zu vs %zu", a.size(), b.size());
        framesSeen += (long)a.size();
        for (const auto& f : frames) SAME(dect2::vdl2FcsOk(f.data(), f.size()) == R::vdl2FcsOk(f.data(), f.size()) && dect2::vdl2Fcs(f.data(), f.size()) == R::vdl2Fcs(f.data(), f.size()), "fcs");
    }
    printf("  hdlc: %ld frames extracted\n", framesSeen);
}

static void testAddr(long iters) {
    for (int t = -3; t < 20; t++) SAME(!strcmp(dect2::vdl2AddrTypeName(t), R::vdl2AddrTypeName(t)), "addr type name %d", t);
    for (long i = 0; i < iters; i++) {
        dect2::Vdl2Addr a; R::Vdl2Addr b;
        a.addr = b.addr = r32(); a.type = b.type = (int)(g() % 16); a.status = b.status = g() % 2;
        const bool last = g() % 2;
        uint8_t oa[4], ob[4];
        dect2::vdl2PutAddr(oa, a, last); R::vdl2PutAddr(ob, b, last);
        SAME(!memcmp(oa, ob, 4), "put addr");
        uint8_t raw[4] = {rb(), rb(), rb(), rb()};
        const dect2::Vdl2Addr x = dect2::vdl2GetAddr(raw);
        const R::Vdl2Addr y = R::vdl2GetAddr(raw);
        SAME(x.addr == y.addr && x.type == y.type && x.status == y.status, "get addr");
    }
}

static std::vector<uint8_t> info(int kind) {
    switch (kind) {
    case 0: return dect2::vdl2AcarsInfo(RS(7, "ABCDEFGHIJ-.0123"), RS(2, "ABCH1_2Q0"), (char)('0' + g() % 10), RS(g() % 80, "ABC xyz 0123 N25E055 /.-,\n"));
    case 1: {
        std::vector<uint8_t> a = rndBytes(g() % 20), b = rndBytes(g() % 20), u = rndBytes(g() % 50);
        return dect2::vdl2X25ClnpData((int)(g() % 4096), (int)(g() % 8), (int)(g() % 8), a, b, u);
    }
    case 2: return dect2::vdl2X25CallRequest((int)(g() % 4096), RS(g() % 12, "0123456789"), RS(g() % 12, "0123456789"));
    case 3: return dect2::vdl2XidGsif((double)((int)(g() % 1800) - 900) / 10.0, (double)((int)(g() % 3600) - 1800) / 10.0, RS(g() % 30, "OMDBLTBYEGLL "));
    case 4: return dect2::vdl2XidAircraftLe((double)((int)(g() % 1800) - 900) / 10.0, (double)((int)(g() % 3600) - 1800) / 10.0, (int)(g() % 45000), RS(g() % 6, "OMDB"));
    default: return rndBytes(g() % 60);
    }
}

// a random X.25 packet: the packet types of the decoder and random bytes around them
static std::vector<uint8_t> x25Packet() {
    static const uint8_t types[] = {0x0B, 0x0F, 0x13, 0x17, 0x1B, 0x1F, 0xFB, 0xFF, 0x23, 0x27, 0xF1, 0x01, 0x21, 0x05, 0x25, 0x09, 0x29, 0x00, 0x02, 0x10, 0x32};
    std::vector<uint8_t> p = {(uint8_t)(0x10 | g() % 16), rb(), g() % 3 ? types[g() % sizeof types] : rb()};
    const size_t n = g() % 30;
    for (size_t i = 0; i < n; i++) p.push_back(rb());
    if (!(p[2] & 1) && g() % 2) { p.push_back(g() % 2 ? 0x81 : (g() % 2 ? 0x82 : 0x85)); for (size_t i = g() % 25; i > 0; i--) p.push_back(rb()); if (p[3] == 0x81 && p.size() > 8) { p[4] = (uint8_t)(g() % 40); p[7] = 0x1C; } }
    return p;
}

static void testFrames(long iters) {
    long parsed = 0, withPos = 0, acars = 0, x25 = 0, xidn = 0;
    for (long i = 0; i < iters; i++) {
        dect2::Vdl2Addr d, s;
        d.addr = s.addr = r32(); s.addr = r32(); d.type = (int)(g() % 8); s.type = g() % 2 ? 1 : (int)(g() % 8); d.status = g() % 2; s.status = g() % 2;
        std::vector<uint8_t> f;
        const int mode = (int)(g() % 12);
        if (mode < 7) {
            const int kind = mode;
            std::vector<uint8_t> inf = kind == 6 ? x25Packet() : info(kind);
            uint8_t ctrl;
            switch (g() % 5) {
            case 0: ctrl = dect2::vdl2ControlI((int)(g() % 8), (int)(g() % 8), g() % 2); break;
            case 1: ctrl = (uint8_t)(0x01 | (g() % 4) << 2 | (g() % 2) << 4 | (g() % 8) << 5); break;
            case 2: ctrl = (uint8_t)(dect2::kVdl2CtrlXID | (g() % 2 ? 0x10 : 0)); break;
            case 3: ctrl = (uint8_t)(dect2::kVdl2CtrlUI | (g() % 2 ? 0x10 : 0)); break;
            default: ctrl = rb(); break;
            }
            if (kind == 3 || kind == 4) ctrl = (uint8_t)(dect2::kVdl2CtrlXID | (g() % 2 ? 0x10 : 0));
            f = dect2::vdl2Frame(d, s, ctrl, inf);
            f.resize(f.size() - 2);                                  // the FCS is checked before parsing
            if (mode == 5 || g() % 9 == 0) f = mutate(f);
        } else {
            f = rndBytes(g() % 40);
            if (f.size() >= 9 && g() % 2) { const dect2::Vdl2Addr x{r32() & 0xFFFFFF, (int)(g() % 8), false}; dect2::vdl2PutAddr(&f[0], x, false); dect2::vdl2PutAddr(&f[4], d, true); }
        }
        dect2::Vdl2Message a, b;
        const bool oa = dect2::vdl2ParseFrame(f.data(), f.size(), a), ob = R::vdl2ParseFrame(f.data(), f.size(), b);
        SAME(oa == ob && (!oa || msgEq(a, b)), "parse mode %d size %zu: '%s' vs '%s'", mode, f.size(), a.summary.c_str(), b.summary.c_str());
        parsed += oa; withPos += oa && a.hasPos; acars += oa && a.acars; x25 += oa && a.proto == "X.25"; xidn += oa && a.proto == "XID";
    }
    printf("  frames: %ld parsed, %ld with a position, %ld ACARS, %ld X.25, %ld XID\n", parsed, withPos, acars, x25, xidn);
}

int main() {
    const char* itc = getenv("ONAIR_DIFF_ITERS");
    const long iters = itc ? atol(itc) : 200000;
    testBitLayer(iters);
    testRs(iters);
    testBurst(iters / 4);
    testHdlc(iters);
    testAddr(iters);
    testFrames(iters);
    printf("test_vdl2_proto_rs: %ld checks, %ld mismatches\n", checks, fails);
    return fails ? 1 : 0;
}
