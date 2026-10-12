// HFDL test signal (hfdl_gen.h)
#include "dect2/hfdl_gen.h"
#include "dect2/portable_rng.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace dect2 {

namespace {

constexpr int kSps = 8;            // samples per symbol of the stored waveform
constexpr int kSpan = 6;           // pulse half length in symbols
constexpr int kPulseRes = 128;

double rrc(double t, double a) {   // t in symbols; unit energy per symbol
    if (std::fabs(t) < 1e-9) return 1 - a + 4 * a / M_PI;
    if (std::fabs(std::fabs(t) - 1 / (4 * a)) < 1e-9) return a / std::sqrt(2.0) * ((1 + 2 / M_PI) * std::sin(M_PI / (4 * a)) + (1 - 2 / M_PI) * std::cos(M_PI / (4 * a)));
    return (std::sin(M_PI * t * (1 - a)) + 4 * a * t * std::cos(M_PI * t * (1 + a))) / (M_PI * t * (1 - 16 * a * a * t * t));
}
const std::vector<float>& pulse() {
    static std::vector<float> p;
    if (p.empty()) { p.resize(2 * kSpan * kPulseRes + 2); for (size_t i = 0; i < p.size(); i++) p[i] = (float)rrc((double)i / kPulseRes - kSpan, kHfdlRrcAlpha); }
    return p;
}
float pulseAt(double t) {          // t in symbols
    const double u = (t + kSpan) * kPulseRes;
    if (u < 0 || u >= 2 * kSpan * kPulseRes) return 0.f;
    const int i = (int)u; const float f = (float)(u - i);
    const auto& p = pulse();
    return p[(size_t)i] * (1 - f) + p[(size_t)i + 1] * f;
}
cf32 cubic(const std::vector<cf32>& x, double pos) {
    const int i = (int)std::floor(pos);
    const float f = (float)(pos - i);
    auto at = [&](int k) { return k >= 0 && k < (int)x.size() ? x[(size_t)k] : cf32(0, 0); };
    const cf32 a = at(i - 1), b = at(i), c = at(i + 1), d = at(i + 2);
    const cf32 c0 = b, c1 = 0.5f * (c - a), c2 = a - 2.5f * b + 2.f * c - 0.5f * d, c3 = 0.5f * (d - a) + 1.5f * (b - c);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

} // namespace

double hfdlBurstSeconds(int m1) { return (kHfdlPrekey + hfdlBurstSyms(m1)) / kHfdlBaud; }
double HfdlWave::endSample(double rate) const { return start + (double)bb.size() / (kSps * kHfdlBaud) * rate; }

HfdlWave hfdlMakeWave(int m1, const std::vector<uint8_t>& pdu, const std::vector<HfdlPath>& paths, uint32_t seed) {
    const std::vector<cf32> s = hfdlBurst(m1, pdu);
    double maxDelay = 0;
    for (const auto& p : paths) maxDelay = std::max(maxDelay, p.delaySec);
    const int extra = (int)std::ceil(maxDelay * kHfdlBaud * kSps) + 2;
    const size_t n = (s.size() + 2 * kSpan) * kSps;
    std::vector<cf32> x(n + (size_t)extra);
    for (size_t i = 0; i < n; i++) {
        const double t = (double)i / kSps - kSpan;
        const int k0 = (int)std::floor(t);
        cf32 v(0, 0);
        for (int k = k0 - kSpan; k <= k0 + kSpan + 1; k++) if (k >= 0 && k < (int)s.size()) v += s[(size_t)k] * pulseAt(t - k);
        x[i] = v;
    }
    HfdlWave w;
    if (paths.empty()) { w.bb = std::move(x); return w; }
    w.bb.assign(x.size(), cf32(0, 0));
    PortableRng rng(seed * 7919u + 13);   // the same fading on every platform
    const double fsb = kHfdlBaud * kSps;
    for (const auto& p : paths) {
        // fading: a sum of 12 sinusoids with Gaussian Doppler frequencies of the given two-sided spread
        constexpr int K = 12;
        double fd[K], ph[K];
        for (int k = 0; k < K; k++) { fd[k] = p.spreadHz > 0 ? rng.gauss(p.spreadHz / 2) : 0; ph[k] = rng.uniform() * 2 * M_PI; }
        const double g = std::pow(10.0, p.gainDb / 20);
        const double dS = p.delaySec * fsb;
        for (size_t i = 0; i < w.bb.size(); i++) {
            const double t = (double)i / fsb;
            cf32 h;
            if (p.spreadHz > 0) { std::complex<double> a(0, 0); for (int k = 0; k < K; k++) a += std::polar(1.0, 2 * M_PI * fd[k] * t + ph[k]); h = cf32(a / std::sqrt((double)K)); }
            else h = cf32(1, 0);
            h *= (float)g * std::polar(1.f, (float)(2 * M_PI * p.shiftHz * t));
            w.bb[i] += h * cubic(x, (double)i - dS);
        }
    }
    return w;
}

void hfdlRender(const HfdlWave& w, cf32* out, int64_t outStart, size_t n, double rate) {
    const double step = kSps * kHfdlBaud / rate;              // stored samples per output sample
    // bb[0] lies kSpan symbols before the first symbol, which is at output sample `start`
    const double off = w.start - kSpan * rate / kHfdlBaud;
    const double i0 = std::max(0.0, std::ceil(off - outStart));
    const double i1 = std::min((double)n, std::floor(off + w.bb.size() / step - outStart));
    if (i1 <= i0) return;
    const double w0 = 2 * M_PI * w.carHz / rate;
    for (size_t i = (size_t)i0; i < (size_t)i1; i++) {
        const double o = (double)((int64_t)i + outStart);
        const double pos = (o - off) * step;
        const double ph = w.phase + w0 * o;
        out[i] += (float)w.amp * cubic(w.bb, pos) * cf32((float)std::cos(ph), (float)std::sin(ph));
    }
}

// ---------------------------------------------------------------- traffic

struct HfdlTraffic::Impl {
    HfdlGenOptions o;
    std::mt19937 rng;
    struct Air { uint32_t icao; std::string reg, flight; int chan; int step = 0; int acId = 0; double lat, lon, dlat, dlon; double level, cfo; };
    std::vector<Air> air;
    std::vector<HfdlTxBurst> pending;
    int frame = 0;
    explicit Impl(const HfdlGenOptions& op) : o(op), rng(op.seed) {
        static const char* regs[] = {"N123UA", "G-VIIA", "C-FIUA", "EI-DUO", "D-AIMA", "A6-EUA", "JA736J", "VH-OQA", "B-LRA", "TF-FIA", "PH-BHA", "9V-SKA"};
        static const char* fls[] = {"UA0901", "BA0117", "AC0848", "EI0105", "LH0400", "EK0201", "JL0006", "QF0011", "CX0888", "FI0615", "KL0641", "SQ0026"};
        std::uniform_real_distribution<double> U(0, 1);
        for (int i = 0; i < std::max(1, o.aircraft); i++) {
            Air a;
            a.icao = 0xA00000u + (uint32_t)(rng() % 0x5FFFF) + (uint32_t)i * 0x1000u;
            a.reg = regs[i % 12]; a.flight = fls[i % 12];
            a.chan = i % (int)std::max<size_t>(1, o.channelsHz.size());
            a.acId = 10 + i;
            a.lat = 35 + 25 * U(rng); a.lon = -60 + 50 * U(rng);
            a.dlat = (U(rng) - 0.5) * 0.02; a.dlon = 0.03 + U(rng) * 0.02;
            a.level = -10 * U(rng);
            a.cfo = (U(rng) * 2 - 1) * o.cfoSpreadHz;
            air.push_back(a);
        }
    }
    int gsOf(int c) const { return c < (int)o.gsIds.size() ? o.gsIds[(size_t)c] : 1; }
    uint32_t fullMask(int gs) const { const HfdlStationInfo* s = hfdlStation(gs); return s ? (uint32_t)((1u << s->khz.size()) - 1) : 1u; }
    int fit(int m1, size_t len) const {
        if ((int)len <= hfdlPayloadOctets(m1)) return m1;
        const bool two = m1 >= 4;
        for (int r = m1 & 3; r < 4; r++) if ((int)len <= hfdlPayloadOctets(r + (two ? 4 : 0))) return r + (two ? 4 : 0);
        return 7;
    }
    void makeFrame() {
        const double t0 = frame * kHfdlFrameSec;
        const int utc = (int)(t0) % 86400 + 36000;
        for (int c = 0; c < (int)o.channelsHz.size(); c++) {
            const int gs = gsOf(c);
            HfdlSquitterSpec sq;
            sq.gsId = gs; sq.frameIndex = ((utc / 32) & 0xFFF); sq.mask = fullMask(gs);
            sq.other[0].id = gsOf((c + 1) % (int)o.channelsHz.size()); sq.other[0].mask = fullMask(sq.other[0].id);
            if (o.channelsHz.size() > 2) { sq.other[1].id = gsOf((c + 2) % (int)o.channelsHz.size()); sq.other[1].mask = fullMask(sq.other[1].id); }
            pending.push_back({t0 + 0.05 + 0.01 * c, c, 1, hfdlSpdu(sq), 0, 0});
            int slot = 1;
            const int want = std::max(1, (int)std::lround(3 * o.rateFactor));
            int done = 0;
            for (size_t k = 0; k < air.size() * (size_t)want && slot < 13 && done < want; k++) {
                Air& a = air[(k + (size_t)frame) % air.size()];
                if (a.chan != c) continue;
                std::vector<uint8_t> pdu;
                int m1 = 1;
                bool down = true;
                a.lat += a.dlat * 32; a.lon += a.dlon * 32;
                const int ut = utc + slot * 2;
                switch (a.step) {
                case 0: pdu = hfdlMpduDown(gs, 0, {hfdlLogonRequest(a.icao)}); m1 = 0; break;
                case 1: pdu = hfdlMpduUp(gs, 0, {hfdlLogonConfirm(a.icao, a.acId)}); m1 = 1; down = false; break;
                case 2: pdu = hfdlMpduDown(gs, a.acId, {hfdlUnnumbered(hfdlPerfData(a.flight, a.lat, a.lon, ut, gs, 0))}); m1 = 2; break;
                case 3: pdu = hfdlMpduDown(gs, a.acId, {hfdlUnnumbered(hfdlAcarsHfnpdu(a.reg, "H1", '2', "#DFB REQ WX " + a.flight + " FL350 M.84", true))}); m1 = 7; break;
                case 4: pdu = hfdlMpduDown(gs, a.acId, {hfdlUnnumbered(hfdlFreqData(a.flight, a.lat, a.lon, ut, {{gs, true, fullMask(gs)}, {gsOf((c + 1) % (int)o.channelsHz.size()), true, 3}}))}); m1 = 5; break;
                case 5: pdu = hfdlMpduUp(gs, a.acId, {hfdlUnnumbered(hfdlAcarsHfnpdu(a.reg, "QA", 'A', "WX OK. CONTINUE AS FILED", false))}); m1 = 6; down = false; break;
                case 6: pdu = hfdlMpduDown(gs, a.acId, {hfdlUnnumbered(hfdlAcarsHfnpdu(a.reg, "5Z", '3', "/B6 POS N" + std::to_string((int)a.lat) + " W" + std::to_string((int)-a.lon), true))}); m1 = 3; break;
                default: pdu = hfdlMpduUp(gs, a.acId, {hfdlUnnumbered(hfdlAcarsHfnpdu(a.reg, "C1", 'B', "MSG RCVD", false))}); m1 = 4; down = false; break;
                }
                a.step = a.step >= 7 ? 2 : a.step + 1;
                m1 = fit(m1, pdu.size());
                const int need = m1 >= 4 ? 2 : 1;
                if (slot + need > 13) break;
                pending.push_back({t0 + slot * kHfdlSlotSec + 0.05, c, m1, pdu, down ? a.level : 0, down ? a.cfo : 0});
                slot += need;
                done++;
            }
        }
        std::sort(pending.begin(), pending.end(), [](const HfdlTxBurst& a, const HfdlTxBurst& b) { return a.t > b.t; });   // the next at the back
        frame++;
    }
    HfdlTxBurst next() {
        if (pending.empty()) makeFrame();
        HfdlTxBurst b = std::move(pending.back());
        pending.pop_back();
        return b;
    }
};

HfdlTraffic::HfdlTraffic(const HfdlGenOptions& o) : p_(std::make_unique<Impl>(o)) {}
HfdlTraffic::~HfdlTraffic() = default;
HfdlTxBurst HfdlTraffic::next() { return p_->next(); }

// ---------------------------------------------------------------- the real-time generator

namespace {
class HfdlSynth : public ModeSynth {
public:
    HfdlSynth(const HfdlGenOptions& o, const SynthConfig& c, double rate)
        : o_(o), rate_(rate), fsEff_(rate * (1.0 + c.sroPpm * 1e-6)), cfo_(c.cfoHz), traffic_(o), noise_(77 + o.seed) {
        sigma_ = c.snrDb >= 99 ? 0.0 : std::sqrt(amp0_ * amp0_ / std::pow(10.0, c.snrDb / 10) * (rate / kHfdlBaud) / 2);
        nextB_ = traffic_.next();
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override {
        std::normal_distribution<float> g(0.f, (float)sigma_);
        for (size_t i = 0; i < n; i++) out[i] = sigma_ > 0 ? cf32(g(noise_), g(noise_)) : cf32(0, 0);
        const double tEnd = (double)(pos_ + (int64_t)n) / fsEff_ + 0.05;
        while (nextB_.t < tEnd) {
            HfdlWave w = hfdlMakeWave(nextB_.m1, nextB_.pdu, o_.paths, (uint32_t)(seedN_++));
            w.start = nextB_.t * fsEff_;
            const double f = o_.channelsHz[(size_t)nextB_.chan];
            w.carHz = f + kHfdlCarrierHz - o_.centerHz + nextB_.carOffHz + cfo_;
            w.amp = amp0_ * std::pow(10.0, nextB_.levelDb / 20);
            w.phase = (double)(noise_() % 6283) / 1000.0;
            if (o_.log) { std::lock_guard<std::mutex> lk(o_.log->mu); o_.log->sent.push_back({nextB_.t, f, nextB_.m1, nextB_.pdu}); }
            waves_.push_back(std::move(w));
            nextB_ = traffic_.next();
        }
        for (const auto& w : waves_) hfdlRender(w, out, pos_, n, fsEff_);
        pos_ += (int64_t)n;
        waves_.erase(std::remove_if(waves_.begin(), waves_.end(), [&](const HfdlWave& w) { return w.endSample(fsEff_) < (double)pos_; }), waves_.end());
    }
private:
    HfdlGenOptions o_;
    double rate_, fsEff_, cfo_, sigma_ = 0, amp0_ = 0.1;
    HfdlTraffic traffic_;
    std::mt19937 noise_;
    HfdlTxBurst nextB_;
    std::vector<HfdlWave> waves_;
    int64_t pos_ = 0;
    uint64_t seedN_ = 1;
};
} // namespace

std::unique_ptr<ModeSynth> makeHfdlSynthEx(const HfdlGenOptions& o, const SynthConfig& cfg, double sampleRate) {
    if (sampleRate <= 0) return nullptr;
    return std::make_unique<HfdlSynth>(o, cfg, sampleRate);
}

std::unique_ptr<ModeSynth> makeHfdlSynth(const SynthConfig& cfg, double sampleRate) {
    HfdlGenOptions o;
    if (cfg.modeOpt[0] > 0) o.aircraft = std::min(24, cfg.modeOpt[0]);
    if (cfg.modeOpt[1] > 0) o.seed = (uint32_t)cfg.modeOpt[1];
    if (cfg.modeOpt[2] == 1) o.paths = {{0, 0, 0.5, 0}, {0.001, -3, 0.5, 0}};
    if (cfg.modeOpt[2] == 2) o.paths = {{0, 0, 1.0, 0}, {0.002, -1, 1.0, 0}};
    if (cfg.modeVal[0] > 0) o.rateFactor = cfg.modeVal[0];
    return makeHfdlSynthEx(o, cfg, sampleRate);
}

} // namespace dect2
