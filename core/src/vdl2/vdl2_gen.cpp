// VDL Mode 2 test signal: see vdl2_gen.h.
#include "dect2/vdl2/vdl2_gen.h"
#include "dect2/aero/aero_adsc.h"
#include "dect2/vdl2/vdl2_proto.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <queue>
#include <random>
#include <string>
#include "dect2/common/mathconst.h"

namespace dect2 {

namespace {
using dect2::math::kPi;
constexpr int kRamp = 3, kTail = 2, kSpan = 5, kRes = 256;   // ramp and tail symbols, pulse half-length (symbols), table steps per symbol

const std::vector<float>& rcTable() {
    static std::vector<float> t = [] {
        std::vector<float> v((size_t)(2 * kSpan * kRes + 2));
        const double a = 0.6;
        for (size_t i = 0; i < v.size(); i++) {
            const double x = (double)i / kRes - kSpan;            // in symbols
            const double s = std::fabs(x) < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double d = 1 - 4 * a * a * x * x;
            const double c = std::fabs(d) < 1e-9 ? kPi / 4 : std::cos(kPi * a * x) / d;
            const double w = 0.5 + 0.5 * std::cos(kPi * x / kSpan);   // gentle truncation
            v[i] = (float)(s * c * (std::fabs(d) < 1e-9 ? 1.0 : 1.0) * w);
        }
        return v;
    }();
    return t;
}
inline float rc(double x) {                                     // x in symbols
    const double u = (x + kSpan) * kRes;
    if (u <= 0 || u >= 2 * kSpan * kRes) return 0.f;
    const int i = (int)u;
    const float f = (float)(u - i);
    const auto& t = rcTable();
    return t[(size_t)i] * (1 - f) + t[(size_t)i + 1] * f;
}
} // namespace

Vdl2Wave vdl2MakeWave(const Vdl2Frames& frames) {
    Vdl2Wave w;
    const std::vector<uint8_t> bits = vdl2BurstBits(vdl2HdlcBits(frames));
    static const float rampEnv[kRamp] = {0.2f, 0.6f, 0.9f};
    for (int i = 0; i < kRamp; i++) { w.ph.push_back(0); w.env.push_back(rampEnv[i]); }
    for (int i = 0; i < kVdl2PreambleSyms; i++) { w.ph.push_back((int8_t)(((kVdl2PreamblePhase[i] % 8) + 8) % 8)); w.env.push_back(1.f); }
    int cur = w.ph.back();
    for (size_t i = 0; i < bits.size(); i += 3) {
        const int b = (bits[i] << 2) | ((i + 1 < bits.size() ? bits[i + 1] : 0) << 1) | (i + 2 < bits.size() ? bits[i + 2] : 0);
        cur = (cur + vdl2StepForBits(b)) % 8;
        w.ph.push_back((int8_t)cur); w.env.push_back(1.f);
    }
    for (int i = 0; i < kTail; i++) { w.ph.push_back((int8_t)cur); w.env.push_back(i ? 0.25f : 0.6f); }
    return w;
}

double Vdl2Wave::endSample(double rate) const { return start + ((double)ph.size() + kSpan) * rate / kVdl2SymbolRate; }

double vdl2BurstSeconds(const Vdl2Frames& frames) {
    const size_t bits = vdl2BurstBits(vdl2HdlcBits(frames)).size();
    return (double)(kRamp + kVdl2PreambleSyms + (bits + 2) / 3 + kTail) / kVdl2SymbolRate;
}

// The first training symbol (index kRamp) is at w.start; symbol k at start + (k - kRamp) * T.
void vdl2Render(const Vdl2Wave& w, cf32* out, int64_t outStart, size_t n, double rate) {
    const double T = rate / kVdl2SymbolRate;
    const double s0 = w.start - (kRamp + kSpan) * T, s1 = w.start + ((double)w.ph.size() - kRamp + kSpan) * T;
    const int64_t a = std::max<int64_t>(outStart, (int64_t)std::ceil(s0)), b = std::min<int64_t>(outStart + (int64_t)n, (int64_t)std::floor(s1));
    if (a >= b) return;
    cf32 pts[8];
    for (int k = 0; k < 8; k++) pts[k] = std::polar((float)w.amp, (float)(k * kPi / 4));
    const int ns = (int)w.ph.size();
    for (int64_t i = a; i < b; i++) {
        const double u = ((double)i - w.start) / T + kRamp;     // position in symbols
        const int k0 = std::max(0, (int)std::ceil(u - kSpan)), k1 = std::min(ns - 1, (int)std::floor(u + kSpan));
        cf32 acc(0, 0);
        for (int k = k0; k <= k1; k++) acc += pts[(size_t)w.ph[(size_t)k]] * (w.env[(size_t)k] * rc(u - k));
        const double ph = 2 * kPi * w.carHz * ((double)i - w.start) / rate + w.phase;
        out[i - outStart] += acc * cf32((float)std::cos(ph), (float)std::sin(ph));
    }
}

// ---------------------------------------------------------------- traffic

namespace {
const char* kRegs[24] = {"G-EUUA", "D-AIBC", "F-HBXA", "EI-DVM", "A6-EDA", "HB-JNA", "PH-BXA", "SE-ROA", "OO-SNA", "9H-QAA", "TC-JJA", "OE-LBA",
                         "G-XLEA", "D-ABYA", "F-GZNA", "EC-MXV", "LN-RKA", "OY-KBA", "SP-LRA", "CS-TUA", "I-ADJA", "OH-LWA", "N784AV", "C-FIVA"};
const char* kFlights[24] = {"BA0117", "LH0400", "AF1680", "EI0154", "EK0002", "LX0318", "KL1001", "SK0525", "SN2093", "KM0101", "TK1980", "OS0451",
                            "BA0015", "LH0716", "AF0084", "IB3166", "DY1302", "SK1415", "LO0281", "TP1351", "AZ0201", "AY1331", "AV0121", "AC0855"};
const char* kDest[8] = {"OMDB", "KJFK", "LFPG", "EDDF", "EHAM", "LEMD", "LIRF", "EGLL"};

struct Air {
    uint32_t icao;
    std::string reg, flight;
    int chan;
    double lat, lon, trk, kt;
    int alt;
    double levelDb, carOff;
    int step = 0, ns = 0, nr = 0, msg = 0, blk = 0;
    double next = 0;
};
struct Gs { uint32_t addr; double lat, lon; std::string airports; double next = 0; double carOff = 0; };
struct Ev { double t; int kind; int who; std::vector<std::vector<uint8_t>> frames; bool operator>(const Ev& o) const { return t > o.t; } };   // kind 0 aircraft, 1 ground station, 2 reply
} // namespace

struct Vdl2Traffic::Impl {
    Vdl2GenOptions o;
    std::mt19937 rng;
    std::vector<Air> air;
    std::vector<Gs> gs;
    std::vector<double> busy;
    std::priority_queue<Ev, std::vector<Ev>, std::greater<Ev>> q;

    double uni(double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); }

    explicit Impl(const Vdl2GenOptions& opt) : o(opt), rng(opt.seed ? opt.seed : 1) {
        const int nc = std::max<int>(1, (int)o.channelsHz.size());
        busy.assign((size_t)nc, 0.0);
        for (int c = 0; c < nc; c++) {
            Gs g;
            g.addr = 0x10A000u + (uint32_t)c * 0x123u;
            g.lat = 51.47 + 0.3 * std::sin(c * 1.3);
            g.lon = -0.45 + 0.5 * std::cos(c * 1.7);
            static const char* ap[6] = {"EGLL EGKK", "EGSS EGGW", "EGLC", "EGKK", "EGLL", "EGLL EGLC EGKK"};
            g.airports = ap[c % 6];
            g.next = uni(0.05, 1.0);
            g.carOff = uni(-40, 40);
            gs.push_back(g);
            q.push({g.next, 1, c, {}});
        }
        const int na = std::max(1, std::min(24, o.aircraft));
        for (int i = 0; i < na; i++) {
            Air a;
            a.icao = 0x400000u + (uint32_t)(i * 0x1F3D7 % 0x3FFFFF) + 0x1234;
            a.reg = kRegs[i]; a.flight = kFlights[i];
            a.chan = (i + nc - 1) % nc;            // the first on the CSC (the last channel of the European list)
            a.lat = 51.47 + uni(-1.2, 1.2); a.lon = -0.45 + uni(-2, 2);
            a.trk = uni(0, 360); a.kt = uni(380, 480); a.alt = 25000 + (int)uni(0, 13) * 1000;
            a.levelDb = -uni(0, std::max(1e-6, o.levelSpreadDb)); a.carOff = uni(-o.cfoSpreadHz, o.cfoSpreadHz);
            a.next = uni(0.2, 2.5) / std::max(0.1, o.rateFactor);
            if (i == 0 && !o.reg.empty()) a.reg = o.reg;
            if (!o.text.empty()) a.step = 1;
            air.push_back(a);
            q.push({a.next, 0, i, {}});
        }
    }

    Vdl2Addr airAddr(const Air& a, bool resp = false) const { return {a.icao, 1, resp}; }
    Vdl2Addr gsAddr(int c, bool resp = false) const { return {gs[(size_t)c].addr, 4, resp}; }

    std::string downText(Air& a, const std::string& payload) {
        char b[16];
        snprintf(b, sizeof b, "M%02d", a.msg++ % 100);
        return std::string(b) + "A" + a.flight + payload;
    }

    std::vector<uint8_t> acars(Air& a, const std::string& label, const std::string& payload) {
        const char blk = (char)('0' + a.blk++ % 10);
        return vdl2Frame(gsAddr(a.chan), airAddr(a), vdl2ControlI(a.ns++, a.nr, false), vdl2AcarsInfo(a.reg, label, blk, downText(a, payload)));
    }

    void move(Air& a, double dt) {
        const double d = a.kt * dt / 3600.0 * 20;            // sped up 20 times so the map shows movement
        a.lat += d / 60 * std::cos(a.trk * kPi / 180);
        a.lon += d / 60 * std::sin(a.trk * kPi / 180) / std::cos(a.lat * kPi / 180);
    }

    Ev aircraftEvent(Air& a, double t) {
        Ev e{t, 0, 0, {}};
        const int kind = a.step == 0 ? 0 : 1 + (a.step - 1) % 7;
        a.step++;
        char b[200];
        switch (kind) {
        case 0:   // link establishment
            e.frames.push_back(vdl2Frame(gsAddr(a.chan), airAddr(a), (uint8_t)(kVdl2CtrlXID | 0x10), vdl2XidAircraftLe(a.lat, a.lon, a.alt, kDest[a.icao % 8])));
            break;
        case 1:   // free text
            if (!o.text.empty()) snprintf(b, sizeof b, "%s", o.text.c_str());
            else snprintf(b, sizeof b, "#DFB/FUEL %d/ETA %02d%02d/WX REQ %s", 120 + (int)(a.icao % 50), (int)(t / 60) % 24, (int)t % 60, kDest[a.icao % 8]);
            e.frames.push_back(acars(a, "H1", b));
            break;
        case 2: { // ADS-C basic report, flight id, earth reference
            std::vector<uint8_t> g = aeroAdscBasicGroup(a.lat, a.lon, a.alt, std::fmod(t, 3600.0));
            const auto f = aeroAdscFlightIdGroup(a.flight);
            g.insert(g.end(), f.begin(), f.end());
            const auto er = aeroAdscEarthRefGroup(a.trk, a.kt, 0);
            g.insert(g.end(), er.begin(), er.end());
            e.frames.push_back(acars(a, "B6", aeroBuildAdscText("LHRCAYA", a.reg, g)));
            break;
        }
        case 3:   // label 16 position
            snprintf(b, sizeof b, "%c %.3f,%c %.3f,%d,6, 290", a.lat < 0 ? 'S' : 'N', std::fabs(a.lat), a.lon < 0 ? 'W' : 'E', std::fabs(a.lon), a.alt);
            e.frames.push_back(acars(a, "16", b));
            break;
        case 4: { // X.25 call request to the ground router
            e.frames.push_back(vdl2Frame(gsAddr(a.chan), airAddr(a), vdl2ControlI(a.ns++, a.nr, false), vdl2X25CallRequest(1, "", "")));
            break;
        }
        case 5: { // X.25 data, CLNP, COTP, ATN application bytes
            std::vector<uint8_t> src = {0x47, 0x00, 0x27, 0x81, 0x41, 0x43, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, (uint8_t)(a.icao >> 16), (uint8_t)(a.icao >> 8), (uint8_t)a.icao, 0, 0, 0, 0x01};
            std::vector<uint8_t> dst = {0x47, 0x00, 0x27, 0x41, 0x47, 0x42, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xA0, 0x00, 0, 0, 0, 0x21};
            std::vector<uint8_t> user;
            for (int i = 0; i < 18; i++) user.push_back((uint8_t)(rng() & 0xFF));
            e.frames.push_back(vdl2Frame(gsAddr(a.chan), airAddr(a), vdl2ControlI(a.ns++, a.nr, false), vdl2X25ClnpData(1, a.ns & 7, a.nr, src, dst, user)));
            break;
        }
        case 6: { // FANS-1/A CPDLC downlink (ARINC 622 envelope)
            std::string hex;
            for (int i = 0; i < 24; i++) { snprintf(b, sizeof b, "%X", (unsigned)(rng() & 15)); hex += b; }
            e.frames.push_back(acars(a, "AA", "/LHRCAYA.AT1." + a.reg + hex));
            break;
        }
        default:  // acknowledgement and a short text in one burst (two frames)
            e.frames.push_back(vdl2Frame(gsAddr(a.chan), airAddr(a, true), (uint8_t)(0x01 | ((a.nr & 7) << 5)), {}));
            e.frames.push_back(acars(a, "H1", "#DFB/STATUS OK"));
            break;
        }
        // the ground answers most I frames
        if (kind >= 1 && kind != 7 && rng() % 100 < 70) {
            Ev r{t, 2, (int)(&a - air.data()), {}};
            a.nr = (a.nr + 1) & 7;
            if (kind == 1 && rng() % 2) {
                const std::string up = "WX " + std::string(kDest[a.icao % 8]) + " 24012KT 9999 FEW030 18/09 Q1015";
                r.frames.push_back(vdl2Frame(airAddr(a), gsAddr(a.chan), vdl2ControlI(0, a.ns, false), vdl2AcarsInfo(a.reg, "C1", (char)('A' + a.msg % 20), up, 0x15)));
            } else r.frames.push_back(vdl2Frame(airAddr(a), gsAddr(a.chan, true), (uint8_t)(0x01 | ((a.ns & 7) << 5)), {}));
            r.t = t + 0.05 + uni(0, 0.1);
            q.push(std::move(r));
        }
        return e;
    }

    std::vector<Vdl2TxBurst> held;   // a few bursts ahead, so a burst that waited for its channel still comes out in time order
    Vdl2TxBurst next() {
        while (held.size() < 12) held.push_back(make());
        auto it = std::min_element(held.begin(), held.end(), [](const Vdl2TxBurst& x, const Vdl2TxBurst& y) { return x.t < y.t; });
        Vdl2TxBurst b = std::move(*it);
        held.erase(it);
        return b;
    }

    Vdl2TxBurst make() {
        while (true) {
            Ev e = q.top();
            q.pop();
            Vdl2TxBurst b;
            if (e.kind == 0) {
                Air& a = air[(size_t)e.who];
                const double dt = uni(3.5, 8.5) / std::max(0.1, o.rateFactor);
                move(a, dt);
                a.next = e.t + dt;
                q.push({a.next, 0, e.who, {}});
                Ev x = aircraftEvent(a, e.t);
                b.chan = a.chan; b.frames = std::move(x.frames); b.levelDb = a.levelDb; b.carOffHz = a.carOff;
            } else if (e.kind == 1) {
                Gs& g = gs[(size_t)e.who];
                g.next = e.t + uni(6, 10) / std::max(0.25, o.rateFactor);
                q.push({g.next, 1, e.who, {}});
                b.chan = e.who;
                b.frames.push_back(vdl2Frame({0xFFFFFF, 7, false}, gsAddr(e.who), kVdl2CtrlXID, vdl2XidGsif(g.lat, g.lon, g.airports)));
                b.carOffHz = g.carOff;
            } else {
                const Air& a = air[(size_t)e.who];
                b.chan = a.chan; b.frames = std::move(e.frames); b.carOffHz = gs[(size_t)a.chan].carOff;
            }
            const double dur = vdl2BurstSeconds(b.frames);
            b.t = std::max(e.t, busy[(size_t)b.chan] + o.guardSec + uni(0, 0.01));
            busy[(size_t)b.chan] = b.t + dur;
            if (b.chan < (int)o.levelDb.size()) b.levelDb += o.levelDb[(size_t)b.chan];
            return b;
        }
    }
};

Vdl2Traffic::Vdl2Traffic(const Vdl2GenOptions& o) : p_(new Impl(o)) {}
Vdl2Traffic::~Vdl2Traffic() = default;
Vdl2TxBurst Vdl2Traffic::next() { return p_->next(); }

// ---------------------------------------------------------------- the real-time generator

namespace {
class Vdl2Synth : public ModeSynth {
public:
    Vdl2Synth(const Vdl2GenOptions& o, const SynthConfig& c, double rate)
        : o_(o), rate_(rate), fsEff_(rate * (1.0 + c.sroPpm * 1e-6)), cfo_(c.cfoHz), traffic_(o), noise_(77 + o.seed) {
        const double amp0 = 0.08;
        amp0_ = amp0;
        const double psig = amp0 * amp0 * 0.85;
        sigma_ = c.snrDb >= 99 ? 0.0 : std::sqrt(psig / std::pow(10.0, c.snrDb / 10) * (rate / kVdl2SymbolRate) / 2);
        nextB_ = traffic_.next();
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override {
        std::normal_distribution<float> g(0.f, (float)sigma_);
        for (size_t i = 0; i < n; i++) out[i] = sigma_ > 0 ? cf32(g(noise_), g(noise_)) : cf32(0, 0);
        const double tEnd = (double)(pos_ + (int64_t)n) / fsEff_ + 0.01;
        while (nextB_.t < tEnd) {
            Vdl2Wave w = vdl2MakeWave(nextB_.frames);
            w.start = nextB_.t * fsEff_;
            const double f = o_.channelsHz[(size_t)nextB_.chan];
            w.carHz = f - o_.centerHz + nextB_.carOffHz + cfo_;
            w.amp = amp0_ * std::pow(10.0, nextB_.levelDb / 20);
            w.phase = (double)(noise_() % 6283) / 1000.0;
            if (o_.log) { std::lock_guard<std::mutex> lk(o_.log->mu); for (auto& fr : nextB_.frames) o_.log->sent.push_back({nextB_.t, f, fr}); }
            waves_.push_back(std::move(w));
            nextB_ = traffic_.next();
        }
        for (const auto& w : waves_) vdl2Render(w, out, pos_, n, fsEff_);
        pos_ += (int64_t)n;
        waves_.erase(std::remove_if(waves_.begin(), waves_.end(), [&](const Vdl2Wave& w) { return w.endSample(fsEff_) < (double)pos_; }), waves_.end());
    }
private:
    Vdl2GenOptions o_;
    double rate_, fsEff_, cfo_, sigma_ = 0, amp0_ = 0.08;
    Vdl2Traffic traffic_;
    std::mt19937 noise_;
    Vdl2TxBurst nextB_;
    std::vector<Vdl2Wave> waves_;
    int64_t pos_ = 0;
};
} // namespace

std::unique_ptr<ModeSynth> makeVdl2SynthEx(const Vdl2GenOptions& o, const SynthConfig& cfg, double sampleRate) {
    if (sampleRate <= 0) return nullptr;
    return std::make_unique<Vdl2Synth>(o, cfg, sampleRate);
}

std::unique_ptr<ModeSynth> makeVdl2Synth(const SynthConfig& cfg, double sampleRate) {
    Vdl2GenOptions o;
    if (cfg.modeOpt[0] > 0) o.aircraft = std::min(24, cfg.modeOpt[0]);
    if (cfg.modeOpt[1] > 0) o.seed = (uint32_t)cfg.modeOpt[1];
    if (cfg.modeOpt[2] == 1) o.channelsHz = {136.975e6};
    if (cfg.modeVal[0] > 0) o.rateFactor = cfg.modeVal[0];
    if (cfg.modeVal[1] > 0) { o.levelDb.assign(o.channelsHz.size(), 0.0); o.levelDb.back() = -cfg.modeVal[1]; }
    return makeVdl2SynthEx(o, cfg, sampleRate);
}

} // namespace dect2
