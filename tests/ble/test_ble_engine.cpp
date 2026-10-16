// Bluetooth LE through the engine (setStandard(53)): the synthetic source plays the test schedule, the receiver reports through latestRx().
// No sound.
#include "dect2/ble/ble_rx.h"
#include "dect2/engine/engine.h"
#include <chrono>
#include <cstdio>
#include <thread>
using namespace dect2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main() {
    const ModeTuning mt = bleTuning();
    CHECK(mt.stdMode == 53 && modeTuningById("ble") && modeTuningById("ble")->stdMode == 53, "tuning table");
    Engine e;
    DeviceInfo dev;
    TuneSettings t;
    t.centerHz = mt.defMhz * 1e6;
    t.bandwidthMhz = mt.bandwidthMhz;
    t.sampleRate = mt.sampleRate;
    t.synth.mode = 53;
    t.synth.snrDb = 25;
    FileOptions fo;
    e.setStandard(53);
    CHECK(e.start(dev, t, fo), "engine start");
    CHECK(e.activeStandard() == 52, "active standard %d", e.activeStandard());
    RxTelemetry rx;
    uint64_t seq = 0;
    int reports = 0, wrong = 0;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 5.0) {
        if (e.latestRx(rx, seq)) { seq = rx.seq; reports++; if (rx.standard != 52) wrong++; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const BleTelemetry& d = rx.ble;
    printf("  %d reports: %s, %zu devices, %llu bad\n", reports, bleSummary(d).c_str(), d.devices.size(), (unsigned long long)d.blocksBad);
    CHECK(reports > 10 && wrong == 0, "%d reports, %d wrong", reports, wrong);
    CHECK(rx.rateOk, "rate");
    CHECK(d.devices.size() >= 5 && d.blocksOk > 1000, "%zu devices, %llu packets", d.devices.size(), (unsigned long long)d.blocksOk);
    CHECK(d.channel == 37, "channel %d", d.channel);
    CHECK(e.droppedSamples() == 0, "dropped %llu", (unsigned long long)e.droppedSamples());
    e.stop();
    printf(fails ? "ble engine: %d FAILED\n" : "ble engine: all passed\n", fails);
    return fails ? 1 : 0;
}
