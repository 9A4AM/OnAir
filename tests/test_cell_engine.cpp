// Cell towers through the engine (setStandard(34)): the synthetic source plays the 10 MHz test cell at 20 Msps; the receiver finds it,
// reads its MIB and system information and reports through latestRx() with its EARFCN.
#include "dect2/cell_rx.h"
#include "dect2/cell_tel.h"
#include "dect2/engine.h"
#include "dect2/modes.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
using namespace dect2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main() {
    const ModeTuning mt = cellTuning();
    CHECK(mt.stdMode == 34 && modeTuningById("cell") && modeTuningById("cell")->stdMode == 34, "tuning table");
    CHECK(lteHzToEarfcn(806e6) == 6300 && std::fabs(lteEarfcnToHz(6200) - 796e6) < 1 && lteBandOfEarfcn(3500) == 8, "EARFCN table");
    Engine e;
    DeviceInfo dev;
    TuneSettings t;
    t.centerHz = 806e6;
    t.bandwidthMhz = mt.bandwidthMhz;
    t.sampleRate = mt.sampleRate;
    t.synth.mode = 34;
    t.synth.snrDb = 25;
    FileOptions fo;
    e.setStandard(34);
    CHECK(e.start(dev, t, fo), "engine start");
    CHECK(e.activeStandard() == 33, "active standard %d", e.activeStandard());
    RxTelemetry rx;
    uint64_t seq = 0;
    int reports = 0, wrongStd = 0;
    bool ok = false;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 25.0) {
        if (e.latestRx(rx, seq)) {
            seq = rx.seq; reports++;
            if (rx.standard != 33) wrongStd++;
            for (const auto& c : rx.cell.cells) if (c.pci == 123 && c.hasSib1 && c.hasSib2) ok = true;
            if (ok) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    printf("  %d reports: %s\n", reports, cellSummary(rx.cell).c_str());
    CHECK(ok, "test cell not identified");
    for (const auto& c : rx.cell.cells)
        if (c.pci == 123) CHECK(c.earfcn == 6300 && c.mcc == 1 && c.mnc == 1 && c.enb == 54321 && c.sector == 2 && c.nRb == 50, "cell fields: EARFCN %d", c.earfcn);
    CHECK(wrongStd == 0 && rx.rateOk, "standard / rate");
    CHECK(!modeSummary(rx).empty(), "summary");
    e.stop();
    printf(fails ? "cell engine: %d FAILED\n" : "cell engine: all passed\n", fails);
    return fails ? 1 : 0;
}
