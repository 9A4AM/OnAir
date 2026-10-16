// TETRAPOL codec round-trip and receiver on the test signal (D_SYSTEM_INFO).
#include "dect2/tetrapol/tetrapol_codec.h"
#include "dect2/tetrapol/tetrapol_gen.h"
#include "dect2/tetrapol/tetrapol_rx.h"
#include "dect2/tetrapol/tetrapol_tel.h"
#include "dect2/engine/modes.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace dect2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void testCodec() {
    tetrapol::DataFrame in;
    in.scramble = kTetrapolTestScramble;
    tetrapol::packSysinfo(in.payload, kTetrapolTestCountry, kTetrapolTestSystem, kTetrapolTestBn, kTetrapolTestCell,
                          kTetrapolTestLa, kTetrapolTestChannel, 2);
    uint8_t bits[tetrapol::kFrameBits];
    tetrapol::encodeDataFrame(in, bits);
    tetrapol::DataFrame out;
    CHECK(tetrapol::decodeFrame(bits, kTetrapolTestScramble, out) && out.ok, "decode");
    int co, sy, bn, ce, la, ch, ba;
    CHECK(tetrapol::parseSysinfo(out.payload, co, sy, bn, ce, la, ch, ba), "sysinfo");
    CHECK(co == kTetrapolTestCountry && sy == kTetrapolTestSystem && bn == kTetrapolTestBn && ce == kTetrapolTestCell
          && la == kTetrapolTestLa && ch == kTetrapolTestChannel && ba == 2, "fields %d %d %d %d %d %d %d", co, sy, bn, ce, la, ch, ba);
    int off = -1;
    tetrapol::DataFrame h;
    CHECK(tetrapol::huntFrame(bits, tetrapol::kFrameBits, -1, off, h) && off == 0 && h.scramble == kTetrapolTestScramble, "hunt");
}

static TetrapolTelemetry run(double snr, double cfo, double ppm, double rate, int blocks) {
    SynthConfig sc;
    sc.mode = 81; sc.snrDb = snr; sc.cfoHz = cfo; sc.sroPpm = ppm;
    auto syn = makeTetrapolSynth(sc, rate);
    TetrapolReceiver rx;
    rx.configure(rate);
    rx.setCenterHz(kTetrapolTestCentreHz);
    const size_t chunk = (size_t)std::max(1024.0, rate * 0.05);
    std::vector<cf32> buf(chunk);
    uint64_t guard = 0;
    TetrapolTelemetry t;
    for (;;) {
        syn->generate(buf.data(), chunk);
        rx.feed(buf.data(), chunk);
        rx.telemetry(t, 0);
        if (t.blocksOk >= (uint64_t)blocks || ++guard > 800) break;
    }
    return t;
}

int main() {
    testCodec();
    const ModeTuning mt = tetrapolTuning();
    CHECK(mt.stdMode == 81 && modeTuningById("tetrapol") && modeTuning(81), "tuning table");
    const TetrapolTelemetry t = run(30, 0, 0, 250000, 3);
    printf("  loopback %s; ok %llu bad %llu cells %zu\n", tetrapolSummary(t).c_str(),
           (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad, t.cells.size());
    CHECK(t.blocksOk >= 3, "frames %llu", (unsigned long long)t.blocksOk);
    CHECK(!t.cells.empty() && t.cells[0].hasSysinfo, "no sysinfo");
    if (!t.cells.empty()) {
        const auto& c = t.cells[0];
        CHECK(c.country == kTetrapolTestCountry && c.systemId == kTetrapolTestSystem && c.cellId == kTetrapolTestCell
              && c.locArea == kTetrapolTestLa && c.scramble == kTetrapolTestScramble,
              "cell %d/%d/%d LA %d scr %d", c.country, c.systemId, c.cellId, c.locArea, c.scramble);
    }
    printf(fails ? "%d failure(s)\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
