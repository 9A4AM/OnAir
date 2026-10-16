// CubeSat telemetry test signal: see cubesat_gen.h.
#include "dect2/cubesat/cubesat_gen.h"
#include "dect2/cubesat/cubesat_rx.h"
#include "dect2/dsp/gen_util.h"
#include "dect2/packet/packet_ax25.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace dect2 {

using namespace cubesat;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kWork = 96000.0;
constexpr double kLeadSec = 0.6;
constexpr double kChanBw = 25000.0;

ax25::Address addr(const std::string& s) {
    ax25::Address a;
    const size_t d = s.find('-');
    a.call = s.substr(0, d);
    a.ssid = d == std::string::npos ? 0 : atoi(s.c_str() + d + 1);
    return a;
}

std::vector<uint8_t> ax25Bytes(const std::string& from, const std::string& to, const std::string& info) {
    ax25::Frame f;
    f.from = addr(from); f.to = addr(to);
    f.info = info;
    return ax25::buildFrame(f);       // FCS included
}

std::vector<uint8_t> bytesOf(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

std::vector<uint8_t> msbBits(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> b;
    for (uint8_t v : bytes) for (int k = 7; k >= 0; k--) b.push_back((uint8_t)((v >> k) & 1));
    return b;
}

struct Mod {
    std::vector<cf32>* out;
    double theta = 0;
    void silence(double sec) { out->insert(out->end(), (size_t)std::lround(sec * kWork), cf32(0.f, 0.f)); }
    void push(double devHz) {
        theta += 2 * kPi * devHz / kWork;
        if (theta > kPi) theta -= 2 * kPi;
        out->push_back(cf32((float)std::cos(theta), (float)std::sin(theta)));
    }
};

void sendAfsk(Mod& m, const std::vector<uint8_t>& levels) {
    double ph = 0;
    for (uint8_t lv : levels)
        for (int i = 0; i < 80; i++) {
            ph += 2 * kPi * (lv ? 1200.0 : 2200.0) / kWork;
            if (ph > 2 * kPi) ph -= 2 * kPi;
            m.push(2800.0 * std::sin(ph));
        }
}

// levels -> +-1, Gaussian filter BT 0.5, frequency deviation devHz
void sendFsk(Mod& m, const std::vector<uint8_t>& levels, int baud, double devHz) {
    const int sps = (int)std::lround(kWork / baud);
    const double sigma = std::sqrt(std::log(2.0)) / (2 * kPi * 0.5 * baud) * kWork;
    const int half = (int)std::ceil(3 * sigma);
    std::vector<double> g((size_t)(2 * half + 1));
    double sum = 0;
    for (int i = -half; i <= half; i++) { g[(size_t)(i + half)] = std::exp(-0.5 * i * i / (sigma * sigma)); sum += g[(size_t)(i + half)]; }
    for (double& x : g) x /= sum;
    const long n = (long)levels.size() * sps;
    auto nrz = [&](long i) { i = std::max(0L, std::min(n - 1, i)); return levels[(size_t)(i / sps)] ? 1.0 : -1.0; };
    for (long i = 0; i < n; i++) {
        double acc = 0;
        for (int k = -half; k <= half; k++) acc += g[(size_t)(k + half)] * nrz(i + k);
        m.push(devHz * acc);
    }
}

struct Item { CubeGenFrame f; std::vector<uint8_t> air; };   // air: the bytes to send (HDLC / ASM framing applied below)

std::vector<Item> items() {
    std::vector<Item> v;
    auto csp = [](int src, int dst, int dport, int sport, const std::string& p) {
        CspHeader h; h.prio = 2; h.src = src; h.dst = dst; h.dport = dport; h.sport = sport;
        return buildCsp(h, bytesOf(p));
    };
    {   // AX.25 AFSK 1200
        Item it; it.f.framing = Ax25Afsk1200; it.f.baud = 1200; it.f.from = "RS0ISS";
        it.air = ax25Bytes("RS0ISS", "CQ", ">CubeSat test: 1200 baud AFSK packet");
        it.f.data.assign(it.air.begin(), it.air.end() - 2);
        v.push_back(it);
    }
    {   // AX.25 G3RUH 9600
        Item it; it.f.framing = Ax25G3ruh; it.f.baud = 9600;
        it.air = ax25Bytes("TSTSAT-1", "BEACON", "TLM bat=7.92V temp=+12.5C mode=nominal uptime=123456");
        it.f.from = "TSTSAT-1";
        it.f.data.assign(it.air.begin(), it.air.end() - 2);
        v.push_back(it);
    }
    {   // AX100 Mode 5, GMSK 9600
        Item it; it.f.framing = Ax100Mode5; it.f.baud = 9600;
        it.f.data = csp(1, 10, 31, 0, "AX100 mode 5 beacon: obc ok, eps 8.1 V, 4 boots");
        it.air = ax100Mode5Encode(it.f.data);
        v.push_back(it);
    }
    {   // AX100 Mode 6, G3RUH 9600
        Item it; it.f.framing = Ax100Mode6; it.f.baud = 9600;
        it.f.data = csp(5, 10, 8, 1, "AX100 mode 6: RS inside HDLC, G3RUH scrambled");
        it.air = ax100Mode6Encode(it.f.data);
        v.push_back(it);
    }
    {   // CCSDS RS, GMSK 9600
        Item it; it.f.framing = CcsdsRs; it.f.baud = 9600;
        std::vector<uint8_t> d = bytesOf("CCSDS frame: virtual channel 0, housekeeping");
        d.resize(223, 0);
        it.f.data = d;
        it.air = ccsdsEncode(d, 255);
        v.push_back(it);
    }
    {   // AX100 Mode 5, GMSK 4800
        Item it; it.f.framing = Ax100Mode5; it.f.baud = 4800;
        it.f.data = csp(1, 10, 31, 0, "AX100 at 4800 baud");
        it.air = ax100Mode5Encode(it.f.data);
        v.push_back(it);
    }
    return v;
}

struct Cycle {
    std::vector<CubeGenFrame> frames;
    std::vector<cf32> bb;
};

const Cycle& cycleFor(int mode) {
    static Cycle cache[1 + kNumFramings];
    static bool built[1 + kNumFramings] = {};
    const int k = mode >= 1 && mode <= kNumFramings ? mode : 0;
    if (built[k]) return cache[k];
    Cycle& c = cache[k];
    Mod m;
    m.out = &c.bb;
    m.silence(kLeadSec);
    uint32_t scr = 0;
    for (const Item& it : items()) {
        if (k && it.f.framing != k - 1) continue;
        c.frames.push_back(it.f);
        switch (it.f.framing) {
        case Ax25Afsk1200:
            sendAfsk(m, ax25::nrziEncode(ax25::hdlcBits(it.air, 24, 4)));
            m.silence(0.25);
            break;
        case Ax25G3ruh:
        case Ax100Mode6:
            sendFsk(m, ax25::scramble(ax25::nrziEncode(ax25::hdlcBits(it.air, 60, 6)), scr), it.f.baud, 3000.0);
            m.silence(0.15);
            break;
        default: {                       // the sync word framings: a 0101 preamble, the frame, a few bytes of tail
            std::vector<uint8_t> bytes(48, 0x55);
            bytes.insert(bytes.end(), it.air.begin(), it.air.end());
            bytes.insert(bytes.end(), 4, 0x55);
            sendFsk(m, msbBits(bytes), it.f.baud, it.f.baud / 4.0 * 1.25);
            m.silence(0.15);
            break;
        }
        }
    }
    built[k] = true;
    return c;
}

class CubesatSynth : public ModeSynth {
public:
    CubesatSynth(const SynthConfig& cfg, double rate) : rate_(rate), cyc_(cycleFor(cfg.modeOpt[0])), noise_(11) {
        const double pn = rate / kChanBw / std::pow(10.0, cfg.snrDb / 10.0);
        sigma_ = cfg.snrDb < -100 ? 0.f : (float)std::sqrt(pn / 2);
        scale_ = sigma_ > 0.15f ? 0.15f / sigma_ : 1.f;
        if (sigma_ == 0.f) scale_ = 0.4f;
        const double w = 2 * kPi * (cfg.cfoHz - cubesatTuning().tuneOffsetHz) / rate;
        step_ = cf32((float)std::cos(w), (float)std::sin(w));
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override {
        const std::vector<cf32>& bb = cyc_.bb;
        const double adv = kWork / rate_;
        const size_t len = bb.size();
        for (size_t i = 0; i < n; i++) {
            const size_t i0 = (size_t)pos_;
            const float fr = (float)(pos_ - (double)i0);
            const cf32 a = bb[i0 % len], b = bb[(i0 + 1) % len];
            out[i] = (a + (b - a) * fr) * rot_ * scale_;
            rot_ *= step_;
            pos_ += adv;
            if (pos_ >= (double)len) pos_ -= (double)len;
        }
        rot_ /= std::abs(rot_);
        if (sigma_ > 0.f) noise_.add(out, n, sigma_ * scale_);
    }
private:
    double rate_;
    const Cycle& cyc_;
    genutil::NoiseSource noise_;
    float sigma_ = 0, scale_ = 1;
    cf32 step_, rot_ = cf32(1.f, 0.f);
    double pos_ = 0;
};

} // namespace

std::vector<CubeGenFrame> cubesatGenCycle(int mode) { return cycleFor(mode).frames; }
double cubesatGenCycleSec(int mode) { return (double)cycleFor(mode).bb.size() / kWork; }

std::unique_ptr<ModeSynth> makeCubesatSynth(const SynthConfig& cfg, double sampleRate) {
    if (sampleRate < cubesatTuning().minSampleRate - 1) return nullptr;
    return std::make_unique<CubesatSynth>(cfg, sampleRate);
}

} // namespace dect2
