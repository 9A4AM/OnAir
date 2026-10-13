// Wi-Fi HaLow / 802.11ah through the engine (setStandard(72)): the synthetic source plays the test schedule, the receiver reports through latestRx().
// No sound.
#include "dect2/halow_rx.h"
#include "dect2/engine.h"
#include <chrono>
#include <cstdio>
#include <thread>
using namespace dect2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main() {
    const ModeTuning mt = halowTuning();
    CHECK(mt.stdMode == 72 && modeTuningById("halow") && modeTuningById("halow")->stdMode == 72, "tuning table");
    CHECK(minSampleRateFor(72, 4) == 2e6, "minimum rate");
    Engine e;
    DeviceInfo dev;
    TuneSettings t;
    t.centerHz = mt.defMhz * 1e6;
    t.bandwidthMhz = mt.bandwidthMhz;
    t.sampleRate = mt.sampleRate;
    t.synth.mode = 72;
    t.synth.snrDb = 25;
    FileOptions fo;
    e.setStandard(72);
    CHECK(e.start(dev, t, fo), "engine start");
    CHECK(e.activeStandard() == 71, "active standard %d", e.activeStandard());
    RxTelemetry rx;
    uint64_t seq = 0;
    int reports = 0, wrong = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 5.0) {
        if (e.latestRx(rx, seq)) { seq = rx.seq; reports++; if (rx.standard != 71) wrong++; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const HalowTelemetry& d = rx.halow;
    printf("  %d reports: %s, %zu networks, %llu bad\n", reports, halowSummary(d).c_str(), d.nets.size(), (unsigned long long)d.blocksBad);
    CHECK(reports > 10 && wrong == 0, "%d reports, %d wrong", reports, wrong);
    CHECK(rx.rateOk, "rate");
    CHECK(d.nets.size() == 4 && d.blocksOk > 300 && d.blocksBad == 0, "%zu networks, %llu frames", d.nets.size(), (unsigned long long)d.blocksOk);
    CHECK(d.centerMhz == 863.5 && d.chanHint == 1, "channel hint %d at %.1f MHz", d.chanHint, d.centerMhz);
    CHECK(d.ppsHist.size() >= 3 && d.pps > 20, "pps %.0f, %zu history", d.pps, d.ppsHist.size());
    CHECK(e.droppedSamples() == 0, "dropped %llu", (unsigned long long)e.droppedSamples());
    e.halow().clearResults();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    e.stop();
    printf(fails ? "halow engine: %d FAILED\n" : "halow engine: all passed\n", fails);
    return fails ? 1 : 0;
}
