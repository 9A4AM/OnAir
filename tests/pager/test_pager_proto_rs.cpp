// Differential test: the C++ POCSAG / FLEX layer (a generated copy in dect2::pager_ref, see cmake/gen_rs_ref.cmake) against the Rust one (dect2::pager
// when DECT2_RUST is used). The same inputs go to both and every result has to be identical: code words (valid, damaged, random), text readers, the
// "looks like text" heuristic, POCSAG transmissions built by the generator and then damaged, the FLEX speed table, frame information words and FLEX
// phases (built by the generator, built word by word with random vector words, damaged, random), ONAIR_DIFF_ITERS (default 200000) per family.
#include "dect2/pager/pager_proto.h"
#include "pager_proto_ref.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
namespace A = dect2::pager;
namespace R = dect2::pager_ref;

static long fails = 0, checks = 0, pocMsgs = 0, flexMsgs = 0, flexAlpha = 0, flexNum = 0, flexLong = 0, looksTrue = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)

static std::mt19937 g(2024);
static std::string rnd(const char* alphabet, size_t n) { std::string s; for (size_t i = 0; i < n; i++) s += alphabet[g() % strlen(alphabet)]; return s; }

static bool msgEq(const dect2::PagerMessage& a, const dect2::PagerMessage& b) {
    return a.flex == b.flex && a.address == b.address && a.function == b.function && a.type == b.type && a.text == b.text && a.fixed == b.fixed &&
           a.damaged == b.damaged && a.numText == b.numText && a.alphaText == b.alphaText && a.looksAlpha == b.looksAlpha && a.readHow == b.readHow;
}
static bool msgsEq(const std::vector<dect2::PagerMessage>& a, const std::vector<dect2::PagerMessage>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) if (!msgEq(a[i], b[i])) return false;
    return true;
}

static std::vector<uint8_t> randBits(size_t n, int mode) {
    std::vector<uint8_t> b(n);
    if (mode == 0) { for (auto& x : b) x = (uint8_t)(g() & 1); return b; }
    // text-like: 7-bit characters from a text alphabet, sometimes with noise
    static const char txt[] = "Hello World 0123456789 ALERT: server DOWN, call 555-1234 now! xY~[]";
    for (size_t i = 0; i + 7 <= n; i += 7) {
        int c = (g() % 20 == 0) ? (int)(g() & 0x7F) : (unsigned char)txt[g() % (sizeof txt - 1)];
        for (int k = 0; k < 7; k++) b[i + (size_t)k] = (uint8_t)((c >> k) & 1);
    }
    return b;
}

static bool wordEq(const A::PocsagWord& a, const R::PocsagWord& b) { return a.kind == b.kind && a.a == b.a && a.b == b.b && a.bits == b.bits && a.fixed == b.fixed; }

static void testWords(long iters) {
    for (int d = 0; d < 0x200000; d += 17) SAME(A::encodeWord((uint32_t)d) == R::encodeWord((uint32_t)d), "encode %x", d);
    for (long i = 0; i < iters; i++) {
        uint32_t w = (uint32_t)g();
        const int how = (int)(g() % 5);
        if (how) {
            w = R::encodeWord((uint32_t)g() & 0x1FFFFF);
            for (int k = (int)(g() % 5); k > 0; k--) w ^= 1u << (g() % 32);
            if (how == 4) w = (w & 0xFFFFFF00u) | (g() & 0xFF);
        }
        const A::Fixed a = A::correctWord(w);
        const R::Fixed b = R::correctWord(w);
        SAME(a.ok == b.ok && a.bits == b.bits && a.data == b.data, "correct %08x", w);
        SAME(A::rev21(w) == R::rev21(w), "rev21 %08x", w);
        SAME(wordEq(A::pocsagClassify(w), R::pocsagClassify(w)), "classify %08x", w);
    }
    // the sync and idle words with every one and two bit damage
    for (uint32_t base : {A::kPocsagIdle, A::kPocsagSync})
        for (int i = 0; i < 32; i++) for (int j = i; j < 32; j++) {
            const uint32_t w = base ^ (1u << i) ^ (j == i ? 0 : (1u << j));
            SAME(wordEq(A::pocsagClassify(w), R::pocsagClassify(w)), "classify damaged %08x", w);
        }
}

static void testText(long iters) {
    for (long i = 0; i < iters; i++) {
        const size_t n = g() % 400;
        const std::vector<uint8_t> b = randBits(n, (int)(g() % 2));
        std::vector<uint8_t> c = b;
        const std::vector<uint8_t>& cc = c;
        for (int k = (int)(g() % 3); k > 0 && !c.empty(); k--) c[g() % c.size()] ^= 1;
        for (const std::vector<uint8_t>* v : {&b, &cc}) {
            const bool la = A::pocsagLooksAlpha(*v), lb = R::pocsagLooksAlpha(*v);
            SAME(la == lb, "looksAlpha n=%zu", v->size());
            looksTrue += la;
            SAME(A::alphaFromBits(*v) == R::alphaFromBits(*v), "alphaFromBits n=%zu", v->size());
            for (bool fx : {false, true}) SAME(A::numericFromBits(*v, fx) == R::numericFromBits(*v, fx), "numericFromBits n=%zu flex=%d", v->size(), (int)fx);
        }
    }
}

static std::vector<R::PocsagPage> randPages(int n) {
    std::vector<R::PocsagPage> p;
    for (int i = 0; i < n; i++) {
        R::PocsagPage pg;
        pg.ric = (uint32_t)(g() % 2097152);
        pg.fn = (int)(g() % 4);
        const int t = (int)(g() % 3);
        pg.type = t == 0 ? dect2::kPagerNumeric : t == 1 ? dect2::kPagerAlpha : dect2::kPagerTone;
        pg.text = t == 0 ? rnd("0123456789 -U*[]", g() % 40) : rnd("Hello World 0123456789 ALERT: call-me! xY", g() % 120);
        p.push_back(pg);
    }
    return p;
}

static void feedPocsag(const std::vector<uint8_t>& bits, int readMode, int damaged) {
    // skip the preamble (alternating bits) and walk the batches: sync word then 16 code words
    size_t pos = 0;
    while (pos + 32 <= bits.size()) {
        uint32_t w = 0;
        for (int i = 0; i < 32; i++) w = (w << 1) | bits[pos + (size_t)i];
        if (w == A::kPocsagSync) break;
        pos++;
    }
    A::PocsagParser pa; R::PocsagParser pb;
    pa.readMode = pb.readMode = readMode;
    std::vector<dect2::PagerMessage> oa, ob;
    while (pos + 32 * 17 <= bits.size()) {
        pos += 32;
        for (int slot = 0; slot < 16; slot++, pos += 32) {
            uint32_t w = A::wordFromBits(&bits[pos]);
            if (damaged && g() % 4 == 0) for (int k = (int)(g() % 4); k > 0; k--) w ^= 1u << (g() % 32);
            if (damaged && g() % 40 == 0) w = (uint32_t)g();
            pa.word(w, slot, oa);
            pb.word(w, slot, ob);
        }
    }
    pa.flush(oa); pb.flush(ob);
    SAME(msgsEq(oa, ob), "pocsag parser readMode %d damaged %d: %zu / %zu messages", readMode, damaged, oa.size(), ob.size());
    SAME(pa.ok == pb.ok && pa.fixed == pb.fixed && pa.failed == pb.failed, "pocsag counters");
    pocMsgs += (long)oa.size();
}

static void testPocsag(long iters) {
    for (long i = 0; i < iters / 20; i++) {
        const std::vector<R::PocsagPage> pages = randPages(1 + (int)(g() % 6));
        std::vector<A::PocsagPage> pa;
        for (const auto& p : pages) { A::PocsagPage q; q.ric = p.ric; q.fn = p.fn; q.type = p.type; q.text = p.text; pa.push_back(q); }
        const std::vector<uint8_t> bits = A::pocsagBits(pa, 64);
        SAME(bits == R::pocsagBits(pages, 64), "pocsagBits generator");
        feedPocsag(bits, (int)(g() % 4), (int)(g() % 2));
    }
}

// a phase built word by word: f[] are the 21-bit field values (first transmitted bit in bit 0)
static std::vector<uint8_t> phaseFromWords(const uint32_t* f) {
    std::vector<uint8_t> bits((size_t)A::kFlexPhaseWords * 32);
    for (int blk = 0; blk < 11; blk++)
        for (int w = 0; w < 8; w++) {
            const uint32_t W = R::encodeWord(R::rev21(f[blk * 8 + w] & 0x1FFFFF));
            for (int j = 0; j < 32; j++) bits[(size_t)(blk * 256 + j * 8 + w)] = (uint8_t)((W >> (31 - j)) & 1);
        }
    return bits;
}

static void comparePhase(const std::vector<uint8_t>& bits) {
    std::vector<dect2::PagerMessage> oa, ob;
    A::FlexPhaseStat sa; R::FlexPhaseStat sb;
    A::flexDecodePhase(bits.data(), oa, sa);
    R::flexDecodePhase(bits.data(), ob, sb);
    SAME(msgsEq(oa, ob), "flex phase: %zu / %zu messages", oa.size(), ob.size());
    SAME(sa.ok == sb.ok && sa.fixed == sb.fixed && sa.failed == sb.failed && sa.biwOk == sb.biwOk, "flex phase stats");
    flexMsgs += (long)oa.size();
    for (const auto& m : oa) { flexAlpha += m.type == dect2::kPagerAlpha; flexNum += m.type == dect2::kPagerNumeric; }
}

static void damage(std::vector<uint8_t>& bits, int how) {
    if (how == 1) for (int k = (int)(g() % 12); k > 0; k--) bits[g() % bits.size()] ^= 1;
    else if (how == 2) { const size_t s = g() % bits.size(); for (size_t i = s; i < bits.size() && i < s + 40 + g() % 200; i++) bits[i] = (uint8_t)(g() & 1); }
}

static bool modeEq(const A::FlexMode* a, const R::FlexMode* b) {
    return (a == nullptr) == (b == nullptr) && (!a || (a->code == b->code && a->baud == b->baud && a->levels == b->levels && a->speed == b->speed));
}

static void testFlex(long iters) {
    for (int code = 0; code < 65536; code++)
        for (int md = 0; md <= 5; md++) SAME(modeEq(A::flexModeForCode((uint16_t)code, md), R::flexModeForCode((uint16_t)code, md)), "modeForCode %04x %d", code, md);
    for (int sp = -2; sp < 10; sp++) SAME(modeEq(A::flexModeForSpeed(sp), R::flexModeForSpeed(sp)), "modeForSpeed %d", sp);
    for (int cy = -3; cy < 20; cy++) for (int fr = -3; fr < 135; fr++) SAME(A::flexFiwData(cy, fr) == R::flexFiwData(cy, fr), "fiwData %d %d", cy, fr);
    for (uint32_t f = 0; f < 0x200000; f += 3) {
        int c1 = -1, f1 = -1, c2 = -1, f2 = -1;
        const bool a = A::flexFiwCheck(f, c1, f1), b = R::flexFiwCheck(f, c2, f2);
        SAME(a == b && (!a || (c1 == c2 && f1 == f2)), "fiwCheck %x", f);
    }
    for (long i = 0; i < iters / 4; i++) {
        // phases from the generator
        std::vector<R::FlexPage> pg;
        std::vector<A::FlexPage> pa;
        const int n = (int)(g() % 14);
        for (int k = 0; k < n; k++) {
            R::FlexPage p;
            p.cap = 1 + g() % 1933000;
            const int t = (int)(g() % 3);
            p.type = t == 0 ? dect2::kPagerTone : t == 1 ? dect2::kPagerNumeric : dect2::kPagerAlpha;
            p.text = t == 1 ? rnd("0123456789 -U[]", g() % 40) : rnd("Hello World 0123456789 ALERT: call-me!", g() % 100);
            pg.push_back(p);
            A::FlexPage q; q.cap = p.cap; q.type = p.type; q.text = p.text; pa.push_back(q);
        }
        std::vector<uint8_t> bits = R::flexPhaseBits(pg);
        SAME(bits == A::flexPhaseBits(pa), "flexPhaseBits generator");
        damage(bits, (int)(g() % 3));
        comparePhase(bits);
    }
    static const uint32_t types[8] = {2, 3, 5, 5, 7, 4, 0, 6};
    for (long i = 0; i < iters / 2; i++) {
        // phases built word by word: the BIW, random address words (short and long) and random vector / message words
        uint32_t f[88];
        for (auto& x : f) x = (uint32_t)g() & 0x1FFFFF;
        const int aoff = 1 + (int)(g() % 4), voff = aoff + 1 + (int)(g() % 12);
        f[0] = ((uint32_t)voff << 10) | ((uint32_t)(aoff - 1) << 8) | (g() & 0xFF);
        for (int k = aoff; k < voff && k < 88; k++) {
            const int t = (int)(g() % 6);
            if (t < 4) f[k] = 0x8000 + 1 + g() % 0x100000;           // short address
            else if (t == 4) f[k] = (g() % 2) ? 1 + g() % 0x8000 : 0x1E0001 + g() % 0x1FFF;   // long address (first word)
            if (g() % 12 == 0) f[k] = (g() % 2) ? 0 : 0x1FFFFF;
        }
        for (int k = voff; k < 88; k++) {
            if (g() % 3) {
                const uint32_t type = (g() % 5 == 0) ? (g() % 8) : types[g() % 8];
                f[k] = (f[k] & ~0x70u) | (type << 4);
                f[k] = (f[k] & ~(0x7Fu << 7)) | ((uint32_t)(g() % 3 ? 1 + g() % 80 : g() % 100) << 7);
            }
        }
        std::vector<uint8_t> bits = phaseFromWords(f);
        damage(bits, (int)(g() % 3));
        comparePhase(bits);
        for (int k = aoff; k < voff && k < 88; k++) flexLong += (f[k] < 0x8001 || f[k] > 0x1E0000);
    }
    for (long i = 0; i < iters / 20; i++) {
        std::vector<uint8_t> bits(2816);
        for (auto& x : bits) x = (uint8_t)(g() & 1);
        comparePhase(bits);
    }
}

int main() {
    const char* it = getenv("ONAIR_DIFF_ITERS");
    const long iters = it ? atol(it) : 200000;
    testWords(iters);
    testText(iters);
    testPocsag(iters);
    testFlex(iters);
    printf("test_pager_proto_rs: %ld checks, %ld mismatches (pocsag messages %ld, flex messages %ld, alpha %ld, numeric %ld, long addr %ld, looks-alpha %ld)\n",
           checks, fails, pocMsgs, flexMsgs, flexAlpha, flexNum, flexLong, looksTrue);
    return fails ? 1 : 0;
}
