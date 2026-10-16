// VDL Mode 4 receiver: see vdl4_rx.h.
#include "dect2/vdl4/vdl4_rx.h"
#include "dect2/results/results_io.h"
#include "dect2/vdl4/vdl4_proto.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <ctime>
#include <mutex>
#include "dect2/common/mathconst.h"

namespace dect2 {

namespace {
using cd = std::complex<double>;
using dect2::math::kPi;
constexpr double kDefaultCenter = 136.85e6;

bool syncAt(const std::vector<uint8_t>& bits, size_t i) {
    if (i + kVdl4SyncBits > bits.size()) return false;
    for (int k = 0; k < kVdl4SyncBits; k++) if (bits[i + k] != kVdl4SyncPattern[k]) return false;
    return true;
}

struct Chan {
    double freqHz = 0, offHz = 0;
    bool inBand = false;
    cd rot = 1, step = 1;
    float floor = 0;
    Vdl4ChannelInfo info;
    std::vector<uint8_t> line;
    double lastDisc = 0;
    int discRun = 0;
};
} // namespace

std::vector<double> vdl4EuropeChannels() { return {136.725e6, 136.775e6, 136.825e6, 136.875e6, 136.900e6, 136.975e6}; }

struct Vdl4Receiver::Impl {
    std::mutex mu;
    double rate = 0, offset = 0, center = 0;
    std::vector<double> want = vdl4EuropeChannels();
    bool rebuild = true;
    std::function<void(const std::string&)> logCb;
    std::vector<Chan> ch;
    uint64_t nIn = 0;
    Vdl4Telemetry tel;
    uint64_t serial = 0, seq = 0;
    double lastGood = -1e9;

    void log(const std::string& s) { if (logCb) logCb(s); }

    void rebuildCh() {
        ch.clear();
        const double dial = center > 0 ? center : kDefaultCenter;
        for (double f : want) {
            Chan c;
            c.freqHz = f;
            c.offHz = f - dial - offset;
            c.inBand = std::fabs(c.offHz) < rate * 0.45;
            c.info.freqHz = f;
            c.info.inBand = c.inBand;
            const double w = 2 * kPi * c.offHz / rate;
            c.step = cd(std::cos(w), std::sin(w));
            ch.push_back(c);
        }
        rebuild = false;
    }

    void onFrame(Chan& c, const std::vector<uint8_t>& fr, float snr, float lvl, double cfo) {
        if (!vdl4FcsOk(fr.data(), fr.size())) { c.info.bad++; tel.blocksBad++; return; }
        Vdl4BurstFields bf;
        if (!vdl4ParseBurst(fr.data(), fr.size() - 2, bf)) { c.info.bad++; tel.blocksBad++; return; }
        Vdl4Message m;
        if (!vdl4DecodeMessage(bf, c.freqHz, m)) return;
        m.serial = ++serial;
        m.timeSec = tel.nowSec;
        m.wallTime = (int64_t)std::time(nullptr);
        m.levelDb = lvl;
        m.snrDb = snr;
        m.cfoHz = (float)cfo;
        tel.messages.insert(tel.messages.begin(), m);
        if (tel.messages.size() > kVdl4MsgMax) tel.messages.pop_back();
        tel.blocksOk++;
        c.info.good++;
        c.info.frames++;
        tel.dataValid = true;
        tel.snrDb = snr;
        lastGood = tel.nowSec;
        tel.state = 2;
        if (m.hasPos) {
            tel.positionsTotal++;
            auto& ac = tel.aircraft;
            auto it = std::find_if(ac.begin(), ac.end(), [&](const Vdl4Aircraft& a) { return a.addr == m.src; });
            if (it == ac.end()) {
                Vdl4Aircraft a;
                a.addr = m.src;
                ac.insert(ac.begin(), a);
                it = ac.begin();
            }
            it->messages++;
            it->freqHz = c.freqHz;
            it->lastHeardSec = tel.nowSec;
            it->lastWall = m.wallTime;
            it->levelDb = lvl;
            if (m.hasPos) {
                it->hasPos = true;
                it->lat = m.lat;
                it->lon = m.lon;
                it->posKind = m.posKind;
                it->posTime = tel.nowSec;
            }
            if (m.hasAlt) { it->hasAlt = true; it->altFt = m.altFt; }
        }
    }

    void demodChan(Chan& c, cf32 x) {
        c.rot *= c.step;
        if (++c.discRun > 256) { c.discRun = 0; const double n = std::abs(c.rot); if (n > 0) c.rot /= n; }
        const cd s(x.real(), x.imag());
        const cd m = s * c.rot;
        const double p = std::arg(m);
        const double d = p - c.lastDisc;
        c.lastDisc = p;
        double w = d;
        if (w > kPi) w -= 2 * kPi;
        if (w < -kPi) w += 2 * kPi;
        const uint8_t bit = w > 0 ? 1 : 0;
        c.line.push_back(bit);
        const float pwr = x.real() * x.real() + x.imag() * x.imag();
        c.floor = c.floor ? 0.995f * c.floor + 0.005f * pwr : pwr;
        if (c.line.size() > 8000) c.line.erase(c.line.begin(), c.line.begin() + 4000);
        for (size_t i = 0; i + kVdl4SyncBits + 80 < c.line.size(); i++) {
            if (!syncAt(c.line, i)) continue;
            std::vector<uint8_t> slice(c.line.begin() + i, c.line.end());
            std::vector<uint8_t> data;
            if (!vdl4BurstDataBits(slice, data)) continue;
            const auto frames = vdl4HdlcFrames(data);
            for (const auto& fr : frames) {
                const float snr = 10.f * std::log10(std::max(pwr / std::max(c.floor, 1e-12f), 1e-6f));
                onFrame(c, fr, snr, 10.f * std::log10(std::max(pwr, 1e-12f)), c.offHz);
            }
            c.line.erase(c.line.begin(), c.line.begin() + (long)i + kVdl4DataStartBit + 200);
            c.info.bursts++;
            c.info.active = true;
            c.info.lastHeardSec = tel.nowSec;
            c.info.levelDb = 10.f * std::log10(std::max(pwr, 1e-12f));
            break;
        }
    }
};

Vdl4Receiver::Vdl4Receiver() : p_(new Impl) {}
Vdl4Receiver::~Vdl4Receiver() = default;

void Vdl4Receiver::configure(double r) { std::lock_guard<std::mutex> lk(p_->mu); p_->rate = r; p_->rebuild = true; }
void Vdl4Receiver::setSignalOffset(double hz) { std::lock_guard<std::mutex> lk(p_->mu); if (p_->offset != hz) { p_->offset = hz; p_->rebuild = true; } }
void Vdl4Receiver::setCenterHz(double hz) { std::lock_guard<std::mutex> lk(p_->mu); if (p_->center != hz) { p_->center = hz; p_->rebuild = true; } }
bool Vdl4Receiver::ready() const { return p_->rate >= vdl4Tuning().minSampleRate - 1; }
void Vdl4Receiver::setLogCallback(std::function<void(const std::string&)> cb) { std::lock_guard<std::mutex> lk(p_->mu); p_->logCb = std::move(cb); }
void Vdl4Receiver::setChannels(const std::vector<double>& hz) { std::lock_guard<std::mutex> lk(p_->mu); if (hz != p_->want) { p_->want = hz; p_->rebuild = true; } }

void Vdl4Receiver::reset() {
    std::lock_guard<std::mutex> lk(p_->mu);
    for (auto& c : p_->ch) { c.line.clear(); c.info = Vdl4ChannelInfo(); c.info.freqHz = c.freqHz; c.info.inBand = c.inBand; }
    p_->tel.state = 0;
}

void Vdl4Receiver::feed(const cf32* x, size_t n) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->rebuild) p_->rebuildCh();
    if (!ready()) return;
    const double spb = p_->rate / kVdl4BitRate;
    const int dec = std::max(1, (int)std::floor(spb / 4));
    for (size_t i = 0; i < n; i++) {
        p_->tel.nowSec = p_->nIn / p_->rate;
        if ((p_->nIn++ % (uint64_t)dec) != 0) continue;
        for (auto& c : p_->ch) {
            if (!c.inBand) continue;
            p_->demodChan(c, x[i]);
        }
    }
    if (p_->tel.nowSec - p_->lastGood > 30) p_->tel.state = p_->tel.blocksOk ? 1 : 0;
    for (auto& c : p_->ch) {
        if (p_->tel.nowSec - c.info.lastHeardSec > 2) c.info.active = false;
        c.info.noiseDb = 10.f * std::log10(std::max(c.floor, 1e-12f));
    }
    p_->tel.channels.clear();
    for (const auto& c : p_->ch) p_->tel.channels.push_back(c.info);
    p_->tel.inputRate = p_->rate;
    p_->tel.centerHz = p_->center;
    p_->seq++;
}

void Vdl4Receiver::clearResults() {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->tel.messages.clear();
    p_->tel.aircraft.clear();
    p_->tel.stations.clear();
    p_->tel.blocksOk = p_->tel.blocksBad = p_->tel.positionsTotal = 0;
    p_->tel.dataValid = false;
}

std::string Vdl4Receiver::saveResults() const { return std::string(); }
bool Vdl4Receiver::loadResults(const std::string&) { return false; }

bool Vdl4Receiver::telemetry(Vdl4Telemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->seq == lastSeq) return false;
    out = p_->tel;
    out.seq = p_->seq;
    return true;
}

ModeTuning vdl4Tuning() {
    ModeTuning t;
    t.stdMode = 84;
    t.id = "vdl4";
    t.name = "VDL Mode 4";
    t.minMhz = 118;
    t.maxMhz = 140;
    t.defMhz = 136.850;
    t.sampleRate = 2000000;
    t.basebandHz = 1750000;
    t.bandwidthMhz = 0.4;
    t.minSampleRate = 250000;
    t.tuneOffsetHz = 0;
    return t;
}

} // namespace dect2
