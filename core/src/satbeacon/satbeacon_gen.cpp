// Satellite beacon test signal (see satbeacon_gen.h).
#include "dect2/satbeacon/satbeacon_gen.h"
#include "dect2/dsp/gen_util.h"
#include "dect2/common/mathconst.h"
#include <algorithm>
#include <cmath>

namespace dect2 {

namespace {
uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
}

std::vector<uint8_t> satbeaconFrameBytes(uint32_t seed, uint32_t n) {
    std::vector<uint8_t> b(kSbGenFrameBytes, 0);
    b[0] = 0xFA; b[1] = 0xF3; b[2] = 0x20; b[3] = 0xFE;
    b[4] = (uint8_t)(n % 32);
    b[7] = 0x1E; b[8] = 0xA3; b[9] = 0x2B;
    for (int i = 10; i < kSbGenFrameBytes - 1; i++) {
        const uint64_t col = mix64(((uint64_t)seed << 32) ^ (uint64_t)i);
        const uint64_t val = mix64(((uint64_t)seed << 48) ^ ((uint64_t)n << 12) ^ (uint64_t)i);
        const uint64_t pv = mix64(((uint64_t)seed << 48) ^ ((uint64_t)(n % 32) << 12) ^ (uint64_t)i ^ 0x5555);
        const uint64_t kind = col & 15;
        if (kind < 6) {                              // a word that never changes: mostly 00 and FF as in the recorded beacon
            static const uint8_t fixedWords[4] = {0x00, 0x00, 0xFF, 0x60};
            b[(size_t)i] = fixedWords[(col >> 8) & 3];
        } else if (kind < 8) b[(size_t)i] = 0xFB;    // idle fill
        else if (kind < 12) b[(size_t)i] = (uint8_t)(pv & 0xFF);   // a commutated word: its value depends on the position in the major frame
        else b[(size_t)i] = (uint8_t)(val & 0xFF);                // a sample: changes every frame
    }
    b[15] = (uint8_t)(17 + (uint64_t)n * 128 / 300);   // a clock: one count per second (a frame is 2048 bits at 4800 bit/s = 0.42667 s)
    b[122] = (uint8_t)(n % 32);                         // a copy of the counter
    unsigned sum = 0;
    for (int i = 0; i < kSbGenFrameBytes - 1; i++) sum += b[(size_t)i];
    b[kSbGenFrameBytes - 1] = (uint8_t)(sum & 255);     // the checksum: the sum of the others
    return b;
}

struct SatbeaconGenerator::Impl {
    SatbeaconGenConfig c;
    genutil::NoiseSource noise;
    float sigma = 0;
    double ph1 = 0, ph2 = 0, phS = 0;
    int64_t n = 0;
    uint32_t frameNo = 0xFFFFFFFFu;
    std::vector<uint8_t> frame;

    explicit Impl(const SatbeaconGenConfig& cfg) : c(cfg), noise(cfg.seed) {
        if (c.snrDb < 99) {
            // carrier amp^2 over noise power in 1 kHz = snr
            const double v = c.amp * c.amp * c.rate / (1000.0 * std::pow(10.0, c.snrDb / 10));
            sigma = (float)std::sqrt(v / 2);
        }
    }
    int bitAt(int64_t k) {
        if (k < 0) k = 0;
        const int64_t perFrame = (int64_t)kSbGenFrameBytes * 8;
        const uint32_t f = (uint32_t)(k / perFrame);
        if (f != frameNo) { frameNo = f; frame = satbeaconFrameBytes(c.seed, f); }
        const int64_t j = k % perFrame;
        return (frame[(size_t)(j >> 3)] >> (7 - (j & 7))) & 1;
    }
    // chip k: bi-phase-L (1: high then low, 0: low then high) or the NRZ bit
    int chipAt(int64_t k) {
        if (!c.biphase) return bitAt(k) ? 1 : -1;
        const int b = bitAt(k >> 1);
        const bool first = (k & 1) == 0;
        return (b ? first : !first) ? 1 : -1;
    }
    double data(double t) {
        const double rc = c.bitRate * (c.biphase ? 2.0 : 1.0) * (1.0 + c.clockPpm * 1e-6);
        const double u = t * rc;
        const int64_t k = (int64_t)std::floor(u);
        const double fr = u - k;
        auto w = [](double x) { return 0.5 - 0.5 * std::cos(math::kPi * x); };
        const int cur = chipAt(k);
        if (fr < 0.1) { const int prev = chipAt(k - 1); const double a = w((fr + 0.1) / 0.2); return prev + (cur - prev) * a; }
        if (fr > 0.9) { const int next = chipAt(k + 1); const double a = w((fr - 0.9) / 0.2); return cur + (next - cur) * a; }
        return cur;
    }
};

SatbeaconGenerator::SatbeaconGenerator(const SatbeaconGenConfig& c) : p_(std::make_unique<Impl>(c)) {}
SatbeaconGenerator::~SatbeaconGenerator() = default;

void SatbeaconGenerator::generate(cf32* out, size_t cnt) {
    Impl& m = *p_;
    const double R = m.c.rate;
    const double a2 = m.c.amp * std::pow(10.0, m.c.refDb / 20);
    for (size_t k = 0; k < cnt; k++) {
        const double t = (double)(m.n + (int64_t)k) / R;
        const double fc = m.c.carrierHz + m.c.driftHzPerSec * t;
        m.ph1 += 2 * math::kPi * fc / R;
        if (m.ph1 > 2 * math::kPi) m.ph1 -= 2 * math::kPi;
        double pm = 0;
        if (m.c.telemetry) {
            m.phS += 2 * math::kPi * m.c.subcarrierHz / R; if (m.phS > 2 * math::kPi) m.phS -= 2 * math::kPi;
            pm = m.c.pmIndex * m.data(t) * std::sin(m.phS);
        }
        double re = m.c.amp * std::cos(m.ph1 + pm), im = m.c.amp * std::sin(m.ph1 + pm);
        if (m.c.reference) {
            m.ph2 += 2 * math::kPi * (fc + m.c.refOffsetHz) / R;
            if (m.ph2 > 2 * math::kPi) m.ph2 -= 2 * math::kPi;
            re += a2 * std::cos(m.ph2); im += a2 * std::sin(m.ph2);
        }
        out[k] = cf32((float)re, (float)im);
    }
    if (m.sigma > 0) m.noise.add(out, cnt, m.sigma);
    m.n += (int64_t)cnt;
}

namespace {
class SatbeaconSynth : public ModeSynth {
public:
    SatbeaconSynth(const SynthConfig& s, double rate) : rate_(rate) {
        SatbeaconGenConfig c;
        c.rate = rate;
        c.snrDb = s.snrDb;
        if (s.cfoHz != 0) c.carrierHz = s.cfoHz;
        c.telemetry = s.modeOpt[0] != 1;
        c.reference = s.modeOpt[0] != 2;
        gen_ = std::make_unique<SatbeaconGenerator>(c);
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override { gen_->generate(out, n); }
private:
    double rate_;
    std::unique_ptr<SatbeaconGenerator> gen_;
};
} // namespace

std::unique_ptr<ModeSynth> makeSatbeaconSynth(const SynthConfig& cfg, double sampleRate) {
    if (sampleRate <= 0) return nullptr;
    return std::make_unique<SatbeaconSynth>(cfg, sampleRate);
}

} // namespace dect2
