// DVB-S2X super-frames (Annex E): the sequences, the stripper on generated super-framed symbols (format 0 with and without super-frame pilots, format 1, noise and a
// carrier offset), a carrier without super-frames passed through unchanged, and the whole receiver on a super-framed signal. The tests follow those of DecSAT (Arclamp,
// GPL-3.0-or-later), whose super-frame code this is adapted from.
#include "../../core/src/dvbs/dvbs_sf.h"
#include "dect2/dvbs/dvbs_gen.h"
#include "dect2/dvbs/dvbs_rx.h"
#include "dect2/dvbs/dvbs_testkit.h"
#include "dect2/dvbs/dvbs_tx.h"
#include <cmath>
#include <cstring>
#include <cstdio>
using namespace dect2;
using namespace dect2::dvbs;
#include "../common/test_util.h"

static std::vector<cf32> superframed(int format, bool sfPilots, size_t n, double cycles, double snrDb, int sf) {
    DvbsTxConfig c;
    c.standard = 2; c.mod = kQpsk; c.rate = 3; c.shortFrame = true; c.symbolRate = 1e6;
    c.superframe = sf >= 0 ? format : -1; c.sfPilots = sfPilots;
    auto tx = makeDvbsTx(c, nullptr);
    std::vector<cf32> x(n);
    tx->generate(x.data(), n);
    const double sigma = std::pow(10.0, -snrDb / 20.0) * 0.70710678;
    uint64_t s = 0x9E3779B97F4A7C15ull;
    auto rnd = [&]() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (double)(s >> 11) / (double)(1ull << 53); };
    for (size_t k = 0; k < n; k++) {
        const double u1 = std::max(rnd(), 1e-12), u2 = rnd();
        const double u3 = std::max(rnd(), 1e-12), u4 = rnd();
        const cf32 noise((float)(sigma * std::sqrt(-2 * std::log(u1)) * std::cos(6.283185307179586 * u2)), (float)(sigma * std::sqrt(-2 * std::log(u3)) * std::cos(6.283185307179586 * u4)));
        x[k] = x[k] * std::polar(1.0f, (float)(6.283185307179586 * cycles * (double)k + 0.7)) + noise;
    }
    return x;
}

static void runStripper(const std::vector<cf32>& x, SfStripper& st, std::vector<cf32>& out) {
    for (size_t i = 0; i < x.size(); i += 16384) st.push(&x[i], std::min<size_t>(16384, x.size() - i), out);
}

int main() {
    // the sequences
    {
        bool c[15];
        auto row = [&](int f, const char* s) { sfCodeword(f, c); bool ok = true; for (int j = 0; j < 15; j++) if (c[j] != (s[j] == '1')) ok = false; return ok; };
        CHECK(row(8, "000000011111111") && row(4, "000111100001111") && row(2, "011001100110011") && row(1, "101010101010101") && row(0, "000000000000000"), "simplex code rows");
        for (int f = 0; f < 16; f++) {
            float soft[15];
            sfCodeword(f, c);
            for (int j = 0; j < 15; j++) soft[j] = c[j] ? -30.f : 30.f;
            soft[0] = -soft[0]; soft[5] = -soft[5]; soft[11] = -soft[11];
            float m;
            CHECK(sfDecodeSffi(soft, m) == f, "format %d survives three errors", f);
        }
        const auto s0 = sfSosfSequence(0);
        bool ones = true;
        for (float v : s0) if (v != 1.f) ones = false;
        CHECK(ones, "SOSF 0 is all ones");
        CHECK(kSfl % kSfPilotPeriod == 0 && kSfl / kSfPilotPeriod == 415, "the pilot grid fills the super-frame");
        size_t np = 0;
        for (size_t k = 0; k < (size_t)kSfl; k++) if (sfPart(k, true) == SfPart::Pilot) np++;
        CHECK(np == 415 * 36, "pilot symbols %zu", np);
        SfScrambler a(0), b(5);
        int seen[4] = {};
        for (int i = 0; i < 4000; i++) seen[a.quarter[(size_t)i]]++;
        CHECK(seen[0] > 800 && seen[1] > 800 && seen[2] > 800 && seen[3] > 800, "scrambler uses all four values");
        bool differ = false;
        for (int i = 0; i < 100; i++) if (a.quarter[(size_t)i] != b.quarter[(size_t)i]) differ = true;
        CHECK(differ, "other code numbers differ");
    }
    // the stripper: format 0 with super-frame pilots
    {
        const auto x = superframed(0, true, 2 * (size_t)kSfl + 100000, 0.002, 8.0, 0);
        SfStripper st;
        std::vector<cf32> out;
        runStripper(x, st, out);
        const SfView v = st.view();
        printf("  slips %llu\n", (unsigned long long)v.slips);
        printf("  format 0 with pilots: locked %d format %d pilots %d read %llu sffi %.2f, %zu of %zu symbols out\n", v.locked, v.format, v.pilots, (unsigned long long)v.superframes, (double)v.sffiMatch, out.size(), x.size());
        CHECK(v.locked && v.format == 0 && v.pilots && v.superframes >= 2 && v.sffiMatch > 0.8f, "found, format 0, pilots on");
        CHECK(x.size() - out.size() > (size_t)(kSfHead + 300 * kSfPilotLen), "the super-frames' own symbols were taken out: %zu", x.size() - out.size());
    }
    // format 1 without pilots, lower SNR, negative offset
    {
        const auto x = superframed(1, false, 2 * (size_t)kSfl + 100000, -0.001, 6.0, 0);
        SfStripper st;
        std::vector<cf32> out;
        runStripper(x, st, out);
        const SfView v = st.view();
        CHECK(v.locked && v.format == 1 && !v.pilots, "format 1, pilots off: locked %d format %d pilots %d", v.locked, v.format, v.pilots);
    }
    // no super-frames: passed through
    {
        const auto x = superframed(0, false, 4 * (size_t)kSfl, 0.0, 12.0, -1);
        SfStripper st;
        std::vector<cf32> out;
        runStripper(x, st, out);
        const SfView v = st.view();
        CHECK(!v.locked && v.noneFound, "a carrier without super-frames: none found");
        CHECK(out.size() == x.size() && memcmp(out.data(), x.data(), x.size() * sizeof(cf32)) == 0, "passed through unchanged");
    }
    // the whole receiver on a super-framed signal: format 0 with and without super-frame pilots
    for (int pil = 0; pil < 3; pil++) {
        RunConfig rc = makeRun(2, kQpsk, 3, 1e6, 2e6, s2QefEsN0(kQpsk, 3, false) + (pil == 2 ? 20.0 : 6.0), 8.0);
        rc.sig.tx.shortFrame = true;
        rc.sig.tx.superframe = 0; rc.sig.tx.sfPilots = pil != 0;   // case 2: the same with a clean signal
        const RunResult r = runCase(rc);
        printf("  receiver, super-frames %s: %llu good packets, first after %.2f s, %s\n", pil ? "with SF-pilots" : "without", (unsigned long long)r.good, r.firstPacketSecs, r.tel.superframe.c_str());
        printf("    frames seen %llu dummy %llu ok %llu bad %llu bchBad %llu crcErr %llu\n", (unsigned long long)r.tel.framesSeen, (unsigned long long)r.tel.framesDummy, (unsigned long long)r.tel.blocksOk, (unsigned long long)r.tel.blocksBad, (unsigned long long)r.tel.bchBad, (unsigned long long)r.tel.crcErrors);
        if (r.gaps > 2) for (const auto& l : r.log) printf("    log: %s\n", l.c_str());
        CHECK(r.good > 100 && r.bad == 0 && r.gaps <= 2, "super-framed carrier (pilots %d): %llu good, %llu bad, %llu gaps", pil, (unsigned long long)r.good, (unsigned long long)r.bad, (unsigned long long)r.gaps);
        CHECK(r.tel.superframe.find("format 0") != std::string::npos && r.tel.superframe.find(pil ? "pilots on" : "pilots off") != std::string::npos, "reported: %s", r.tel.superframe.c_str());
    }
    // the stripper's output is exactly the stream without the super-frame's own symbols (clean signal)
    for (int pil = 0; pil < 2; pil++) {
        const auto x = superframed(0, pil != 0, 2 * (size_t)kSfl + 50000, 0.0, 60.0, 0);
        SfStripper st;
        std::vector<cf32> out;
        runStripper(x, st, out);
        std::vector<cf32> want;
        for (size_t k = 0; k < x.size(); k++) if (sfPart(k % (size_t)kSfl, pil != 0) == SfPart::Payload) want.push_back(x[k]);
        size_t bad = 0;
        const size_t n = std::min(out.size(), want.size());
        for (size_t k = 0; k < n; k++) if (std::abs(out[k] - want[k]) > 0.05f) bad++;
        printf("  exact strip (pilots %d): %zu symbols out of %zu wanted, %zu differ\n", pil, out.size(), want.size(), bad);
        CHECK(bad == 0 && out.size() + 32768 + 100 >= want.size(), "the stripped stream is exactly the payload symbols (pilots %d): %zu differ", pil, bad);
    }
    return testSummary("dvbs sf");
}
