// LBJ receiver: FM + POCSAG 1200 on the railway warning channel (~821.2375 MHz). Demodulation reuses the pagers POCSAG path.
#include "dect2/lbj/lbj_rx.h"
#include "dect2/lbj/lbj_proto.h"
#include "dect2/pager/pager_rx.h"
#include "dect2/pager/pager_tel.h"
#include "dect2/dsp/exact_resampler.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace dect2 {

ModeTuning lbjTuning() {
    ModeTuning t;
    t.stdMode = 83;
    t.id = "lbj";
    t.name = "LBJ";
    t.minMhz = 821.0;
    t.maxMhz = 821.5;
    t.defMhz = lbj::kDefaultMhz;
    t.sampleRate = 960000;
    t.basebandHz = 35000;
    t.bandwidthMhz = 0.035;
    t.minSampleRate = 256000;
    t.tuneOffsetHz = 50000;
    return t;
}

struct LbjReceiver::Impl {
    std::mutex mu;
    LbjTelemetry tel;
    std::function<void(const std::string&)> log;
    PagerReceiver pager;
    ExactResampler rs;
    std::vector<cf32> rsBuf;
    uint64_t pagerSeq = 0;
    std::unordered_set<uint64_t> seenPagerSerial;
    lbj::LbjSessionState sessions;
    double fs = 0, pagerFs = 0, offHz = 0, centreHz = 0;
    bool rateSet = false;

    static double pagerInputRate(double in) {
        constexpr double kPagerMin = 1000000.0;
        if (in >= kPagerMin - 1) return in;
        return 1200000.0;
    }

    void pushMessage(LbjMessage m) {
        m.wallTime = (int64_t)std::time(nullptr);
        if (log) {
            char line[320];
            snprintf(line, sizeof line, "LBJ %s km %s %s", m.train.c_str(), m.kmMarker.c_str(), m.speedKmh.c_str());
            log(line);
        }
        if (tel.messages.size() >= kLbjLogMax) tel.messages.pop_back();
        tel.messages.insert(tel.messages.begin(), std::move(m));
        tel.dataValid = true;
        tel.state = 2;
        tel.blocksOk++;
        tel.seq++;
    }

    void ingestPager(const PagerTelemetry& pt) {
        if (!pt.messages) return;
        for (const PagerMessage& pm : *pt.messages) {
            if (pm.flex || pm.speed != kPocsag1200) continue;
            if (seenPagerSerial.count(pm.serial)) continue;
            seenPagerSerial.insert(pm.serial);
            if (!lbj::isLbjPocsagAddress(pm.address)) continue;
            tel.blocksBad += pm.damaged ? 1 : 0;
            const std::string& bcd = !pm.numText.empty() ? pm.numText : pm.text;
            LbjMessage m;
            if (lbj::parsePocsagPage(pm.address, pm.function, bcd, pm.damaged, pm.timeSec, sessions, m)) pushMessage(std::move(m));
        }
        tel.levelDb = pt.levelDb;
        tel.cfoHz = pt.cfoHz;
        tel.snrDb = pt.snrDb;
        tel.blocks = pt.blocksOk + pt.blocksBad;
        if (tel.messages.empty() && pt.state >= 1) tel.state = 1;
    }
};

LbjReceiver::LbjReceiver() : p_(std::make_unique<Impl>()) {
    p_->pager.setProtocols(1);
    p_->pager.setPocsagBauds(2);
}
LbjReceiver::~LbjReceiver() = default;

void LbjReceiver::configure(double inputRateHz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->fs = inputRateHz;
    p_->rateSet = inputRateHz >= lbjTuning().minSampleRate - 1;
    p_->pagerFs = p_->rateSet ? Impl::pagerInputRate(inputRateHz) : 0;
    if (p_->rateSet) {
        p_->pager.configure(p_->pagerFs);
        if (std::fabs(p_->pagerFs - inputRateHz) > 1) {
            p_->rs.configure(inputRateHz, p_->pagerFs);
            p_->rs.reset();
        }
    }
    p_->tel.inputRate = inputRateHz;
    p_->tel.state = p_->rateSet ? 1 : 0;
}
void LbjReceiver::setSignalOffset(double hz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->offHz = hz;
    p_->pager.setSignalOffset(hz);
}
void LbjReceiver::setCenterHz(double hz) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->centreHz = hz;
    p_->tel.centerHz = hz;
}
bool LbjReceiver::ready() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return p_->rateSet && p_->pager.ready();
}
void LbjReceiver::reset() {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->pager.reset();
    p_->pagerSeq = 0;
    p_->seenPagerSerial.clear();
    p_->sessions = lbj::LbjSessionState();
    p_->tel = LbjTelemetry();
    p_->tel.inputRate = p_->fs;
    p_->tel.centerHz = p_->centreHz;
    p_->tel.state = p_->rateSet ? 1 : 0;
}
void LbjReceiver::clearMessages() {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->tel.messages.clear();
    p_->tel.dataValid = false;
    p_->tel.state = p_->rateSet ? 1 : 0;
    p_->tel.seq++;
}
void LbjReceiver::feed(const cf32* x, size_t n) {
    if (!x || !n) return;
    std::lock_guard<std::mutex> lk(p_->mu);
    if (!p_->rateSet) {
        p_->tel.state = 0;
        return;
    }
    const cf32* px = x;
    size_t pn = n;
    if (std::fabs(p_->pagerFs - p_->fs) > 1) {
        p_->rsBuf.clear();
        p_->rs.process(x, n, p_->rsBuf);
        px = p_->rsBuf.data();
        pn = p_->rsBuf.size();
    }
    p_->pager.feed(px, pn);
    PagerTelemetry pt;
    if (p_->pager.telemetry(pt, p_->pagerSeq)) {
        p_->pagerSeq = pt.seq;
        p_->tel.timeSec = pt.timeSec;
        p_->ingestPager(pt);
    } else if (p_->tel.timeSec < pt.timeSec)
        p_->tel.timeSec = pt.timeSec;
    p_->tel.blocks++;
}
bool LbjReceiver::telemetry(LbjTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->tel.seq <= lastSeq) return false;
    out = p_->tel;
    return true;
}
void LbjReceiver::clearResults() { clearMessages(); }
std::string LbjReceiver::saveResults() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    std::string s = "lbj1\n";
    for (const auto& m : p_->tel.messages) {
        s += m.train;
        s += '\t';
        s += m.kmMarker;
        s += '\t';
        s += m.loco;
        s += '\n';
    }
    return s;
}
bool LbjReceiver::loadResults(const std::string& blob) {
    if (blob.compare(0, 5, "lbj1\n") != 0) return false;
    std::vector<LbjMessage> msgs;
    size_t i = 5;
    while (i < blob.size() && msgs.size() < kLbjLogMax) {
        size_t e = blob.find('\n', i);
        if (e == std::string::npos) e = blob.size();
        const std::string line = blob.substr(i, e - i);
        size_t a = line.find('\t');
        size_t b = line.find('\t', a + 1);
        if (a != std::string::npos && b != std::string::npos) {
            LbjMessage m;
            m.train = line.substr(0, a);
            m.kmMarker = line.substr(a + 1, b - a - 1);
            m.loco = line.substr(b + 1);
            msgs.push_back(std::move(m));
        }
        i = e + 1;
    }
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->tel.messages = std::move(msgs);
    p_->tel.dataValid = !p_->tel.messages.empty();
    p_->tel.seq++;
    return true;
}
void LbjReceiver::setLogCallback(std::function<void(const std::string&)> cb) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->log = std::move(cb);
    p_->pager.setLogCallback({});
}
bool LbjReceiver::busy() const { return false; }

} // namespace dect2
