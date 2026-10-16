// Whole-span DMR with the carriers decoded in parallel (WideBank on the shared pool) against the sequential loop: five generated carriers are
// recorded once, then fed through two WideModes banks, one with WideParallel off. The carriers table and the merged call log must be identical;
// prints the CPU and wall time of both feeds.
#include "dect2/dmr/dmr_gen.h"
#include "dect2/engine/wide_modes.h"
#include "dect2/t2/t2rx.h"
#include <sys/resource.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace dect2;
static const double kFs = 2.4e6;
static double cpu() { rusage u; getrusage(RUSAGE_SELF, &u); return u.ru_utime.tv_sec + u.ru_stime.tv_sec + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) * 1e-6; }

struct Out { std::vector<std::string> rows; std::vector<std::string> calls; std::vector<std::string> log; double wall = 0, cpuS = 0; };

static Out feedAll(const std::vector<cf32>& sig, size_t blk, bool par) {
    WideParallel::enabled() = par;
    Out o;
    WideModes wm;
    wm.setLogCallback([&](const std::string& s) { o.log.push_back(s); });
    wm.configure(10, kFs, 0);
    uint64_t seq = 0; RxTelemetry t;
    const double c0 = cpu();
    const auto w0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i + blk <= sig.size(); i += blk) { wm.feed(10, sig.data() + i, blk); wm.telemetry(10, t, seq); }
    o.wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - w0).count();
    o.cpuS = cpu() - c0;
    for (auto& r : t.wide) { char b[160]; snprintf(b, sizeof b, "%d %.0f %.2f %.2f %d %s", r.id, r.hz, r.levelDb, r.snrDb, r.state, r.info.c_str()); o.rows.push_back(b); }
    for (auto& c : t.dmr.callLog) { char b[160]; snprintf(b, sizeof b, "%d %d %d %.0f %d", (int)c.src, (int)c.dst, (int)c.kind, c.carrierHz, c.carrierId); o.calls.push_back(b); }
    WideParallel::enabled() = true;
    return o;
}

int main() {
    const double hz[5] = {-400000 + 700, -150000 - 900, 25000 + 300, 190000 - 500, 450000 + 100};
    const double rms[5] = {0.10, 0.07, 0.05, 0.08, 0.04};
    const size_t blk = (size_t)(kFs * 0.1), nblk = 60;
    std::vector<cf32> sum(blk * nblk, cf32(0, 0)), tmp(blk * nblk);
    for (int k = 0; k < 5; k++) {
        DmrGenConfig g; g.rate = kFs; g.cc = 1 + k; g.seed = (uint32_t)(11 + 7 * k); g.cfoHz = hz[k]; g.rms = rms[k]; g.snrDb = 35; g.traffic = 2;
        DmrSignal s(g);
        for (size_t i = 0; i < nblk; i++) s.generate(tmp.data() + i * blk, blk);
        for (size_t i = 0; i < sum.size(); i++) sum[i] += tmp[i];
    }
    Out a = feedAll(sum, blk, false), b = feedAll(sum, blk, true);
    int fails = 0;
    if (a.rows != b.rows || a.rows.size() < 4) { printf("FAIL: carriers table (%zu vs %zu rows)\n", a.rows.size(), b.rows.size()); fails++; }
    if (a.calls != b.calls || a.calls.empty()) { printf("FAIL: call log (%zu vs %zu)\n", a.calls.size(), b.calls.size()); fails++; }
    if (a.log != b.log) { printf("FAIL: log lines (%zu vs %zu)\n", a.log.size(), b.log.size()); fails++; }
    printf("5 carriers, 6 s: %zu rows, %zu calls, %zu log lines; sequential %.2f s wall %.2f s cpu; parallel %.2f s wall %.2f s cpu\n", a.rows.size(), a.calls.size(), a.log.size(), a.wall, a.cpuS, b.wall, b.cpuS);
    printf(fails ? "FAILED %d\n" : "ok\n", fails);
    return fails ? 1 : 0;
}
