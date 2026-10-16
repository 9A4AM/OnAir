// JS8Call test signal (see js8_gen.h).
#include "dect2/js8/js8_gen.h"
#include "dect2/js8/js8_proto.h"
#include "dect2/js8/js8_rx.h"
#include "dect2/dsp/gen_util.h"
#include <chrono>
#include <cmath>
#include <complex>

namespace dect2 {

namespace {
constexpr double kPi = 3.14159265358979323846;

struct Sender {
    Js8GenStation s;
    std::vector<std::vector<int>> frames;   // the tones of each frame of its message
    double amp = 0, ph = 0;
};
} // namespace

struct Js8Generator::Impl {
    Js8GenConfig cfg;
    genutil::NoiseSource noise;
    std::vector<Sender> snd;
    int64_t n = 0;
    explicit Impl(const Js8GenConfig& c) : cfg(c), noise(c.seed) {
        const double sigma = c.noiseSigma > 0 ? c.noiseSigma : 0.02;
        for (const auto& g : c.stations) {
            Sender s;
            s.s = g;
            s.amp = std::sqrt(2 * sigma * sigma * 2500 / c.rate * std::pow(10.0, g.snrDb / 10));
            std::vector<js8::TxFrame> fr;
            js8::TxFrame f{};
            switch (g.kind) {
            case Js8GenStation::Heartbeat: if (js8::packHeartbeat(g.call, g.grid, f.bits)) { f.flags = js8::kFirst | js8::kLast; fr.push_back(f); } break;
            case Js8GenStation::Cq: if (js8::packCq(g.call, g.grid, g.num, f.bits)) { f.flags = js8::kFirst | js8::kLast; fr.push_back(f); } break;
            case Js8GenStation::Directed: fr = js8::buildMessage(g.call, g.to, g.cmd, g.num, ""); break;
            case Js8GenStation::Text: fr = js8::buildMessage(g.call, g.to, g.cmd, g.num, g.text); break;
            }
            for (const auto& t : fr) {
                uint8_t b87[87];
                js8::frameBits(t.bits, t.flags, b87);
                s.frames.push_back(js8::tones(g.submode, b87));
            }
            if (!s.frames.empty()) snd.push_back(std::move(s));
        }
    }
};

Js8Generator::Js8Generator(const Js8GenConfig& c) : p_(std::make_unique<Impl>(c)) {}
Js8Generator::~Js8Generator() = default;

void Js8Generator::generate(cf32* out, size_t n) {
    Impl& m = *p_;
    const double dt = 1.0 / m.cfg.rate;
    for (size_t i = 0; i < n; i++) out[i] = cf32(0, 0);
    for (Sender& g : m.snd) {
        const js8::Spec& sp = js8::spec(g.s.submode);
        const double P = sp.period, baud = sp.baud(), dur = sp.durSec();
        const int nf = (int)g.frames.size();
        const int cyc = nf + std::max(0, g.s.everySlots);
        for (size_t i = 0; i < n; i++) {
            const double t = m.cfg.slotPhaseSec + (double)(m.n + (int64_t)i) * dt;
            const double off = sp.startSec + g.s.dt;
            const double k = std::floor((t - off) / P);
            const double tt = t - (k * P + off);   // into the transmission of slot k
            if (tt < 0 || tt >= dur) continue;
            const int rel = (int)k - g.s.firstSlot;
            if (rel < 0) continue;
            if (g.s.everySlots <= 0 && rel >= nf) continue;
            const int idx = g.s.everySlots <= 0 ? rel : rel % cyc;
            if (idx >= nf) continue;
            const int sym = std::min(js8::kSymbols - 1, (int)(tt * baud));
            const double f = m.cfg.dialOffsetHz + m.cfg.cfoHz + g.s.hz + g.s.driftHzPerMin * t / 60 + g.frames[(size_t)idx][(size_t)sym] * baud;
            g.ph = std::remainder(g.ph + 2 * kPi * f * dt, 2 * kPi);
            const double edge = std::min(1.0, std::min(tt, dur - tt) / 0.01);   // 10 ms ramps
            const double a = g.amp * (0.5 - 0.5 * std::cos(kPi * edge));
            out[i] += cf32((float)(a * std::cos(g.ph)), (float)(a * std::sin(g.ph)));
        }
    }
    m.n += (int64_t)n;
    if (m.cfg.noiseSigma > 0) m.noise.add(out, n, (float)m.cfg.noiseSigma);
}

std::vector<Js8GenStation> js8TestStations() {
    std::vector<Js8GenStation> v;
    Js8GenStation s;
    s.kind = Js8GenStation::Heartbeat; s.call = "LY2ABC"; s.grid = "KO24"; s.hz = 600; s.snrDb = -8; s.firstSlot = 0; s.everySlots = 1;
    v.push_back(s);
    s = Js8GenStation(); s.kind = Js8GenStation::Cq; s.call = "DL1XYZ"; s.grid = "JO62"; s.num = 0; s.hz = 1100; s.snrDb = -12; s.dt = 0.4; s.everySlots = 4;
    v.push_back(s);
    s = Js8GenStation(); s.kind = Js8GenStation::Directed; s.call = "OK1XX"; s.grid = "JN79"; s.to = "LY2ABC"; s.cmd = 25; s.num = -12; s.hz = 1500; s.snrDb = -6; s.firstSlot = 1; s.everySlots = 4;
    v.push_back(s);
    s = Js8GenStation(); s.kind = Js8GenStation::Text; s.call = "G4ABC"; s.grid = "IO91"; s.to = "DL1XYZ"; s.cmd = 31; s.text = "HELLO FROM LONDON. HOW COPY?"; s.hz = 1900; s.snrDb = -4; s.dt = -0.3; s.everySlots = 2;
    v.push_back(s);
    s = Js8GenStation(); s.kind = Js8GenStation::Text; s.call = "EA3XYZ"; s.grid = "JN11"; s.to = "OK1XX"; s.cmd = 5; s.text = "SP5AB PLEASE RELAY. TNX"; s.hz = 2400; s.snrDb = -6; s.firstSlot = 0; s.everySlots = 3;
    v.push_back(s);
    s = Js8GenStation(); s.kind = Js8GenStation::Heartbeat; s.submode = 1; s.call = "YL2XYZ"; s.grid = "KO26"; s.hz = 2800; s.snrDb = -2; s.everySlots = 1;
    v.push_back(s);
    s = Js8GenStation(); s.kind = Js8GenStation::Cq; s.submode = 2; s.call = "ES1ABC"; s.grid = "KO29"; s.num = 7; s.hz = 350; s.snrDb = 0; s.firstSlot = 1; s.everySlots = 1;
    v.push_back(s);
    return v;
}

namespace {
class Js8Synth : public ModeSynth {
public:
    Js8Synth(const SynthConfig& s, double rate) : rate_(rate) {
        Js8GenConfig c;
        c.rate = rate;
        c.dialOffsetHz = -js8Tuning().tuneOffsetHz;
        c.cfoHz = s.cfoHz;
        c.noiseSigma = 0.05;
        // in step with the system clock: the receiver takes its slots from it
        c.slotPhaseSec = std::fmod(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(), 3600.0);
        c.stations = js8TestStations();
        if (s.modeOpt[0] == 1) c.stations.resize(1);
        for (auto& g : c.stations) g.snrDb += s.snrDb;
        gen_ = std::make_unique<Js8Generator>(c);
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override { gen_->generate(out, n); }
private:
    double rate_;
    std::unique_ptr<Js8Generator> gen_;
};
} // namespace

std::unique_ptr<ModeSynth> makeJs8Synth(const SynthConfig& cfg, double sampleRate) {
    if (sampleRate <= 0) return nullptr;
    return std::make_unique<Js8Synth>(cfg, sampleRate);
}

} // namespace dect2
