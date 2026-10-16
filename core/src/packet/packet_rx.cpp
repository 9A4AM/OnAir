// APRS / Packet receiver: one 25 kHz FM channel, AX.25 at 1200 baud (Bell 202 AFSK) and 9600 baud (G3RUH) decoded at the same time.
// Front end: mix out the signal offset, decimate to about 100 kHz, resample to exactly 96 kHz, channel filter, FM discriminator.
// 1200 baud: the audio goes to 48 kHz and three demodulators run side by side (tone correlators over one bit, with different windows, tone
// weights and clock-loop speeds - the way good TNCs do it); a frame is kept once even when several of them decode it.
// 9600 baud: two slicers with different filters, a clock loop, the G3RUH descrambler (x^17 + x^12 + 1) and NRZI.
// The frame layer is packet_ax25.*, the APRS layer packet_aprs.*.
#include "dect2/packet/packet_rx.h"
#include "dect2/results/results_io.h"
#include "dect2/net/feeds.h"
#include "dect2/dmr/dmr_dsp.h"
#include "dect2/dsp/exact_resampler.h"
#include "dect2/packet/packet_aprs.h"
#include "dect2/packet/packet_ax25.h"
#include "dect2/packet/packet_demod.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <deque>
#include "../ham_listen.h"
#include <mutex>
#include <unordered_map>

namespace dect2 {

namespace {

using pktdemod::kMidRate;
constexpr size_t kMaxFrames = 200, kMaxStations = 300;
constexpr int64_t kDupWindow = (int64_t)(0.03 * kMidRate);   // the same bytes this close together are one transmission
using pktdemod::Afsk1200;
typedef pktdemod::FskSlicer Fsk9600;

uint64_t hashBytes(const std::vector<uint8_t>& d) {
    uint64_t h = 1469598103934665603ULL;
    for (uint8_t b : d) { h ^= b; h *= 1099511628211ULL; }
    return h;
}

struct Rec { PacketStation s; };

} // namespace

struct PacketReceiver::Impl {
    std::mutex procMu, telMu;
    std::function<void(const std::string&)> log;

    double fs = 0;
    bool wideInput = false;                // setWideInput(): one carrier brought to 192 ksps by the whole-span bank (the radio's minimum rate does not apply)
    bool isReady = false;
    pktdemod::FmFront fe;
    hfdig::Listen listen;                  // "Listen": the demodulated audio, 8 kHz, on the sound card (off by default)
    dmr::Fir<float> audio8;                // 48 -> 8 kHz low-pass for Listen
    std::vector<float> a8;
    dmr::Fir<float> audio48;               // 96 -> 48 kHz low-pass for the 1200 baud path
    dmr::Fir<float> lp9600[2];
    Afsk1200 afsk[3];
    Fsk9600 fsk[2];
    std::vector<float> a48, f9[2];

    int64_t nextReport = 0;
    uint64_t seq = 0;

    // results
    struct Recent { uint64_t h; int64_t t; int baud; };
    std::deque<Recent> recent;
    struct Fail { int baud; int64_t t; bool carrier; };
    std::deque<Fail> fails;
    std::deque<PacketFrameInfo> frames;
    std::unordered_map<std::string, Rec> stations;
    PacketTelemetry t;
    int lastState = 0;
    PacketTelemetry pub;
    double lastNow = 0;                    // signal time of the last report: the kept lists move back by it when the clock starts again

    double now() const { return (double)fe.n96 / kMidRate; }
    void say(const std::string& s) { if (log) log(s); }

    void configure(double rate) {
        fs = rate;
        isReady = false;
        if (!fe.configure(fs, wideInput ? 192000.0 : packetTuning().minSampleRate)) return;
        audio8.design(dmr::lowpassTaps(3200.0, 800.0, 48000.0, 50, 9), 6);
        audio48.design(dmr::lowpassTaps(3300.0, 8000.0, kMidRate, 50, 9), 2);
        lp9600[0].design(dmr::lowpassTaps(5200.0, 11000.0, kMidRate, 40, 9), 1);
        lp9600[1].design(dmr::lowpassTaps(4000.0, 9000.0, kMidRate, 40, 9), 1);
        afsk[0].init(40, false, 1.0f, 0.70f, 0.80f);
        afsk[1].init(40, true, 0.7f, 0.75f, 0.85f);
        afsk[2].init(32, false, 1.5f, 0.80f, 0.88f);
        fsk[0].init(0.70f, 0.80f);
        fsk[1].init(0.75f, 0.85f);
        for (int i = 0; i < 3; i++) afsk[i].hdlc.setCallback([this, i](const std::vector<uint8_t>& dat, bool ok) { onRaw(1200, i, dat, ok, afsk[i].pos * 2); });
        for (int i = 0; i < 2; i++) fsk[i].hdlc.setCallback([this, i](const std::vector<uint8_t>& dat, bool ok) { onRaw(9600, i, dat, ok, fsk[i].pos); });
        isReady = true;
        resetState();
    }

    void resetState() {
        const double off = fe.offsetHz;
        fe.reset(); fe.offsetHz = off;
        audio48.reset(); audio8.reset(); listen.reset(); lp9600[0].reset(); lp9600[1].reset();
        for (auto& a : afsk) a.reset();
        for (auto& a : fsk) a.reset();
        nextReport = 0;
        recent.clear(); fails.clear();
        shiftTimes(-lastNow);              // the frames and stations stay (clearResults)
        lastNow = 0;
        const uint64_t s = seq;
        t = PacketTelemetry();
        t.inputRate = fs;
        t.last1200Sec = t.last9600Sec = t.lastAprsSec = -1e9;
        lastState = 0;
        std::lock_guard<std::mutex> lk(telMu);
        const uint64_t ps = pub.seq;
        pub = PacketTelemetry();
        pub.seq = std::max(s, ps);
        seq = pub.seq;
        pub.inputRate = fs;
        fillLists(pub);
        pub.seq = ++seq;
        say("APRS / Packet: listening for 1200 and 9600 baud AX.25");
    }

    void shiftTimes(double dt) {
        for (auto& f : frames) f.timeSec += dt;
        for (auto& kv : stations) kv.second.s.lastHeardSec += dt;
    }
    void fillLists(PacketTelemetry& o) const {
        o.frames.assign(frames.begin(), frames.end());
        std::vector<const PacketStation*> order;
        for (const auto& kv : stations) order.push_back(&kv.second.s);
        std::sort(order.begin(), order.end(), [](const PacketStation* a, const PacketStation* b) { return a->lastHeardSec > b->lastHeardSec; });
        o.stations.clear();
        for (const PacketStation* s : order) o.stations.push_back(*s);
    }
    void republish() {   // procMu held: the lists alone, with no signal to wait for
        std::lock_guard<std::mutex> lk(telMu);
        fillLists(pub);
        pub.seq = ++seq;
    }

    // ---- frames
    void onRaw(int baud, int variant, const std::vector<uint8_t>& dat, bool ok, int64_t tt) {
        if (!ok) {
            if (variant == 0) fails.push_back({baud, tt, fe.carrierRecent(0.2) && tt - fe.carrierSince > (int64_t)(0.05 * kMidRate)});         // one demodulator counts the failures; another one may still get the frame
            return;
        }
        const uint64_t h = hashBytes(dat);
        while (!recent.empty() && recent.front().t < tt - 4 * kDupWindow) recent.pop_front();
        for (const Recent& r : recent) if (r.h == h && r.baud == baud && std::llabs(r.t - tt) < kDupWindow) return;
        recent.push_back({h, tt, baud});
        accept(baud, dat, tt);
    }

    void flushFails(bool all) {
        while (!fails.empty() && (all || fails.front().t < fe.n96 - (int64_t)(1.3 * kMidRate))) {
            const Fail f = fails.front();
            fails.pop_front();
            if (!f.carrier) continue;                              // no carrier then: random bits of noise
            if (f.baud == 1200) t.bad1200++; else t.bad9600++;
            t.blocksBad++;
            if (t.blocksBad <= 20) say(std::string("APRS: a ") + std::to_string(f.baud) + " baud frame failed the check at " + std::to_string((double)f.t / kMidRate) + " s");
        }
    }

    void touchStation(const std::string& call, const aprs::Info* a, bool object, const std::string& via, double tnow) {
        auto it = stations.find(call);
        if (it == stations.end()) {
            if (stations.size() >= kMaxStations) {
                auto old = stations.begin();
                for (auto j = stations.begin(); j != stations.end(); ++j) if (j->second.s.lastHeardSec < old->second.s.lastHeardSec) old = j;
                stations.erase(old);
            }
            it = stations.emplace(call, Rec()).first;
            it->second.s.call = call;
            if (stations.size() <= 40) say("APRS: new station " + call);
        }
        PacketStation& s = it->second.s;
        s.lastHeardSec = tnow;
        s.count++;
        s.isObject = object;
        s.via = via;
        if (!a) return;
        if (a->hasPos) {
            s.hasPos = true; s.lat = a->lat; s.lon = a->lon;
            s.hasAlt = a->hasAlt; s.altM = a->altM;
            s.hasCourse = a->hasCourse; s.courseDeg = a->courseDeg;
            s.hasSpeed = a->hasSpeed; s.speedKnots = a->speedKnots;
            if (a->symTable) { s.symTable = a->symTable; s.symCode = a->symCode; }
            s.comment = a->comment;
        } else if (a->type == "Status") s.comment = a->text;
    }

    void accept(int baud, const std::vector<uint8_t>& dat, int64_t tt) {
        const double tnow = (double)tt / kMidRate;
        ax25::Frame f;
        if (!ax25::parseFrame(dat.data(), dat.size(), f)) { t.blocksBad++; if (baud == 1200) t.bad1200++; else t.bad9600++; return; }
        t.blocksOk++;
        if (baud == 1200) { t.ok1200++; t.last1200Sec = tnow; } else { t.ok9600++; t.last9600Sec = tnow; }
        aprs::Info a;
        const bool isAprs = f.isUi() && f.pid == 0xF0 && aprs::parse(f.to.call, f.info, a);
        PacketFrameInfo pf;
        pf.timeSec = tnow; pf.from = f.from.str(); pf.to = f.to.str(); pf.path = f.pathStr(); pf.info = f.info; pf.baud = baud;
        if (isAprs) {
            pf.type = a.type; pf.summary = a.summary; pf.hasPos = a.hasPos; pf.lat = a.lat; pf.lon = a.lon;
            t.aprsFrames++; t.lastAprsSec = tnow;
        } else {
            pf.type = f.typeName();
            pf.summary = "AX.25 " + f.typeName() + (f.info.empty() ? "" : ": " + f.info);
        }
        if (Feeds::get().wantAprs()) Feeds::get().aprs(dat, pf.from, pf.to, pf.path, f.info, f.isUi() && f.pid == 0xF0);   // the data feeds out (feeds.h)
        frames.push_back(pf);
        while (frames.size() > kMaxFrames) frames.pop_front();
        touchStation(pf.from, isAprs ? &a : nullptr, false, "", tnow);
        if (isAprs && (a.type == "Object" || a.type == "Item") && !a.name.empty()) touchStation(a.name, &a, true, pf.from, tnow);
        // a good frame explains a failure of another demodulator at the same moment (a failure is only counted 1.3 s after it, for this)
        for (auto it = fails.begin(); it != fails.end();) {
            // the other speed's demodulator sees rubbish while a frame of this speed is on the air: those are not failures either
            const bool same = it->baud == baud && std::llabs(it->t - tt) < (int64_t)(0.08 * kMidRate);
            const bool other = it->baud != baud && it->t <= tt && tt - it->t < (int64_t)(1.2 * kMidRate);
            if (same || other) it = fails.erase(it); else ++it;
        }
    }

    void publish() {
        flushFails(false);
        const double tnow = now();
        lastNow = tnow;
        t.timeSec = tnow;
        t.carrier = fe.carrierRecent(0.2);
        t.dataValid = tnow - std::max(t.last1200Sec, t.last9600Sec) < 15.0;
        t.state = t.dataValid ? 2 : (t.carrier || fe.carrierRecent(5) ? 1 : 0);
        t.cfoHz = fe.cfoHz();
        t.snrDb = (float)fe.snrEma;
        t.levelDb = fe.takeLevelDb();
        if (t.state != lastState) {
            say(t.state == 2 ? "APRS: decoding frames" : t.state == 1 ? "APRS: signal, no good frame lately" : "APRS: no signal");
            lastState = t.state;
        }
        PacketTelemetry o = t;
        fillLists(o);
        std::lock_guard<std::mutex> lk(telMu);
        o.seq = ++seq;
        pub = std::move(o);
    }

    void feed(const cf32* x, size_t n) {
        if (!isReady || n == 0) return;
        const std::vector<float>& disc = fe.process(x, n);
        a48.clear();
        audio48.process(disc.data(), disc.size(), a48);
        if (listen.on.load() && !a48.empty()) { a8.clear(); audio8.process(a48.data(), a48.size(), a8); listen.play(a8.data(), a8.size()); }
        if (!a48.empty()) for (auto& a : afsk) a.process(a48.data(), a48.size());
        for (int i = 0; i < 2; i++) {
            f9[i].clear();
            lp9600[i].process(disc.data(), disc.size(), f9[i]);
            if (!f9[i].empty()) fsk[i].process(f9[i].data(), f9[i].size());
        }
        fe.commit();
        while (fe.n96 >= nextReport) {
            nextReport += (int64_t)(0.25 * kMidRate);
            publish();
        }
    }
};

PacketReceiver::PacketReceiver() : p_(std::make_unique<Impl>()) {}
PacketReceiver::~PacketReceiver() = default;

void PacketReceiver::configure(double inputRateHz) {
    std::lock_guard<std::mutex> lk(p_->procMu);
    p_->configure(inputRateHz);
}
void PacketReceiver::setWideInput(bool on) {
    std::lock_guard<std::mutex> lk(p_->procMu);
    p_->wideInput = on;
    if (p_->fs > 0) p_->configure(p_->fs);
}
void PacketReceiver::setSignalOffset(double hz) {
    std::lock_guard<std::mutex> lk(p_->procMu);
    p_->fe.setOffset(hz);
}
bool PacketReceiver::ready() const {
    std::lock_guard<std::mutex> lk(p_->procMu);
    return p_->isReady;
}
void PacketReceiver::reset() {
    std::lock_guard<std::mutex> lk(p_->procMu);
    if (p_->isReady) p_->resetState();
}
void PacketReceiver::feed(const cf32* x, size_t n) {
    std::lock_guard<std::mutex> lk(p_->procMu);
    p_->feed(x, n);
}
bool PacketReceiver::telemetry(PacketTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->telMu);
    if (p_->pub.seq <= lastSeq) return false;
    out = p_->pub;
    return true;
}
void PacketReceiver::clearResults() {
    std::lock_guard<std::mutex> lk(p_->procMu);
    p_->frames.clear(); p_->stations.clear();
    p_->republish();
}
std::string PacketReceiver::saveResults() const {
    std::lock_guard<std::mutex> lk(p_->procMu);
    return results::pack(p_->lastNow, p_->frames, p_->stations);
}
bool PacketReceiver::loadResults(const std::string& blob) {
    double t0 = 0;
    std::deque<PacketFrameInfo> frames;
    std::unordered_map<std::string, Rec> stations;
    if (!results::unpack(blob, t0, frames, stations)) return false;
    std::lock_guard<std::mutex> lk(p_->procMu);
    p_->frames = std::move(frames); p_->stations = std::move(stations);
    p_->shiftTimes(p_->lastNow - t0);
    p_->republish();
    return true;
}
void PacketReceiver::setListen(bool on) { p_->listen.on = on; }
bool PacketReceiver::listening() const { return p_->listen.on.load(); }
void PacketReceiver::setVolume(float v) { p_->listen.volume = std::max(0.f, std::min(1.f, v)); }
void PacketReceiver::setMuted(bool m) { p_->listen.muted = m; }
void PacketReceiver::setSilent(bool s) { p_->listen.silent = s; }
void PacketReceiver::setListenTap(std::function<void(const float* x, size_t n)> cb) { p_->listen.setTap(std::move(cb)); }
void PacketReceiver::setLogCallback(std::function<void(const std::string&)> cb) {
    std::lock_guard<std::mutex> lk(p_->procMu);
    p_->log = std::move(cb);
}

ModeTuning packetTuning() {
    ModeTuning t;
    t.stdMode = 26; t.id = "packet"; t.name = "APRS / Packet";
    t.minMhz = 25; t.maxMhz = 1000; t.defMhz = 144.8;
    t.sampleRate = 2000000;
    t.basebandHz = 1750000;
    t.bandwidthMhz = 0.025;
    t.minSampleRate = 1000000;
    t.tuneOffsetHz = 50000;                  // the radio sits 50 kHz above the channel, away from its DC spike (as Inmarsat-C and Aero)
    return t;
}

} // namespace dect2
