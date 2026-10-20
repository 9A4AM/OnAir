// EDACS frame encode / vote and the test-signal modulator (see edacs_proto.h for the sources and what is not established).
#include "dect2/edacs/edacs_proto.h"
#include <cmath>

namespace dect2::edacs {

static void put(std::vector<uint8_t>& b, uint64_t v, int n) { for (int i = n - 1; i >= 0; i--) b.push_back((uint8_t)((v >> i) & 1)); }

static void putMessage(std::vector<uint8_t>& b, uint32_t data28, uint32_t check12) {
    const uint64_t m = ((uint64_t)(data28 & 0xFFFFFFF) << 12) | (check12 & 0xFFF);
    put(b, m, 40);
    put(b, ~m & 0xFFFFFFFFFFULL, 40);   // the middle copy is inverted
    put(b, m, 40);
}

std::vector<uint8_t> encodeFrame(uint32_t msgA, uint32_t msgB, uint32_t checkA, uint32_t checkB) {
    std::vector<uint8_t> b;
    for (int i = 0; i < 16; i++) b.push_back((uint8_t)((i & 1) ^ 1));   // dotting 1010...
    static const uint8_t kPlaceholder[16] = {1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 1};   // stands for the 11-bit Barker word + 5 dotting bits (the real word is not public)
    for (uint8_t v : kPlaceholder) b.push_back(v);
    for (int i = 0; i < 16; i++) b.push_back((uint8_t)((i & 1) ^ 1));
    putMessage(b, msgA, checkA);
    putMessage(b, msgB, checkB);
    return b;
}

uint64_t vote(const uint8_t* b, int& dis) {
    uint64_t v = 0;
    dis = 0;
    for (int i = 0; i < kMsgBits; i++) {
        const int x = b[i] & 1, y = (b[40 + i] & 1) ^ 1, z = b[80 + i] & 1;
        const int s = x + y + z;
        if (s == 1 || s == 2) dis++;
        v = (v << 1) | (uint64_t)(s >= 2);
    }
    return v;
}

std::vector<cf32> modulate(const std::vector<uint8_t>& bits, double fs, double offsetHz, double devHz, float amp, bool invert) {
    const double pi = 3.14159265358979323846;
    const size_t n = (size_t)std::llround((double)bits.size() / kBaud * fs);
    std::vector<cf32> out(n);
    double phase = 0, lev = 0;
    const double a = 1.0 - std::exp(-2 * pi * 0.75 * kBaud / fs);   // a little edge smoothing
    for (size_t i = 0; i < n; i++) {
        size_t k = (size_t)((double)i * kBaud / fs);
        if (k >= bits.size()) k = bits.size() - 1;
        lev += a * ((bits[k] ? 1.0 : -1.0) * (invert ? -1.0 : 1.0) - lev);
        phase += 2 * pi * (lev * devHz + offsetHz) / fs;
        if (phase > pi) phase -= 2 * pi; else if (phase < -pi) phase += 2 * pi;
        out[i] = cf32(amp * (float)std::cos(phase), amp * (float)std::sin(phase));
    }
    return out;
}

} // namespace dect2::edacs
