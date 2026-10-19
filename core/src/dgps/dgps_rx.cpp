// DGPS beacons: MSK demodulation at 200 bit/s and the RTCM SC-104 word, header and parity layer (ITU-R M.823).
#include "dect2/dgps/dgps_rx.h"
#include <algorithm>
#include <cmath>

namespace dect2::dgps {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr uint8_t kPreamble[8] = {0, 1, 1, 0, 0, 1, 1, 0};   // 0x66, MSB first

// Parity equations of the 30-bit word: each parity bit is the XOR of the listed data bits (1-based, D1 first).
constexpr int kParityTaps[6][15] = {
    {1, 2, 3, 5, 6, 10, 11, 12, 13, 14, 17, 18, 20, 23, 0},
    {2, 3, 4, 6, 7, 11, 12, 13, 14, 15, 18, 19, 21, 24, 0},
    {1, 3, 4, 5, 7, 8, 12, 13, 14, 15, 16, 19, 20, 22, 0},
    {2, 4, 5, 6, 8, 9, 13, 14, 15, 16, 17, 20, 21, 23, 0},
    {1, 3, 5, 6, 7, 9, 10, 14, 15, 16, 17, 18, 21, 22, 24},
    {3, 5, 6, 8, 9, 10, 11, 12, 14, 18, 19, 21, 22, 23, 24},
};

void parityOf(const uint8_t* d, uint8_t* p) {
    for (int k = 0; k < 6; k++) {
        uint8_t x = 0;
        for (int t = 0; t < 15 && kParityTaps[k][t]; t++) x ^= d[kParityTaps[k][t] - 1];
        p[k] = x;
    }
}

// Decode one word from 30 channel bits. pol inverts the channel, prev is the last transmitted bit of the previous
// word (it inverts the 24 data bits when set). The preamble is checked only on the first word of a message.
bool decodeWord(const uint8_t* raw, int pol, int prev, bool preamble, uint8_t* d, int& lastBit) {
    uint8_t t[kWordBits];
    for (int i = 0; i < kWordBits; i++) t[i] = (uint8_t)(raw[i] ^ pol);
    for (int i = 0; i < 24; i++) d[i] = (uint8_t)(t[i] ^ prev);
    if (preamble)
        for (int i = 0; i < 8; i++)
            if (d[i] != kPreamble[i]) return false;
    uint8_t p[6];
    parityOf(d, p);
    for (int k = 0; k < 6; k++)
        if (p[k] != t[24 + k]) return false;
    lastBit = t[29];
    return true;
}

// Value of len bits from d (MSB first) starting at index at.
int field(const uint8_t* d, int at, int len) {
    int v = 0;
    for (int i = 0; i < len; i++) v = (v << 1) | d[at + i];
    return v;
}

// Look for a message header (two words) starting at b. Tries both channel polarities and both values of the
// bit before the message, since neither is known when the search starts.
bool findHeader(const uint8_t* b, Header& h) {
    for (int pol = 0; pol < 2; pol++) {
        for (int prev = 0; prev < 2; prev++) {
            uint8_t d1[24], d2[24];
            int last1 = 0, last2 = 0;
            if (!decodeWord(b, pol, prev, true, d1, last1)) continue;
            if (!decodeWord(b + kWordBits, pol, last1, false, d2, last2)) continue;
            h.type = field(d1, 8, 6);
            h.stationId = field(d1, 14, 10);
            h.zcount = field(d2, 0, 13);
            h.seq = field(d2, 13, 3);
            h.length = field(d2, 16, 5);
            h.health = field(d2, 21, 3);
            return true;
        }
    }
    return false;
}

// Append one 24-bit data word and its parity to the channel bits.
void putWord(uint32_t data, int pol, int& prev, std::vector<uint8_t>& out) {
    uint8_t d[24], p[6];
    for (int i = 0; i < 24; i++) d[i] = (uint8_t)((data >> (23 - i)) & 1);
    parityOf(d, p);
    for (int i = 0; i < 24; i++) out.push_back((uint8_t)((d[i] ^ prev) ^ pol));
    for (int k = 0; k < 6; k++) out.push_back((uint8_t)(p[k] ^ pol));
    prev = out.back() ^ pol;
}

}  // namespace

void Receiver::reset() {
    st_ = Stats{};
    pending_.clear();
}

void Receiver::process(const std::complex<float>* iq, size_t n, double fs) {
    if (!iq || n < 2 || fs <= 0) return;

    double power = 0;
    for (size_t k = 0; k < n; k++) power += (double)iq[k].real() * iq[k].real() + (double)iq[k].imag() * iq[k].imag();
    st_.levelDb = 10.0 * std::log10(std::max(power / (double)n, 1e-20));

    // Instantaneous frequency from the phase step between samples.
    const size_t m = n - 1;
    std::vector<double> f(m);
    double mean = 0;
    for (size_t k = 0; k < m; k++) {
        double re = (double)iq[k + 1].real() * iq[k].real() + (double)iq[k + 1].imag() * iq[k].imag();
        double im = (double)iq[k + 1].imag() * iq[k].real() - (double)iq[k + 1].real() * iq[k].imag();
        f[k] = std::atan2(im, re) * fs / (2.0 * kPi);
        mean += f[k];
    }
    mean /= (double)m;
    st_.carrierOffsetHz = mean;

    // Bit timing: integrate the offset-corrected frequency over each bit for every start phase and keep the phase
    // with the largest total magnitude (the MSK tones move the frequency to one side or the other every bit).
    const double sps = fs / kBitRate;
    const size_t nb = (size_t)std::floor((double)m / sps);
    if (nb < 4) return;
    std::vector<double> cum(m + 1, 0.0);
    for (size_t k = 0; k < m; k++) cum[k + 1] = cum[k] + (f[k] - mean);
    auto edge = [&](size_t phase, size_t k) {
        size_t e = phase + (size_t)std::llround((double)k * sps);
        return std::min(e, m);
    };
    size_t bestPhase = 0;
    double bestEnergy = -1;
    const size_t phases = std::max<size_t>(1, (size_t)std::floor(sps));
    for (size_t ph = 0; ph < phases; ph++) {
        double energy = 0;
        for (size_t k = 0; k < nb; k++) energy += std::fabs(cum[edge(ph, k + 1)] - cum[edge(ph, k)]);
        if (energy > bestEnergy) { bestEnergy = energy; bestPhase = ph; }
    }
    for (size_t k = 0; k < nb; k++) {
        double acc = cum[edge(bestPhase, k + 1)] - cum[edge(bestPhase, k)];
        pending_.push_back(acc > 0 ? 1 : 0);
    }

    // Scan for headers. A header needs 60 bits, so the unscanned tail is kept for the next block.
    size_t s = 0;
    while (s + 2 * kWordBits <= pending_.size()) {
        Header h;
        if (findHeader(&pending_[s], h)) {
            st_.framesOk++;
            st_.typesSeen.insert(h.type);
            st_.last = h;
            s += 2 * kWordBits;
        } else {
            s++;
        }
    }
    pending_.erase(pending_.begin(), pending_.begin() + (std::ptrdiff_t)std::min(s, pending_.size()));
}

std::vector<uint8_t> encodeMessage(const Header& h, const std::vector<uint32_t>& extra, int pol) {
    std::vector<uint8_t> out;
    int prev = 0;
    // Word 1: preamble 8, type 6, station ID 10.
    uint32_t w1 = (0x66u << 16) | ((uint32_t)(h.type & 0x3F) << 10) | (uint32_t)(h.stationId & 0x3FF);
    putWord(w1, pol, prev, out);
    // Word 2: Z-count 13, sequence 3, length 5, health 3.
    uint32_t w2 = ((uint32_t)(h.zcount & 0x1FFF) << 11) | ((uint32_t)(h.seq & 0x7) << 8) |
                  ((uint32_t)(h.length & 0x1F) << 3) | (uint32_t)(h.health & 0x7);
    putWord(w2, pol, prev, out);
    for (uint32_t w : extra) putWord(w & 0xFFFFFFu, pol, prev, out);
    return out;
}

std::vector<std::complex<float>> modulate(const std::vector<uint8_t>& bits, double fs, double offsetHz, float amp) {
    std::vector<std::complex<float>> out;
    const double sps = fs / kBitRate;
    double phase = 0;
    for (size_t k = 0; k < bits.size(); k++) {
        size_t count = (size_t)std::llround((double)(k + 1) * sps) - (size_t)std::llround((double)k * sps);
        double f = offsetHz + (bits[k] ? kDeviationHz : -kDeviationHz);
        for (size_t i = 0; i < count; i++) {
            out.emplace_back((float)(amp * std::cos(phase)), (float)(amp * std::sin(phase)));
            phase += 2.0 * kPi * f / fs;
        }
    }
    return out;
}

}  // namespace dect2::dgps
