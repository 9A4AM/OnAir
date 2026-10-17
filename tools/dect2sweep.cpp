// Fast wideband sweep scanner:
//   dect2sweep --from MHz --to MHz [--bin kHz] [--dwell ms] [--settle ms|auto] [--rate Msps] [--gain dB | --lna dB --vga dB --amp]
//              [--floor-pct 50] [--thr dB] [--interval s] [--count n] [--csv out.csv] [--signals log.csv]
//   dect2sweep --compare a.csv b.csv      (noise floor and level change between two saved scans)
#include "dect2/engine/engine.h"
#include "dect2/sweep/sweep.h"
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace dect2;
using namespace dect2::sweep;

static std::atomic<bool> g_stop{false};
static void onSig(int) { g_stop = true; }

int main(int argc, char** argv) {
    SweepConfig c;
    TuneSettings t;
    t.lnaDb = 32; t.vgaDb = 20; t.ampOn = false;
    std::string csv, sigCsv;
    bool autoSettle = true;
    double gain = -1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto nx = [&]() { return i + 1 < argc ? atof(argv[++i]) : 0.0; };
        if (a == "--compare" && i + 2 < argc) {
            std::vector<Sweep> x, y;
            std::string err;
            if (!readCsv(argv[i + 1], x, err) || !readCsv(argv[i + 2], y, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
            SweepDiff d = compareSweeps(x.back(), y.back());
            printf("noise floor change %+.2f dB, mean level change %+.2f dB\nlargest rise %+.1f dB at %.4f MHz, largest fall %+.1f dB at %.4f MHz\n",
                   d.floorChangeDb, d.meanChangeDb, d.maxRiseDb, d.maxRiseHz / 1e6, d.maxFallDb, d.maxFallHz / 1e6);
            return 0;
        }
        if (a == "--from") c.startHz = nx() * 1e6; else if (a == "--to") c.stopHz = nx() * 1e6; else if (a == "--bin") c.binHz = nx() * 1e3;
        else if (a == "--dwell") c.dwellSec = nx() / 1e3;
        else if (a == "--settle") { if (i + 1 < argc && !strcmp(argv[i + 1], "auto")) i++; else { c.settleSec = nx() / 1e3; autoSettle = false; } }
        else if (a == "--rate") t.sampleRate = nx() * 1e6; else if (a == "--gain") gain = nx();
        else if (a == "--lna") t.lnaDb = (int)nx(); else if (a == "--vga") t.vgaDb = (int)nx(); else if (a == "--amp") t.ampOn = true;
        else if (a == "--floor-pct") c.floorPercentile = nx(); else if (a == "--thr") c.thresholdDb = nx();
        else if (a == "--crop") c.cropFrac = nx(); else if (a == "--interval") c.minIntervalSec = nx(); else if (a == "--count") c.maxSweeps = (int)nx();
        else if (a == "--csv" && i + 1 < argc) csv = argv[++i]; else if (a == "--signals" && i + 1 < argc) sigCsv = argv[++i];
    }
    std::string err;
    auto list = listRadios(err);
    if (list.empty()) { fprintf(stderr, "no radio found\n"); return 1; }
    DeviceInfo dev = list[0];
    if (dev.isGeneric()) {
        if (dev.maxRateHz > 0) t.sampleRate = std::min(t.sampleRate, dev.maxRateHz);
        t.gainDb = gain >= 0 ? gain : std::max(dev.gainMinDb, dev.gainMaxDb * 0.6);
    } else if (gain >= 0) { t.lnaDb = (int)gain; }
    t.centerHz = c.startHz;
    t.bandwidthMhz = t.sampleRate / 1e6;
    IqSweepSource src(dev, t);
    if (!src.open(err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    signal(SIGINT, onSig);
    if (autoSettle) {
        const double st = calibrateSettle(src, c.startHz, c.stopHz, err);
        if (st > 0) { c.settleSec = st; fprintf(stderr, "settle time measured: %.1f ms\n", st * 1e3); }
    }
    SignalLog log;
    std::vector<Sweep> kept;
    bool ok = runSchedule(src, c, [&](const Sweep& s) {
        log.update(s);
        printf("sweep %.3f-%.3f MHz: %d hops, %.0f MHz/s, noise floor %.1f dBFS/bin, %zu signals\n", c.startHz / 1e6, c.stopHz / 1e6, s.hops,
               (c.stopHz - c.startHz) / 1e6 / std::max(1e-6, s.elapsedSec), s.floorOverallDb, s.signals.size());
        for (auto& d : s.signals) printf("  %10.4f MHz  bw %7.1f kHz  peak %6.1f dBFS  SNR %5.1f dB\n", d.freqHz / 1e6, d.bwHz / 1e3, d.peakDb, d.snrDb);
        fflush(stdout);
        if (!csv.empty()) { kept.clear(); kept.push_back(s); std::string e; writeCsv(csv, kept, e); }
        return !g_stop.load();
    }, err, &g_stop);
    src.close();
    if (!sigCsv.empty()) { std::string e; writeSignalsCsv(sigCsv, log.records(), e); }
    if (!ok) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    return 0;
}
