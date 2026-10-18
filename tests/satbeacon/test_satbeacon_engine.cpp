// The satellite beacon through the engine (setStandard(107)): the synthetic source plays the carrier with telemetry and the receiver reports through latestRx().
#include "dect2/engine/engine.h"
#include "dect2/satbeacon/satbeacon_live.h"
#include "dect2/satbeacon/satbeacon_tel.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
using namespace dect2;
#include "../common/test_util.h"

int main() {
    const ModeTuning mt = satbeaconTuning();
    Engine e;
    DeviceInfo dev;
    TuneSettings t;
    t.centerHz = mt.defMhz * 1e6; t.bandwidthMhz = mt.bandwidthMhz; t.sampleRate = mt.sampleRate;
    t.synth.mode = 107; t.synth.snrDb = 30;
    FileOptions fo;
    e.setStandard(107);
    CHECK(e.start(dev, t, fo), "engine start");
    CHECK(e.activeStandard() == 106, "active standard %d", e.activeStandard());
    RxTelemetry rx;
    uint64_t seq = 0;
    bool got = false;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 90) {
        if (e.latestRx(rx, seq)) { seq = rx.seq; if (rx.standard == 106 && rx.satbeacon.state == 3) { got = true; break; } }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    printf("  %s, carrier %+.1f Hz\n", sbSummary(rx.satbeacon).c_str(), rx.satbeacon.cfoHz);
    CHECK(got, "no frames in 90 s");
    CHECK(std::fabs(rx.satbeacon.cfoHz - 236100) < 400, "carrier %.1f Hz", rx.satbeacon.cfoHz);   // it drifts 0.4 Hz/s while the test runs
    CHECK(std::fabs(rx.satbeacon.baud - 4800) < 5, "bit rate %.2f", rx.satbeacon.baud);
    CHECK(rx.satbeacon.frameBits == 2048 && rx.satbeacon.syncName == "FAF320 (IRIG 106)", "frame length %d sync %s", rx.satbeacon.frameBits, rx.satbeacon.syncName.c_str());
    CHECK(rx.rateOk && rx.dataValid, "rate %d valid %d", (int)rx.rateOk, (int)rx.dataValid);
    CHECK(e.droppedSamples() == 0, "dropped %llu", (unsigned long long)e.droppedSamples());
    e.stop();
    return testSummary("satbeacon engine");
}
