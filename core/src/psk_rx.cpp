// PSK31 / PSK63 multi-signal receiver (see psk_rx.h).
#include "dect2/psk_rx.h"
#include "dect2/psk_tel.h"
#include "dect2/psk_varicode.h"
#include "dect2/cw_morse.h"
#include "dect2/marine_dsp.h"
#include "dect2/fftutil.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <mutex>
#include <utility>
#include <vector>

namespace dect2 {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kBase = marine::kBaseRate;      // 24 kHz complex
constexpr int kN = 4096, kHop = 2048;            // window spectrum
constexpr double kBinHz = kBase / kN;
constexpr double kLoHz = 100, kHiHz = 3900;      // the audio passband searched
constexpr int kLoBin = (int)(kLoHz / kBinHz) + 1, kHiBin = (int)(kHiHz / kBinHz);
constexpr int kR = 24;                           // 24 kHz -> 1 kHz
constexpr int kTri = 2 * kR - 1;
constexpr double kRate1 = kBase / kR;            // 1 kHz

struct Chan {
    int id = 0;
    double f = 0, f0 = 0;        // offset from the dial now, and where it was found
    bool manual = false;
    std::complex<double> rot{1, 0}, step{1, 0};
    int rotCount = 0;
    cf32 tri[2 * kTri] = {};
    int triPos = 0, phase = 0;
    std::vector<cf32> mf;        // matched filter history (twice, so a window never wraps)
    int mfPos = 0;
    // symbol clock
    std::vector<float> bins;
    int ph = 0, idx = 0, since = 0, syms = 0;
    std::vector<cf32> yh;        // the last symbol's worth of filter outputs (the Gardner mid sample)
    int yhPos = 0;
    cf32 lastSym{0, 0}, sym1{0, 0}, sym2{0, 0}, lastSym2{0, 0};   // the last symbols (the QPSK equaliser)
    float pw = 0, gacc = 0;
    std::complex<float> prevY{0, 0};
    bool havePrev = false;
    float q = 0.2f;              // phase decision quality: 1 clean, 0 noise
    float errVar = 1;            // mean square phase error: the SNR (Es/N0 = 1 / variance for differential detection)
    bool dcd = false;
    psk::Viterbi vit;
    psk::VaricodeRx vrx;
    std::string text;
    std::vector<PskLine> lines;
    bool lineOpen = false;
    std::vector<std::string> calls;
    double firstSec = 0, lastActive = 0, lastPeak = 0;
    uint64_t chars = 0, bad = 0;
    void setFreq(double hz) { f = hz; step = std::polar(1.0, -2 * kPi * hz / kBase); }
};

} // namespace

struct PskReceiver::Impl {
    std::mutex mu;
    std::function<void(const std::string&)> log;
    double rate = 0, offsetHz = 0;
    PskTelemetry pub;
    std::atomic<bool> resetReq{true}, offsetReq{false}, clearReq{false};
    bool focusReq = false; double focusHz = 0;   // under mu
    std::atomic<float> thrDb{8.f};
    std::atomic<int> modeReq{0};
    // receiver thread
    PskTelemetry tel;
    double curRate = 0;
    bool running = false;
    int mode = 0, sps = 32;
    std::vector<float> h;        // matched filter
    int64_t nIn = 0, nBase = 0;
    double nextReport = 0;
    double power = 0; int64_t nPower = 0;
    marine::Front front;
    std::vector<cf32> base, fftIn;
    Fft fft{kN};
    std::vector<float> win, held, acc;
    int accN = 0, frames = 0;
    float floorLin = 0;
    float triW[kTri] = {};
    std::vector<Chan> chans;
    int nextId = 1, focusId = -1;

    Impl() {
        win.resize(kN);
        for (int i = 0; i < kN; i++) win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2 * kPi * i / kN));
        for (int j = 0; j < kTri; j++) triW[j] = (float)((kR - std::abs(j - (kR - 1))) / (double)(kR * kR));
    }

    double sigSec() const { return (double)nBase / kBase; }
    double baud() const { return pskBaud(mode); }

    void setupMode(int m) {
        mode = std::max(0, std::min(kPskModes - 1, m));
        sps = (int)std::lround(kRate1 / baud());
        const int L = 2 * sps;
        h.assign((size_t)L, 0.f);
        double s = 0;
        for (int i = 0; i < L; i++) { h[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2 * kPi * (i + 0.5) / L)); s += h[(size_t)i]; }
        for (float& v : h) v = (float)(v / s);
        chans.clear(); focusId = -1;
        tel.mode = mode;
    }

    void resetState() {
        std::function<void(const std::string&)> cb;
        double off;
        { std::lock_guard<std::mutex> lk(mu); curRate = rate; off = offsetHz; cb = log; }
        const uint64_t s = tel.seq, ss = tel.specSeq;
        tel = PskTelemetry();
        tel.seq = s; tel.specSeq = ss;
        tel.inputRate = curRate;
        tel.loHz = kLoBin * kBinHz; tel.hiHz = kHiBin * kBinHz;
        nIn = nBase = 0; nextReport = 0; power = 0; nPower = 0;
        running = curRate >= pskTuning().minSampleRate - 1;
        if (running) { front.configure(curRate); front.setOffsetHz(off); }
        base.clear(); fftIn.clear();
        const int W = kHiBin - kLoBin + 1;
        held.assign((size_t)W, 0.f); acc.assign((size_t)W, 0.f); accN = 0; frames = 0; floorLin = 0;
        setupMode(modeReq.load());
        if (cb && curRate > 0) cb(running ? std::string("PSK: looking for ") + pskModeName(mode) + " signals from dial + 100 Hz to dial + 3.9 kHz"
                                          : "PSK: the sample rate is too low (250 kHz at least)");
    }

    Chan* find(double hz, double tol) {
        Chan* best = nullptr;
        double bd = tol;
        for (Chan& c : chans) if (std::fabs(c.f - hz) <= bd) { bd = std::fabs(c.f - hz); best = &c; }
        return best;
    }

    Chan& add(double hz, bool manual) {
        Chan c;
        c.id = nextId++;
        c.manual = manual;
        c.setFreq(hz); c.f0 = hz;
        c.mf.assign((size_t)(4 * sps), cf32(0, 0));
        c.bins.assign((size_t)sps, 0.f);
        c.yh.assign((size_t)sps, cf32(0, 0));
        c.firstSec = c.lastActive = c.lastPeak = sigSec();
        chans.push_back(std::move(c));
        return chans.back();
    }

    // the smoothed held spectrum at bin i (over the signal's width)
    float smooth(const std::vector<float>& v, int i, int w) const {
        float s = 0; int n = 0;
        for (int j = std::max(0, i - w); j <= std::min((int)v.size() - 1, i + w); j++) { s += v[(size_t)j]; n++; }
        return n ? s / n : 0.f;
    }

    void spectrumFrame() {
        std::vector<cf32> b(kN);
        for (int i = 0; i < kN; i++) b[(size_t)i] = fftIn[(size_t)i] * win[(size_t)i];
        fft.forward(b.data());
        const int W = kHiBin - kLoBin + 1;
        for (int i = 0; i < W; i++) {
            const float p = std::norm(b[(size_t)(kLoBin + i)]) * (4.f / ((float)kN * kN));
            float& hh = held[(size_t)i];
            hh += (p > hh ? 0.5f : 0.05f) * (p - hh);
            acc[(size_t)i] += p;
        }
        accN++;
        std::vector<float> srt(held);
        std::nth_element(srt.begin(), srt.begin() + W / 2, srt.end());
        floorLin = srt[(size_t)W / 2];
        if (++frames < 3) return;
        const int w = std::max(1, (int)std::lround(0.6 * baud() / kBinHz));
        std::vector<float> sm((size_t)W);
        for (int i = 0; i < W; i++) sm[(size_t)i] = smooth(held, i, w);
        const float thr = floorLin * std::pow(10.f, thrDb.load() / 10.f);
        const double now = sigSec();
        const int sep = std::max(2, (int)std::lround(1.5 * baud() / kBinHz));
        for (int i = 1; i < W - 1; i++) {
            const float v = sm[(size_t)i];
            if (v < thr) continue;
            bool top = true;
            for (int j = std::max(0, i - sep); j <= std::min(W - 1, i + sep) && top; j++) if (j != i && (sm[(size_t)j] > v || (sm[(size_t)j] == v && j < i))) top = false;
            if (!top) continue;
            const double a = sm[(size_t)i - 1], bb = v, c = sm[(size_t)i + 1];
            const double den = a - 2 * bb + c;
            const double d = den < 0 ? std::max(-0.5, std::min(0.5, 0.5 * (a - c) / den)) : 0;
            const double hz = (kLoBin + i + d) * kBinHz;
            // the IMD products and keying sidebands of a much stronger signal are not signals of their own
            bool skirt = false;
            const int span = (int)(3 * baud() / kBinHz);
            for (int j = std::max(0, i - span); j <= std::min(W - 1, i + span); j++) if (sm[(size_t)j] > 300.f * v) skirt = true;
            if (Chan* ch = find(hz, 0.8 * baud())) {
                ch->lastPeak = now;
                if (!ch->manual && !ch->dcd) { ch->setFreq(ch->f + 0.1 * (hz - ch->f)); ch->f0 = ch->f; }
            } else if (!skirt && !find(hz, 1.4 * baud()) && (int)chans.size() < kPskMaxSignals) {
                add(hz, false);
            }
        }
        chans.erase(std::remove_if(chans.begin(), chans.end(), [&](const Chan& c) {
            return c.id != focusId && now - std::max(c.lastActive, c.lastPeak) > 40; }), chans.end());
        for (size_t i = 0; i < chans.size(); i++)
            for (size_t j = i + 1; j < chans.size();) {
                if (std::fabs(chans[i].f - chans[j].f) < 0.5 * baud() && chans[j].id != focusId && chans[j].chars <= chans[i].chars) chans.erase(chans.begin() + (long)j);
                else if (std::fabs(chans[i].f - chans[j].f) < 0.5 * baud() && chans[i].id != focusId && chans[i].chars < chans[j].chars) { chans.erase(chans.begin() + (long)i); j = i + 1; }
                else j++;
            }
    }

    void putChar(Chan& c, int ch) {
        if (ch == -2) { c.bad++; return; }
        c.chars++;
        c.lastActive = sigSec();
        if (ch == '\r' || ch == '\n') {
            c.lineOpen = false;
            if (!c.text.empty() && c.text.back() != '\n') c.text += '\n';
        } else {
            if (ch == '\t') ch = ' ';
            if (ch < 32 || ch > 126) return;
            if (!c.lineOpen) {
                PskLine l; l.timeSec = sigSec();
                c.lines.push_back(l);
                if (c.lines.size() > kPskLinesMax) c.lines.erase(c.lines.begin());
                c.lineOpen = true;
            }
            c.lines.back().text += (char)ch;
            c.text += (char)ch;
        }
        if (c.text.size() > kPskTextMax) c.text.erase(0, c.text.size() - kPskTextMax);
        if (ch == ' ' || ch == '\r' || ch == '\n') {
            std::string flat = c.text;
            std::replace(flat.begin(), flat.end(), '\n', ' ');
            c.calls = cw::callsigns(flat);
        }
    }

    // one symbol of a signal
    void symbol(Chan& c, std::complex<float> y) {
        if (!c.havePrev) { c.prevY = y; c.havePrev = true; return; }
        const std::complex<float> d = y * std::conj(c.prevY);
        c.prevY = y;
        if (std::abs(d) < 1e-20f) return;
        const double a = std::arg(d);
        const bool qp = pskQpsk(mode);
        const double err = qp ? std::arg(std::pow(std::complex<double>(d), 4)) / 4 : std::arg(std::pow(std::complex<double>(d), 2)) / 2;
        // quality: 1 for clean decisions, 0 for noise (the phase error spread over its whole range)
        const double en = err / (qp ? kPi / 4 : kPi / 2);
        c.q += 0.1f * ((float)(1 - 3 * en * en) - c.q);
        c.errVar += 0.05f * ((float)(err * err) - c.errVar);
        if (!c.dcd && c.q > 0.5f) c.dcd = true;
        else if (c.dcd && c.q < 0.25f) {
            c.dcd = false;
            c.lineOpen = false;
            c.vrx.reset();
        }
        // AFC: the remaining phase step per symbol is the frequency error
        const double ferr = err * baud() / (2 * kPi);
        const double gain = c.dcd ? 0.04 : 0.1;
        if ((!c.manual || c.dcd) && c.q > 0.2f) {
            double nf = c.f + gain * ferr;
            if (std::fabs(nf - c.f0) > 2 * baud()) nf = c.f0;   // pulled away: start over where it was found
            c.setFreq(nf);
        }
        if (qp) {
            float cq[4];
            for (int k = 0; k < 4; k++) cq[k] = (float)std::cos(a - k * kPi / 2);
            float b1[2] = {-2, -2}, b0[2] = {-2, -2};   // best correlation with bit 0 = 0/1, bit 1 = 0/1
            for (int k = 0; k < 4; k++) {
                const int sym = psk::qpskSymbol(k);
                b0[sym & 1] = std::max(b0[sym & 1], cq[k]);
                b1[(sym >> 1) & 1] = std::max(b1[(sym >> 1) & 1], cq[k]);
            }
            const int bit = c.vit.decode(b0[1] - b0[0], b1[1] - b1[0]);
            if (bit >= 0 && c.dcd) { const int ch = c.vrx.bit(bit); if (ch != -1) putChar(c, ch); }
        } else {
            const int bit = d.real() > 0 ? 1 : 0;
            if (c.dcd) { const int ch = c.vrx.bit(bit); if (ch != -1) putChar(c, ch); }
        }
        if (c.lineOpen && sigSec() - c.lastActive > 5) c.lineOpen = false;
    }

    // one 1 kHz sample of a signal: matched filter, symbol clock
    void sample1k(Chan& c, cf32 d) {
        const int L = (int)h.size();
        c.mf[(size_t)c.mfPos] = d; c.mf[(size_t)(c.mfPos + L)] = d;
        if (++c.mfPos >= L) c.mfPos = 0;
        const cf32* w = &c.mf[(size_t)c.mfPos];
        cf32 y(0, 0);
        for (int j = 0; j < L; j++) y += w[j] * h[(size_t)j];
        const float e = std::abs(y);
        float& bn = c.bins[(size_t)c.ph];
        bn += (c.syms < sps ? 0.3f : 0.08f) * (e - bn);
        c.yh[(size_t)c.yhPos] = y;
        c.yhPos = (c.yhPos + 1) % sps;
        c.since++;
        if (c.ph == c.idx && c.since > sps / 2) {
            c.since = 0;
            const cf32 mid = c.yh[(size_t)((c.yhPos - 1 - sps / 2 + 2 * sps) % sps)];
            c.pw += 0.05f * (std::norm(y) - c.pw);
            // Gardner: (previous - this) x the sample between them; > 0 when the samples come too early
            const float ge = std::real((c.lastSym - y) * std::conj(mid)) / std::max(1e-30f, c.pw);
            c.lastSym = y;
            if (pskQpsk(mode)) {
                // the matched filter leaves 1/6 of each neighbour in a symbol (the PSK31 pulse is two symbols long): harmless for BPSK, where
                // it only changes the amplitude, but it turns QPSK phases, so take it out (one symbol late)
                c.sym2 = c.sym1; c.sym1 = y;
                symbol(c, c.sym2 * (1.f + 2.f / 36) - (1.f / 6) * (c.lastSym2 + y));
                c.lastSym2 = c.sym2;
            } else {
                symbol(c, y);
            }
            int best = 0;
            for (int k = 1; k < sps; k++) if (c.bins[(size_t)k] > c.bins[(size_t)best]) best = k;
            if (!c.dcd && c.syms < 3 * sps) { c.idx = best; c.gacc = 0; }   // acquiring: straight to the peak of the envelope
            else {
                c.gacc += ge;
                if (c.gacc > 2.f) { c.idx = (c.idx + 1) % sps; c.gacc = 0; }
                else if (c.gacc < -2.f) { c.idx = (c.idx + sps - 1) % sps; c.gacc = 0; }
            }
            c.syms++;
        }
        if (++c.ph >= sps) c.ph = 0;
    }

    void process(const cf32* x, size_t n) {
        for (size_t i = 0; i < n; i++) {
            fftIn.push_back(x[i]);
            if (fftIn.size() >= (size_t)kN) { spectrumFrame(); fftIn.erase(fftIn.begin(), fftIn.begin() + kHop); }
        }
        for (size_t i = 0; i < n; i++) {
            const cf32 v = x[i];
            nBase++;
            for (Chan& c : chans) {
                const std::complex<double> m = std::complex<double>(v.real(), v.imag()) * c.rot;
                c.rot *= c.step;
                if (++c.rotCount >= 4096) { c.rot /= std::abs(c.rot); c.rotCount = 0; }
                const cf32 mf((float)m.real(), (float)m.imag());
                c.tri[c.triPos] = mf; c.tri[c.triPos + kTri] = mf;
                if (++c.triPos >= kTri) c.triPos = 0;
                if (++c.phase < kR) continue;
                c.phase = 0;
                const cf32* w = &c.tri[c.triPos];
                cf32 d(0, 0);
                for (int j = 0; j < kTri; j++) d += w[j] * triW[j];
                sample1k(c, d);
            }
        }
    }

    void report() {
        tel.seq++;
        tel.timeSec = curRate > 0 ? (double)nIn / curRate : 0;
        tel.levelDb = nPower ? (float)(10 * std::log10(power / (double)nPower + 1e-20)) : -200.f;
        power = 0; nPower = 0;
        tel.floorDb = (float)(10 * std::log10(floorLin + 1e-20));
        tel.mode = mode;
        if (accN > 0) {
            tel.spectrum.resize(acc.size());
            for (size_t i = 0; i < acc.size(); i++) { tel.spectrum[i] = (float)(10 * std::log10(acc[i] / accN + 1e-20)); acc[i] = 0; }
            accN = 0;
            tel.specSeq++;
        }
        tel.signals.clear();
        uint64_t ok = 0, bad = 0;
        int decoding = 0;
        float best = -100;
        tel.cfoHz = 0;
        for (const Chan& c : chans) {
            PskSignal s;
            s.id = c.id; s.offsetHz = c.f; s.afcHz = c.f - c.f0; s.manual = c.manual;
            s.snrDb = (float)std::max(-5.0, std::min(40.0, 10 * std::log10(1.0 / std::max(1e-4f, c.errVar)))); s.quality = std::max(0.f, c.q); s.dcd = c.dcd;
            s.lastActiveSec = c.lastActive; s.firstSec = c.firstSec;
            s.chars = c.chars; s.text = c.text; s.lines = c.lines; s.calls = c.calls;
            ok += c.chars; bad += c.bad;
            if (c.dcd) decoding++;
            if (c.id == focusId || (focusId < 0 && s.snrDb > best)) { tel.snrDb = s.snrDb; best = s.snrDb; if (c.id == focusId) tel.cfoHz = s.afcHz; }
            tel.signals.push_back(std::move(s));
        }
        std::sort(tel.signals.begin(), tel.signals.end(), [](const PskSignal& a, const PskSignal& b) { return a.offsetHz < b.offsetHz; });
        tel.blocksOk = std::max(tel.blocksOk, ok); tel.blocksBad = std::max(tel.blocksBad, bad);
        tel.focusId = focusId;
        tel.state = decoding ? 2 : chans.empty() ? 0 : 1;
        tel.dataValid = decoding > 0;
        std::lock_guard<std::mutex> lk(mu);
        pub = tel;
    }
};

PskReceiver::PskReceiver() : p_(std::make_unique<Impl>()) {}
PskReceiver::~PskReceiver() = default;

void PskReceiver::configure(double r) { std::lock_guard<std::mutex> lk(p_->mu); p_->rate = r; p_->resetReq = true; }
void PskReceiver::setSignalOffset(double hz) { std::lock_guard<std::mutex> lk(p_->mu); p_->offsetHz = hz; p_->offsetReq = true; }
bool PskReceiver::ready() const { std::lock_guard<std::mutex> lk(p_->mu); return p_->rate >= pskTuning().minSampleRate - 1; }
void PskReceiver::reset() {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->resetReq = true;
    const uint64_t s = p_->pub.seq;
    p_->pub = PskTelemetry();
    p_->pub.seq = s;
}

void PskReceiver::feed(const cf32* x, size_t n) {
    Impl& m = *p_;
    if (m.resetReq.exchange(false)) { m.offsetReq = false; m.resetState(); }
    if (m.offsetReq.exchange(false) && m.running) { std::lock_guard<std::mutex> lk(m.mu); m.front.setOffsetHz(m.offsetHz); }
    if (m.clearReq.exchange(false)) { m.chans.clear(); m.focusId = -1; }
    if (m.modeReq.load() != m.mode) m.setupMode(m.modeReq.load());
    {
        std::lock_guard<std::mutex> lk(m.mu);
        if (m.focusReq) {
            m.focusReq = false;
            Chan* c = m.find(m.focusHz, 0.8 * m.baud());
            if (!c && (int)m.chans.size() < kPskMaxSignals + 4) c = &m.add(m.focusHz, true);
            if (c) m.focusId = c->id;
            for (Chan& o : m.chans) if (o.manual && o.id != m.focusId && o.chars == 0) o.manual = false;
        }
    }
    if (m.curRate <= 0) return;
    for (size_t i = 0; i < n; i++) {
        float re = x[i].real(), im = x[i].imag();
        if (!std::isfinite(re) || !std::isfinite(im)) re = im = 0;
        m.power += (double)re * re + (double)im * im;
    }
    m.nPower += (int64_t)n;
    m.nIn += (int64_t)n;
    if (m.running) {
        m.base.clear();
        m.front.process(x, n, m.base);
        for (cf32& v : m.base) if (!std::isfinite(v.real()) || !std::isfinite(v.imag())) v = cf32(0, 0);
        m.process(m.base.data(), m.base.size());
    }
    while ((double)m.nIn >= m.nextReport) {
        m.nextReport += 0.25 * m.curRate;
        m.report();
    }
}

bool PskReceiver::telemetry(PskTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->pub.seq <= lastSeq) return false;
    out = p_->pub;
    return true;
}
void PskReceiver::setLogCallback(std::function<void(const std::string&)> cb) { std::lock_guard<std::mutex> lk(p_->mu); p_->log = std::move(cb); }
void PskReceiver::setMode(int m) { p_->modeReq = std::max(0, std::min(kPskModes - 1, m)); }
void PskReceiver::setFocusHz(double hz) { std::lock_guard<std::mutex> lk(p_->mu); p_->focusReq = true; p_->focusHz = hz; }
void PskReceiver::clearSignals() { p_->clearReq = true; }
void PskReceiver::setThresholdDb(float db) { p_->thrDb = std::max(4.f, std::min(30.f, db)); }

ModeTuning pskTuning() {
    ModeTuning t;
    t.stdMode = 38; t.id = "psk"; t.name = "PSK31";
    t.minMhz = 1.8; t.maxMhz = 30; t.defMhz = 14.070;   // the 20 m PSK31 dial frequency (upper sideband)
    t.sampleRate = 2000000;
    t.basebandHz = 1750000;
    t.bandwidthMhz = 0.004;
    t.minSampleRate = 250000;
    t.tuneOffsetHz = 20000;                              // as HF digital: the radio sits 20 kHz above the dial, away from its DC spike
    return t;
}

const PskPreset* pskPresets(int& count) {
    static const PskPreset kP[] = {{"80 m", 3.580}, {"40 m", 7.040}, {"20 m", 14.070}, {"15 m", 21.070}, {"10 m", 28.120}};
    count = (int)(sizeof kP / sizeof *kP);
    return kP;
}

} // namespace dect2
