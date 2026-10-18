// Satellite beacon receiver parts (see satbeacon_dsp.h).
#include "dect2/satbeacon/satbeacon_dsp.h"
#include "dect2/common/mathconst.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

namespace dect2 {
namespace sb {

namespace {
constexpr double kTwoPi = 6.283185307179586;

double dbOf(double p) { return 10.0 * std::log10(std::max(p, 1e-30)); }

// windowed-sinc low-pass, cutoff as a fraction of the sample rate
std::vector<float> lowpass(int taps, double cutoff) {
    std::vector<float> h(taps);
    const double m = (taps - 1) / 2.0;
    double sum = 0;
    for (int i = 0; i < taps; i++) {
        const double x = i - m;
        const double s = (std::fabs(x) < 1e-9) ? 2 * cutoff : std::sin(kTwoPi * cutoff * x) / (math::kPi * x);
        const double w = 0.54 - 0.46 * std::cos(kTwoPi * i / (taps - 1));
        h[i] = (float)(s * w);
        sum += h[i];
    }
    for (auto& v : h) v = (float)(v / sum);
    return h;
}

// parabolic interpolation of a log-power peak: offset in bins and the peak value (linear power)
void parabola(double a, double b, double c, double& off, double& peak) {
    const double la = std::log(std::max(a, 1e-30)), lb = std::log(std::max(b, 1e-30)), lc = std::log(std::max(c, 1e-30));
    const double d = la - 2 * lb + lc;
    off = (std::fabs(d) < 1e-12) ? 0.0 : 0.5 * (la - lc) / d;
    off = std::max(-0.6, std::min(0.6, off));
    peak = std::exp(lb - 0.25 * (la - lc) * off);
}
} // namespace

// ---------------------------------------------------------------- LineFinder

void LineFinder::configure(double rate) {
    rate_ = rate;
    int n = 1 << 14;
    while (n < (1 << 19) && rate / n > 8.0) n <<= 1;
    nfft_ = n;
    fft_ = std::make_unique<Fft>(nfft_);
    win_.resize(nfft_);
    sumW_ = sumW2_ = 0;
    for (int i = 0; i < nfft_; i++) {
        win_[i] = (float)(0.5 - 0.5 * std::cos(kTwoPi * (i + 0.5) / nfft_));
        sumW_ += win_[i]; sumW2_ += (double)win_[i] * win_[i];
    }
    buf_.assign(nfft_, cf32(0, 0));
    reset();
}

void LineFinder::reset() {
    fill_ = 0; nAvg_ = 0;
    psd_.assign(nfft_, 0.f);
    lines_.clear();
    plot_.assign(kSbSpecPts, 0.f);
    noiseDbHz_ = -200;
}

bool LineFinder::push(const cf32* x, size_t n) {
    bool ready = false;
    if (!fft_) return false;
    const int need = std::max(1, (int)std::ceil(1.0 * rate_ / nfft_));   // frames per second of data
    for (size_t i = 0; i < n; i++) {
        buf_[fill_] = x[i] * win_[fill_];
        fill_++;
        if (fill_ == nfft_) {
            fft_->forward(buf_.data());
            for (int k = 0; k < nfft_; k++) psd_[k] += std::norm(buf_[k]);
            fill_ = 0;
            if (++nAvg_ >= need) { analyse(); nAvg_ = 0; std::fill(psd_.begin(), psd_.end(), 0.f); ready = true; }
        }
    }
    return ready;
}

void LineFinder::analyse() {
    const int N = nfft_;
    std::vector<float> A(N);
    for (int k = 0; k < N; k++) A[k] = psd_[k] / (float)nAvg_;
    std::vector<float> tmp(A);
    // the median must not see the lines or the DC spike: that is the point of a median
    std::nth_element(tmp.begin(), tmp.begin() + N / 2, tmp.end());
    const double med = std::max((double)tmp[N / 2], 1e-30);
    // the spectrum is an average of nAvg_ frames: the median of that sum of exponentials is about (n - 1/3) / n of its mean
    const double noiseMean = med / ((nAvg_ - 1.0 / 3.0) / nAvg_);   // mean |X|^2 of the noise
    const double sigma2 = noiseMean / sumW2_;                  // noise power per sample
    noiseDbHz_ = dbOf(sigma2 / rate_);
    const double bin = rate_ / N;
    auto hzOf = [&](double k) { return (k < N / 2 ? k : k - N) * bin; };
    // max-hold plot, over the noise, from -rate/2 to +rate/2 (fftshifted)
    plot_.assign(kSbSpecPts, 0.f);
    const int per = std::max(1, N / kSbSpecPts);
    for (int i = 0; i < kSbSpecPts; i++) {
        float mx = 0;
        for (int j = 0; j < per; j++) {
            const int idx = (((i * per + j) + N / 2) % N);   // shifted: position 0 = -rate/2 -> bin N/2
            mx = std::max(mx, A[idx]);
        }
        plot_[i] = (float)(dbOf(mx) - dbOf(noiseMean));
    }
    // narrow lines
    struct C { int k; float a; };
    std::vector<C> cand;
    const int skip = std::max(2, (int)std::ceil(1500.0 / bin));   // the DC spike
    const double thr = med * std::pow(10.0, 1.4);                  // 14 dB over the median
    for (int k = 2; k < N - 2; k++) {
        const int kk = k;
        const int f = kk < N / 2 ? kk : N - kk;
        if (f <= skip) continue;
        if (A[k] < thr) continue;
        if (!(A[k] >= A[k - 1] && A[k] >= A[k + 1] && A[k] >= A[k - 2] && A[k] >= A[k + 2])) continue;
        // narrow: the skirts 6 to 20 bins away are well below
        double skirt = 0; int cnt = 0;
        for (int d = 6; d <= 20; d++) {
            skirt += A[(k + d) % N] + A[(k - d + N) % N];
            cnt += 2;
        }
        skirt /= cnt;
        if (A[k] < 8.0 * skirt) continue;
        cand.push_back({k, A[k]});
    }
    std::sort(cand.begin(), cand.end(), [](const C& a, const C& b) { return a.a > b.a; });
    lines_.clear();
    for (const auto& c : cand) {
        bool near = false;
        for (const auto& l : lines_) if (std::fabs(l.hz - hzOf(c.k)) < 12 * bin) { near = true; break; }
        if (near) continue;
        double off, pk;
        parabola(A[(c.k - 1 + N) % N], A[c.k], A[(c.k + 1) % N], off, pk);
        Line l;
        l.hz = hzOf(c.k + off);
        l.peakDb = dbOf(pk / (sumW_ * sumW_));
        l.snrBinDb = dbOf(pk / noiseMean);
        lines_.push_back(l);
        if ((int)lines_.size() >= kSbLineMax) break;
    }
}

// ---------------------------------------------------------------- frame length

namespace {
// the cached FFT of a power-of-two size
Fft& fftOf(int n) {
    static thread_local std::map<int, std::unique_ptr<Fft>> cache;
    auto& p = cache[n];
    if (!p) p = std::make_unique<Fft>(n);
    return *p;
}
}

int frameLengthOf(const std::vector<uint8_t>& b, float* agreement) {
    const int N = (int)b.size();
    if (agreement) *agreement = 0.5f;
    const int maxL = std::min(16384, N / 12);   // at least twelve frames to compare
    constexpr int kMinL = 64;                   // shorter repeats are the chips of a constant bit, not frames
    if (N < 1024 || maxL < kMinL) return 0;
    int Nf = 1;
    while (Nf < 2 * N) Nf <<= 1;
    std::vector<cf32> X((size_t)Nf, cf32(0, 0));
    double ones = 0;
    for (int i = 0; i < N; i++) { X[(size_t)i] = cf32(b[(size_t)i] ? 1.f : -1.f, 0); ones += b[(size_t)i]; }
    Fft& f = fftOf(Nf);
    f.forward(X.data());
    for (auto& v : X) v = cf32(std::norm(v), 0);
    f.inverse(X.data());
    // agreement of the stream with itself shifted by L; the chance level depends on the share of ones
    const double p1 = ones / N, base = p1 * p1 + (1 - p1) * (1 - p1);
    std::vector<float> sc((size_t)maxL + 1, 0.f);
    float best = 0;
    for (int L = kMinL; L <= maxL; L++) {
        const double ac = X[(size_t)L].real() / Nf / (N - L);   // mean of x[i] x[i+L], x = +-1
        sc[(size_t)L] = (float)(0.5 + 0.5 * ac);
        best = std::max(best, sc[(size_t)L]);
    }
    // chance: the best of thousands of lags of random bits is about 4.5 sigma over the chance level, sigma = 0.5 / sqrt(N); ask for 8
    if (best < std::max(base + 0.04, base + 8.0 * 0.5 / std::sqrt((double)N))) return 0;
    int arg = kMinL;
    for (int L = kMinL; L <= maxL; L++) if (sc[(size_t)L] >= best) { arg = L; break; }
    // the best lag is often a multiple of the frame: take the smallest divisor that is nearly as good
    int pick = arg;
    for (int d = kMinL; d < arg; d++)
        if (arg % d == 0 && sc[(size_t)d] >= base + 0.85 * (best - base)) { pick = d; break; }
    if (agreement) *agreement = sc[(size_t)pick];
    return pick;
}

// ---------------------------------------------------------------- Channel

const std::vector<Channel::Sync>& Channel::syncWords() {
    static const std::vector<Sync> w = {
        {"FAF320 (IRIG 106)", 24, 0xFAF320u},
        {"FE6B2840 (IRIG 106)", 32, 0xFE6B2840u},
        {"EB90 (IRIG 106)", 16, 0xEB90u},
        {"1ACFFC1D (CCSDS)", 32, 0x1ACFFC1Du},
    };
    return w;
}

void Channel::reset() {
    total = 0; frameL = frameOff = fixedBits = 0; syncIdx = -1; counterByte = -1; counterMod = 0;
    agree = 0.5f; ok = bad = 0;
    tmpl.clear(); mask.clear(); stab.clear(); frames.clear();
    bits_.clear(); base_ = 0; nextLearn_ = 2400; nextFrame_ = 0; inv_ = false; folded_ = 0; frameId_ = 1;
    prevCounter_ = -1; ctrMax_ = 0; ctrWrap_ = false;
}

void Channel::push(int bit) {
    bits_.push_back((uint8_t)bit);
    total++;
    if (bits_.size() > 131072) { bits_.erase(bits_.begin(), bits_.begin() + 32768); base_ += 32768; }
    if (total >= nextLearn_) { nextLearn_ = total + 2400; learn(); }
    if (frameL > 0) exportFrames();
}

// a known sync word that repeats at a constant spacing, as sent or inverted; L = the spacing, off = its position modulo L (absolute bit index)
bool Channel::findSync(const std::vector<uint8_t>& last, uint64_t first, int& L, int& off, bool& inv, int& idx) const {
    const auto& words = syncWords();
    const int N = (int)last.size();
    int bestCount = 0;
    for (size_t w = 0; w < words.size(); w++) {
        const int nb = words[w].bits;
        const uint32_t mask = nb == 32 ? 0xFFFFFFFFu : ((1u << nb) - 1);
        for (int pol = 0; pol < 2; pol++) {
            const uint32_t pat = pol ? (~words[w].word & mask) : words[w].word;
            std::vector<int> pos;
            uint32_t acc = 0;
            for (int i = 0; i < N; i++) {
                acc = ((acc << 1) | last[(size_t)i]) & mask;
                if (i >= nb - 1 && acc == pat) pos.push_back(i - nb + 1);
            }
            if (pos.size() < 3) continue;
            std::map<int, int> cnt;
            for (size_t k = 1; k < pos.size(); k++) { const int d = pos[k] - pos[k - 1]; if (d >= 64) cnt[d]++; }
            for (const auto& c : cnt) {
                if (c.second >= 3 && c.second >= 0.4 * ((double)N / c.first - 1) && c.second > bestCount) {
                    bestCount = c.second; L = c.first; inv = pol == 1; idx = (int)w;
                    // the frame start modulo L: the first position that has a successor at L
                    for (size_t k = 0; k + 1 < pos.size(); k++) if (pos[k + 1] - pos[k] == c.first) { off = (int)((first + (uint64_t)pos[k]) % (uint64_t)c.first); break; }
                }
            }
        }
    }
    return bestCount > 0;
}

void Channel::learn() {
    const size_t N = std::min<size_t>(bits_.size(), 65536);
    std::vector<uint8_t> last(bits_.end() - (long)N, bits_.end());
    const uint64_t first = base_ + (bits_.size() - N);
    int L = 0, off = 0, sidx = -1;
    bool sinv = false;
    const bool synced = findSync(last, first, L, off, sinv, sidx);
    float agr = 0.5f;
    if (!synced) { L = frameLengthOf(last, &agr); if (L == 0) return; }
    std::vector<int> ones((size_t)L, 0), tot((size_t)L, 0);
    for (size_t i = 0; i < N; i++) {
        const int c = (int)((first + i) % (uint64_t)L);
        ones[(size_t)c] += last[i]; tot[(size_t)c]++;
    }
    if (tot[0] < 6) return;
    std::vector<float> f((size_t)L);
    std::vector<uint8_t> maj((size_t)L);
    double match = 0;
    for (int c = 0; c < L; c++) {
        const float m = tot[(size_t)c] ? (float)ones[(size_t)c] / tot[(size_t)c] : 0.5f;
        maj[(size_t)c] = m >= 0.5f;
        f[(size_t)c] = std::max(m, 1.0f - m);
        match += (double)m * m + (1.0 - m) * (1.0 - m);
    }
    if (synced) agr = (float)(match / L);
    bool inv;
    if (synced) {
        inv = sinv;
    } else {
        // frame start: the beginning of the longest run of stable columns (gaps of up to two columns are bridged)
        int bestStart = 0, bestLen = 0;
        for (int s = 0; s < L; s++) {
            if (f[(size_t)s] < 0.75f || f[(size_t)((s + L - 1) % L)] >= 0.75f) continue;   // only a run beginning
            int len = 0, gap = 0;
            for (int d = 0; d < L; d++) {
                if (f[(size_t)((s + d) % L)] >= 0.75f) { len = d + 1; gap = 0; }
                else if (++gap > 2) break;
            }
            if (len > bestLen) { bestLen = len; bestStart = s; }
        }
        off = bestLen >= 8 ? bestStart : 0;
        // the alignment settles once enough frames have been folded; after that it only follows a change of the frame length
        const bool settled = (L == frameL) && syncIdx < 0 && folded_ >= 48;
        if (settled) off = frameOff;
        inv = settled ? inv_ : (maj[(size_t)off] == 0);   // polarity: the first bit of the template is a 1
        folded_ = (L == frameL) ? tot[0] : 0;
    }
    const bool changed = (L != frameL || off != frameOff || (synced ? sidx : -1) != syncIdx || inv != inv_);
    frameL = L; frameOff = off; agree = agr; syncIdx = synced ? sidx : -1;
    tmpl.assign((size_t)(L + 7) / 8, 0); mask.assign((size_t)(L + 7) / 8, 0); stab.assign((size_t)L, 0.f);
    fixedBits = 0;
    for (int j = 0; j < L; j++) {
        const int c = (off + j) % L;
        const uint8_t v = inv ? !maj[(size_t)c] : maj[(size_t)c];
        if (v) tmpl[(size_t)j >> 3] |= (uint8_t)(0x80 >> (j & 7));
        stab[(size_t)j] = f[(size_t)c];
        if (f[(size_t)c] >= 0.85f) { mask[(size_t)j >> 3] |= (uint8_t)(0x80 >> (j & 7)); fixedBits++; }
    }
    inv_ = inv;
    if (changed) {
        frames.clear(); ok = bad = 0; frameId_ = 1; counterByte = -1; counterMod = 0; prevCounter_ = -1; ctrMax_ = 0; ctrWrap_ = false;
        const uint64_t start = base_ + (bits_.size() > (size_t)(32 * L) ? bits_.size() - (size_t)(32 * L) : 0);
        uint64_t k = start - start % (uint64_t)L + (uint64_t)off;
        while (k < start) k += (uint64_t)L;
        nextFrame_ = k;
    }
}

void Channel::exportFrames() {
    const int L = frameL;
    bool added = false;
    while (nextFrame_ >= base_ && nextFrame_ + (uint64_t)L <= base_ + bits_.size()) {
        SbFrame fr;
        fr.id = frameId_++;
        fr.bytes.assign((size_t)(L + 7) / 8, 0);
        const size_t o = (size_t)(nextFrame_ - base_);
        for (int j = 0; j < L; j++) {
            uint8_t v = bits_[o + (size_t)j];
            if (inv_) v ^= 1;
            if (v) fr.bytes[(size_t)j >> 3] |= (uint8_t)(0x80 >> (j & 7));
        }
        int err = 0;
        for (int j = 0; j < L; j++) {
            if (!(mask[(size_t)j >> 3] & (0x80 >> (j & 7)))) continue;
            err += ((fr.bytes[(size_t)j >> 3] ^ tmpl[(size_t)j >> 3]) >> (7 - (j & 7))) & 1;
        }
        if (fixedBits > 0 && err * 2 > fixedBits) {   // a polarity flip of the demodulator
            for (auto& v : fr.bytes) v = (uint8_t)~v;
            if (L & 7) fr.bytes.back() &= (uint8_t)(0xFF << (8 - (L & 7)));
            err = fixedBits - err;
        }
        fr.fixedErrors = err;
        fr.good = fixedBits < 8 || err <= std::max(3, (int)(fixedBits * 0.25));
        if (counterByte >= 0 && (size_t)counterByte < fr.bytes.size()) {
            fr.counter = fr.bytes[(size_t)counterByte];
            if (prevCounter_ > 0 && fr.counter == 0) ctrWrap_ = true;
            prevCounter_ = fr.counter; ctrMax_ = std::max(ctrMax_, fr.counter);
            if (ctrWrap_) counterMod = ctrMax_ + 1;
        }
        if (fr.good) ok++; else bad++;
        frames.push_front(std::move(fr));
        while (frames.size() > kSbFrameMax) frames.pop_back();
        nextFrame_ += (uint64_t)L;
        added = true;
    }
    if (nextFrame_ < base_) nextFrame_ += ((base_ - nextFrame_ + (uint64_t)L - 1) / (uint64_t)L) * (uint64_t)L;
    if (added && counterByte < 0 && frames.size() >= 10) findCounter();
}

// the byte that counts up by one from frame to frame (and wraps to 0): the minor frame counter
void Channel::findCounter() {
    if (frames.size() < 10) return;
    const size_t nb = frames.front().bytes.size();
    for (size_t c = 0; c < nb; c++) {
        int pairs = 0, step = 0, wrap = 0, maxv = 0, distinct = 0;
        bool seen[256] = {};
        int prev = -1;
        for (auto it = frames.rbegin(); it != frames.rend(); ++it) {   // oldest first
            const int v = it->bytes[c];
            maxv = std::max(maxv, v);
            if (!seen[v]) { seen[v] = true; distinct++; }
            if (prev >= 0) { pairs++; if (v == prev + 1) step++; else if (v == 0 && prev > 0) wrap++; }
            prev = v;
        }
        if (pairs >= 8 && distinct >= 6 && step + wrap >= 0.9 * pairs && step >= 0.7 * pairs) {
            counterByte = (int)c;
            ctrMax_ = maxv; ctrWrap_ = wrap > 0; prevCounter_ = frames.front().bytes[c];
            counterMod = ctrWrap_ ? ctrMax_ + 1 : 0;
            for (auto& f : frames) f.counter = f.bytes[c];
            return;
        }
    }
}

// ---------------------------------------------------------------- Beacon

Beacon::Beacon() { pmWin_.resize(kPmFft); for (int i = 0; i < kPmFft; i++) pmWin_[(size_t)i] = (float)(0.5 - 0.5 * std::cos(kTwoPi * (i + 0.5) / kPmFft)); }
Beacon::~Beacon() = default;

void Beacon::configure(double inputRate) {
    rate_ = inputRate;
    dec_ = std::max(1, (int)std::floor(rate_ / 200e3));
    wrate_ = rate_ / dec_;
    taps_ = lowpass(dec_ * 8 + 1, 0.44 / dec_);
    lf_.configure(rate_);
    // PLL, 15 Hz, damping 0.707
    const double wn = kTwoPi * 15.0 / wrate_;
    kp_ = 2 * 0.707 * wn;
    ki_ = wn * wn;
    reset();
}

void Beacon::reset() {
    lf_.reset();
    haveCarrier_ = false;
    lostCnt_ = farCnt_ = 0;
    freqHist_.clear();
    timeS_ = 0; lastHistT_ = 0; nWork_ = 0;
    noiseDbHz_ = -200;
    startCarrier(0);
    haveCarrier_ = false;
}

void Beacon::startCarrier(double hz) {
    frontHz_ = hz; frontPh_ = 0; pllFreqHz_ = hz;
    hist_.assign(taps_.size(), cf32(0, 0));
    hpos_ = 0; phaseCnt_ = 0;
    pllPh_ = pllW_ = 0;
    avgRe_ = avgIm_ = avgAbs_ = 0;
    lockedRaw_ = false;
    errAcc_ = errN_ = 0; errStd_ = 0;
    pmFill_ = 0; pmCount_ = 0;
    pmBuf_.assign(kPmFft, 0.f);
    pmAcc_.assign(kPmFft / 2, 0.f);
    pmPlot_.assign(kSbSpecPts, 0.f);
    pmNoise_ = 0;
    nTones_ = 0; subHz_ = spacingHz_ = 0; subDb_ = 0; toneSetT_ = 0;
    sub_ = Sub();
    ampAvg_ = 0;
    edgeBuf_.clear(); prevS_ = 0;
    clkInit_ = false; baud_ = clkPh_ = clkB_ = 0; clkZ_ = {0, 0}; accBit_ = 0; clkLock_ = false; clkLockAvg_ = 0; ampEdge_ = 0; lastIdx_ = 0;
    prevChip_ = 0; chipN_ = 0; pairD_[0] = pairD_[1] = 0; chipAbs_ = 0; biphase_ = false;
    ch_[0].reset(); ch_[1].reset();
    lostCnt_ = 0;
}

void Beacon::process(const cf32* x, size_t n) {
    if (rate_ <= 0 || n == 0) return;
    if (lf_.push(x, n)) {
        const auto& L = lf_.lines();
        noiseDbHz_ = lf_.noiseDbHz();
        if (L.empty()) {
            if (haveCarrier_ && ++lostCnt_ >= 5) { startCarrier(0); haveCarrier_ = false; }
        } else if (!haveCarrier_) {
            startCarrier(L[0].hz);
            haveCarrier_ = true;
        } else {
            // does the strongest line still agree with the tracked one? Else after three looks, follow it
            bool tracked = false;
            for (const auto& l : L) if (std::fabs(l.hz - pllFreqHz_) < 150.0) tracked = true;
            if (!tracked) { if (++farCnt_ >= 3) { startCarrier(L[0].hz); haveCarrier_ = true; farCnt_ = 0; } }
            else { farCnt_ = 0; lostCnt_ = 0; }
        }
    }
    if (!haveCarrier_) return;
    const double step = kTwoPi * frontHz_ / rate_;
    const std::complex<double> rot(std::cos(step), -std::sin(step));
    std::complex<double> ph(std::cos(frontPh_), -std::sin(frontPh_));
    const size_t nt = taps_.size();
    for (size_t i = 0; i < n; i++) {
        ph *= rot;
        if ((i & 1023) == 1023) ph /= std::abs(ph);
        const cf32 m = x[i] * cf32((float)ph.real(), (float)ph.imag());
        hist_[hpos_] = m;
        hpos_ = hpos_ + 1 == nt ? 0 : hpos_ + 1;
        if (++phaseCnt_ == dec_) {
            phaseCnt_ = 0;
            float re = 0, im = 0;
            size_t idx = hpos_;
            for (size_t k = 0; k < nt; k++) {
                const float h = taps_[k];
                re += h * hist_[idx].real(); im += h * hist_[idx].imag();
                idx = idx + 1 == nt ? 0 : idx + 1;
            }
            feedWork(cf32(re, im));
        }
    }
    frontPh_ = -std::arg(ph);
    feedAnalyzer();
}

// the new good frames of the shown stream go to the analyzer; a change of stream, of the frame layout or a restart of the counting starts it again
void Beacon::feedAnalyzer() {
    const int ai = active();
    const Channel& c = ch_[ai];
    if (ai != anChan_ || c.frames.empty() || c.frames.front().id < anId_) { an_.reset(); anChan_ = ai; anId_ = 0; }
    if (c.frames.empty()) return;
    for (auto it = c.frames.rbegin(); it != c.frames.rend(); ++it) {   // oldest first
        if (it->id <= anId_) continue;
        anId_ = it->id;
        if (it->good) an_.add(it->bytes, c.counterByte, c.counterMod, c.frameL / std::max(1.0, clkLock_ ? clkB_ / (ai == 1 ? 2.0 : 1.0) : 4800.0));
    }
}

void Beacon::feedWork(cf32 y) {
    // carrier loop: the phase error is the phase modulation (the loop is far narrower than the subcarrier)
    const double cs = std::cos(pllPh_), sn = std::sin(pllPh_);
    const double rr = y.real() * cs + y.imag() * sn, ri = y.imag() * cs - y.real() * sn;
    const double e = std::atan2(ri, rr);
    pllPh_ += pllW_ + kp_ * e;
    pllW_ += ki_ * e;
    if (pllPh_ > math::kPi) pllPh_ -= kTwoPi; else if (pllPh_ < -math::kPi) pllPh_ += kTwoPi;
    pllFreqHz_ = frontHz_ + pllW_ * wrate_ / kTwoPi;
    nWork_++;
    timeS_ = (double)nWork_ / wrate_;
    const double a = 1.0 / (0.1 * wrate_);
    avgRe_ += a * (rr - avgRe_); avgIm_ += a * (ri - avgIm_);
    const double ab = std::hypot(rr, ri);
    avgAbs_ += a * (ab - avgAbs_);
    lockedRaw_ = nWork_ > (uint64_t)(0.5 * wrate_) && std::hypot(avgRe_, avgIm_) > 0.25 * avgAbs_;
    errAcc_ += e * e; errN_ += 1;
    if (errN_ >= wrate_) { errStd_ = std::sqrt(errAcc_ / errN_); errAcc_ = errN_ = 0; }
    if (timeS_ - lastHistT_ >= 1.0) {
        lastHistT_ = timeS_;
        freqHist_.push_back({timeS_, pllFreqHz_});
        while (freqHist_.size() > 300) freqHist_.pop_front();
    }
    if (!lockedRaw_) return;
    const float p = (float)e;
    pmBuf_[(size_t)pmFill_] = p * pmWin_[(size_t)pmFill_];
    pmFill_++;
    if (pmFill_ == kPmFft) { pmFill_ = 0; pmBlock(); }
    if (sub_.on) feedSub(p);
}

void Beacon::pmBlock() {
    // spectrum of the phase modulation, averaged about two seconds
    std::vector<cf32> X(kPmFft);
    for (int i = 0; i < kPmFft; i++) X[(size_t)i] = cf32(pmBuf_[(size_t)i], 0);
    pmFft_.forward(X.data());
    for (int k = 0; k < kPmFft / 2; k++) pmAcc_[(size_t)k] += std::norm(X[(size_t)k]);
    const int need = std::max(8, (int)std::ceil(2.0 * wrate_ / kPmFft));
    if (++pmCount_ < need) return;
    std::vector<float> A((size_t)kPmFft / 2);
    for (int k = 0; k < kPmFft / 2; k++) A[(size_t)k] = pmAcc_[(size_t)k] / (float)pmCount_;
    std::fill(pmAcc_.begin(), pmAcc_.end(), 0.f);
    pmCount_ = 0;
    std::vector<float> tmp(A.begin() + 8, A.end());
    std::nth_element(tmp.begin(), tmp.begin() + (long)tmp.size() / 2, tmp.end());
    pmNoise_ = std::max((double)tmp[tmp.size() / 2], 1e-30);
    const int per = std::max(1, (kPmFft / 2) / kSbSpecPts);
    pmPlot_.assign(kSbSpecPts, 0.f);
    for (int i = 0; i < kSbSpecPts; i++) {
        float mx = 0;
        for (int j = 0; j < per; j++) mx = std::max(mx, A[(size_t)std::min(kPmFft / 2 - 1, i * per + j)]);
        pmPlot_[(size_t)i] = (float)(dbOf(mx) - dbOf(pmNoise_));
    }
    // the subcarrier: searched while none is set, and again when no clock locked for a while
    if (!(nTones_ == 0 || (!clkLock_ && timeS_ - toneSetT_ > 25.0))) return;
    const double bin = wrate_ / kPmFft;
    struct C { int k; float a; };
    std::vector<C> cand;
    const int lo = (int)(3000 / bin), hi = (int)(0.46 * wrate_ / bin);
    // data on the subcarrier spreads it by the bit rate, so the search runs on a smoothed spectrum (about +-300 Hz) against the floor of the whole band
    const int hw = std::max(2, (int)(300 / bin));
    std::vector<float> Sm((size_t)kPmFft / 2, 0.f);
    {
        double acc = 0;
        for (int k = 0; k < kPmFft / 2; k++) {
            acc += A[(size_t)k];
            if (k - 2 * hw - 1 >= 0) acc -= A[(size_t)(k - 2 * hw - 1)];
            const int c = k - hw;
            if (c >= 0) Sm[(size_t)c] = (float)(acc / (2 * hw + 1));
        }
    }
    for (int k = std::max(lo, hw + 2); k < std::min(hi, kPmFft / 2 - hw - 2); k++) {
        if (!(Sm[(size_t)k] >= Sm[(size_t)k - 1] && Sm[(size_t)k] >= Sm[(size_t)k + 1] && Sm[(size_t)k] >= Sm[(size_t)(k - hw)] && Sm[(size_t)k] >= Sm[(size_t)(k + hw)])) continue;
        if (Sm[(size_t)k] < 10.0f * (float)pmNoise_) continue;    // 10 dB over the floor
        cand.push_back({k, Sm[(size_t)k]});
    }
    std::sort(cand.begin(), cand.end(), [](const C& a, const C& b) { return a.a > b.a; });
    if (cand.empty()) return;
    auto freqOf = [&](int k) {   // centroid of the hump
        double sw = 0, sf = 0;
        for (int d = -hw; d <= hw; d++) { const double w = A[(size_t)(k + d)]; sw += w; sf += w * (k + d); }
        return sf / sw * bin;
    };
    const double f0 = freqOf(cand[0].k);
    double f1 = 0; bool two = false;
    for (size_t j = 1; j < cand.size(); j++) {
        if (std::fabs(cand[j].k * bin - f0) < 1000.0) continue;
        if (cand[j].a < 0.25f * cand[0].a) break;
        f1 = freqOf(cand[j].k); two = true; break;
    }
    const double subHz = two ? 0.5 * (f0 + f1) : f0;
    const double spacing = two ? std::fabs(f0 - f1) : 0.0;
    const int nt = two ? 2 : 1;
    if (nt != nTones_ || std::fabs(subHz - subHz_) > 150 || std::fabs(spacing - spacingHz_) > 150) {
        nTones_ = nt; subHz_ = subHz; spacingHz_ = spacing;
        subDb_ = (float)(dbOf(cand[0].a) - dbOf(pmNoise_));
        toneSetT_ = timeS_;
        startSub();
    }
}

void Beacon::startSub() {
    sub_ = Sub();
    sub_.M = std::max(1, (int)std::floor(wrate_ / 80e3));
    sub_.fs = wrate_ / sub_.M;
    // the low-pass keeps the data of the subcarrier: about the tone spacing (a bi-phase subcarrier has its humps half the chip rate either side), else 8 kHz
    double cf = nTones_ > 1 ? spacingHz_ : 8000.0;
    cf = std::max(2000.0, std::min(cf, 0.45 * sub_.fs));
    sub_.taps = lowpass(401, cf / wrate_);
    sub_.hist.assign(sub_.taps.size(), cf32(0, 0));
    sub_.on = true;
    edgeBuf_.clear(); prevS_ = 0; ampAvg_ = 0;
    clkInit_ = false; baud_ = clkPh_ = clkB_ = 0; clkZ_ = {0, 0}; accBit_ = 0; clkLock_ = false; clkLockAvg_ = 0; ampEdge_ = 0; lastIdx_ = 0;
    prevChip_ = 0; chipN_ = 0; pairD_[0] = pairD_[1] = 0; chipAbs_ = 0; biphase_ = false;
    ch_[0].reset(); ch_[1].reset();
}

void Beacon::feedSub(float p) {
    Sub& t = sub_;
    t.ph += kTwoPi * subHz_ / wrate_;
    if (t.ph > kTwoPi) t.ph -= kTwoPi;
    const float c = (float)std::cos(t.ph), s = (float)std::sin(t.ph);
    const size_t nt = t.taps.size();
    t.hist[t.hp] = cf32(p * c, -p * s);
    t.hp = t.hp + 1 == nt ? 0 : t.hp + 1;
    if (++t.cnt < t.M) return;
    t.cnt = 0;
    float re = 0, im = 0;
    size_t idx = t.hp;
    for (size_t k = 0; k < nt; k++) {
        const float h = t.taps[k];
        re += h * t.hist[idx].real(); im += h * t.hist[idx].imag();
        idx = idx + 1 == nt ? 0 : idx + 1;
    }
    // Costas loop for BPSK at the decimated rate
    const double cs = std::cos(t.cph), sn = std::sin(t.cph);
    const double yr = re * cs + im * sn, yi = im * cs - re * sn;
    const double mag = std::hypot(yr, yi) + 1e-9;
    const double err = (yr >= 0 ? yi : -yi) / mag;
    const double a = 0.01, b = a * a / 4;
    t.cfr += b * err; t.cph += t.cfr + a * err;
    if (t.cph > math::kPi) t.cph -= kTwoPi; else if (t.cph < -math::kPi) t.cph += kTwoPi;
    soft((float)yr);
}

void Beacon::soft(float s) {
    const double fs = sub_.fs;
    ampAvg_ += 0.001 * (std::fabs(s) - ampAvg_);
    const float sn = (float)(s / std::max(ampAvg_, 1e-9));
    const float d = sn - prevS_;
    prevS_ = sn;
    const float dE = d * d;
    // the symbol rate from the edge energy: the strongest line (the chip rate of a bi-phase stream, the bit rate of an NRZ one)
    if (baud_ <= 0 || !clkLock_) {
        edgeBuf_.push_back(dE);
        if (edgeBuf_.size() >= 131072) {
            std::vector<cf32> X(131072);
            double mean = 0;
            for (float v : edgeBuf_) mean += v;
            mean /= (double)edgeBuf_.size();
            for (int i = 0; i < 131072; i++) {
                const double w = 0.5 - 0.5 * std::cos(kTwoPi * (i + 0.5) / 131072);
                X[(size_t)i] = cf32((float)((edgeBuf_[(size_t)i] - mean) * w), 0);
            }
            Fft& big = fftOf(131072);
            big.forward(X.data());
            const double bin = fs / 131072;
            const int lo = (int)(60 / bin), hi = (int)(std::min(40000.0, 0.4 * fs) / bin);
            std::vector<float> P((size_t)hi + 2);
            for (int k = 0; k <= hi + 1; k++) P[(size_t)k] = std::norm(X[(size_t)k]);
            int bk = lo;
            for (int k = lo; k <= hi; k++) if (P[(size_t)k] > P[(size_t)bk]) bk = k;
            std::vector<float> tmp(P.begin() + lo, P.begin() + hi);
            std::nth_element(tmp.begin(), tmp.begin() + (long)tmp.size() / 2, tmp.end());
            if (bk > lo && bk < hi && P[(size_t)bk] > 30.0f * tmp[tmp.size() / 2]) {
                double off, pk;
                parabola(P[(size_t)bk - 1], P[(size_t)bk], P[(size_t)bk + 1], off, pk);
                double nb = (bk + off) * bin;
                // beacons use round rates: snap to one when the estimate is within 1.5 %
                static const double kRates[] = {50, 75, 100, 150, 200, 300, 400, 600, 800, 1000, 1200, 1600, 1800, 2000, 2400, 3000, 3200, 4800, 6400, 9600, 12800, 19200, 38400};
                for (double r : kRates) if (std::fabs(nb - r) < 0.015 * r) { nb = r; break; }
                // a clock that has run is kept, with its phase, unless the rate really changed
                if (baud_ <= 0 || std::fabs(nb - baud_) > 0.01 * baud_) {
                    baud_ = nb; clkB_ = nb;
                    clkPh_ = 0; clkZ_ = {0, 0}; clkLock_ = false; clkLockAvg_ = 0; clkInit_ = false;
                    prevChip_ = 0; chipN_ = 0; pairD_[0] = pairD_[1] = 0; chipAbs_ = 0; biphase_ = false;
                }
            }
            edgeBuf_.clear();
        }
    }
    if (baud_ <= 0) return;
    // clock loop on the edge phase; the rate is allowed to move by 0.5 % (the radio's own clock error)
    const double lam = 1.0 - 1.0 / (20.0 * fs / clkB_);
    const double ang = kTwoPi * std::fmod(clkPh_, 1.0);
    clkZ_ = clkZ_ * lam + std::complex<double>(dE * std::cos(ang), -dE * std::sin(ang));
    ampEdge_ = ampEdge_ * lam + dE;
    clkPh_ += clkB_ / fs;
    const double frac = clkPh_ - std::floor(clkPh_);
    if (frac >= 0.25 && frac < 0.75) accBit_ += sn;
    const int64_t idx = (int64_t)std::floor(clkPh_ + 0.25);
    if (!clkInit_) { clkInit_ = true; lastIdx_ = idx; }
    if (idx > lastIdx_) {
        lastIdx_ = idx;
        const float v = (float)accBit_;
        accBit_ = 0;
        chip(v);
        // loop update once per symbol
        const double fe = -std::arg(clkZ_) / kTwoPi;
        const double lockM = std::abs(clkZ_) / std::max(ampEdge_, 1e-9);
        clkLockAvg_ += 0.02 * (lockM - clkLockAvg_);
        clkLock_ = clkLock_ ? clkLockAvg_ > 0.06 : clkLockAvg_ > 0.12;
        const double kpC = 0.05, kiC = 0.0002;
        const double dph = kpC * fe;
        clkPh_ -= dph;
        clkZ_ *= std::polar(1.0, kTwoPi * dph);
        clkB_ *= (1.0 - kiC * fe);
        clkB_ = std::max(baud_ * 0.995, std::min(baud_ * 1.005, clkB_));
    }
}

// one chip (symbol) from the clock: as an NRZ bit, and in pairs as a bi-phase bit (the sign of the difference of the two chips)
void Beacon::chip(float v) {
    ch_[0].push(v > 0 ? 1 : 0);
    chipAbs_ += (1.0 / 256.0) * (std::fabs(v) - chipAbs_);
    chipN_++;
    if (chipN_ >= 2) {
        // a bi-phase pair is two opposite chips: their sum is near zero in the right pairing and not in the other; for NRZ data the sum is
        // large whenever the bit repeats (half the time for random data, always for a constant bit)
        const double sm = std::fabs((double)prevChip_ + v);
        const int par = (int)(chipN_ & 1);
        pairD_[par] += (1.0 / 128.0) * (sm - pairD_[par]);
        const int a = pairD_[1] < pairD_[0] ? 1 : 0;
        const double r = pairD_[a] / (2.0 * std::max(chipAbs_, 1e-9));
        if (chipN_ > 256) { if (r < 0.3) biphase_ = true; else if (r > 0.42) biphase_ = false; }
        if (par == a) ch_[1].push(prevChip_ - v > 0 ? 1 : 0);
    }
    prevChip_ = v;
}

void Beacon::fill(SbTelemetry& t) const {
    t.timeSec = timeS_; t.inputRate = rate_; t.workRate = wrate_;
    const auto& L = lf_.lines();
    t.lines.clear();
    t.lineSpacingHz = 0;
    for (size_t i = 0; i < L.size(); i++) {
        SbLine l;
        l.offsetHz = L[i].hz; l.levelDb = (float)L[i].snrBinDb;
        l.relDb = (float)(L[i].peakDb - L[0].peakDb);
        l.side = i > 0 && std::fabs(L[i].hz - L[0].hz) < 120e3;
        t.lines.push_back(l);
        if (i > 0 && !l.side && t.lineSpacingHz == 0) t.lineSpacingHz = std::fabs(L[i].hz - L[0].hz);
    }
    t.rfSpectrum = lf_.plot();
    t.noiseDb = (float)noiseDbHz_;
    t.carrierLocked = haveCarrier_ && lockedRaw_;
    t.cfoHz = haveCarrier_ ? pllFreqHz_ : (L.empty() ? 0.0 : L[0].hz);
    t.levelDb = -200; t.snrDb = 0;
    if (!L.empty()) {
        // the level of the tracked line
        size_t bi = 0; double bd = 1e9;
        for (size_t i = 0; i < L.size(); i++) if (std::fabs(L[i].hz - t.cfoHz) < bd) { bd = std::fabs(L[i].hz - t.cfoHz); bi = i; }
        t.levelDb = (float)L[bi].peakDb;
        t.snrDb = (float)(L[bi].peakDb - noiseDbHz_);
    }
    // drift: least-squares slope of the last minutes
    t.driftHzPerMin = 0;
    if (freqHist_.size() >= 20) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0; const double n = (double)freqHist_.size();
        const double t0 = freqHist_.front().first;
        for (const auto& p : freqHist_) { const double x = p.first - t0; sx += x; sy += p.second; sxx += x * x; sxy += x * p.second; }
        const double den = n * sxx - sx * sx;
        if (den > 0) t.driftHzPerMin = 60.0 * (n * sxy - sx * sy) / den;
    }
    t.pmRmsDeg = (float)(errStd_ * 180.0 / math::kPi);
    t.tones = nTones_; t.subcarrierHz = subHz_; t.toneSpacingHz = spacingHz_; t.subcarrierDb = subDb_;
    const int ai = active();
    const Channel& ac = ch_[ai];
    t.coding = ai;
    t.baud = clkLock_ ? (ai == 1 ? clkB_ / 2 : clkB_) : 0;
    t.clockLocked = clkLock_;
    t.pmSpectrum = pmPlot_; t.pmSpanHz = wrate_ / 2;
    t.frameBits = ac.frameL; t.frameOffset = ac.frameOff; t.frameAgreement = ac.agree; t.fixedBits = ac.fixedBits;
    t.syncName = ac.syncIdx >= 0 ? Channel::syncWords()[(size_t)ac.syncIdx].name : "";
    t.counterByte = ac.counterByte; t.counterMod = ac.counterMod;
    t.bitsTotal = ac.total;
    t.framesTotal = ac.ok + ac.bad;
    t.blocksOk = ac.ok; t.blocksBad = ac.bad;
    t.templateBytes = ac.tmpl; t.fixedMask = ac.mask; t.colStability = ac.stab;
    t.frames.assign(ac.frames.begin(), ac.frames.end());
    // frames count as found when a known sync word starts them, or after many frames of a learned layout (early periodicities are chance)
    const bool found = ac.ok >= 4 && (ac.syncIdx >= 0 || ac.ok >= 24);
    t.dataValid = found;
    t.state = !haveCarrier_ ? 0 : (found ? 3 : (clkLock_ ? 2 : 1));
}

} // namespace sb
} // namespace dect2
