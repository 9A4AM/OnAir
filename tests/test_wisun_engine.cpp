// Wi-SUN FAN through the engine (setStandard(79)): the synthetic source plays the test schedule at 20 Msps, the receiver reports through latestRx().
// No sound.
#include "dect2/wisun_rx.h"
#include "dect2/engine.h"
#include <chrono>
#include <cstdio>
#include <thread>
using namespace dect2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main() {
    const ModeTuning mt = wisunTuning();
    CHECK(mt.stdMode == 79 && modeTuningById("wisun") && modeTuningById("wisun")->stdMode == 79, "tuning table");
    Engine e;
    DeviceInfo dev;
    TuneSettings t;
    t.centerHz = mt.defMhz * 1e6;
    t.bandwidthMhz = mt.bandwidthMhz;
    t.sampleRate = mt.sampleRate;
    t.synth.mode = 79;
    t.synth.snrDb = 25;
    FileOptions fo;
    e.setStandard(79);
    CHECK(e.start(dev, t, fo), "engine start");
    CHECK(e.activeStandard() == 78, "active standard %d", e.activeStandard());
    RxTelemetry rx;
    uint64_t seq = 0;
    int reports = 0, wrong = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 5.0) {
        if (e.latestRx(rx, seq)) { seq = rx.seq; reports++; if (rx.standard != 78) wrong++; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const WisunTelemetry& d = rx.wisun;
    printf("  %d reports: %s, %llu bad, %llu dropped, load %.0f%%, %.1f - %.1f MHz\n", reports, wisunSummary(d).c_str(), (unsigned long long)d.blocksBad,
           (unsigned long long)d.dropped, d.loadPct, d.loMhz, d.hiMhz);
    CHECK(reports > 10 && wrong == 0, "%d reports, %d wrong", reports, wrong);
    CHECK(rx.rateOk, "rate");
    CHECK(d.region == "ANZ" && d.loMhz < 915.5 && d.hiMhz > 927.5, "coverage %s %.1f - %.1f", d.region.c_str(), d.loMhz, d.hiMhz);
    CHECK(d.pans.size() == 1 && d.nodes.size() == 3 && d.blocksOk > 100 && d.blocksBad == 0, "%zu PANs, %zu nodes, %llu frames", d.pans.size(), d.nodes.size(),
          (unsigned long long)d.blocksOk);
    CHECK(!d.pans.empty() && d.pans[0].netName == "OnAir-Test-FAN", "network name");
    CHECK(d.ppsHist.size() >= 3 && d.pps > 10, "pps %.0f, %zu history", d.pps, d.ppsHist.size());
    CHECK(e.droppedSamples() == 0, "dropped %llu", (unsigned long long)e.droppedSamples());
    e.wisun().clearResults();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    e.stop();
    printf(fails ? "wisun engine: %d FAILED\n" : "wisun engine: all passed\n", fails);
    return fails ? 1 : 0;
}
