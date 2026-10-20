// The KAAL receiver wrapper on input it must survive without a signal: noise, silence, a constant, full-scale random, blocks of 1, 7, 96, 4801 samples,
// at 48 kHz and at radio rates. The synthetic source of the app once crashed the receiver in acquire() on noise. Nothing may crash or lock on noise.
#include "dect2/kaal/kaal_live.h"
#include "dect2/kaal/kaal_tel.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
using namespace dect2;
#include "../common/test_util.h"

int main() {
    std::mt19937_64 rng(12345);
    std::normal_distribution<float> g(0.f, 1.f);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    struct Src { const char* name; int kind; };
    const Src srcs[] = {{"noise", 0}, {"silence", 1}, {"constant", 2}, {"full-scale random", 3}, {"sparse spikes", 4}};
    const size_t blocks[] = {1, 7, 96, 4801, 65536};
    const double rates[] = {48000, 96000, 240000, 2.4e6};
    int cases = 0, locked = 0;
    for (double rate : rates)
        for (const Src& s : srcs)
            for (size_t bs : blocks) {
                KaalLiveReceiver rx;
                rx.setSilent(true);
                rx.configure(rate);
                const size_t total = (size_t)(rate * 9);   // 9 s: past the 3 s acquisition window and a few windows more
                size_t done = 0;
                std::vector<cf32> b(bs);
                KaalTelemetry t;
                uint64_t sq = 0;
                while (done < total) {
                    const size_t n = std::min(bs, total - done);
                    for (size_t i = 0; i < n; i++) {
                        switch (s.kind) {
                        case 0: b[i] = cf32(0.05f * g(rng), 0.05f * g(rng)); break;
                        case 1: b[i] = cf32(0.f, 0.f); break;
                        case 2: b[i] = cf32(0.3f, -0.2f); break;
                        case 3: b[i] = cf32(u(rng), u(rng)); break;
                        default: b[i] = (rng() % 997 == 0) ? cf32(1.f, 1.f) : cf32(0.f, 0.f); break;
                        }
                    }
                    rx.feed(b.data(), n);
                    rx.telemetry(t, sq);
                    done += n;
                    if (bs == 1 && done > (size_t)(rate * 0.2)) break;   // a block of one sample at a radio rate: the first moments are enough
                }
                cases++;
                if (rx.telemetry(t, 0) && t.state == 2) locked++;
            }
    printf("%d cases, locked on noise in %d\n", cases, locked);
    CHECK(locked == 0, "the receiver locked on input without a signal (%d cases)", locked);
    return testSummary("kaal_live_fuzz");
}
