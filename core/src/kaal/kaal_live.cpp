// KAAL for the engine: see kaal_live.h.
#include "dect2/kaal/kaal_live.h"
#include "dect2/kaal/kaal_rx.h"
#include "dect2/kaal/kaal_beacon.h"
#include "dect2/kaal/kaal_tel.h"
#include "dect2/audio/audioout.h"
#include "dect2/dsp/fftutil.h"
#include "dect2/dsp/resampler.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    // The input goes to 48 kHz here, then a coarse carrier search (the two radios' oscillators differ by up to several kHz at UHF, more than the
    // receiver's own capture range): the centre of the 8 kHz wide signal in the averaged spectrum is mixed to 0 Hz while the receiver is not locked
    RationalResampler rs;
    bool resampling = false;
    std::vector<cf32> y48;
    Fft fft{1024};
    std::vector<float> win;
    std::vector<cf32> seg;                   // the segment being filled (1024 samples), hop 512
    std::vector<double> psd;
    int psdCount = 0;
    double carrier = 0;                      // Hz: the carrier offset taken out
    double mixPhase = 0;
    cf32 dcm{0.f, 0.f};                      // slow DC estimate: the RTL-SDR's spike at the tune centre
    bool locked = false;
    uint64_t sinceDecision = 0;
    double contrastDb = 0;
    bool decided = false, refined = false;
    int badRestarts = 0;
    double fine = 0, finePhase = 0;          // the voice receiver's residual carrier (Hz), once it is locked
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

    void coarseDecide() {
        // the window of 8 kHz (-6 dB width of the 8 ksym/s RRC signal) with the most power
        const int W = (int)std::lround(8000.0 / 48000.0 * 1024.0);   // bins
        std::vector<double> sm(1024, 0.0);
        double tot = 0;
        for (int b = 0; b < 1024; b++) tot += psd[(size_t)b];
        if (tot <= 0) return;
        double best = -1, sum = 0;
        int bestC = 0;
        std::vector<double> sc(1024);
        for (int b = 0; b < W; b++) sum += psd[(size_t)b];
        for (int c = 0; c < 1024; c++) {   // window [c - W/2, c + W/2) wrapped
            double acc = 0;
            for (int k = -W / 2; k < W - W / 2; k++) acc += psd[(size_t)((c + k + 1024) & 1023)];
            sc[(size_t)c] = acc;
            if (acc > best) { best = acc; bestC = c; }
        }
        std::vector<double> sorted(sc);
        std::nth_element(sorted.begin(), sorted.begin() + 512, sorted.end());
        const double med = sorted[512];
        contrastDb = med > 0 ? 10 * std::log10(best / med) : 0;
        if (contrastDb < 5.0) return;   // no signal of this width
        // the left edge of the signal: where the spectrum rises to -6 dB of its flat top (an 8 ksym/s root-raised-cosine signal is 6 dB down at +-4 kHz,
        // its Nyquist frequency). The right edge is not used: the beacon slice sits on it (4.5 kHz above the centre). A weighted mean of the bins
        // in the window gives the start, the crossing is found with a little smoothing and interpolated
        const int ext = (int)std::lround(2500.0 / 48000.0 * 1024.0);
        const double meanIn = best / (double)W;
        const double lvl = 0.25 * meanIn;
        auto sp = [&](int k) { double a = 0; for (int j = -2; j <= 2; j++) a += psd[(size_t)((k + j + 2048) & 1023)]; return a / 5.0; };
        int kk = bestC - W / 2 - ext;
        const int kEnd = bestC;
        while (kk < kEnd && sp(kk) < lvl) kk++;
        double cb;
        {
            const double p1 = sp(kk), p0 = sp(kk - 1);
            const double frac = p1 > p0 ? std::max(0.0, std::min(1.0, (lvl - p0) / (p1 - p0))) : 0.0;
            const double left = (double)(kk - 1) + frac;                  // bins
            cb = left + 4333.0 / 48000.0 * 1024.0;                        // the centre is 4.33 kHz above the -6 dB point (the PSD is the raised cosine, 25 % at 4.33 kHz)
        }
        double hz = cb * 48000.0 / 1024.0;
        if (hz >= 24000.0) hz -= 48000.0;
        if (!decided || std::fabs(hz - carrier) > 600.0) {   // a new carrier: what was gathered under the old one is useless, start the search again
            carrier = hz;
            decided = true;
            vrx.configure(48000.0);
            brx.configure(48000.0);
        }
    }

    // one block of 48 kHz samples: DC block, spectrum, mix, the receivers
    void process48(const cf32* x, size_t n) {
        const double dph = 2 * 3.141592653589793 * carrier / 48000.0;
        std::vector<cf32>& z = y48;
        std::vector<cf32> mixed(n);
        for (size_t i = 0; i < n; i++) {
            dcm += (x[i] - dcm) * 0.0008f;
            const cf32 v = x[i] - dcm;
            seg.push_back(v);
            if (seg.size() == 1024) {
                std::vector<cf32> f(1024);
                for (int k = 0; k < 1024; k++) f[(size_t)k] = seg[(size_t)k] * win[(size_t)k];
                fft.forward(f.data());
                const double a = psdCount ? 0.04 : 1.0;
                for (int k = 0; k < 1024; k++) psd[(size_t)k] += a * ((double)std::norm(f[(size_t)k]) - psd[(size_t)k]);
                psdCount++;
                seg.erase(seg.begin(), seg.begin() + 512);
            }
            mixPhase -= dph;
            if (mixPhase < -6.283185307179586) mixPhase += 6.283185307179586;
            if (mixPhase > 6.283185307179586) mixPhase -= 6.283185307179586;
            mixed[i] = v * cf32((float)std::cos(mixPhase), (float)std::sin(mixPhase));
        }
        (void)z;
        vrx.feed(mixed.data(), n);
        if (fine != 0.0) {   // the beacon receiver has little capture range: it gets the signal with the voice receiver's own carrier estimate taken out too
            const double dpf = 2 * 3.141592653589793 * fine / 48000.0;
            for (size_t i = 0; i < n; i++) {
                finePhase -= dpf;
                if (finePhase < -6.283185307179586) finePhase += 6.283185307179586;
                if (finePhase > 6.283185307179586) finePhase -= 6.283185307179586;
                mixed[i] *= cf32((float)std::cos(finePhase), (float)std::sin(finePhase));
            }
        }
        brx.feed(mixed.data(), n);
        {   // the sweep-only scanner reads the signals the voice receiver cannot lock to
            const bool vlocked = vrx.stats().locked;
            if (!vlocked) { scan.feed(mixed.data(), n); scanWasFed = true; }
            else if (scanWasFed) { scan.reset(); scanWasFed = false; }
            kaal::SweepIdResult ir;
            while (scan.poll(ir)) {
                scanId = ir;
                std::function<void(const std::string&)> lg;
                { std::lock_guard<std::mutex> pk(pubMu); lg = log; }
                if (lg) lg("KAAL sweep ID " + ir.callsign);
            }
        }
        sinceDecision += n;
        if (!locked && sinceDecision >= 24000 && psdCount > 20) { sinceDecision = 0; coarseDecide(); }
    }

    void startStream() {
        resampling = std::fabs(rate - 48000.0) > 1e-6;
        if (resampling) resampling = rs.configure(rate, 48000.0);
        if (resampling) rs.reset();
        win.resize(1024);
        for (int k = 0; k < 1024; k++) win[(size_t)k] = (float)(0.5 - 0.5 * std::cos(2 * 3.141592653589793 * k / 1024));
        seg.clear(); psd.assign(1024, 0.0); psdCount = 0; carrier = 0; fine = 0; finePhase = 0; decided = false; refined = false; badRestarts = 0; mixPhase = 0; dcm = cf32(0, 0); locked = false; sinceDecision = 0;
        vrx.configure(48000.0);
        brx.configure(48000.0);
        scan.configure(48000.0);
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
        if (st.locked && !refined && std::fabs((double)st.cfoHz) > 15.0) {
            // The coarse estimate (the spectrum edge) is good to a few hundred hertz; the receiver measures what is left to a few hertz at lock. Take it
            // out of the mixer and search once more: with no carrier error left the frames and the beacon decode cleanly.
            refined = true;
            carrier += (double)st.cfoHz;
            vrx.configure(48000.0);
            brx.configure(48000.0);
            fine = 0; locked = false;
            std::function<void(const std::string&)> lg;
            { std::lock_guard<std::mutex> pk(pubMu); lg = log; }
            if (lg) lg("KAAL carrier refined to " + std::to_string((long)std::lround(carrier)) + " Hz");
            return;
        }
        if (st.locked && refined && badRestarts < 4 && st.framesGood + st.framesBad >= 60 && st.framesBad * 100 > 15 * (st.framesGood + st.framesBad)) {
            // locked, but a sixth of the frames fail: the block alignment chosen from the window it happened to gather was the wrong one. A later window
            // may pick the right one: search again (the carrier is already right)
            badRestarts++;
            vrx.configure(48000.0);
            brx.configure(48000.0);
            fine = 0; locked = false;
            return;
        }
        locked = st.locked;
        if (!st.locked) fine = 0.0; else if (fine == 0.0 || std::fabs((double)st.cfoHz - fine) > 3.0) fine = (double)st.cfoHz;
        t.cfoHz = st.cfoHz + carrier; t.snrDb = st.snrDb; t.sweepDb = st.sweepDb;
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
    if (m.resampling) {
        m.y48.clear();
        m.rs.process(x, n, m.y48);
        if (!m.y48.empty()) m.process48(m.y48.data(), m.y48.size());
    } else {
        m.process48(x, n);
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
        const bool sameStation = m.beaconValid && m.bf.callsign == f.callsign;
        if (!text.empty() || !sameStation) m.btext = text;   // a header-only frame (every 2 s) must not wipe the bio of the last full frame (every 10 s)
        m.bf = f; m.beaconValid = true; m.beacons++;
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

void KaalLiveReceiver::setLogCallback(std::function<void(const std::string&)> cb) {
    p_->vrx.setLogCallback(cb);   // the receiver's own lines too (acquired, sync lost)
    std::lock_guard<std::mutex> lk(p_->pubMu);
    p_->log = std::move(cb);
}
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
ModeTuning kappTuning() {
    ModeTuning t;
    t.stdMode = 126; t.id = "kapp"; t.name = "KAPP (WIP)";
    t.minMhz = 30; t.maxMhz = 6000; t.defMhz = 433;
    t.sampleRate = 250000; t.bandwidthMhz = 0.1; t.minSampleRate = 125000;
    return t;
}
ModeTuning kaftTuning() {
    ModeTuning t;
    t.stdMode = 127; t.id = "kaft"; t.name = "KAFT (WIP)";
    t.minMhz = 100; t.maxMhz = 6000; t.defMhz = 1290;
    t.sampleRate = 2.4e6; t.bandwidthMhz = 1.0; t.minSampleRate = 1.25e6;
    return t;
}

} // namespace dect2
