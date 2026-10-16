// DECT through the engine (setStandard(80)): the synthetic source plays the test base stations, the receiver reports through latestRx().
#include "dect2/engine/engine.h"
#include "dect2/dect/dect_rx.h"
#include <chrono>
#include <cstdio>
#include <thread>
using namespace dect2;
#include "../common/test_util.h"

int main() {
    const ModeTuning mt = dectTuning();
    CHECK(mt.stdMode == 80 && modeTuningById("dect") && modeTuningById("dect")->stdMode == 80, "tuning table");
    Engine e;
    DeviceInfo dev;
    TuneSettings t;
    t.centerHz = 1897.344e6 + mt.tuneOffsetHz;
    t.bandwidthMhz = mt.bandwidthMhz;
    t.sampleRate = mt.sampleRate;
    t.synth.mode = 80;
    t.synth.snrDb = 25;
    t.synth.modeOpt[0] = 0;
    FileOptions fo;
    e.setStandard(80);
    CHECK(e.start(dev, t, fo), "engine start");
    CHECK(e.activeStandard() == 79, "active standard %d", e.activeStandard());
    RxTelemetry rx;
    uint64_t seq = 0;
    int reports = 0, wrong = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 8.0) {
        if (e.latestRx(rx, seq)) { seq = rx.seq; reports++; if (rx.standard != 79) wrong++; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const DectTelemetry& d = rx.dect;
    printf("  %d reports: %s\n", reports, dectSummary(d).c_str());
    CHECK(reports > 10 && wrong == 0, "%d reports, %d wrong", reports, wrong);
    CHECK(rx.rateOk, "rate");
    CHECK(d.bases.size() == 2, "%zu base stations", d.bases.size());
    CHECK(d.blocksOk >= 100, "%llu frames", (unsigned long long)d.blocksOk);
    CHECK(e.droppedSamples() == 0, "dropped %llu", (unsigned long long)e.droppedSamples());
    e.dect().clearResults();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    e.stop();
    printf(fails ? "dect engine: %d FAILED\n" : "dect engine: all passed\n", fails);
    return fails ? 1 : 0;
}
