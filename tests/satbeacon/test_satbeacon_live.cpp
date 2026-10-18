// The satellite beacon receiver as the engine runs it: IQ chunks in, telemetry out; the CSV log file, reset, and input that must not poison the loops.
#include "dect2/satbeacon/satbeacon_gen.h"
#include "dect2/satbeacon/satbeacon_live.h"
#include "dect2/satbeacon/satbeacon_tel.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
using namespace dect2;
#include "../common/test_util.h"

static void feedSeconds(SatbeaconLiveReceiver& r, SatbeaconGenerator& g, double rate, double seconds, bool poison = false) {
    std::vector<cf32> x(32768);
    for (int64_t n = 0; n < (int64_t)(seconds * rate); n += (int64_t)x.size()) {
        g.generate(x.data(), x.size());
        if (poison) { x[5] = cf32(NAN, 0); x[77] = cf32(0, INFINITY); }
        r.feed(x.data(), x.size());
    }
}

int main() {
    const double rate = 1e6;
    const std::string path = "/tmp/satbeacon_live_test.csv";
    std::remove(path.c_str());
    SatbeaconLiveReceiver r;
    CHECK(!r.ready(), "not ready before configure");
    r.configure(rate);
    CHECK(r.ready(), "ready after configure");
    r.setCenterHz(11705.250e6);
    r.setLogFile(path);
    SatbeaconGenConfig c; c.rate = rate; c.snrDb = 30; c.reference = false;
    SatbeaconGenerator g(c);
    SbTelemetry t;
    uint64_t seq = 0;
    feedSeconds(r, g, rate, 50, true);
    CHECK(r.telemetry(t, seq), "telemetry");
    seq = t.seq;
    CHECK(!r.telemetry(t, seq), "no new telemetry without a new sequence number");
    printf("  %s\n", sbSummary(t).c_str());
    CHECK(t.state == 3, "state %d", t.state);
    CHECK(std::fabs(t.cfoHz - (236100 - 0.4 * t.timeSec)) < 4.0, "carrier %.1f Hz", t.cfoHz);
    CHECK(t.logging && t.logPath == path, "logging to %s", t.logPath.c_str());
    CHECK(t.centreHz == 11705.250e6, "centre %.0f", t.centreHz);
    CHECK(sbSummary(t).find("frames") != std::string::npos, "summary names the frames: %s", sbSummary(t).c_str());
    // the CSV: a header, carrier lines and frame lines with the frame bytes in hex
    {
        std::ifstream f(path);
        std::string line, all;
        int carrier = 0, frames = 0, lines = 0;
        while (std::getline(f, line)) {
            lines++;
            if (lines == 1) CHECK(line.rfind("kind,time_s", 0) == 0, "header: %s", line.c_str());
            if (line.rfind("carrier,", 0) == 0) carrier++;
            if (line.rfind("frame,", 0) == 0) { frames++; if (frames == 1) all = line; }
        }
        printf("  CSV: %d lines, %d carrier, %d frame\n", lines, carrier, frames);
        CHECK(carrier >= 20, "carrier lines %d", carrier);
        CHECK(frames >= 40, "frame lines %d", frames);
        CHECK(all.find("FAF320FE") != std::string::npos, "frame line carries the frame: %s", all.c_str());
    }
    // reset: the chain starts over, the telemetry shows no frames until it is learned again
    r.reset();
    SatbeaconGenerator g2(c);
    feedSeconds(r, g2, rate, 1);
    CHECK(r.telemetry(t, 0), "telemetry after reset");
    CHECK(t.framesTotal == 0 && t.frames.empty(), "frames cleared by reset (%llu)", (unsigned long long)t.framesTotal);
    // pure noise: nothing found, nothing invented
    {
        SatbeaconLiveReceiver n;
        n.configure(rate);
        SatbeaconGenConfig q; q.rate = rate; q.snrDb = 30; q.telemetry = false; q.reference = false; q.seed = 3;
        // a carrier far below the noise: silence for the receiver
        q.amp = 0.001; q.snrDb = -20;
        SatbeaconGenerator gw(q);
        feedSeconds(n, gw, rate, 8);
        SbTelemetry tn;
        CHECK(n.telemetry(tn, 0), "noise telemetry");
        CHECK(tn.state == 0 && tn.frames.empty() && tn.tones == 0, "noise: state %d tones %d", tn.state, tn.tones);
    }
    std::remove(path.c_str());
    return testSummary("satbeacon live");
}
