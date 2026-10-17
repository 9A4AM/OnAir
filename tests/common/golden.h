// Golden-vector helper for the tests of the Rust-backed protocol layers (tests/<mode>/test_<mode>_proto_rs.cpp).
//
// The Rust crates replaced C++ implementations. The old tests compared both on seeded random and damaged inputs; the C++ is gone, so each test now feeds
// every result of the same seeded run into a Digest (FNV-1a, 64 bit) and compares the final value with kGolden, recorded once from the C++ implementation
// the Rust one replaced (and checked equal to the Rust one). A different digest means some result changed: run with ONAIR_GOLDEN_DUMP=1 to print the
// digest of every stage, and see git history of the test for how the value was recorded. Floating point values are rounded to 1e-6 before hashing so
// the digest does not depend on the last bit of libm.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>

namespace golden {

class Digest {
public:
    void byte(uint8_t b) { h_ ^= b; h_ *= 0x100000001b3ull; }
    void u64(uint64_t v) { for (int i = 0; i < 8; i++) byte((uint8_t)(v >> (8 * i))); }
    void bytes(const void* p, size_t n) { u64(n); for (size_t i = 0; i < n; i++) byte(((const uint8_t*)p)[i]); }
    void dbl(double v) {
        if (std::isnan(v)) { u64(0x7ff8); return; }
        if (std::isinf(v)) { u64(v > 0 ? 0x7ff0 : 0xfff0); return; }
        const double r = std::round(v * 1e6);
        u64((uint64_t)(int64_t)(std::fabs(r) < 9e18 ? r : 0));
    }
    void add(const std::string& s) { bytes(s.data(), s.size()); }
    void add(const char* s) { size_t n = 0; while (s && s[n]) n++; bytes(s, n); }
    template <class T> void add(const std::vector<T>& v) { u64(v.size()); for (const auto& e : v) add(e); }
    template <class T, class = typename std::enable_if<std::is_arithmetic<T>::value || std::is_enum<T>::value>::type>
    void add(T v) {
        if constexpr (std::is_floating_point<T>::value) dbl((double)v);
        else u64((uint64_t)(int64_t)v);
    }
    template <class T, size_t N> void add(const T (&a)[N]) { u64(N); for (size_t i = 0; i < N; i++) add(a[i]); }
    template <class A, class B, class... R> void add(const A& a, const B& b, const R&... r) { add(a); add(b); (add(r), ...); }
    uint64_t value() const { return h_; }
private:
    uint64_t h_ = 0xcbf29ce484222325ull;
};

// Compares the digest of a finished run with the recorded one. A run with a non-default iteration count (ONAIR_DIFF_ITERS) or ONAIR_GOLDEN_DUMP=1
// only prints. Returns the test's exit code contribution (0 = fine).
inline int check(const char* name, const Digest& d, uint64_t expected, bool defaultRun) {
    printf("%s: digest %016llx", name, (unsigned long long)d.value());
    if (!defaultRun || getenv("ONAIR_GOLDEN_DUMP")) { printf(" (not compared)\n"); return 0; }
    if (d.value() != expected) { printf(" != recorded %016llx\n", (unsigned long long)expected); return 1; }
    printf(" ok\n");
    return 0;
}

}  // namespace golden
