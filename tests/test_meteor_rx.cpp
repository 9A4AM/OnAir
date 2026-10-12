// Meteor-M LRPT receiver on the test signal: OQPSK 72k (N2-3 / N2-4), QPSK 72k and OQPSK 80k interleaved, with carrier offsets, a
// symbol clock off, noise and other input rates; the picture strips must come back exactly as the strip coder makes them (the lossy
// step is only the coder's). Automatic modulation finds QPSK. Noise alone gives nothing. No real recording was available.
#include "dect2/meteor_rx.h"
#include "dect2/meteor_tel.h"
#include "dect2/meteor_gen.h"
#include "dect2/meteor_proto.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <vector>
using namespace dect2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

// the strip the receiver should show for picture row r, block k, channel c
static void expected(int c, int r, int k, uint8_t out[8][112]) {
    uint8_t px[8][112];
    for (int y = 0; y < 8; y++) for (int x = 0; x < 112; x++) px[y][x] = meteorTestPixel(c, k * 112 + x, r * 8 + y);
    meteor::McuStrip s;
    meteor::decodeImagePacket(meteor::encodeImagePacket(px, k * 14, 80, 20000, 0), s);
    std::memcpy(out, s.px, sizeof s.px);
}

struct Result { MeteorTelemetry t; int rowsChecked = 0, rowsWrong = 0; };

static Result run(const char* name, MeteorGenConfig g, double seconds, int modSetting, double offsetHz = 0) {
    MeteorGenerator gen(g);
    MeteorReceiver rx;
    rx.setModulation(modSetting);
    if (getenv("METEOR_LOG")) rx.setLogCallback([](const std::string& m) { printf("    log: %s\n", m.c_str()); });
    rx.setSignalOffset(offsetHz);
    rx.configure(g.rate);
    std::vector<cf32> buf(65536);
    MeteorTelemetry t;
    uint64_t seq = 0;
    for (double done = 0; done < seconds; done += (double)buf.size() / g.rate) {
        gen.generate(buf.data(), buf.size());
        if (offsetHz != 0) {   // the signal sits offsetHz from the centre
            static double ph = 0;
            for (auto& v : buf) { v *= cf32((float)std::cos(ph), (float)std::sin(ph)); ph += 2 * 3.14159265358979 * offsetHz / g.rate; if (ph > 3.14159265358979) ph -= 2 * 3.14159265358979; }
        }
        rx.feed(buf.data(), buf.size());
        if (rx.telemetry(t, seq)) seq = t.seq;
    }
    Result r; r.t = t;
    // complete strips must match a picture row exactly
    std::map<int, int> rowOf;
    if (t.image) {
        for (size_t c = 0; c < 6; c++) {
            for (const auto& s : t.image->ch[c]) {
                if (!s || s->got != 0x3FFF) continue;
                int match = -1;
                uint8_t e[8][112];
                for (int pr = 0; pr < g.rows && match < 0; pr++) {
                    bool all = true;
                    for (int k = 0; k < 14 && all; k++) {
                        expected((int)c, pr, k, e);
                        for (int y = 0; y < 8 && all; y++) all = !std::memcmp(e[y], &s->px[(size_t)y * kMeteorWidth + (size_t)k * 112], 112);
                    }
                    if (all) match = pr;
                }
                r.rowsChecked++;
                if (match < 0) r.rowsWrong++;
                else if (c == (size_t)(g.apids[0] - 64)) rowOf[s->row] = match;
            }
        }
    }
    // rows follow each other
    int seqWrong = 0;
    for (auto it = rowOf.begin(); it != rowOf.end(); ++it) {
        auto nx = std::next(it);
        if (nx != rowOf.end() && nx->first == it->first + 1 && nx->second != (it->second + 1) % g.rows) seqWrong++;
    }
    r.rowsWrong += seqWrong;
    printf("  %-34s %s | frames %llu ok %llu bad, RS fixed %llu, cfo %+.0f Hz, SNR %.1f dB, clock %+.0f ppm, rows %d, strips %d checked, %d wrong\n", name,
           meteorSummary(t).c_str(), (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad, (unsigned long long)t.rsCorrected, t.cfoHz, t.snrDb,
           t.clockPpm, t.image ? t.image->rows : 0, r.rowsChecked, r.rowsWrong);
    return r;
}

static int only = -1, idx = 0;
static bool want() { return only < 0 || idx++ == only; }

int main(int argc, char** argv) {
    if (argc > 1) only = atoi(argv[1]);
    const ModeTuning mt = meteorTuning();
    CHECK(mt.stdMode == 61 && mt.defMhz == 137.9, "tuning");
    if (want()) {
        MeteorGenConfig g; g.modulation = 1; g.snrDb = 14; g.cfoHz = 1800; g.clockPpm = 40;
        const Result r = run("OQPSK 72k, +1.8 kHz, 14 dB", g, 8, 1);
        CHECK(r.t.blocksOk > 30 && r.rowsChecked >= 20 && r.rowsWrong == 0, "OQPSK 72k");
        CHECK(std::fabs(r.t.cfoHz - 1800) < 60, "cfo %.0f", r.t.cfoHz);
    }
    if (want()) {
        MeteorGenConfig g; g.modulation = 1; g.snrDb = 25; g.cfoHz = -4500; g.rate = 2.4e6;
        const Result r = run("OQPSK 72k, -4.5 kHz, 2.4 Msps, +200k", g, 6, 0, -200e3);
        CHECK(r.t.blocksOk > 20 && r.rowsWrong == 0 && r.t.modulation == 1, "OQPSK 72k at 2.4 Msps, offset");
    }
    if (want()) {
        MeteorGenConfig g; g.modulation = 2; g.snrDb = 14; g.cfoHz = -2500; g.clockPpm = -60; g.apids = {64, 65, 68};
        const Result r = run("QPSK 72k, -2.5 kHz, 14 dB, night", g, 8, 2);
        CHECK(r.t.blocksOk > 30 && r.rowsChecked >= 20 && r.rowsWrong == 0, "QPSK 72k");
        CHECK(r.t.image && r.t.image->packets[4] > 0 && r.t.image->packets[2] == 0, "night channels");
    }
    if (want()) {
        MeteorGenConfig g; g.modulation = 2; g.snrDb = 20; g.cfoHz = 700;
        const Result r = run("QPSK 72k found by automatic", g, 18, 0);
        CHECK(r.t.blocksOk > 20 && r.t.modulation == 2 && r.rowsWrong == 0, "automatic finds QPSK (running %d)", r.t.modulation);
    }
    if (want()) {
        MeteorGenConfig g; g.modulation = 3; g.snrDb = 16; g.cfoHz = 1200; g.rate = 400e3;
        const Result r = run("OQPSK 80k interleaved, 400 ksps", g, 26, 3);
        CHECK(r.t.blocksOk > 20 && r.rowsChecked >= 10 && r.rowsWrong == 0, "OQPSK 80k interleaved");
    }
    if (want()) {
        MeteorGenConfig g; g.modulation = getenv("TM") ? atoi(getenv("TM")) : 1; g.snrDb = getenv("TS") ? atof(getenv("TS")) : 8; if (getenv("TR")) g.rate = atof(getenv("TR"));
        const Result r = run("OQPSK 72k at 8 dB", g, getenv("TT") ? atof(getenv("TT")) : 8, g.modulation);
        CHECK(r.t.blocksOk > 10 && r.rowsWrong == 0, "8 dB");
    }
    if (want()) {
        MeteorGenConfig g; g.amp = 0.0; g.snrDb = 99; g.modulation = 1;
        MeteorGenerator gen(g);
        MeteorReceiver rx; rx.configure(1e6);
        std::vector<cf32> buf(65536);
        std::mt19937 rng(3); std::normal_distribution<float> nd(0, 0.05f);
        MeteorTelemetry t; uint64_t seq = 0;
        for (int i = 0; i < 60; i++) { for (auto& v : buf) v = cf32(nd(rng), nd(rng)); rx.feed(buf.data(), buf.size()); if (rx.telemetry(t, seq)) seq = t.seq; }
        CHECK(t.blocksOk == 0 && (!t.image || t.image->rows == 0), "noise: %llu frames", (unsigned long long)t.blocksOk);
    }
    printf(fails ? "FAILED (%d)\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
