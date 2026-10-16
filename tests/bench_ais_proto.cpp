// Time per frame of the AIS frame layer: C++ (dect2::ais_ref, a generated copy) against the Rust crate (dect2::ais). Run from a release build:
// bench_ais_proto prints ns per call for the pieces and for the whole receive path (find frames, decode, armour into a sentence).
#include "dect2/ais_proto.h"
#include "ais_proto_ref.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
namespace A = dect2::ais;
namespace R = dect2::ais_ref;

template <class F> static double bestNs(F&& f, long calls) {
    double best = 1e30;
    for (int rep = 0; rep < 7; rep++) {
        const auto t0 = std::chrono::steady_clock::now();
        for (long i = 0; i < calls; i++) f(i);
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / (double)calls);
    }
    return best;
}

static volatile unsigned long long sink = 0;

int main() {
    std::mt19937 g(5);
    std::vector<A::Bits> streams, payloads;
    for (int i = 0; i < 64; i++) {
        A::Bits p;
        A::putU(p, 0, 6, (i % 5 == 4) ? 5 : (i % 3 ? 1 : 18));
        A::putU(p, 8, 30, 227000000u + g() % 1000000);
        const size_t n = p.size() ? ((i % 5 == 4) ? 424 : 168) : 168;
        for (size_t k = 38; k < n; k++) A::putU(p, k, 1, g() & 1);
        p.resize(n);
        payloads.push_back(p);
        A::Bits s(40, 0);
        const A::Bits d = A::nrziDecode(A::burstLineBits(p));
        s.insert(s.end(), d.begin(), d.end());
        for (int k = 0; k < 30; k++) s.push_back(g() & 1);
        streams.push_back(s);
    }
    const long N = 20000;
    struct Row { const char* what; double cpp, rust; } rows[6];
    rows[0] = {"hdlcFrames (find + destuff + FCS)", bestNs([&](long i) { sink += R::hdlcFrames(streams[i & 63]).good.size(); }, N),
               bestNs([&](long i) { sink += A::hdlcFrames(streams[i & 63]).good.size(); }, N)};
    rows[1] = {"decodeMessage", bestNs([&](long i) { R::AisMsg m; sink += R::decodeMessage(payloads[i & 63], m) + m.mmsi; }, N * 5),
               bestNs([&](long i) { A::AisMsg m; sink += A::decodeMessage(payloads[i & 63], m) + m.mmsi; }, N * 5)};
    rows[2] = {"armour + unarmour", bestNs([&](long i) { int f; sink += R::unarmour(R::armour(payloads[i & 63], f), f).size(); }, N * 5),
               bestNs([&](long i) { int f; sink += A::unarmour(A::armour(payloads[i & 63], f), f).size(); }, N * 5)};
    rows[3] = {"parseNmea", bestNs([&](long i) { std::string c; int f, a, b; sink += R::parseNmea("!AIVDM,1,1,,A,15RTgt0PAso;90TKcjM8h6g208CQ,0*4A", c, f, a, b); (void)i; }, N * 5),
               bestNs([&](long i) { std::string c; int f, a, b; sink += A::parseNmea("!AIVDM,1,1,,A,15RTgt0PAso;90TKcjM8h6g208CQ,0*4A", c, f, a, b); (void)i; }, N * 5)};
    rows[4] = {"whole receive path per frame", bestNs([&](long i) {
                   for (const auto& fr : R::hdlcFrames(streams[i & 63]).good) { R::AisMsg m; R::decodeMessage(fr, m); int f; sink += R::armour(fr, f).size() + m.mmsi; } }, N),
               bestNs([&](long i) {
                   for (const auto& fr : A::hdlcFrames(streams[i & 63]).good) { A::AisMsg m; A::decodeMessage(fr, m); int f; sink += A::armour(fr, f).size() + m.mmsi; } }, N)};
    printf("A = %s\n%-36s %10s %10s %8s\n", A::protoImplementation(), "", "C++ ns", "Rust ns", "Rust/C++");
    int worst = 0;
    for (int i = 0; i < 5; i++) {
        printf("%-36s %10.1f %10.1f %7.2fx\n", rows[i].what, rows[i].cpp, rows[i].rust, rows[i].rust / rows[i].cpp);
        if (i == 4 && rows[i].rust > rows[i].cpp * 1.10) worst = 1;
    }
    return worst;
}
