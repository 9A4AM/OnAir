// KAAL for the engine: see kaal_live.h.
#include "dect2/kaal/kaal_live.h"
#include "dect2/kaal/kaal_rx.h"
#include "dect2/kaal/kaal_beacon.h"
#include "dect2/kaal/kaal_tel.h"
#include "dect2/audio/audioout.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace dect2 {

struct KaalLiveReceiver::Impl {
    std::mutex feedMu;                       // the processing state below
    std::mutex pubMu;                        // the published telemetry and the callbacks
    kaal::VoiceRx vrx;
    kaal::BeaconSliceRx brx;
    kaal::SweepIdScanner scan;               // station ID from the sweep alone, while the voice receiver is not locked
    kaal::SweepIdResult scanId;              // the last ID the scanner found (kept while the voice receiver has the signal)
    bool scanWasFed = false;
    double rate = 0;
    bool ready = false;
    uint64_t samples = 0, nextReport = 0, seq = 0;
    float level = 0;                         // decaying peak of the speech, 0 .. 1
    float lastSample = 0;                    // for the 16 kHz to 48 kHz interpolation
    std::vector<int16_t> pcm;
    std::vector<float> stereo;
    // the last beacon
    bool beaconValid = false;
    uint64_t beacons = 0;
    double beaconAtSec = 0;
    kaal::BeaconFields bf;
    std::string btext;
    // sound
    std::atomic<float> volume{1.f};
    std::atomic<bool> muted{false}, silent{false};
    std::unique_ptr<AudioOut> audioOut;
    std::function<void(const std::string&)> log;
    std::function<void(const int16_t*, size_t)> tap;
    KaalTelemetry pub;
    bool wasLocked = false;

    void play(const int16_t* x, size_t n) {
        if (n == 0) return;
        stereo.resize(n * 3 * 2);
        float peak = 0;
        size_t o = 0;
        for (size_t i = 0; i < n; i++) {
            const float cur = (float)x[i] / 32768.f;
            peak = std::max(peak, std::fabs(cur));
            for (int k = 1; k <= 3; k++) {
                const float v = lastSample + (cur - lastSample) * (float)k / 3.f;
                stereo[o++] = v; stereo[o++] = v;
            }
            lastSample = cur;
        }
        level = std::max(peak, level * 0.9f);
        if (silent.load()) return;
        if (!audioOut) { audioOut = std::make_unique<AudioOut>(); audioOut->start(48000); audioOut->setStartThreshold(48000 / 5); }
        audioOut->setVolume(volume.load());
        audioOut->setMuted(muted.load());
        if (audioOut->bufferedFrames() > 48000 * 3 / 2) audioOut->flush();
        audioOut->write(stereo.data(), (int)(stereo.size() / 2));
    }

    void startStream() {
        vrx.configure(rate);
        brx.configure(rate);
        scan.configure(rate);
        scanId = kaal::SweepIdResult();
        scanWasFed = false;
        samples = 0; nextReport = (uint64_t)(0.25 * rate); level = 0; lastSample = 0;
        beaconValid = false; beacons = 0; beaconAtSec = 0; btext.clear(); bf = kaal::BeaconFields();
        wasLocked = false;
    }

    void report() {
        const kaal::RxStats st = vrx.stats();
        KaalTelemetry t;
        t.seq = ++seq;
        t.state = st.locked ? 2 : samples ? 1 : 0;
        t.cfoHz = st.cfoHz; t.snrDb = st.snrDb; t.sweepDb = st.sweepDb;
        t.audioLevel = level;
        level *= 0.7f;
        t.blocksOk = st.framesGood; t.blocksBad = st.framesBad;
        t.bitrateKbps = st.bitrateKbps;
        if (st.idValid) { t.sweepIdValid = true; t.sweepId = st.idCall; t.sweepIdGateDb = st.idGateDb; }
        else if (scanId.valid) { t.sweepIdValid = true; t.sweepId = scanId.callsign; t.sweepIdGateDb = scanId.gateDb; }
        t.dataValid = st.locked && st.framesGood > 0;
        t.inputRate = rate;
        t.timeSec = rate > 0 ? (double)samples / rate : 0;
        t.beaconValid = beaconValid; t.beacons = beacons;
        t.beaconAgeSec = beaconValid ? std::max(0.0, t.timeSec - beaconAtSec) : 0;
        if (beaconValid) {
            t.callsign = bf.callsign; t.grid = bf.grid; t.text = btext;
            t.lat = bf.lat; t.lon = bf.lon; t.powerDbm = bf.powerDbm; t.linkQuality = bf.linkQuality;
        }
        std::function<void(const std::string&)> lg;
        {
            std::lock_guard<std::mutex> lk(pubMu);
            pub = t;
            lg = log;
        }
        if (lg && st.locked != wasLocked) lg(st.locked ? "KAAL locked" : "KAAL lock lost");
        wasLocked = st.locked;
    }
};

KaalLiveReceiver::KaalLiveReceiver() : p_(std::make_unique<Impl>()) {}
KaalLiveReceiver::~KaalLiveReceiver() = default;

void KaalLiveReceiver::configure(double r) {
    Impl& m = *p_;
    std::lock_guard<std::mutex> lk(m.feedMu);
    m.rate = r;
    m.ready = r >= kaalTuning().minSampleRate - 1;
    if (m.ready) m.startStream();
    std::lock_guard<std::mutex> pk(m.pubMu);
    m.pub = KaalTelemetry();
    m.pub.seq = ++m.seq;
    m.pub.inputRate = r;
}

bool KaalLiveReceiver::ready() const {
    std::lock_guard<std::mutex> lk(p_->feedMu);
    return p_->ready;
}

void KaalLiveReceiver::reset() {
    Impl& m = *p_;
    std::lock_guard<std::mutex> lk(m.feedMu);
    if (m.ready) m.startStream();
    std::lock_guard<std::mutex> pk(m.pubMu);
    m.pub = KaalTelemetry();
    m.pub.seq = ++m.seq;
    m.pub.inputRate = m.rate;
}

void KaalLiveReceiver::feed(const cf32* x, size_t n) {
    Impl& m = *p_;
    if (n == 0) return;
    std::lock_guard<std::mutex> lk(m.feedMu);
    if (!m.ready) return;
    m.vrx.feed(x, n);
    m.brx.feed(x, n);
    {   // the sweep-only scanner reads the signals the voice receiver cannot lock to
        const bool locked = m.vrx.stats().locked;
        if (!locked) { m.scan.feed(x, n); m.scanWasFed = true; }
        else if (m.scanWasFed) { m.scan.reset(); m.scanWasFed = false; }
        kaal::SweepIdResult ir;
        while (m.scan.poll(ir)) {
            m.scanId = ir;
            std::function<void(const std::string&)> lg;
            { std::lock_guard<std::mutex> pk(m.pubMu); lg = m.log; }
            if (lg) lg("KAAL sweep ID " + ir.callsign);
        }
    }
    m.samples += n;
    m.pcm.resize(4096);
    for (;;) {
        const size_t k = m.vrx.popAudio(m.pcm.data(), m.pcm.size());
        if (k == 0) break;
        if (m.tap) m.tap(m.pcm.data(), k);
        m.play(m.pcm.data(), k);
    }
    kaal::BeaconFields f;
    std::string text;
    while (m.brx.poll(f, text)) {
        m.bf = f; m.btext = text; m.beaconValid = true; m.beacons++;
        m.beaconAtSec = m.rate > 0 ? (double)m.samples / m.rate : 0;
        std::function<void(const std::string&)> lg;
        { std::lock_guard<std::mutex> pk(m.pubMu); lg = m.log; }
        if (lg) lg("KAAL beacon " + f.callsign + " " + f.grid);
    }
    if (m.samples >= m.nextReport) {
        m.nextReport = m.samples + (uint64_t)(0.25 * m.rate);
        m.report();
    }
}

bool KaalLiveReceiver::telemetry(KaalTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->pubMu);
    if (p_->pub.seq == 0 || p_->pub.seq == lastSeq) return false;
    out = p_->pub;
    return true;
}

void KaalLiveReceiver::setLogCallback(std::function<void(const std::string&)> cb) { std::lock_guard<std::mutex> lk(p_->pubMu); p_->log = std::move(cb); }
void KaalLiveReceiver::setVolume(float v) { p_->volume = std::max(0.f, std::min(1.f, v)); }
void KaalLiveReceiver::setMuted(bool m) { p_->muted = m; }
void KaalLiveReceiver::setSilent(bool s) { p_->silent = s; }
void KaalLiveReceiver::setAudioTap(std::function<void(const int16_t*, size_t)> cb) { std::lock_guard<std::mutex> lk(p_->feedMu); p_->tap = std::move(cb); }

ModeTuning kaalTuning() {
    ModeTuning t;
    t.stdMode = 124; t.id = "kaal"; t.name = "KAAL";
    t.minMhz = 0.1; t.maxMhz = 6000; t.defMhz = 433.0;   // modeTuning() opens the range to any frequency; the signal is 10 kHz wide
    t.sampleRate = 240000;        // the radio rate asked for (5 times 48 kHz); the receiver resamples to 48 kHz itself
    t.basebandHz = 0;
    t.bandwidthMhz = 0.012;
    t.minSampleRate = 48000;
    t.tuneOffsetHz = 0;
    return t;
}

ModeTuning kavbTuning() {
    ModeTuning t;
    t.stdMode = 125; t.id = "kavb"; t.name = "KAVB (WIP)";
    t.minMhz = 100; t.maxMhz = 6000; t.defMhz = 1290;   // amateur UHF / SHF video; modeTuning() opens the range
    t.sampleRate = 2.4e6; t.bandwidthMhz = 2.0; t.minSampleRate = 2e6;
    return t;
}
} // namespace dect2
