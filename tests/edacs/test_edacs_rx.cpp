// EDACS: frame round trip, bit errors and the full chain (generated FM signal with noise -> receiver). Generated signal only: the BCH check bits and the
// Barker word are placeholders in the generator and are not checked by the receiver (not established from public non-code sources).
#include "dect2/edacs/edacs_proto.h"
#include "dect2/edacs/edacs_rx.h"
#include "dect2/edacs/edacs_tel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

static int fails = 0;
#define CHECK(c, fmt, ...) do { if (!(c)) { std::printf("FAIL line %d: " fmt "\n", __LINE__ __VA_OPT__(,) __VA_ARGS__); fails++; } } while (0)   // Flawfinder: ignore (the format is a literal)

using namespace dect2;

static unsigned rngState = 999;
static float gauss() { float s = 0; for (int i = 0; i < 6; i++) { rngState = rngState * 1664525u + 1013904223u; s += ((rngState >> 8) & 0xFFFF) / 65536.f - 0.5f; } return s * 2.0f * 0.7071f; }

static uint32_t first[2];
static std::vector<uint8_t> frames(int count, std::set<uint32_t>& words) {
    std::vector<uint8_t> b;
    { unsigned r = 77; for (int i = 0; i < 400; i++) { r = r * 1664525u + 1013904223u; b.push_back((uint8_t)((r >> 20) & 1)); } }   // lead-in: random bits, the slicer thresholds settle: the slicer thresholds settle
    for (int k = 0; k < count; k++) {
        const uint32_t a = (0x1234567u + 0x0F3A5C1u * (uint32_t)k) & 0xFFFFFFF, c = (0xABCDEF1u ^ (0x0377711u * (uint32_t)(k + 3))) & 0xFFFFFFF;
        if (k > 0) { words.insert(a); words.insert(c); } else { first[0] = a; first[1] = c; }   // the first frame may be lost while the thresholds settle
        const auto f = edacs::encodeFrame(a, c, (a * 2654435761u) >> 20, (c * 2246822519u) >> 20);
        b.insert(b.end(), f.begin(), f.end());
    }
    { unsigned r = 91; for (int i = 0; i < 80; i++) { r = r * 1664525u + 1013904223u; b.push_back((uint8_t)((r >> 20) & 1)); } }   // tail: the last frame is voted after its payload and a few bits more
    return b;
}

static EdacsTelemetry run(const std::vector<cf32>& sig, double fs, double off, double sigma, bool inv, bool noiseOnly = false) {
    EdacsReceiver rx;
    rx.configure(fs);
    rx.setSignalOffset(off);
    rx.setInverted(inv);
    EdacsTelemetry t;
    uint64_t seq = 0;
    std::vector<cf32> blk(4096);
    const size_t total = noiseOnly ? (size_t)(fs * 2) : sig.size() + (size_t)(fs * 0.3);   // silence at the end so the last report is published
    for (size_t i = 0; i < total; i += blk.size()) {
        const size_t n = std::min(blk.size(), total - i);
        for (size_t k = 0; k < n; k++) {
            cf32 v = (noiseOnly || i + k >= sig.size()) ? cf32(0, 0) : sig[i + k];
            if (sigma > 0) v += cf32((float)sigma * gauss(), (float)sigma * gauss());
            blk[k] = v;
        }
        rx.feed(blk.data(), n);
        EdacsTelemetry x;
        if (rx.telemetry(x, seq)) { seq = x.seq; t = x; }
    }
    return t;
}

static int matched(const EdacsTelemetry& t, const std::set<uint32_t>& words, int& stray) {
    std::set<uint32_t> got;
    stray = 0;
    for (const auto& f : t.frames) { got.insert(f.msgA); got.insert(f.msgB); if (!words.count(f.msgA) && f.msgA != first[0]) stray++; if (!words.count(f.msgB) && f.msgB != first[1]) stray++; }
    int n = 0;
    for (uint32_t w : words) n += got.count(w) ? 1 : 0;
    return n;
}

int main() {
    {   // frame structure and vote
        const auto f = edacs::encodeFrame(0xABCDEF1, 0x1234567, 0x111, 0x222);
        CHECK((int)f.size() == edacs::kFrameBits, "frame has %zu bits", f.size());
        int d = 0;
        uint64_t v = edacs::vote(f.data() + 48, d);
        CHECK(d == 0 && (uint32_t)(v >> 12) == 0xABCDEF1u && (uint32_t)(v & 0xFFF) == 0x111, "vote %llx disagreements %d", (unsigned long long)v, d);
        v = edacs::vote(f.data() + 168, d);
        CHECK(d == 0 && (uint32_t)(v >> 12) == 0x1234567u, "vote B %llx", (unsigned long long)v);
        // bit errors: one error in a single copy is outvoted, the same bit wrong in two copies is not
        auto g = f;
        g[48 + 5] ^= 1; g[48 + 40 + 9] ^= 1; g[48 + 80 + 20] ^= 1;
        v = edacs::vote(g.data() + 48, d);
        CHECK(d == 3 && (uint32_t)(v >> 12) == 0xABCDEF1u, "single-copy errors not outvoted (d=%d)", d);
        g[48 + 40 + 5] ^= 1;
        v = edacs::vote(g.data() + 48, d);
        CHECK((uint32_t)(v >> 12) != 0xABCDEF1u, "two bad copies of one bit were outvoted");
    }
    const double fs = 480000, off = -100000;
    std::set<uint32_t> words;
    const auto bits = frames(10, words);
    {   // clean
        const auto sig = edacs::modulate(bits, fs, off, 1400.0, 0.5f);
        const EdacsTelemetry t = run(sig, fs, off, 0, false);
        int stray = 0;
        const int n = matched(t, words, stray);
        if (n != (int)words.size()) for (const auto& f : t.frames) std::printf("  got %07X %07X err %d/%d t=%.4f syncs=%llu\n", f.msgA, f.msgB, f.errA, f.errB, f.timeSec, (unsigned long long)t.syncs);
        for (uint32_t w : words) std::printf("  want %07X\n", w);
        CHECK(n == (int)words.size() && stray == 0, "clean: matched %d of %zu words, stray %d (good %llu bad %llu)", n, words.size(), stray, (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad);
        CHECK(t.blocksOk >= 9, "clean: %llu good frames", (unsigned long long)t.blocksOk);
        CHECK(t.locked, "clean: not locked");
        std::printf("clean: matched %d of %zu, good %llu bad %llu\n", n, words.size(), (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad);
    }
    {   // inverted polarity: right with the setting, the complement without it
        const auto sig = edacs::modulate(bits, fs, off, 1400.0, 0.5f, true);
        int stray = 0;
        const EdacsTelemetry a = run(sig, fs, off, 0, true);
        const int n = matched(a, words, stray);
        CHECK(n == (int)words.size() && stray == 0, "inverted with setting: matched %d, stray %d", n, stray);
        const EdacsTelemetry b = run(sig, fs, off, 0, false);
        int stray2 = 0;
        const int n2 = matched(b, words, stray2);
        CHECK(n2 == 0, "inverted without setting matched %d words", n2);
    }
    {   // noise
        const auto sig = edacs::modulate(bits, fs, off, 1400.0, 0.5f);
        const EdacsTelemetry t = run(sig, fs, off, 0.1, false);
        int stray = 0;
        const int n = matched(t, words, stray);
        CHECK(n >= (int)words.size() - 4 && stray == 0, "noise: matched %d of %zu, stray %d", n, words.size(), stray);
        std::printf("noise: matched %d of %zu, stray %d, good %llu bad %llu\n", n, words.size(), stray, (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad);
    }
    {   // noise only
        const EdacsTelemetry t = run({}, fs, off, 0.1, false, true);
        CHECK(t.blocksOk == 0, "noise only: %llu good frames", (unsigned long long)t.blocksOk);
    }
    std::printf("%s: %d failure(s)\n", fails ? "test_edacs_rx FAILED" : "test_edacs_rx OK", fails);
    return fails ? 1 : 0;
}
