// Differential test: the C++ error correction layer (a generated copy in dect2::fecp_ref, see cmake/gen_rs_ref.cmake) against the one the program uses
// (dect2::fecp: the Rust crate onair_fec when DECT2_RUST is on). The same inputs go to both and every result has to be identical, word for word:
// Reed-Solomon (204,188) / (207,187) / (120,110) encoders and decoders over ONAIR_DIFF_ITERS (default 100000) random words per code (clean, 1..t+3 byte errors,
// pure noise, bursts, all-zero / all-one words) and the DVB-T2 BCH encoder / decoder (normal and short frames, every t = 1..12, lengths from 1 bit to a
// full frame, 0..t+3 bit errors, noise). The Rust C ABI is also called directly with null pointers and wrong lengths (must return the error value, not crash).
#include "dect2/fec/fec_proto.h"
#include "fec_proto_ref.h"
#include "dect2/rs/fec.h"
#include "dect2/t2/t2fec_tables.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
namespace A = dect2::fecp;
namespace R = dect2::fecp_ref;

static long fails = 0, checks = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)
static std::mt19937 g(97531);
static uint32_t r32() { return g(); }
using Buf = std::vector<uint8_t>;

typedef void (*EncFn)(const uint8_t*, uint8_t*);
typedef int (*DecFn)(uint8_t*);

static void rsFamily(const char* name, int n, int r, EncFn ea, EncFn er, DecFn da, DecFn dr, long iters) {
    const int k = n - r;
    long corrected = 0, failed = 0;
    for (long it = 0; it < iters; it++) {
        Buf data(k);
        const int fill = (int)(r32() % 8);
        for (auto& x : data) x = fill == 0 ? 0 : fill == 1 ? 0xFF : (uint8_t)r32();
        Buf ca(n), cr(n);
        ea(data.data(), ca.data()); er(data.data(), cr.data());
        SAME(ca == cr, "%s encode it %ld", name, it);
        Buf w = cr;
        const int mode = (int)(r32() % 10);
        if (mode == 0) { for (auto& x : w) x = (uint8_t)r32(); }                                        // noise
        else if (mode == 1) { const int p = (int)(r32() % n), l = 1 + (int)(r32() % 20); for (int i = 0; i < l && p + i < n; i++) w[p + i] ^= (uint8_t)(1 + r32() % 255); }   // burst
        else if (mode != 2) { const int e = (int)(r32() % (r / 2 + 4)); for (int i = 0; i < e; i++) w[r32() % n] ^= (uint8_t)(1 + r32() % 255); }  // 0..t+3 errors
        Buf wa = w, wr = w;
        const int ra = da(wa.data()), rr = dr(wr.data());
        SAME(ra == rr && wa == wr, "%s decode it %ld mode %d (%d vs %d)", name, it, mode, ra, rr);
        if (ra > 0) corrected++; else if (ra < 0) failed++;
    }
    printf("  %s: %ld words, %ld corrected, %ld uncorrectable\n", name, iters, corrected, failed);
}

static void bchFamily(long iters) {
    long clean = 0, fixed = 0, bad = 0;
    // full frames first (slow), then many shorter / odd lengths
    for (int cfg = 0; cfg < 3; cfg++) {
        const bool sh = cfg == 2; const int t = cfg == 0 ? 12 : cfg == 1 ? 10 : 12;
        A::Bch ba(sh, t); R::Bch br(sh, t);
        SAME(ba.parityBits() == br.parityBits(), "bch parity bits");
        const int k = sh ? 7032 : cfg == 0 ? 38688 : 53840;
        for (int rep = 0; rep < 30; rep++) {
            Buf msg(k); for (auto& x : msg) x = (uint8_t)(r32() & 1);
            Buf ea = msg, er = msg;
            ba.encode(ea, k); br.encode(er, k);
            SAME(ea == er, "bch full encode");
            const int e = (int)(r32() % (t + 3));
            for (int i = 0; i < e; i++) { const size_t p = r32() % ea.size(); ea[p] ^= 1; }
            er = ea;
            const int ra = ba.decode(ea), rr = br.decode(er);
            SAME(ra == rr && ea == er, "bch full decode (%d vs %d)", ra, rr);
        }
    }
    for (long it = 0; it < iters; it++) {
        const bool sh = r32() & 1; const int t = 1 + (int)(r32() % 12);
        static A::Bch* pa[2][13] = {}; static R::Bch* pr[2][13] = {};
        if (!pa[sh][t]) { pa[sh][t] = new A::Bch(sh, t); pr[sh][t] = new R::Bch(sh, t); }
        A::Bch& ba = *pa[sh][t]; R::Bch& br = *pr[sh][t];
        const int deg = ba.parityBits();
        const int k = (int)(r32() % 3 == 0 ? r32() % 20 : r32() % 4000);
        Buf msg(k); for (auto& x : msg) x = (uint8_t)(r32() & 1);
        Buf ea = msg, er = msg;
        ba.encode(ea, k); br.encode(er, k);
        SAME(ea == er && (int)ea.size() == k + deg, "bch encode it %ld t %d k %d", it, t, k);
        Buf w = er;
        const int mode = (int)(r32() % 8);
        if (mode == 0) { for (auto& x : w) x = (uint8_t)(r32() & 1); }
        else if (mode == 1) { w.resize(r32() % (w.size() + 1)); }                // truncated word
        else { const int e = (int)(r32() % (t + 4)); for (int i = 0; i < e && !w.empty(); i++) w[r32() % w.size()] ^= 1; }
        Buf wa = w, wr = w;
        const int ra = ba.decode(wa), rr = br.decode(wr);
        SAME(ra == rr && wa == wr, "bch decode it %ld t %d n %zu mode %d (%d vs %d)", it, t, w.size(), mode, ra, rr);
        if (ra == 0) clean++; else if (ra > 0) fixed++; else bad++;
    }
    printf("  bch: %ld words, %ld clean, %ld corrected, %ld uncorrectable\n", iters, clean, fixed, bad);
}

// Viterbi: noisy convolutional code words, pure noise (any int8 including -128), punctured (zero) positions, every length from 0 steps to a few thousand
static void viterbiFamily(long iters) {
    long total = 0;
    for (long it = 0; it < iters; it++) {
        const size_t steps = r32() % 4 == 0 ? r32() % 40 : r32() % 1500;
        Buf soft(2 * steps);
        const int mode = (int)(r32() % 6);
        if (mode == 0) { for (auto& x : soft) x = (uint8_t)r32(); }                                   // noise, -128 included
        else {                                                                                       // a code word with errors and punctured positions
            unsigned st = 0;
            for (size_t t = 0; t < steps; t++) {
                const unsigned u = r32() & 1, reg = (u << 6) | st;
                const int x = __builtin_parity(reg & 0x79), y = __builtin_parity(reg & 0x5B);
                st = (u << 5) | (st >> 1);
                const int amp = mode == 1 ? 127 : 20 + (int)(r32() % 100);
                soft[2 * t] = (uint8_t)(int8_t)(x ? -amp : amp); soft[2 * t + 1] = (uint8_t)(int8_t)(y ? -amp : amp);
            }
            const int flips = (int)(steps * (r32() % 20) / 100);
            for (int i = 0; i < flips && steps; i++) { auto& v = soft[r32() % soft.size()]; v = (uint8_t)(int8_t)-(int8_t)v; }
            if (mode >= 4) for (auto& v : soft) if (r32() % 4 == 0) v = 0;                           // punctured
        }
        std::vector<uint8_t> ba(3, 9), br(5, 7);   // prior contents must not matter
        long ma = -1, mr = -2;
        A::viterbiRange((const int8_t*)soft.data(), steps, ba, &ma);
        R::viterbiRange((const int8_t*)soft.data(), steps, br, &mr);
        SAME(ba == br && ma == mr && ba.size() == steps, "viterbi it %ld steps %zu mode %d (metric %ld vs %ld)", it, steps, mode, ma, mr);
        A::viterbiRange((const int8_t*)soft.data(), steps, ba, nullptr);   // the metric pointer may be null
        total += (long)steps;
    }
    printf("  viterbi: %ld blocks, %ld steps\n", iters, total);
}

// LDPC parity-check tables: every real DVB-T2 table of the program, then random address tables (any group count, address count, m not a multiple of 360,
// addresses beyond m, negative addresses, n <= k, k < 0)
template <class T, class U> static bool sameTables(const T& a, const U& b) {
    return a.m == b.m && a.q == b.q && a.chkStart == b.chkStart && a.chkVar == b.chkVar && a.encP == b.encP && a.encD == b.encD && a.layerStart == b.layerStart &&
           a.layerGroup == b.layerGroup && a.layerShift == b.layerShift;
}
static void ldpcFamily(long iters) {
    long built = 0, rejected = 0;
    for (int i = 0; i < dect2::kNumLdpcTables; i++) {
        const dect2::LdpcTable& tb = dect2::kLdpcTables[i];
        std::vector<std::vector<int>> rows;
        for (int r = 0; r < tb.rows; r++) { const int* row = tb.data + (size_t)r * tb.cols; rows.emplace_back(row + 1, row + 1 + row[0]); }
        const int n = tb.name[strlen(tb.name) - 1] == 'S' ? 16200 : 64800, k = tb.rows * 360;
        A::LdpcTables ta; R::LdpcTables tr;
        const bool oa = A::ldpcBuild(k, n, rows, ta), orr = R::ldpcBuild(k, n, rows, tr);
        SAME(oa == orr && oa && sameTables(ta, tr), "ldpc real table %s", tb.name);
        built++;
    }
    for (long it = 0; it < iters; it++) {
        const int q = 1 + (int)(r32() % 40);
        int m = q * 360 + (r32() % 5 == 0 ? (int)(r32() % 360) : 0);
        const int groups = (int)(r32() % 8 == 0 ? r32() % 3 : 1 + r32() % 12);
        std::vector<std::vector<int>> rows(groups);
        for (auto& r : rows) {
            const int na = (int)(r32() % 10 == 0 ? 0 : 1 + r32() % 14);
            for (int j = 0; j < na; j++) {
                int a = (int)(r32() % (uint32_t)m);
                const uint32_t w = r32() % 40;
                if (w == 0) a += m * (1 + (int)(r32() % 3));   // beyond m: wraps
                if (r32() % 400 == 0) a = -1 - (int)(r32() % 50);   // negative: rejected
                r.push_back(a);
            }
        }
        int k = (int)(r32() % 50) * 360, n = k + m;
        const int quirk = (int)(r32() % 40);
        if (quirk == 0) n = k;             // no parity bits
        if (quirk == 1) n = k - 5;         // n < k
        if (quirk == 2) k = -360;          // negative k
        if (quirk == 3) m = 359, n = k + 359;
        A::LdpcTables ta; R::LdpcTables tr;
        const bool oa = A::ldpcBuild(k, n, rows, ta), orr = R::ldpcBuild(k, n, rows, tr);
        SAME(oa == orr && sameTables(ta, tr), "ldpc it %ld k %d n %d groups %d (%d vs %d)", it, k, n, groups, (int)oa, (int)orr);
        if (oa) built++; else rejected++;
    }
    printf("  ldpc: %ld tables built, %ld rejected\n", built, rejected);
}

#ifdef DECT2_RUST_FEC
// the C ABI with hostile arguments: error values, no crash
static void abiMisuse() {
    uint8_t buf[300] = {};
    SAME(!onair_fec_rs_encode(0, nullptr, 188, buf, 204), "null data");
    SAME(!onair_fec_rs_encode(0, buf, 188, nullptr, 204), "null out");
    SAME(!onair_fec_rs_encode(0, buf, 100, buf + 100, 204), "short data");
    SAME(!onair_fec_rs_encode(0, buf, 188, buf + 100, 100), "short out");
    SAME(!onair_fec_rs_encode(9, buf, 188, buf + 100, 204), "bad code");
    SAME(!onair_fec_rs_encode(0, buf, 0, buf, 0), "zero lengths");
    SAME(onair_fec_rs_decode(0, nullptr, 204) == -2, "decode null");
    SAME(onair_fec_rs_decode(0, buf, 203) == -2, "decode length");
    SAME(onair_fec_rs_decode(7, buf, 204) == -2, "decode code");
    SAME(onair_fec_rs_decode(1, buf, 0) == -2, "decode zero");
    SAME(onair_fec_rs_decode(2, buf, 204) == -2, "decode dab length");
    std::vector<uint64_t> ctx(onair_fec_bch_ctx_size() / 8 + 1);
    SAME(!onair_fec_bch_init(nullptr, 100, false, 12), "init null");
    SAME(!onair_fec_bch_init((uint8_t*)ctx.data(), 10, false, 12), "init small");
    SAME(!onair_fec_bch_init((uint8_t*)ctx.data(), ctx.size() * 8, false, 13), "init t 13");
    SAME(!onair_fec_bch_init((uint8_t*)ctx.data(), ctx.size() * 8, false, 0), "init t 0");
    SAME(!onair_fec_bch_init((uint8_t*)ctx.data(), ctx.size() * 8, false, -5), "init t negative");
    SAME(!onair_fec_bch_init(((uint8_t*)ctx.data()) + 1, ctx.size() * 8 - 1, false, 5), "init misaligned");
    SAME(onair_fec_bch_init((uint8_t*)ctx.data(), ctx.size() * 8, true, 12), "init ok");
    SAME(onair_fec_bch_parity_bits(nullptr) == 0, "parity null");
    SAME(onair_fec_bch_parity_bits((uint8_t*)ctx.data()) == 168, "parity bits");
    SAME(!onair_fec_bch_encode(nullptr, buf, 300, 10), "bch enc null ctx");
    SAME(!onair_fec_bch_encode((uint8_t*)ctx.data(), nullptr, 300, 10), "bch enc null bits");
    SAME(!onair_fec_bch_encode((uint8_t*)ctx.data(), buf, 100, 10), "bch enc short buffer");
    SAME(!onair_fec_bch_encode((uint8_t*)ctx.data(), buf, 300, (size_t)-1), "bch enc huge k");
    SAME(onair_fec_bch_decode(nullptr, buf, 10) == -2, "bch dec null ctx");
    SAME(onair_fec_bch_decode((uint8_t*)ctx.data(), nullptr, 10) == -2, "bch dec null bits");
    SAME(onair_fec_bch_decode((uint8_t*)ctx.data(), nullptr, 0) == 0, "bch dec empty");
    SAME(onair_fec_bch_decode((uint8_t*)ctx.data(), buf, 0) == 0, "bch dec zero length");
    int8_t sv[8] = {};
    uint8_t vb[4]; uint64_t vd[4]; int64_t vm = 5;
    SAME(onair_fec_viterbi_range(nullptr, 0, nullptr, nullptr, &vm) && vm == 0, "viterbi zero steps");
    SAME(onair_fec_viterbi_range(nullptr, 0, nullptr, nullptr, nullptr), "viterbi zero steps no metric");
    SAME(!onair_fec_viterbi_range(nullptr, 4, vb, vd, &vm), "viterbi null soft");
    SAME(!onair_fec_viterbi_range(sv, 4, nullptr, vd, &vm), "viterbi null bits");
    SAME(!onair_fec_viterbi_range(sv, 4, vb, nullptr, &vm), "viterbi null dec");
    SAME(!onair_fec_viterbi_range(sv, (size_t)-1, vb, vd, &vm), "viterbi huge steps");
    SAME(onair_fec_viterbi_range(sv, 4, vb, vd, nullptr), "viterbi no metric");
    {   // LDPC ABI: exact lengths, null pointers
        size_t d[5];
        SAME(!onair_fec_ldpc_dims(360, 360, 1, d), "ldpc dims n == k");
        SAME(!onair_fec_ldpc_dims(0, 359, 1, d), "ldpc dims m < 360");
        SAME(!onair_fec_ldpc_dims(0, 360, 1, nullptr), "ldpc dims null");
        SAME(!onair_fec_ldpc_dims(0, 360, (size_t)-1, d), "ldpc dims overflow");
        SAME(onair_fec_ldpc_dims(0, 360, 2, d) && d[0] == 360 && d[1] == 1 && d[3] == 720 && d[4] == 720 + 719, "ldpc dims values");
        uint32_t rl[1] = {1}; int32_t ad[1] = {5};
        std::vector<int32_t> v(1000);
        OnairFecLdpcOut o = {v.data(), 361, v.data(), 360 + 359 + 360, v.data(), 360, v.data(), 360, v.data(), 2, v.data(), 1, v.data(), 1};
        SAME(!onair_fec_ldpc_build(0, 720, rl, 1, ad, 1, &o), "ldpc build wrong lengths");
        SAME(!onair_fec_ldpc_build(0, 720, rl, 1, ad, 1, nullptr), "ldpc build null out");
        SAME(!onair_fec_ldpc_build(0, 720, nullptr, 1, ad, 1, &o), "ldpc build null rows");
        SAME(!onair_fec_ldpc_build(0, 720, rl, 1, nullptr, 1, &o), "ldpc build null addrs");
        SAME(!onair_fec_ldpc_build(0, 720, rl, 1, ad, 0, &o), "ldpc build rows do not add up");
    }
    // negative lengths are clamped by the wrappers: k < 0 encodes zero message bits
    A::Bch b(true, 12);
    std::vector<uint8_t> bits(50, 1);
    b.encode(bits, -7);
    SAME(bits.size() == 168, "negative k");
    b.encode(bits, 100000);   // k beyond the vector is clamped to its size
    SAME(bits.size() == 168 + 168, "huge k");
}
#endif

int main() {
    const long iters = getenv("ONAIR_DIFF_ITERS") ? atol(getenv("ONAIR_DIFF_ITERS")) : 100000;
    printf("fec differential, %ld iterations per family\n", iters);
    rsFamily("rs204", 204, 16, A::rs204Encode, R::rs204Encode, A::rs204Decode, R::rs204Decode, iters);
    rsFamily("rs207", 207, 20, A::rs207Encode, R::rs207Encode, A::rs207Decode, R::rs207Decode, iters);
    rsFamily("rs120", 120, 10, A::rs120Encode, R::rs120Encode, A::rs120Decode, R::rs120Decode, iters);
    bchFamily(iters);
    viterbiFamily(iters);
    ldpcFamily(iters);
#ifdef DECT2_RUST_FEC
    abiMisuse();
#endif
    printf("fec_proto_rs: %ld checks, %ld mismatches\n", checks, fails);
    return fails ? 1 : 0;
}
