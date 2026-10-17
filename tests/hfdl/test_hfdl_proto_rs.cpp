// Differential test of the HFDL layers: the Rust-backed dect2::hfdl* functions (DECT2_RUST_HFDL) against the frozen C++ copy dect2_hfdl_ref:: (cmake/gen_rs_ref.cmake)
// on identical inputs: rates and burst sizes, the A / M sequences and the preamble, the scrambler, the interleavers, the convolutional encoder, the Viterbi decoder
// (random soft values and encoded data with noise, every rate), the bit chain of every rate, bursts, the FCS, the station table and masks, and the PDU parser on built
// (SPDU, MPDU up and down, logon, unnumbered data with performance / frequency data, ACARS, system table), mutated and random PDUs. Doubles bit for bit.
// ONAIR_DIFF_ITERS (default 200000) inputs per family.
#include "dect2/hfdl/hfdl_proto.h"
#include "hfdl_proto_ref.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>
namespace R = dect2_hfdl_ref;

static long fails = 0, checks = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 25) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)
static std::mt19937 g(2718);
static uint8_t rb() { return (uint8_t)g(); }
static bool dEq(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }
static bool fEq(float a, float b) { return memcmp(&a, &b, sizeof a) == 0; }

static bool msgEq(const dect2::HfdlMessage& a, const dect2::HfdlMessage& b) {
    return a.squitter == b.squitter && a.uplink == b.uplink && a.gsId == b.gsId && a.acId == b.acId && a.icao == b.icao && a.kind == b.kind && a.hfnpdu == b.hfnpdu &&
           a.summary == b.summary && a.detail == b.detail && a.frameIndex == b.frameIndex && a.utcSync == b.utcSync && a.activeKhz == b.activeKhz && a.acars == b.acars &&
           a.reg == b.reg && a.flight == b.flight && a.label == b.label && a.labelText == b.labelText && a.msgNum == b.msgNum && a.acarsText == b.acarsText &&
           a.blockId == b.blockId && a.acarsCrcOk == b.acarsCrcOk && a.hasPos == b.hasPos && dEq(a.lat, b.lat) && dEq(a.lon, b.lon) && a.hasAlt == b.hasAlt &&
           a.altFt == b.altFt && a.posKind == b.posKind && a.utcSec == b.utcSec;
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
static double rndCoord(double r) { return ((double)(g() % 2000001) / 1000000.0 - 1.0) * r; }

static void testBitLayer(long iters) {
    for (int m = -3; m < 12; m++) {
        const auto &a = dect2::hfdlRate(m); const auto &b = R::hfdlRate(m);
        SAME(a.m1 == b.m1 && a.bps == b.bps && a.bitsPerSym == b.bitsPerSym && a.quarter == b.quarter && a.twoSlots == b.twoSlots && a.blocks == b.blocks && a.colShift == b.colShift, "rate %d", m);
        SAME(dect2::hfdlBurstSyms(m) == R::hfdlBurstSyms(m) && dect2::hfdlCodedBits(m) == R::hfdlCodedBits(m) && dect2::hfdlPayloadOctets(m) == R::hfdlPayloadOctets(m), "sizes %d", m);
    }
    for (int k = 0; k < 127; k++) {
        SAME(dect2::hfdlABit(k) == R::hfdlABit(k), "A bit %d", k);
        for (int s = 0; s < 8; s++) SAME(dect2::hfdlMBit(s, k) == R::hfdlMBit(s, k), "M bit %d %d", s, k);
    }
    for (int m = 0; m < 8; m++) {
        SAME(dect2::hfdlPreambleRef(m) == R::hfdlPreambleRef(m), "preamble %d", m);
        SAME(dect2::hfdlInterleave(m) == R::hfdlInterleave(m), "interleaver %d", m);
    }
    for (int n = 0; n < 1200; n++) {
        std::vector<int8_t> a, b;
        dect2::hfdlScramblerSigns(n, a); R::hfdlScramblerSigns(n, b);
        SAME(a == b, "scrambler %d", n);
    }
    for (long i = 0; i < iters / 20; i++) {
        std::vector<uint8_t> bits(g() % 400);
        for (auto& x : bits) x = g() & 1;
        if (g() % 4 == 0) for (auto& x : bits) x |= (uint8_t)(g() % 4 ? 0 : 0xFE);   // only the low bit counts
        SAME(dect2::hfdlConvEncode(bits) == R::hfdlConvEncode(bits), "conv encode %zu", bits.size());
    }
    // Viterbi: random soft values, and encoded data with noise
    for (long i = 0; i < iters / 20; i++) {
        const int n = (int)(g() % 300);
        std::vector<float> soft((size_t)n * 2);
        const int kind = (int)(g() % 3);
        if (kind == 0) { for (auto& s : soft) s = (float)((int)(g() % 2001) - 1000) / 100.f; }
        else {
            std::vector<uint8_t> bits((size_t)n);
            for (auto& x : bits) x = g() & 1;
            const auto c = R::hfdlConvEncode(bits);
            const float sigma = kind == 1 ? 0.5f : 1.5f;
            for (size_t j = 0; j < soft.size(); j++) soft[j] = (c[j] ? -1.f : 1.f) + sigma * (float)((int)(g() % 2001) - 1000) / 500.f;
        }
        const auto a = dect2::hfdlViterbi(soft, n), b = R::hfdlViterbi(soft, n);
        SAME(a == b, "viterbi n=%d kind %d", n, kind);
    }
    // the bit chain of every rate: encode -> channel soft values (with noise, some erased) -> decode
    for (int m = 0; m < 8; m++) {
        for (long i = 0; i < iters / 400 + 2; i++) {
            std::vector<uint8_t> pdu(g() % (size_t)(dect2::hfdlPayloadOctets(m) + 1));
            for (auto& x : pdu) x = rb();
            const auto sa = dect2::hfdlBurst(m, pdu), sb = R::hfdlBurst(m, pdu);
            bool eq = sa.size() == sb.size();
            for (size_t j = 0; eq && j < sa.size(); j++) eq = fEq(sa[j].real(), sb[j].real()) && fEq(sa[j].imag(), sb[j].imag());
            SAME(eq, "burst rate %d", m);
            const int N = dect2::hfdlCodedBits(m);
            std::vector<float> chan((size_t)N);
            const int mode = (int)(g() % 3);
            for (auto& c : chan) c = mode == 0 ? (float)((int)(g() % 2001) - 1000) / 250.f : (g() % 2 ? 1.f : -1.f) * (1.f + (float)(g() % 100) / 50.f) * (mode == 2 && g() % 20 == 0 ? 0.f : 1.f);
            const auto a = dect2::hfdlDecodeSoft(m, chan), b = R::hfdlDecodeSoft(m, chan);
            SAME(a == b, "decode soft rate %d", m);
        }
    }
}

static void testFcsStations(long iters) {
    for (long i = 0; i < iters; i++) {
        std::vector<uint8_t> v = rndBytes(g() % 120 + 2);
        const size_t n = v.size() - 2;
        SAME(dect2::hfdlFcs(v.data(), n) == R::hfdlFcs(v.data(), n), "fcs");
        if (g() % 2) { const uint16_t f = R::hfdlFcs(v.data(), n); v[n] = (uint8_t)f; v[n + 1] = (uint8_t)(f >> 8); if (g() % 3 == 0) v[g() % v.size()] ^= 1; }
        SAME(dect2::hfdlFcsOk(v.data(), n) == R::hfdlFcsOk(v.data(), n), "fcs ok");
    }
    {
        const auto& a = dect2::hfdlStations();
        const auto& b = R::hfdlStations();
        bool eq = a.size() == b.size();
        for (size_t i = 0; eq && i < a.size(); i++) eq = a[i].id == b[i].id && std::string(a[i].name) == b[i].name && dEq(a[i].lat, b[i].lat) && dEq(a[i].lon, b[i].lon) && a[i].khz == b[i].khz;
        SAME(eq, "station table");
    }
    for (int gs = -2; gs < 135; gs++) {
        const auto *a = dect2::hfdlStation(gs); const auto *b = R::hfdlStation(gs);
        SAME((a == nullptr) == (b == nullptr) && (!a || a->id == b->id), "station %d", gs);
        for (int k = 0; k < 200; k++) {
            const uint32_t mask = k < 20 ? 1u << k : (uint32_t)g();
            SAME(dect2::hfdlMaskKhz(gs, mask) == R::hfdlMaskKhz(gs, mask), "mask %d %x", gs, mask);
        }
    }
    for (int k = -5; k < 25000; k++) SAME(dect2::hfdlStationOnKhz(k) == R::hfdlStationOnKhz(k), "station on %d", k);
}

static std::vector<uint8_t> rndHfnpdu() {
    std::vector<uint8_t> h;
    const int kind = (int)(g() % 8);
    const std::string fl = RS(g() % 8, "ABCDEFGHJKLMNPRSTUVWXYZ0123456789 ");
    const std::vector<dect2::HfdlSquitterGs> heard = {{(int)(g() % 20), true, g() & 0xFFFFF}, {(int)(g() % 20), true, g() & 0xFFFFF}, {(int)(g() % 130), false, g() & 0xFFFFF}};
    switch (kind) {
    case 0: h = dect2::hfdlPerfData(fl, rndCoord(90), rndCoord(180), (int)(g() % 90000), (int)(g() % 130), (int)(g() % 9)); break;
    case 1: h = dect2::hfdlFreqData(fl, rndCoord(90), rndCoord(180), (int)(g() % 90000), heard); break;
    case 2: h = dect2::hfdlAcarsHfnpdu(RS(7, "-.ABCDEFGHJ0123456789"), RS(2, "ABCH1789_d"), (char)('0' + g() % 10),
                                         g() % 2 ? RS(g() % 60, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /.\r\n-") : std::string("POS N52123E004567,1234,FL350"), false); break;
    case 3: h = {0xFF, 0xD0, (uint8_t)g(), rb(), rb(), rb(), rb()}; break;
    case 4: h = {0xFF, (uint8_t)(g() % 2 ? 0xD2 : 0xDE)}; break;
    case 5: h = {0xFF, rb()}; break;
    case 6: h = rndBytes(g() % 8); break;
    default: h = rndBytes(g() % 60); h.insert(h.begin(), 0xFF); break;
    }
    return h;
}
static std::vector<uint8_t> rndLpdu() {
    switch (g() % 9) {
    case 0: return dect2::hfdlLogonRequest(g() & 0xFFFFFF);
    case 1: return dect2::hfdlLogonConfirm(g() & 0xFFFFFF, (int)(g() % 256));
    case 2: case 3: case 4: return dect2::hfdlUnnumbered(rndHfnpdu());
    case 5: { auto b = rndBytes(g() % 8); const uint8_t t[] = {0x1D, 0x4F, 0x5F, 0x2F, 0x3F, 0xBF, 0x8F, 0x9F}; return dect2::hfdlLpdu(t[g() % 8], b); }
    case 6: { auto h = rndHfnpdu(); return dect2::hfdlLpdu(0x1D, h); }
    case 7: return dect2::hfdlLpdu(rb(), rndBytes(g() % 12));
    default: return mutate(dect2::hfdlUnnumbered(rndHfnpdu()));
    }
}
static std::vector<uint8_t> rndPdu() {
    switch (g() % 6) {
    case 0: {
        dect2::HfdlSquitterSpec s;
        s.gsId = (int)(g() % 130); s.utc = g() % 2; s.frameIndex = (int)(g() % 5000); s.changeNote = (int)(g() % 4); s.version = (int)(g() % 4); s.mask = g() & 0xFFFFF;
        s.systable = (int)(g() % 4096); s.other[0] = {(int)(g() % 130), g() % 2 == 0, g() & 0xFFFFF}; s.other[1] = {(int)(g() % 130), g() % 2 == 0, g() & 0xFFFFF};
        auto b = dect2::hfdlSpdu(s);
        if (g() % 4 == 0) { b[0] = (uint8_t)(b[0] | (g() % 2 ? 0 : 0x20)); const uint16_t f = dect2::hfdlFcs(b.data(), 64); b[64] = (uint8_t)f; b[65] = (uint8_t)(f >> 8); }
        if (g() % 3 == 0) b.resize(66 + g() % 10, 0);
        return b;
    }
    case 1: case 2: { std::vector<std::vector<uint8_t>> l; for (size_t i = g() % 5; i > 0; i--) l.push_back(rndLpdu()); return dect2::hfdlMpduDown((int)(g() % 130), (int)(g() % 256), l); }
    case 3: case 4: { std::vector<std::vector<uint8_t>> l; for (size_t i = g() % 5; i > 0; i--) l.push_back(rndLpdu()); return dect2::hfdlMpduUp((int)(g() % 130), (int)(g() % 256), l); }
    default: return rndBytes(g() % 90);
    }
}

static void testParse(long iters) {
    long parsed = 0, msgs = 0;
    for (long i = 0; i < iters; i++) {
        std::vector<uint8_t> pdu = rndPdu();
        if (g() % 2) pdu = mutate(pdu);
        if (g() % 5 == 0) for (size_t k = g() % 40; k > 0; k--) pdu.push_back(0);   // padding after the PDU
        std::vector<dect2::HfdlMessage> a, b;
        const bool oa = dect2::hfdlParsePdu(pdu.data(), pdu.size(), a), ob = R::hfdlParsePdu(pdu.data(), pdu.size(), b);
        bool eq = oa == ob && a.size() == b.size();
        for (size_t j = 0; eq && j < a.size(); j++) eq = msgEq(a[j], b[j]);
        SAME(eq, "parse pdu %zu octets ok %d/%d msgs %zu/%zu", pdu.size(), oa, ob, a.size(), b.size());
        if (!eq && fails < 4) {
            for (size_t j = 0; j < a.size() && j < b.size(); j++) if (!msgEq(a[j], b[j])) printf("--- rust:\n%s\n%s\n--- c++:\n%s\n%s\n", a[j].summary.c_str(), a[j].detail.c_str(), b[j].summary.c_str(), b[j].detail.c_str());
        }
        parsed += oa; msgs += (long)a.size();
    }
    printf("  parse: %ld of %ld PDUs accepted, %ld messages\n", parsed, iters, msgs);
    SAME(parsed > iters / 4, "too few PDUs accepted (%ld): the generator is off", parsed);
}

int main() {
    const char* it = getenv("ONAIR_DIFF_ITERS");
    const long iters = it ? atol(it) : 200000;
    testBitLayer(iters);
    testFcsStations(iters);
    testParse(iters);
    printf("test_hfdl_proto_rs: %ld checks, %ld mismatches\n", checks, fails);
    return fails ? 1 : 0;
}
