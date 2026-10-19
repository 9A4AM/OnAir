// DME receiver: a generated pulse-pair train (X 12 us, Y 36 us, 2700 pairs/s), an ident keyed at 1350 pairs/s, and pairs
// with a wrong spacing that must not count.
#include "dect2/dme/dme_rx.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
using namespace dect2;
#include "../common/test_util.h"

namespace {

constexpr double kFs = 2.0e6;

struct Pulse { uint64_t s, e; };

// Pulses sorted in time, streamed as a 0/1 envelope.
struct Gen {
    std::vector<Pulse> pulses;
    size_t ptr = 0;
    uint64_t i = 0;

    void add(double tSec) {
        const uint64_t s = (uint64_t)std::llround(tSec * kFs);
        const uint64_t w = (uint64_t)std::llround(dme::kPulseWidthUs * 1e-6 * kFs);
        pulses.push_back({s, s + w});
    }
    void fill(float* out, size_t n) {
        for (size_t k = 0; k < n; k++, i++) {
            while (ptr < pulses.size() && pulses[ptr].e <= i) ptr++;
            out[k] = (ptr < pulses.size() && i >= pulses[ptr].s) ? 1.0f : 0.0f;
        }
    }
};

// adds a stream of pairs with the given spacing between t0 and t1 at the search rate
void addPairs(Gen& g, double t0, double t1, double spacingUs) {
    for (double t = t0; t < t1; t += 1.0 / dme::kSearchRatePps) {
        g.add(t);
        g.add(t + spacingUs * 1e-6);
    }
}

void feedUntil(dme::Receiver& rx, Gen& g, double sec) {
    const uint64_t target = (uint64_t)std::llround(sec * kFs);
    std::vector<float> buf(50000);
    while (g.i < target) {
        const size_t n = (size_t)std::min<uint64_t>(buf.size(), target - g.i);
        g.fill(buf.data(), n);
        rx.feed(buf.data(), n);
    }
}

// Morse for "ORD" at the public ident timing: dot 0.1 s, dash 0.3 s, 0.1 s between elements, 0.3 s between letters.
void addIdent(Gen& g, double t0, const std::vector<std::string>& letters) {
    double t = t0;
    for (size_t li = 0; li < letters.size(); li++) {
        const std::string& code = letters[li];
        for (size_t j = 0; j < code.size(); j++) {
            const double d = code[j] == '-' ? 0.3 : 0.1;
            for (double k = 0;; k++) {
                const double ts = t + k / dme::kIdentRatePps;
                if (ts >= t + d) break;
                g.add(ts);
                g.add(ts + 12e-6);
            }
            t += d;
            if (j + 1 < code.size()) t += 0.1;
        }
        if (li + 1 < letters.size()) t += 0.3;
    }
}

} // namespace

int main() {
    // the Morse table: the letters of the ident
    CHECK(dme::morseLetter("---") == 'O', "O");
    CHECK(dme::morseLetter(".-.") == 'R', "R");
    CHECK(dme::morseLetter("-..") == 'D', "D");
    CHECK(dme::morseLetter("-.-.-.-") == '?', "unknown group");

    // X channel for the first second, Y for the second, then the ident "ORD" after a pause
    {
        Gen g;
        addPairs(g, 0.01, 1.0, dme::kSpacingXUs);
        addPairs(g, 1.0, 2.0, dme::kSpacingYUs);
        addIdent(g, 2.5, {"---", ".-.", "-.."});

        dme::Receiver rx;
        rx.configure(kFs);
        feedUntil(rx, g, 0.9);
        const dme::Telemetry& a = rx.telemetry();
        CHECK(a.channel == dme::Channel::X, "channel at 0.9 s should be X, got %s", dme::channelName(a.channel));
        CHECK(a.pairRatePps > 2565 && a.pairRatePps < 2835, "X pair rate %.0f", a.pairRatePps);

        feedUntil(rx, g, 1.95);
        const dme::Telemetry& b = rx.telemetry();
        CHECK(b.channel == dme::Channel::Y, "channel at 1.95 s should be Y, got %s", dme::channelName(b.channel));
        CHECK(b.pairRatePps > 2565 && b.pairRatePps < 2835, "Y pair rate %.0f", b.pairRatePps);
        CHECK(b.pairsX > 2500 && b.pairsX < 2900, "X pairs %d", b.pairsX);
        CHECK(b.pairsY > 2500 && b.pairsY < 2900, "Y pairs %d", b.pairsY);
        CHECK(!b.identKeyed, "ident keyed before the ident starts");

        feedUntil(rx, g, 6.5);
        const dme::Telemetry& c = rx.telemetry();
        CHECK(c.ident.find("ORD") != std::string::npos, "ident '%s' (want ORD)", c.ident.c_str());
        CHECK(!c.identKeyed, "ident still keyed after the silence");
    }

    // pairs 24 us apart are neither X nor Y: nothing is counted
    {
        Gen g;
        addPairs(g, 0.01, 0.5, 24.0);
        dme::Receiver rx;
        rx.configure(kFs);
        feedUntil(rx, g, 0.6);
        const dme::Telemetry& t = rx.telemetry();
        CHECK(t.pairsX == 0 && t.pairsY == 0 && t.channel == dme::Channel::None, "wrong spacing counted: X %d Y %d", t.pairsX, t.pairsY);
    }
    return testSummary("test_dme");
}
