// KAIT live receiver (the engine's side): samples at 1 Msps fed in chunks, 3 s of noise first; it must find the sweep, capture the 10 s burst, decode it on its
// worker thread, publish the picture and the layers in the telemetry, and survive a reset in the middle.
#include "kait_test_util.h"
#include "dect2/kait/kait_live.h"
#include "dect2/kait/kait_tel.h"
#include "dect2/kapp/kapp_frame.h"

using namespace dect2;

int main() {
    const kait::Image img = kait::syntheticPicture(960, 540);
    const kait::BurstPlan plan = kait::planBurst(img, "LY2LIV", 0);
    CHECK(plan.ok, "plan");
    const std::vector<kait::cf> burst = kait::buildBurst(plan);
    kaittest::Rng rng(21);
    const size_t lead = (size_t)(3.0 * kait::kInternalRate);
    const std::vector<kait::cf> ch = kaittest::channel(burst, 10.0, 2300.0, 6.0, lead, (size_t)(3.0 * kait::kInternalRate), rng);
    const std::vector<kait::cf> rx = kapp::resample(ch, kait::kInternalRate, 1000000.0);

    KaitLiveReceiver r;
    std::vector<std::string> log;
    r.setLogCallback([&](const std::string& s) { log.push_back(s); });
    r.configure(1000000.0);
    CHECK(r.ready(), "not ready at 1 Msps");
    KaitLiveReceiver slow;
    slow.configure(200000.0);
    CHECK(!slow.ready(), "ready at 200 ksps");

    KaitTelemetry t;
    uint64_t last = 0;
    bool sawReceiving = false, sawDecoding = false;
    size_t pos = 0;
    bool resetDone = false;
    while (pos < rx.size()) {
        const size_t n = std::min<size_t>(50000, rx.size() - pos);
        r.feed(rx.data() + pos, n);
        pos += n;
        if (r.telemetry(t, last)) {
            last = t.seq;
            if (t.state == 2) sawReceiving = true;
            if (t.state == 3) sawDecoding = true;
        }
        if (!resetDone && pos > (size_t)(1.0e6 * 1.0)) { r.reset(); resetDone = true; pos = 0; last = 0; }   // reset early: it starts again from the beginning
    }
    r.waitIdle();
    for (int i = 0; i < 5; i++) {   // the last publish may land just after waitIdle
        if (r.telemetry(t, last)) last = t.seq;
    }
    printf("live: state %d, header %d, %s %dx%d, layers %d of %d, %zu of %zu bytes, SNR %.1f dB, CFO %.0f Hz, clock %.1f ppm, receiving seen %d, decoding seen %d\n", t.state, t.headerOk,
           t.callsign.c_str(), t.width, t.height, t.layersComplete, t.nLayers, t.bytesGood, t.bytesTotal, (double)t.snrDb, t.cfoHz, t.clockPpm, sawReceiving, sawDecoding);
    for (const std::string& s : log) printf("  log: %s\n", s.c_str());
    CHECK(t.state == 4 && t.headerOk, "no decoded burst: state %d", t.state);
    CHECK(t.callsign == "LY2LIV" && t.width == 960 && t.height == 540, "header fields");
    CHECK(t.layersComplete >= 7, "layers %d", t.layersComplete);
    CHECK(sawReceiving, "never in the receiving state");
    CHECK(t.image && t.image->w == 960 && kait::psnrRgb(img, *t.image) > 33.0, "picture missing or poor (%.1f dB)", t.image ? kait::psnrRgb(img, *t.image) : 0.0);
    CHECK(t.bursts >= 1 && t.burstsDecoded >= 1, "burst counters %llu %llu", (unsigned long long)t.bursts, (unsigned long long)t.burstsDecoded);
    CHECK(!kaitSummary(t).empty(), "summary");
    // noise only: stays listening
    KaitLiveReceiver q;
    q.configure(1000000.0);
    std::vector<kait::cf> nz(200000);
    for (int i = 0; i < 40; i++) {
        for (kait::cf& v : nz) v = kait::cf{(float)kaittest::Rng(5 + (uint64_t)i).gauss(), 0.f};
        q.feed(nz.data(), nz.size());
    }
    KaitTelemetry tq;
    q.telemetry(tq, 0);
    CHECK(tq.state == 1 && tq.bursts == 0, "noise: state %d bursts %llu", tq.state, (unsigned long long)tq.bursts);
    printf(fails ? "FAILED\n" : "OK\n");
    return fails ? 1 : 0;
}
