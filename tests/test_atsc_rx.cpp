// ATSC receiver: generated 8-VSB signals through the whole receive chain, with carrier offset, clock offset, noise and echoes.
#include "dect2/atsc_gen.h"
#include "dect2/atsc_rx.h"
#include "dect2/dvbt_gen.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
using namespace dect2;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

struct Result { size_t packets = 0, good = 0, errFlag = 0, wrong = 0; double lockSec = -1; AtscTelemetry tel; double cpuRt = 0; };

static Result run(const atsc::ChannelConfig& cc, double inRate, double seconds, unsigned seed = 1, bool verbose = false) {
    atsc::Generator gen(dvbt::testTsSource(), cc, inRate, seed);
    AtscReceiver rx;
    rx.configure(inRate);
    std::vector<uint8_t> got;
    double sigSec = 0;
    rx.setPacketCallback([&](const uint8_t* p, size_t n, double s) { got.insert(got.end(), p, p + n * 188); sigSec += s; });
    // the reference stream: the same source, packet by packet
    auto ref = dvbt::testTsSource();
    std::vector<cf32> blk;
    const size_t chunk = 1 << 15;
    size_t total = 0;
    Result r;
    double cpu = 0;
    while (total < (size_t)(seconds * inRate)) {
        blk.clear();
        gen.generate(chunk, blk);
        const auto t0 = std::chrono::steady_clock::now();
        rx.feed(blk.data(), blk.size());
        cpu += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        total += blk.size();
        if (r.lockSec < 0 && rx.detectLevel() >= 4) r.lockSec = total / inRate;
    }
    uint64_t seq = 0;
    rx.telemetry(r.tel, seq);
    r.cpuRt = cpu / seconds;
    // every decoded packet that is not flagged must be a packet of the reference stream
    r.packets = got.size() / 188;
    std::set<std::string> refSet;
    for (int i = 0; i < 60000; i++) { uint8_t p[188]; ref(p); p[0] = 0x47; refSet.insert(std::string((char*)p, 188)); }
    for (size_t k = 0; k < r.packets; k++) {
        if (got[k * 188 + 1] & 0x80) { r.errFlag++; continue; }
        if (refSet.count(std::string((char*)&got[k * 188], 188))) r.good++; else r.wrong++;
    }
    if (verbose) {
        printf("  level %d  pilot %d seg %d field %d  cfo %.0f Hz sro %.1f ppm  SNR %.1f dB (data %.1f)  sync q %.2f  fields %llu  RS clean %llu corr %llu fail %llu\n",
               rx.detectLevel(), r.tel.pilot, r.tel.segSync, r.tel.fieldSync, r.tel.cfoHz, r.tel.sroPpm, r.tel.snrDb, r.tel.dataSnrDb, r.tel.syncQuality,
               (unsigned long long)r.tel.fields, (unsigned long long)r.tel.rsClean, (unsigned long long)r.tel.rsCorrected, (unsigned long long)r.tel.rsFailed);
    }
    return r;
}

int main(int argc, char** argv) {
    const bool verbose = argc > 1;
    atsc::ChannelConfig c;
    {
        printf("clean, 8 Msps\n");
        Result r = run(c, 8e6, 1.2, 1, true);
        printf("  packets %zu good %zu flagged %zu wrong %zu, locked at %.2f s, CPU %.2fx real time\n", r.packets, r.good, r.errFlag, r.wrong, r.lockSec, r.cpuRt);
        CHECK(r.good > 1000 && r.wrong == 0, "clean signal: %zu good, %zu wrong", r.good, r.wrong);
    }
    if (verbose) return fails;
    printf(fails ? "ATSC receiver tests FAILED\n" : "ATSC receiver tests passed\n");
    return fails ? 1 : 0;
}
