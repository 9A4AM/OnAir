// Test transmitter for the KAAL receiver tests: codec frames -> LDPC -> 4-frame pseudo-random interleave -> 16-QAM with preamble and
// pilots -> RRC -> sweep at -10 dB (same layout as the Python kaal_tx.py), plus channel helpers.
#pragma once
#include "dect2/kaal/kaal_codec.h"
#include "dect2/kaal/kaal_fec.h"
#include "dect2/kaal/kaal_phy.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace kaalrx {
using namespace dect2::kaal;
using cd = std::complex<double>;

// Test speech-like signal: gliding tones with a slow amplitude envelope and a little deterministic noise. n samples at 16 kHz.
inline std::vector<int16_t> testPcm(size_t n) {
    std::vector<int16_t> x(n);
    uint64_t st = 0x1234567ULL;
    for (size_t i = 0; i < n; i++) {
        const double t = (double)i / 16000.0;
        const double env = 0.6 + 0.4 * std::sin(2 * 3.141592653589793 * 1.7 * t);
        const double f1 = 300.0 + 120.0 * std::sin(2 * 3.141592653589793 * 0.8 * t);
        const double nz = ((double)(xorshift64(st) >> 40) / 16777216.0 - 0.5) * 800.0;
        x[i] = (int16_t)(env * (5000.0 * std::sin(2 * 3.141592653589793 * f1 * t) + 2500.0 * std::sin(2 * 3.141592653589793 * 2.7 * f1 * t)) + nz);
    }
    return x;
}

// Frame bits (320 per frame, padded with zero frames to a multiple of 4).
inline std::vector<uint8_t> frameBits(const std::vector<int16_t>& pcm) {
    dect2::KaalCodec c;
    std::vector<uint8_t> b = c.encode(pcm);
    const size_t nf = b.size() / 320;
    b.resize(((nf + 3) / 4) * 4 * 320, 0);
    return b;
}

// Baseband at 48 kHz: data part plus the sweep at -10 dB. dataPower receives the mean power of the data part.
inline std::vector<cd> transmit(const std::vector<uint8_t>& bits, double& dataPower) {
    const size_t nf = bits.size() / 320;
    std::vector<cf> pre = preamble();
    const cf pil = pilotSymbol();
    std::vector<cd> syms;
    for (size_t blk = 0; blk < nf / 4; blk++) {
        std::vector<uint8_t> coded(4 * kCodedBits);
        for (int f = 0; f < 4; f++) ldpcEncode(&bits[(blk * 4 + f) * 320], &coded[(size_t)f * kCodedBits]);
        coded = interleave(coded);
        for (int f = 0; f < 4; f++) {
            const std::vector<cf> data = qam16Map(&coded[(size_t)f * kCodedBits], kCodedBits);
            for (const cf& p : pre) syms.emplace_back(p.real(), p.imag());
            for (size_t j = 0; j < data.size(); j += kPilotEvery - 1) {
                const size_t e = std::min(data.size(), j + (size_t)(kPilotEvery - 1));
                for (size_t k = j; k < e; k++) syms.emplace_back(data[k].real(), data[k].imag());
                if (e - j == (size_t)(kPilotEvery - 1)) syms.emplace_back(pil.real(), pil.imag());
            }
        }
    }
    const std::vector<double> taps = rrcTaps();
    std::vector<cd> sig(syms.size() * kSps + taps.size() - 1, cd(0, 0));
    for (size_t m = 0; m < syms.size(); m++)
        for (size_t k = 0; k < taps.size(); k++) sig[m * kSps + k] += syms[m] * taps[k];
    double p = 0;
    for (const cd& v : sig) p += std::norm(v);
    dataPower = p / (double)sig.size();
    const std::vector<cf> sw = sweepTemplate(sig.size(), 0.0);
    const double a = std::sqrt(dataPower * std::pow(10.0, kSweepPowerDb / 10.0));
    for (size_t i = 0; i < sig.size(); i++) sig[i] += cd(sw[i].real(), sw[i].imag()) * a;
    return sig;
}

struct Gauss {
    uint64_t st = 0x9E3779B97F4A7C15ULL;
    double uni() { return (double)((xorshift64(st) >> 11) + 1) / 9007199254740994.0; }
    void pair(double& a, double& b) {
        const double r = std::sqrt(-2.0 * std::log(uni())), ph = 2 * 3.141592653589793 * uni();
        a = r * std::cos(ph); b = r * std::sin(ph);
    }
};

// Adds white noise for the given Eb/N0 (rate-1/2, 16-QAM, 6 samples per symbol), then rotates by cfoHz.
inline std::vector<std::complex<float>> channel(const std::vector<cd>& sig, double dataPower, double ebn0Db, double cfoHz, uint64_t seed) {
    const double esn0 = ebn0Db + 10 * std::log10(0.5) + 10 * std::log10(4.0);
    const double sigma2 = dataPower * kSps / std::pow(10.0, esn0 / 10.0);
    Gauss g;
    g.st ^= seed * 0x2545F4914F6CDD1DULL;
    std::vector<std::complex<float>> out(sig.size());
    const double sc = std::sqrt(sigma2 / 2);
    for (size_t i = 0; i < sig.size(); i++) {
        double a, b;
        g.pair(a, b);
        cd v = sig[i] + cd(a * sc, b * sc);
        if (cfoHz != 0) {
            const double ph = 2 * 3.141592653589793 * std::fmod(cfoHz * (double)i / kFs, 1.0);
            v *= cd(std::cos(ph), std::sin(ph));
        }
        out[i] = std::complex<float>((float)v.real(), (float)v.imag());
    }
    return out;
}
}  // namespace kaalrx
