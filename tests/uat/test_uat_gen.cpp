#include "dect2/uat_gen.h"
#include "dect2/uat_rx.h"
#include "dect2/modes.h"
#include "dect2/uat_frame.h"
#include <cstdio>
#include <cstring>
using namespace dect2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static bool parseHex(const char* s, uint8_t* out, int n) {
    for (int i = 0; i < n; i++) {
        auto nib = [](char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'A' && c <= 'F') return c - 'A' + 10; if (c >= 'a' && c <= 'f') return c - 'a' + 10; return -1; };
        const int hi = nib(s[i * 2]), lo = nib(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

int main() {
    SynthConfig cfg;
    cfg.mode = 85;
    cfg.snrDb = 28;
    CHECK(makeModeSynth(85, cfg, 2e6) != nullptr, "synth registry");
    CHECK(makeModeSynth(85, cfg, 1.5e6) == nullptr, "rate guard");
    const ModeTuning* mt = modeTuningById("uat");
    CHECK(mt && mt->stdMode == 85 && mt->defMhz == 978.0, "tuning");
    UatTx tx;
    const char* vec = "FF8196782DD44238C1453855F89980C7524F5970940ED83AD89CE7A9BEF8761BBCD9FCC817D82E2D1ACF90CA78DA3C49";
    CHECK(parseHex(vec, tx.frame, UAT_SHORT_FRAME_BYTES), "hex vector");
    tx.frameBytes = UAT_SHORT_FRAME_DATA_BYTES;
    const std::vector<cf32> burst = uatBurst(tx, 2e6);
    CHECK(burst.size() > 100, "burst length %zu", burst.size());
    auto synth = makeUatSynth(cfg, 2e6);
    std::vector<cf32> x(200000);
    synth->generate(x.data(), x.size());
    double pwr = 0;
    for (auto& v : x) pwr += std::norm(v);
    CHECK(pwr > 1e-6, "synth produces signal");
    if (fails) return 1;
    printf("uat gen OK\n");
    return 0;
}
