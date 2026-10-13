// DVB-S and DVB-S2 compute choice: the transport stream that comes out with the error correction forced onto the GPU must be the one the CPU gives
// (and Auto must too). Skipped when the machine has no usable GPU backend.
#include "dect2/dvbs_testkit.h"
#include "dect2/gpu_ldpc.h"
#include <cstdio>
#include <cstdlib>

using namespace dect2;
using namespace dect2::dvbs;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

struct Out { uint64_t packets = 0, bad = 0, hash = 1469598103934665603ull; bool onGpu = false; };

static Out run(int standard, int mod, int rate, double esn0, int computeMode) {
    RunConfig rc = makeRun(standard, mod, rate, 2e6, 4e6, esn0, 3.0);
    DvbsSignalConfig c = rc.sig;
    uint64_t pn = 0;
    c.ts = [&pn](uint8_t* p) { testPacket(pn++, p); };
    DvbsSignal sig(c);
    DvbsReceiver rx;
    rx.setBlocking(true);
    rx.setComputeMode(computeMode);
    rx.configure(c.sampleRate);
    Out o;
    rx.setPacketCallback([&](const uint8_t* p, size_t n, double) {
        for (size_t i = 0; i < n; i++, p += 188) {
            o.packets++;
            if (p[1] & 0x80) o.bad++;
            for (int k = 0; k < 188; k++) { o.hash ^= p[k]; o.hash *= 1099511628211ull; }
        }
    });
    const size_t total = (size_t)(rc.secs * c.sampleRate);
    std::vector<cf32> x(65536);
    for (size_t i = 0; i < total; i += x.size()) {
        const size_t n = std::min(x.size(), total - i);
        sig.generate(x.data(), n);
        for (size_t k = 0; k < n; k++) x[k] = cf32(std::round(std::min(127.f, std::max(-128.f, x[k].real() * 128.f))) / 128.f, std::round(std::min(127.f, std::max(-128.f, x[k].imag() * 128.f))) / 128.f);
        rx.feed(x.data(), n);
    }
    rx.flush();
    DvbsTelemetry t;
    rx.telemetry(t, 0);
    o.onGpu = t.onGpu;
    return o;
}

int main() {
    if (!GpuLdpc::instance().available()) { printf("no usable GPU backend: skipped\n"); return 0; }
    printf("GPU: %s\n", GpuLdpc::instance().deviceName());
    struct Case { int std, mod, rate; const char* name; };
    const Case cases[] = {{2, kQpsk, 5, "DVB-S2 QPSK 2/3"}, {2, kQpsk, 3, "DVB-S2 QPSK 2/5"}, {2, k8psk, 5, "DVB-S2 8PSK 2/3"}, {2, k8psk, 10, "DVB-S2 8PSK 9/10"}, {2, k16apsk, 6, "DVB-S2 16APSK 3/4"}, {1, kQpsk, 2, "DVB-S QPSK 3/4"}};
    for (const Case& cs : cases) {
        const double esn0 = cs.std == 1 ? 8.0 : s2QefEsN0(cs.mod, cs.rate, false) + 1.0;
        const Out cpu = run(cs.std, cs.mod, cs.rate, esn0, 0), gpu = run(cs.std, cs.mod, cs.rate, esn0, 1), autoM = run(cs.std, cs.mod, cs.rate, esn0, 2);
        printf("%s at %.1f dB: CPU %llu packets (%llu damaged), GPU %llu (%llu damaged, on GPU: %s), Auto %llu (%llu damaged): %s\n", cs.name, esn0,
               (unsigned long long)cpu.packets, (unsigned long long)cpu.bad, (unsigned long long)gpu.packets, (unsigned long long)gpu.bad, gpu.onGpu ? "yes" : "no", (unsigned long long)autoM.packets, (unsigned long long)autoM.bad,
               cpu.hash == gpu.hash && cpu.hash == autoM.hash ? "identical" : "DIFFERENT");
        CHECK(cpu.packets > 100, "%s: the CPU run delivered only %llu packets", cs.name, (unsigned long long)cpu.packets);
        CHECK(cpu.packets == gpu.packets && cpu.bad == gpu.bad && cpu.hash == gpu.hash, "%s: GPU stream differs from the CPU stream", cs.name);
        CHECK(cpu.packets == autoM.packets && cpu.bad == autoM.bad && cpu.hash == autoM.hash, "%s: Auto stream differs from the CPU stream", cs.name);
        if (cs.std == 2) CHECK(gpu.onGpu, "%s: nothing was decoded on the GPU", cs.name);
    }
    printf(fails ? "dvbs gpu: %d FAILED\n" : "dvbs gpu: all passed\n", fails);
    return fails ? 1 : 0;
}
