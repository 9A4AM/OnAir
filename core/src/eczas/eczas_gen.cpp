// e-Czas test signal (see eczas_gen.h).
#include "dect2/eczas/eczas_gen.h"
#include "dect2/eczas/eczas_live.h"
#include "dect2/dsp/gen_util.h"
#include "dect2/common/mathconst.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace dect2 {

namespace {
using dect2::math::kPi;
using namespace eczas;

constexpr double kBitSec = 0.02, kRampSec = 0.019, kSlotLen = 3.0;
constexpr double kRampRad = 36.0 * kPi / 180;

double nowUtc() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

// day of the week, 0 = Sunday, of a day count since 1970-01-01
int weekday(int64_t days) { return (int)(((days % 7) + 11) % 7); }

int64_t lastSundayUtc(int year, int month) {   // 01:00 UTC of the last Sunday of the month, UTC epoch seconds
    // days from civil
    auto dfc = [](int y, int m, int d) {
        y -= m <= 2;
        const int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    };
    int64_t d = dfc(year, month, 31);
    while (weekday(d) != 0) d--;
    return d * 86400 + 3600;
}
} // namespace

int polandOffsetHours(int64_t utcSec) {
    int y, mo, d, h, mi, s;
    civilFromUnix(utcSec, y, mo, d, h, mi, s);
    return utcSec >= lastSundayUtc(y, 3) && utcSec < lastSundayUtc(y, 10) ? 2 : 1;
}

struct EczasGenerator::Impl {
    EczasGenConfig c;
    genutil::NoiseSource noise;
    float sigma = 0;
    int64_t n = 0;
    double ph = 0;
    int64_t slotIdx = INT64_MIN;
    uint8_t bits[96]{};
    int prevState = 1;               // keying state before the frame (the slicer assumes 1)
    int lastState = 1;

    explicit Impl(const EczasGenConfig& cfg) : c(cfg), noise(cfg.seed) {
        if (c.startUtc <= 0) c.startUtc = nowUtc();
        if (c.snrDb < 99) {
            const double n100 = c.amp * c.amp / std::pow(10.0, c.snrDb / 10);   // in 100 Hz
            sigma = (float)std::sqrt(n100 * c.rate / 100 / 2);
        }
    }

    void loadSlot(int64_t slot) {
        slotIdx = slot;
        const int64_t t0 = slot * 3;
        const int64_t minute = t0 - ((t0 % 60) + 60) % 60;
        const int k = (int)((t0 - minute) / 3);
        uint8_t f[kFrameBytes];
        EczasGenerator::frameFor(minute, k, c, f);
        for (int i = 0; i < 96; i++) bits[i] = (uint8_t)((f[i / 8] >> (7 - i % 8)) & 1);
    }

    // the keying phase (radians) at time u seconds into the slot
    double phaseAt(int64_t slot, double u) {
        if (slot != slotIdx) loadSlot(slot);
        const double hi = kRampRad;
        auto ph0 = [&](int st) { return st ? hi : 0.0; };
        const int j = (int)std::floor(u / kBitSec);
        if (j < 0) return ph0(1);
        if (j < 96) {
            const int prev = j == 0 ? 1 : bits[j - 1], cur = bits[j];
            if (prev == cur) return ph0(cur);
            const double r = std::min(1.0, (u - j * kBitSec) / kRampSec);
            return ph0(prev) + (ph0(cur) - ph0(prev)) * r;
        }
        // after the frame the keying stays where it is until 2.5 s, then goes back to the state the next frame starts from, slowly
        const int last = bits[95];
        if (last == 1 || u < 2.5) return ph0(last);
        return ph0(0) + (ph0(1) - ph0(0)) * std::min(1.0, (u - 2.5) / 0.4);
    }
};

void EczasGenerator::frameFor(int64_t minuteStart, int slot, const EczasGenConfig& c, uint8_t out[kFrameBytes]) {
    if (c.timeSlotStride > 0 && slot % c.timeSlotStride == 0) {   // the time packet: its stamp is the start of its own slot
        const int64_t slotStart = minuteStart + 3 * slot;
        const int tz = c.tzHours >= 0 ? c.tzHours : polandOffsetHours(slotStart);
        const int64_t per = (slotStart - kEpochUtc) / 3;
        encodeTimeFrameForTest((uint32_t)per, tz, c.flags, c.txState, out);
        return;
    }
    // another packet of the minute frame: header 0x60 + slot is not the time packet; random body, valid check byte
    std::memset(out, 0, kFrameBytes);
    out[0] = out[1] = 0x55;
    out[2] = (uint8_t)(0x40 + (slot & 0x1F));
    uint32_t x = (uint32_t)(minuteStart * 2654435761u) ^ (uint32_t)(slot * 40503u) ^ c.seed;
    for (int i = 3; i < 8; i++) { x = x * 1664525u + 1013904223u; out[i] = (uint8_t)(x >> 24); }
    out[11] = crc8Poly07(out + 3, 5);
}

EczasGenerator::EczasGenerator(const EczasGenConfig& c) : p_(std::make_unique<Impl>(c)) {}
EczasGenerator::~EczasGenerator() = default;

void EczasGenerator::generate(cf32* out, size_t cnt) {
    Impl& m = *p_;
    const double dph = 2 * kPi * (m.c.dialOffsetHz + m.c.cfoHz) / m.c.rate;
    for (size_t k = 0; k < cnt; k++) {
        const double t = m.c.startUtc + (double)m.n / (m.c.rate * (1 + m.c.clockPpm * 1e-6)) - m.c.delayMs * 1e-3;
        double mod = 0;
        if (!m.c.carrierOnly) {
            const int64_t slot = (int64_t)std::floor(t / kSlotLen);
            const double u = t - (double)slot * kSlotLen;
            const int64_t t0 = slot * 3;
            const int kslot = (int)(((t0 % 60) + 60) % 60 / 3);
            if (kslot % std::max(1, m.c.timeSlotStride) == 0 || m.c.otherSlots) mod = m.phaseAt(slot, u);
            else mod = kRampRad;   // no packet in this slot: the carrier rests in the state the next frame starts from
        }
        double a = m.c.amp;
        if (m.c.amDepth > 0) a *= 1.0 - m.c.amDepth * (0.5 + 0.5 * std::sin(2 * kPi * t / 7.3));
        const double ang = m.ph + mod;
        out[k] = cf32((float)(a * std::cos(ang)), (float)(a * std::sin(ang)));
        m.ph += dph;
        if (m.ph > kPi) m.ph -= 2 * kPi; else if (m.ph < -kPi) m.ph += 2 * kPi;
        m.n++;
    }
    if (m.sigma > 0) m.noise.add(out, cnt, m.sigma);
}

namespace {
class EczasSynth : public ModeSynth {
public:
    EczasSynth(const SynthConfig& s, double rate) : rate_(rate) {
        EczasGenConfig c;
        c.rate = rate;
        c.dialOffsetHz = -eczasTuning().tuneOffsetHz;
        c.snrDb = s.snrDb;
        c.cfoHz = s.cfoHz;
        gen_ = std::make_unique<EczasGenerator>(c);
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override { gen_->generate(out, n); }
private:
    double rate_;
    std::unique_ptr<EczasGenerator> gen_;
};
} // namespace

std::unique_ptr<ModeSynth> makeEczasSynth(const SynthConfig& cfg, double sampleRate) {
    if (sampleRate <= 0) return nullptr;
    return std::make_unique<EczasSynth>(cfg, sampleRate);
}

} // namespace dect2
