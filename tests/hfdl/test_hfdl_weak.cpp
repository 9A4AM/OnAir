// Weak HFDL bursts: the receiver falls back from the equaliser to a gain / phase tracker and to known symbols only when a burst does not decode.
#include "dect2/hfdl/hfdl_gen.h"
#include "dect2/hfdl/hfdl_rx.h"
#include "dect2/dsp/portable_rng.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <tuple>
using namespace dect2;
#include "../common/test_util.h"

static const double kRate = 48000, kCentre = 8.935e6, kCh = 8.927e6;

// Es/N0 snrDb for a 0 dB burst
static int run(int m1, double snrDb, double cfo, uint32_t seed, const std::vector<HfdlPath>& paths) {
    HfdlReceiver rx;
    rx.configure(kRate); rx.setCenterHz(kCentre); rx.setChannels({kCh});
    const std::vector<uint8_t> pdu = hfdlMpduDown(1, 23, {hfdlUnnumbered(hfdlAcarsHfnpdu("N1UA", "H1", '2', "WEAK SIGNAL TEST", true))});
    HfdlWave w = hfdlMakeWave(m1, pdu, paths, seed);
    w.start = 0.3 * kRate; w.carHz = kCh + kHfdlCarrierHz - kCentre + cfo; w.amp = 0.1f; w.phase = 0.3 + seed;
    PortableRng rng(seed);
    const double sigma = std::sqrt(0.01 / std::pow(10.0, snrDb / 10) * (kRate / kHfdlBaud) / 2);
    std::vector<cf32> buf(4096);
    for (int64_t pos = 0; pos < (int64_t)((0.3 + hfdlBurstSeconds(m1) + 0.8) * kRate); pos += 4096) {
        for (auto& v : buf) v = cf32((float)rng.gauss(sigma), (float)rng.gauss(sigma));
        hfdlRender(w, buf.data(), pos, 4096, kRate);
        rx.feed(buf.data(), 4096);
    }
    HfdlTelemetry t; rx.telemetry(t, ~0ull);
    int n = 0; for (const auto& m : t.messages) n += m.acars && m.acarsCrcOk;
    return n;
}

int main(int argc, char** argv) {
    if (argc > 1) {   // sweep: weak <m1> <fromDb> <toDb>
        const int m1 = atoi(argv[1]);
        for (double s = atof(argv[2]); s >= atof(argv[3]); s -= 1) {
            int ok = 0; for (uint32_t i = 0; i < 12; i++) ok += run(m1, s, 40 * ((int)i % 3 - 1), 10 + i, {}) == 1;
            printf("m1 %d  %5.1f dB: %d/12\n", m1, s, ok);
        }
        return 0;
    }
    // the same noise on every platform: the equaliser alone decodes 0 of 12 of these, the fallbacks 6 and 10
    for (auto c : {std::make_tuple(1, 0.0, 5), std::make_tuple(0, -3.0, 8), std::make_tuple(1, 4.0, 12), std::make_tuple(0, 2.0, 12)}) {
        int ok = 0;
        for (uint32_t i = 0; i < 12; i++) ok += run(std::get<0>(c), std::get<1>(c), 40 * ((int)i % 3 - 1), 10 + i, {}) == 1;
        printf("  m1 %d at %+.0f dB: %d of 12\n", std::get<0>(c), std::get<1>(c), ok);
        CHECK(ok >= std::get<2>(c), "m1 %d at %+.0f dB: %d of 12, need %d", std::get<0>(c), std::get<1>(c), ok, std::get<2>(c));
    }
    return testSummary("hfdl weak");
}
