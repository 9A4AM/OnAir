// MT63 coding layer (see mt63_proto.h). The half pulse below (index 0..256 in steps of 4, mirrored about 256) is the symbol shape of the
// MT63 modem by Pawel Jalocha SP9VRC as distributed in fldigi (GPL-3.0-or-later); the shape table is data taken from there.
#include "dect2/mt63/mt63_proto.h"
#include <algorithm>
#include <cmath>

namespace dect2::mt63 {

namespace {
const double kHalf[65] = {
    -0.00000, 0.00011, 0.00042, 0.00093, 0.00161, 0.00239, 0.00316, 0.00381,
    0.00414, 0.00398, 0.00315, 0.00153, -0.00094, -0.00427, -0.00836, -0.01306,
    -0.01817, -0.02347, -0.02872, -0.03371, -0.03822, -0.04202, -0.04485, -0.04642,
    -0.04638, -0.04430, -0.03974, -0.03227, -0.02152, -0.00725, 0.01062, 0.03193,
    0.05629, 0.08310, 0.11159, 0.14087, 0.17004, 0.19826, 0.22487, 0.24941,
    0.27171, 0.29193, 0.31049, 0.32812, 0.34574, 0.36437, 0.38508, 0.40887,
    0.43653, 0.46858, 0.50521, 0.54622, 0.59103, 0.63870, 0.68805, 0.73768,
    0.78616, 0.83206, 0.87411, 0.91123, 0.94254, 0.96742, 0.98544, 0.99634,
    0.99999,
};
double tab(int k) {   // k = 0..128 in steps of 4 samples, mirrored
    if (k < 0) k = 0;
    if (k > 128) k = 128;
    return kHalf[k <= 64 ? k : 128 - k];
}
// the interleaver steps of one symbol row: short 4,5,6,7 repeated, long 1,2,...,63,0
int stepOf(bool longIntl, int i) { return longIntl ? (i + 1) & 63 : 4 + (i & 3); }
}

double pulse(double n) {
    if (n < 0 || n > kFft) return 0;
    const double x = n / 4.0;
    int i = (int)std::floor(x);
    if (i > 127) i = 127;
    const double f = x - i;
    const double p0 = tab(i - 1), p1 = tab(i), p2 = tab(i + 1), p3 = tab(i + 2);
    return 0.5 * (2 * p1 + (-p0 + p2) * f + (2 * p0 - 5 * p1 + 4 * p2 - p3) * f * f + (-p0 + 3 * p1 - 3 * p2 + p3) * f * f * f);
}

int intlDelay(bool longIntl, int carrier) {
    const int L = intlLen(longIntl);
    int p = 0;
    for (int j = 0; j < carrier; j++) p = (p + stepOf(longIntl, j)) % L;
    return p;
}

void walshEncode(int code, int8_t w[kCarriers]) {
    code &= 127;
    const int row = code & 63;
    const int sgn = code >= 64 ? -1 : 1;
    for (int i = 0; i < kCarriers; i++) w[i] = (int8_t)(sgn * ((__builtin_popcount((unsigned)(i & row)) & 1) ? -1 : 1));
}

int walshDecode(const float s[kCarriers], float& conf) {
    float a[kCarriers];
    std::copy(s, s + kCarriers, a);
    for (int h = 1; h < kCarriers; h <<= 1)
        for (int i = 0; i < kCarriers; i += 2 * h)
            for (int j = i; j < i + h; j++) { const float x = a[j], y = a[j + h]; a[j] = x + y; a[j + h] = x - y; }
    int best = 0;
    for (int i = 1; i < kCarriers; i++) if (std::fabs(a[i]) > std::fabs(a[best])) best = i;
    double rest = 0;
    for (int i = 0; i < kCarriers; i++) if (i != best) rest += (double)a[i] * a[i];
    conf = (float)(std::fabs(a[best]) / std::sqrt(rest / (kCarriers - 1) + 1e-6));
    return a[best] < 0 ? best + 64 : best;
}

Encoder::Encoder(bool longIntl, uint32_t seed) : longIntl_(longIntl), rng_(seed ? seed : 1) {
    pipe_.resize((size_t)intlLen(longIntl) * kCarriers);
    for (auto& b : pipe_) { rng_ = rng_ * 1664525u + 1013904223u; b = (uint8_t)(rng_ >> 31); }   // random fill, as the modem starts
}

void Encoder::process(int code, uint8_t bits[kCarriers]) {
    const int L = intlLen(longIntl_);
    int8_t w[kCarriers];
    walshEncode(code, w);
    for (int i = 0; i < kCarriers; i++) pipe_[(size_t)ptr_ * kCarriers + (size_t)i] = w[i] < 0 ? 1 : 0;
    for (int i = 0; i < kCarriers; i++) {
        const int row = ((ptr_ - intlDelay(longIntl_, i)) % L + L) % L;
        const uint8_t wb = pipe_[(size_t)row * kCarriers + (size_t)i];   // the Walsh bit (1 = negative) that goes out now
        bits[i] = wb;                                                    // negative = bit 1 = no phase change
    }
    ptr_ = (ptr_ + 1) % L;
}

Deinterleaver::Deinterleaver(bool longIntl) : len_(intlLen(longIntl)), longIntl_(longIntl) {
    for (int i = 0; i < kCarriers; i++) delay_[i] = intlDelay(longIntl, i);
    reset();
}

void Deinterleaver::reset() { ring_.assign((size_t)len_ * kCarriers, 0.f); pos_ = 0; filled_ = 0; }

bool Deinterleaver::push(const float u[kCarriers], int& code, float& conf) {
    std::copy(u, u + kCarriers, &ring_[(size_t)pos_ * kCarriers]);
    pos_ = (pos_ + 1) % len_;
    if (filled_ < len_) { filled_++; if (filled_ < len_) return false; }
    // the newest symbol is at pos_-1; the oldest (the character's first symbol, delay 0) is at pos_
    float s[kCarriers];
    for (int i = 0; i < kCarriers; i++) s[i] = ring_[(size_t)((pos_ + delay_[i]) % len_) * kCarriers + (size_t)i];
    code = walshDecode(s, conf);
    return true;
}

} // namespace dect2::mt63
