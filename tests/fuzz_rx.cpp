// Robustness: feed the receiver noise, NaN/Inf samples, bursts of corruption, dropped chunks, resets and re-configurations
// and make sure it neither crashes nor hangs (build with -fsanitize=address,undefined to make this meaningful).
#include "dect2/t2gen.h"
#include "dect2/t2rx.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
using namespace dect2;

static int fails = 0;
static std::mt19937 rng(12345);

static void feedChunks(T2Receiver& rx, std::vector<cf32>& v, size_t chunk = 1 << 15) {
    for (size_t i = 0; i < v.size(); i += chunk) rx.feed(v.data() + i, std::min(chunk, v.size() - i));
}

static std::vector<cf32> genFrames(TxParams tp, int frames) {
    T2Generator g(tp);
    std::vector<cf32> all, f;
    for (int i = 0; i < frames; i++) { g.nextFrame(f); all.insert(all.end(), f.begin(), f.end()); }
    return all;
}

int main() {
    const double fs = nativeRateHz(8);
    std::normal_distribution<float> nd(0, 1);
    auto t0 = std::chrono::steady_clock::now();
    auto secs = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };

    // 1. pure noise, loud noise, silence
    {
        T2Receiver rx; rx.configure(fs, 8);
        std::vector<cf32> v(3'000'000);
        for (auto& x : v) x = cf32(nd(rng), nd(rng)) * 0.2f;
        feedChunks(rx, v);
        for (auto& x : v) x = cf32(nd(rng), nd(rng)) * 50.f;
        feedChunks(rx, v);
        std::fill(v.begin(), v.end(), cf32(0, 0));
        feedChunks(rx, v);
        printf("noise/silence ok (%.1f s)\n", secs());
    }
    // 2. NaN / Inf samples inside a valid signal
    for (int fft : {1, 4, 5}) {
        TxParams tp; tp.s2field1 = fft; tp.giIdx = 3; tp.pp = fft == 5 ? 7 : 3; tp.ext = fft == 5;
        T2Receiver rx; rx.configure(fs, 8);
        auto v = genFrames(tp, 6);
        for (int k = 0; k < 200; k++) {
            size_t i = rng() % v.size();
            const float bad[3] = {NAN, INFINITY, -INFINITY};
            v[i] = cf32(bad[rng() % 3], bad[rng() % 3]);
        }
        feedChunks(rx, v);
        printf("NaN/Inf fft %d ok (%.1f s)\n", fft, secs());
    }
    // 3. valid signal with bursts of corruption, dropped chunks, repeated chunks and resets
    for (int round = 0; round < 3; round++) {
        TxParams tp; tp.s2field1 = round == 0 ? 1 : round == 1 ? 2 : 0; tp.giIdx = round + 1; tp.pp = round; tp.dataSymbols = 0;
        T2Receiver rx; rx.configure(fs, 8);
        auto v = genFrames(tp, 12);
        std::vector<cf32> out;
        size_t pos = 0;
        while (pos < v.size()) {
            size_t n = std::min<size_t>(v.size() - pos, 20000 + rng() % 200000);
            switch (rng() % 8) {
            case 0: for (size_t i = 0; i < n; i++) out.push_back(cf32(nd(rng), nd(rng))); break;           // burst of noise
            case 1: break;                                                                                  // dropped chunk
            case 2: out.insert(out.end(), v.begin() + pos, v.begin() + pos + n); out.insert(out.end(), v.begin() + pos, v.begin() + pos + n); break; // repeated
            case 3: for (size_t i = 0; i < n; i++) out.push_back(v[pos + i] * 40.f); break;                // clipped/overload burst
            default: out.insert(out.end(), v.begin() + pos, v.begin() + pos + n); break;
            }
            pos += n;
        }
        for (size_t i = 0; i < out.size(); i += 1 << 16) {
            rx.feed(out.data() + i, std::min<size_t>(1 << 16, out.size() - i));
            if (rng() % 40 == 0) rx.reset();
            if (rng() % 60 == 0) { rx.configure(fs, 8); }
        }
        RxTelemetry t; rx.telemetry(t, 0);
        printf("corruption round %d ok (%.1f s) state %d\n", round, secs(), t.state);
    }
    // 4. odd chunk sizes (1 sample, 0 samples, huge)
    {
        T2Receiver rx; rx.configure(fs, 8);
        TxParams tp; tp.s2field1 = 1; tp.giIdx = 3;
        auto v = genFrames(tp, 4);
        for (size_t i = 0; i < 5000; i++) rx.feed(v.data() + i, 1);
        rx.feed(v.data(), 0);
        rx.feed(v.data() + 5000, v.size() - 5000);
        printf("chunk sizes ok (%.1f s)\n", secs());
    }
    // 5. other sample rates and bandwidths
    for (double bw : {8.0, 7.0, 6.0, 5.0, 1.7}) {
        T2Receiver rx; rx.configure(nativeRateHz(bw), bw);
        TxParams tp; tp.s2field1 = 1; tp.giIdx = 3;
        auto v = genFrames(tp, 3);
        feedChunks(rx, v);
        rx.configure(10e6, bw);
        std::vector<cf32> n(500000);
        for (auto& x : n) x = cf32(nd(rng), nd(rng)) * 0.1f;
        feedChunks(rx, n);
    }
    printf("bandwidths ok (%.1f s)\nfuzz passed\n", secs());
    return fails ? 1 : 0;
}
