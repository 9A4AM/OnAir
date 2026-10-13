// Whole-span decoding for the digital voice modes: see wide_modes.h.
#include "dect2/wide_modes.h"
#include "dect2/dmr_rx.h"
#include "dect2/dpmr_rx.h"
#include "dect2/dstar_rx.h"
#include "dect2/m17_rx.h"
#include "dect2/nxdn_rx.h"
#include "dect2/p25_rx.h"
#include "dect2/dpmr_tel.h"
#include "dect2/dstar_tel.h"
#include "dect2/m17_tel.h"
#include "dect2/nxdn_tel.h"
#include "dect2/p25_tel.h"
#include "dect2/t2rx.h"
#include "dect2/wide_bank.h"
#include <algorithm>
#include <cstdio>

namespace dect2 {

namespace {

constexpr size_t kLogCap = 200;

// newest first lists: stamp, append, order by start time, cap
template <class T> void appendNewestFirst(std::vector<T>& d, const std::vector<T>& s, double hz) {
    for (T c : s) { c.carrierId = 1; c.carrierHz = hz; d.push_back(std::move(c)); }
    std::stable_sort(d.begin(), d.end(), [](const T& a, const T& b) { return a.wallTime > b.wallTime; });
    if (d.size() > kLogCap) d.resize(kLogCap);
}

std::string fmt(const char* f, int v) { char b[48]; snprintf(b, sizeof b, f, v); return b; }

template <class Rx> void commonSetup(Rx& r, const std::function<void(const std::string&)>& log) {
    r.setSilent(true);
    if (log) r.setLogCallback(log);
}

template <class Rx> void audioSwitch(Rx& r, bool play) { r.setSilent(!play); r.setMuted(!play); }

} // namespace

struct WideModes::Impl {
    std::function<void(const std::string&)> log;
    std::unique_ptr<WideBank<DmrReceiver, DmrTelemetry>> dmr;
    std::unique_ptr<WideBank<M17Receiver, M17Telemetry>> m17;
    std::unique_ptr<WideBank<DstarReceiver, DstarTelemetry>> dstar;
    std::unique_ptr<WideBank<NxdnReceiver, NxdnTelemetry>> nxdn;
    std::unique_ptr<WideBank<DpmrReceiver, DpmrTelemetry>> dpmr;
    std::unique_ptr<WideBank<P25Receiver, P25Telemetry>> p25;
    bool muted = false;
    int sel = 0;

    template <class Rx, class Tel> WideOps<Rx, Tel> baseOps(double outRate) {
        // every single-channel receiver does its own channel filtering and decimation to 48 kHz, and some refuse an input below 1 Msps (DMR) or 250 ksps:
        // the carrier is handed over at that low rate, mixed to 0 Hz, with only the aliases of the narrowing removed here
        WideOps<Rx, Tel> o;
        o.outRate = outRate; o.occupiedHz = 60000;
        o.setup = [this](Rx& r) { commonSetup(r, log); };
        o.audio = [](Rx& r, bool play) { audioSwitch(r, play); };
        o.clear = [](Rx& r) { r.clearResults(); };
        return o;
    }
    Impl() {
        auto d = baseOps<DmrReceiver, DmrTelemetry>(1.2e6);
        d.clearLists = [](DmrTelemetry& t) { t.callLog.clear(); t.messages.clear(); };
        d.append = [](DmrTelemetry& t, const DmrTelemetry& s, double hz) {
            for (DmrCall c : s.callLog) { c.carrierId = 1; c.carrierHz = hz; t.callLog.push_back(c); }
            for (DmrMessage m : s.messages) { m.carrierId = 1; m.carrierHz = hz; t.messages.push_back(m); }
            if (t.callLog.size() > kLogCap) t.callLog.erase(t.callLog.begin(), t.callLog.end() - (long)kLogCap);
            if (t.messages.size() > kLogCap) t.messages.erase(t.messages.begin(), t.messages.end() - (long)kLogCap);
        };
        d.info = [](const DmrTelemetry& t) { return t.cc >= 0 ? fmt("CC %d", t.cc) + (t.link.empty() ? "" : " " + t.link) : t.link; };
        dmr.reset(new WideBank<DmrReceiver, DmrTelemetry>(d));

        auto m = baseOps<M17Receiver, M17Telemetry>(300000);
        m.clearLists = [](M17Telemetry& t) { t.log.clear(); };
        m.append = [](M17Telemetry& t, const M17Telemetry& s, double hz) { appendNewestFirst(t.log, s.log, hz); };
        m.info = [](const M17Telemetry& t) { return t.log.empty() ? std::string() : t.log.front().src; };
        m17.reset(new WideBank<M17Receiver, M17Telemetry>(m));

        auto s = baseOps<DstarReceiver, DstarTelemetry>(300000);
        s.clearLists = [](DstarTelemetry& t) { t.log.clear(); };
        s.append = [](DstarTelemetry& t, const DstarTelemetry& x, double hz) { appendNewestFirst(t.log, x.log, hz); };
        s.info = [](const DstarTelemetry& t) { return t.log.empty() ? std::string() : t.log.front().my; };
        dstar.reset(new WideBank<DstarReceiver, DstarTelemetry>(s));

        auto n = baseOps<NxdnReceiver, NxdnTelemetry>(300000);
        n.clearLists = [](NxdnTelemetry& t) { t.log.clear(); };
        n.append = [](NxdnTelemetry& t, const NxdnTelemetry& s2, double hz) { appendNewestFirst(t.log, s2.log, hz); };
        n.info = [](const NxdnTelemetry& t) { return t.ran >= 0 ? fmt("RAN %d", t.ran) : std::string(); };
        nxdn.reset(new WideBank<NxdnReceiver, NxdnTelemetry>(n));

        auto p = baseOps<DpmrReceiver, DpmrTelemetry>(300000);
        p.clearLists = [](DpmrTelemetry& t) { t.log.clear(); };
        p.append = [](DpmrTelemetry& t, const DpmrTelemetry& s2, double hz) { appendNewestFirst(t.log, s2.log, hz); };
        p.info = [](const DpmrTelemetry& t) { return t.cc >= 0 ? fmt("CC %d", t.cc) : std::string(); };
        dpmr.reset(new WideBank<DpmrReceiver, DpmrTelemetry>(p));

        auto q = baseOps<P25Receiver, P25Telemetry>(300000);
        q.clearLists = [](P25Telemetry& t) { t.log.clear(); };
        q.append = [](P25Telemetry& t, const P25Telemetry& s2, double hz) { appendNewestFirst(t.log, s2.log, hz); };
        q.info = [](const P25Telemetry& t) { return t.nac >= 0 ? fmt("NAC %03X", t.nac) : std::string(); };
        p25.reset(new WideBank<P25Receiver, P25Telemetry>(q));
    }
};

WideModes::WideModes() : p_(new Impl()) {}
WideModes::~WideModes() = default;

bool WideModes::supports(int m) { return m == 10 || m == 32 || m == 34 || m == 39 || m == 46 || m == 51; }

CarrierCfg WideModes::cfgFor(int m) {
    CarrierCfg c;
    c.stepHz = m == 46 ? 6250.0 : 12500.0;
    c.maxCarriers = 8;
    return c;
}

void WideModes::configure(int m, double rate, double origin) {
    CarrierCfg c = cfgFor(m); c.gridOriginHz = origin;
    switch (m) {
    case 10: p_->dmr->configure(rate, c); break;
    case 32: p_->m17->configure(rate, c); break;
    case 34: p_->dstar->configure(rate, c); break;
    case 39: p_->nxdn->configure(rate, c); break;
    case 46: p_->dpmr->configure(rate, c); break;
    case 51: p_->p25->configure(rate, c); break;
    }
}

void WideModes::reset() { p_->dmr->reset(); p_->m17->reset(); p_->dstar->reset(); p_->nxdn->reset(); p_->dpmr->reset(); p_->p25->reset(); }
void WideModes::clearResults() { p_->dmr->clearResults(); p_->m17->clearResults(); p_->dstar->clearResults(); p_->nxdn->clearResults(); p_->dpmr->clearResults(); p_->p25->clearResults(); }

void WideModes::feed(int m, const cf32* x, size_t n) {
    switch (m) {
    case 10: p_->dmr->feed(x, n); break;
    case 32: p_->m17->feed(x, n); break;
    case 34: p_->dstar->feed(x, n); break;
    case 39: p_->nxdn->feed(x, n); break;
    case 46: p_->dpmr->feed(x, n); break;
    case 51: p_->p25->feed(x, n); break;
    }
}

size_t WideModes::liveCount(int m) const {
    switch (m) {
    case 10: return p_->dmr->liveCount();
    case 32: return p_->m17->liveCount();
    case 34: return p_->dstar->liveCount();
    case 39: return p_->nxdn->liveCount();
    case 46: return p_->dpmr->liveCount();
    case 51: return p_->p25->liveCount();
    }
    return 0;
}

std::vector<double> WideModes::liveHz(int m) const {
    switch (m) {
    case 10: return p_->dmr->liveHz();
    case 32: return p_->m17->liveHz();
    case 34: return p_->dstar->liveHz();
    case 39: return p_->nxdn->liveHz();
    case 46: return p_->dpmr->liveHz();
    case 51: return p_->p25->liveHz();
    }
    return {};
}

bool WideModes::ready(int m) const {
    switch (m) {
    case 10: return p_->dmr->ready();
    case 32: return p_->m17->ready();
    case 34: return p_->dstar->ready();
    case 39: return p_->nxdn->ready();
    case 46: return p_->dpmr->ready();
    case 51: return p_->p25->ready();
    }
    return false;
}

void WideModes::setSelected(int id) {
    p_->sel = id;
    p_->dmr->setSelected(id); p_->m17->setSelected(id); p_->dstar->setSelected(id); p_->nxdn->setSelected(id); p_->dpmr->setSelected(id); p_->p25->setSelected(id);
}

void WideModes::setMuted(bool m) {
    p_->muted = m;
    p_->dmr->setMuted(m); p_->m17->setMuted(m); p_->dstar->setMuted(m); p_->nxdn->setMuted(m); p_->dpmr->setMuted(m); p_->p25->setMuted(m);
}

void WideModes::setLogCallback(std::function<void(const std::string&)> cb) { p_->log = std::move(cb); }

namespace {
template <class Bank, class Tel> bool fillTel(Bank& b, uint64_t& lastSeq, RxTelemetry& t, int stdm, Tel& slot, bool ready) {
    Tel mt; uint64_t seq = 0;
    if (!b.snapshot(mt, t.wide, lastSeq, &seq)) return false;
    lastSeq = seq;
    t.standard = stdm; t.seq = seq; t.state = mt.state; t.cfoHz = mt.cfoHz; t.dataValid = mt.dataValid; t.dataSnrDb = mt.snrDb;
    t.blocksOk = mt.blocksOk; t.blocksBad = mt.blocksBad; t.rateOk = ready;
    slot = std::move(mt);
    return true;
}
} // namespace

bool WideModes::telemetry(int m, RxTelemetry& t, uint64_t& lastSeq) {
    switch (m) {
    case 10: return fillTel(*p_->dmr, lastSeq, t, m, t.dmr, p_->dmr->ready());
    case 32: return fillTel(*p_->m17, lastSeq, t, m, t.m17, p_->m17->ready());
    case 34: return fillTel(*p_->dstar, lastSeq, t, m, t.dstar, p_->dstar->ready());
    case 39: return fillTel(*p_->nxdn, lastSeq, t, m, t.nxdn, p_->nxdn->ready());
    case 46: return fillTel(*p_->dpmr, lastSeq, t, m, t.dpmr, p_->dpmr->ready());
    case 51: return fillTel(*p_->p25, lastSeq, t, m, t.p25, p_->p25->ready());
    }
    return false;
}

} // namespace dect2
