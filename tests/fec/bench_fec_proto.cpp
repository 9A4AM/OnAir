// Speed of the Rust error correction layer (dect2::fecp, the Rust crate) against the C++ one (dect2::fecp_ref, a frozen copy): ns per call, same inputs.
// Run it from a release build; a Rust version that is slower than the C++ one must not become the default (rust/onair_fec/README.md has the numbers).
#include "dect2/fec/fec_proto.h"
#include "fec_proto_ref.h"
#include "dect2/t2/t2fec_tables.h"
#include <algorithm>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <random>
namespace A = dect2::fecp;
namespace R = dect2::fecp_ref;
// CPU time of this thread (the machine is often shared: wall time would measure the other programs too)
struct Clock { using time_point = double; static double now() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; } };
static double secs(double a) { return Clock::now() - a; }
static volatile long sink;

template <class F> static double ns(int reps, F f) {
    f();
    double best = 1e30;   // the machine is shared: the best of 7 rounds
    for (int round = 0; round < 7; round++) {
        const auto t0 = Clock::now();
        for (int i = 0; i < reps; i++) f();
        best = std::min(best, secs(t0) * 1e9 / reps);
    }
    return best;
}

int main() {
    std::mt19937 g(5);
    std::vector<uint8_t> d(188), c(204), w(204);
    for (auto& x : d) x = (uint8_t)g();
    A::rs204Encode(d.data(), c.data());
    auto noisy = [&](int e, int n) { w = c; w.resize(n); for (int i = 0; i < e; i++) w[g() % n] ^= (uint8_t)(1 + g() % 255); };
    printf("%-28s %12s %12s\n", "ns per call", "Rust", "C++");
    printf("%-28s %12.0f %12.0f\n", "rs204 encode", ns(200000, [&] { A::rs204Encode(d.data(), c.data()); sink += c[0]; }), ns(200000, [&] { R::rs204Encode(d.data(), c.data()); sink += c[0]; }));
    A::rs204Encode(d.data(), c.data());
    for (int e : {0, 3, 8}) {
        noisy(e, 204); std::vector<uint8_t> base = w, t;
        char nm[40]; snprintf(nm, sizeof nm, "rs204 decode %d errors", e);
        printf("%-28s %12.0f %12.0f\n", nm, ns(100000, [&] { t = base; sink += A::rs204Decode(t.data()); }), ns(100000, [&] { t = base; sink += R::rs204Decode(t.data()); }));
    }
    std::vector<uint8_t> d2(187), c2(207);
    for (auto& x : d2) x = (uint8_t)g();
    A::rs207Encode(d2.data(), c2.data());
    printf("%-28s %12.0f %12.0f\n", "rs207 encode", ns(200000, [&] { A::rs207Encode(d2.data(), c2.data()); sink += c2[0]; }), ns(200000, [&] { R::rs207Encode(d2.data(), c2.data()); sink += c2[0]; }));
    for (int e : {0, 5, 10}) {
        std::vector<uint8_t> base = c2, t; for (int i = 0; i < e; i++) base[g() % 207] ^= (uint8_t)(1 + g() % 255);
        char nm[40]; snprintf(nm, sizeof nm, "rs207 decode %d errors", e);
        printf("%-28s %12.0f %12.0f\n", nm, ns(100000, [&] { t = base; sink += A::rs207Decode(t.data()); }), ns(100000, [&] { t = base; sink += R::rs207Decode(t.data()); }));
    }
    std::vector<uint8_t> d3(110), c3(120);
    for (auto& x : d3) x = (uint8_t)g();
    A::rs120Encode(d3.data(), c3.data());
    printf("%-28s %12.0f %12.0f\n", "rs120 encode", ns(200000, [&] { A::rs120Encode(d3.data(), c3.data()); sink += c3[0]; }), ns(200000, [&] { R::rs120Encode(d3.data(), c3.data()); sink += c3[0]; }));
    for (int e : {0, 3, 5}) {
        std::vector<uint8_t> base = c3, t; for (int i = 0; i < e; i++) base[g() % 120] ^= (uint8_t)(1 + g() % 255);
        char nm[40]; snprintf(nm, sizeof nm, "rs120 decode %d errors", e);
        printf("%-28s %12.0f %12.0f\n", nm, ns(100000, [&] { t = base; sink += A::rs120Decode(t.data()); }), ns(100000, [&] { t = base; sink += R::rs120Decode(t.data()); }));
    }
    for (const char* nm : {"2_3N", "5_6N", "1_2S"}) {   // the table builder of a normal / short frame code
        const dect2::LdpcTable* tb = nullptr;
        for (int i = 0; i < dect2::kNumLdpcTables; i++) if (!strcmp(dect2::kLdpcTables[i].name, nm)) tb = &dect2::kLdpcTables[i];
        if (!tb) continue;
        std::vector<std::vector<int>> rows;
        for (int r = 0; r < tb->rows; r++) { const int* row = tb->data + (size_t)r * tb->cols; rows.emplace_back(row + 1, row + 1 + row[0]); }
        const int n = nm[3] == 'S' ? 16200 : 64800, k = tb->rows * 360;
        A::LdpcTables ta; R::LdpcTables tr;
        char lab[48]; snprintf(lab, sizeof lab, "ldpc tables %s", nm);
        printf("%-28s %12.0f %12.0f\n", lab, ns(20, [&] { A::ldpcBuild(k, n, rows, ta); sink += ta.m; }), ns(20, [&] { R::ldpcBuild(k, n, rows, tr); sink += tr.m; }));
    }
    for (size_t steps : {size_t(1000), size_t(16768)}) {
        std::vector<int8_t> soft(2 * steps); for (auto& x : soft) x = (int8_t)(int)(g() % 255) ;
        std::vector<uint8_t> b1, b2; long m1 = 0, m2 = 0;
        char nm[48]; snprintf(nm, sizeof nm, "viterbi %zu steps", steps);
        printf("%-28s %12.0f %12.0f\n", nm, ns(300, [&] { A::viterbiRange(soft.data(), steps, b1, &m1); sink += m1; }), ns(300, [&] { R::viterbiRange(soft.data(), steps, b2, &m2); sink += m2; }));
    }
    for (int cfg = 0; cfg < 2; cfg++) {
        const bool sh = cfg == 1; const int t = 12, k = sh ? 7032 : 38688;
        A::Bch ba(sh, t); R::Bch br(sh, t);
        std::vector<uint8_t> msg(k); for (auto& x : msg) x = (uint8_t)(g() & 1);
        std::vector<uint8_t> e1, e2;
        char nm[48];
        snprintf(nm, sizeof nm, "bch %s t12 encode", sh ? "short" : "normal");
        printf("%-28s %12.0f %12.0f\n", nm, ns(300, [&] { e1 = msg; ba.encode(e1, k); sink += e1.back(); }), ns(300, [&] { e2 = msg; br.encode(e2, k); sink += e2.back(); }));
        e1 = msg; ba.encode(e1, k);
        for (int e : {0, 6, 12}) {
            std::vector<uint8_t> base = e1, tt; for (int i = 0; i < e; i++) base[g() % base.size()] ^= 1;
            snprintf(nm, sizeof nm, "bch %s decode %d errors", sh ? "short" : "normal", e);
            printf("%-28s %12.0f %12.0f\n", nm, ns(300, [&] { tt = base; sink += ba.decode(tt); }), ns(300, [&] { tt = base; sink += br.decode(tt); }));
        }
    }
    return 0;
}
