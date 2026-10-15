// Wi-Fi beacon test signal (see wifi_gen.h).
#include "dect2/wifi_gen.h"
#include "dect2/wifi_proto.h"
#include "dect2/gen_util.h"
#include "dect2/source.h"
#include <cmath>
#include <deque>

namespace dect2 {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

struct WifiGenerator::Impl {
    WifiGenConfig cfg;
    genutil::NoiseSource noise;
    float sigma = 0;
    std::deque<cf32> q;
    size_t idx = 0;
    bool done = false;
    double ph = 0;

    explicit Impl(const WifiGenConfig& c) : cfg(c), noise(c.seed) {}

    void silence(double sec) { const size_t n = (size_t)std::llround(sec * cfg.rate); for (size_t i = 0; i < n; i++) q.push_back(cf32(0.f, 0.f)); }

    void refill() {
        if (done || cfg.frames.empty()) { silence(0.001); return; }
        const WifiGenFrame& fr = cfg.frames[idx];
        std::vector<uint8_t> mp = fr.mpdu;
        if (fr.badFcs && !mp.empty()) mp.back() ^= 0x10;
        const std::vector<uint8_t> bits = wifi::ppduBits(mp);
        // DBPSK: a one turns the phase by pi
        std::vector<int8_t> sym(bits.size());
        int s = 1;
        for (size_t i = 0; i < bits.size(); i++) { if (bits[i]) s = -s; sym[i] = (int8_t)s; }
        const double chipsPerSample = wifi::kChipRate * (1 + cfg.clockPpm * 1e-6) / cfg.rate;
        const size_t nChips = sym.size() * 11;
        const size_t ns = (size_t)std::ceil(nChips / chipsPerSample);
        const double off = cfg.cfoHz + fr.cfoHz;
        const double amp = cfg.amp * fr.ampScale;
        const double dph = 2 * kPi * off / cfg.rate;
        for (size_t i = 0; i < ns; i++) {
            const size_t k = (size_t)(i * chipsPerSample);
            const double v = k < nChips ? sym[k / 11] * wifi::kBarker[k % 11] : 0;
            ph = std::remainder(ph + dph, 2 * kPi);
            q.push_back(cf32((float)(amp * v * std::cos(ph)), (float)(amp * v * std::sin(ph))));
        }
        silence(fr.gapSec);
        if (++idx >= cfg.frames.size()) { idx = 0; if (!cfg.loop) done = true; }
    }
};

double WifiGenerator::noiseSigma(const WifiGenConfig& c) {
    if (c.snrDb >= 99) return 0;
    const double n20 = c.amp * c.amp / std::pow(10.0, c.snrDb / 10);   // noise power in 20 MHz
    return std::sqrt(n20 * c.rate / 20e6 / 2);
}

WifiGenerator::WifiGenerator(const WifiGenConfig& c) : p_(std::make_unique<Impl>(c)) {
    p_->sigma = (float)noiseSigma(c);
    p_->silence(c.leadSec);
}
WifiGenerator::~WifiGenerator() = default;

void WifiGenerator::generate(cf32* out, size_t n) {
    Impl& m = *p_;
    for (size_t i = 0; i < n; i++) {
        if (m.q.empty()) m.refill();
        out[i] = m.q.front();
        m.q.pop_front();
    }
    if (m.sigma > 0) m.noise.add(out, n, m.sigma);
}

std::vector<WifiGenFrame> wifiTestSchedule() {
    using namespace wifi;
    std::vector<WifiGenFrame> v;
    auto add = [&](std::vector<uint8_t> f, double gap, double cfo = 0, double amp = 1) {
        WifiGenFrame g;
        g.mpdu = std::move(f); g.gapSec = gap; g.cfoHz = cfo; g.ampScale = amp;
        v.push_back(std::move(g));
    };
    ApSpec home;
    const uint8_t hb[6] = {0x00, 0x04, 0x0E, 0x12, 0x34, 0x56};   // AVM
    std::copy(hb, hb + 6, home.bssid);
    home.ssid = "OnAir-Home"; home.channel = 6; home.security = Wpa2; home.tsf = 123456789; home.seq = 100;
    ApSpec cafe;
    const uint8_t cb[6] = {0xFC, 0xEC, 0xDA, 0xAB, 0xCD, 0xEF};   // Ubiquiti
    std::copy(cb, cb + 6, cafe.bssid);
    cafe.ssid = "OnAir-Cafe"; cafe.channel = 6; cafe.security = Wpa2Wpa3; cafe.tsf = 987654321; cafe.seq = 2000;
    ApSpec hidden;
    const uint8_t xb[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};   // locally administered
    std::copy(xb, xb + 6, hidden.bssid);
    hidden.ssid = "secret"; hidden.hidden = true; hidden.channel = 6; hidden.security = Open; hidden.gRates = false; hidden.interval = 200;
    const uint8_t sta[6] = {0x3A, 0x01, 0x02, 0x03, 0x04, 0x05};
    add(makeBeacon(home), 0.002, 0, 1.0);
    add(makeBeacon(cafe), 0.002, 12e3, 0.6);
    add(makeDataFrame(home.bssid, sta, 120), 0.001);
    add(makeBeacon(hidden), 0.002, -8e3, 0.8);
    add(makeBeacon(home, true, sta), 0.003);
    return v;
}

namespace {
class WifiSynth : public ModeSynth {
public:
    WifiSynth(const SynthConfig& s, double rate) : rate_(rate) {
        WifiGenConfig c;
        c.rate = rate;
        c.snrDb = s.snrDb;
        c.cfoHz = s.cfoHz;
        c.frames = wifiTestSchedule();
        gen_ = std::make_unique<WifiGenerator>(c);
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override { gen_->generate(out, n); }
private:
    double rate_;
    std::unique_ptr<WifiGenerator> gen_;
};
} // namespace

std::unique_ptr<ModeSynth> makeWifiSynth(const SynthConfig& cfg, double sampleRate) {
    if (sampleRate <= 0) return nullptr;
    return std::make_unique<WifiSynth>(cfg, sampleRate);
}

} // namespace dect2
