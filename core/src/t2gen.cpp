#include "dect2/t2gen.h"
#include <Accelerate/Accelerate.h>
#include <cmath>
#include <cstring>

namespace dect2 {

struct T2Generator::Impl {
    FFTSetup fft1k = nullptr, fftN = nullptr;
    int log2N = 0;
    std::mt19937 rng;
    cf32 p1c[kP1CLen], p1a[kP1ALen], p1b[kP1BLen];
    std::vector<float> re, im;
};

static void inverseFft(FFTSetup s, int log2n, std::vector<float>& re, std::vector<float>& im) {
    DSPSplitComplex sc{re.data(), im.data()};
    vDSP_fft_zip(s, &sc, 1, log2n, FFT_INVERSE);
}

static void buildP1(T2Generator::Impl& I, int s1, int s2code) {
    int mod[384], idx = 0;
    auto put = [&](uint8_t b) { for (int j = 7; j >= 0; j--) mod[idx++] = (b >> j) & 1; };
    for (int i = 0; i < 8; i++) put(kS1Patterns[s1][i]);
    for (int i = 0; i < 32; i++) put(kS2Patterns[s2code][i]);
    for (int i = 0; i < 8; i++) put(kS1Patterns[s1][i]);

    int rnd[384], sr = 0x4e46;
    for (int i = 0; i < 384; i++) {
        int b = (sr ^ (sr >> 1)) & 1;
        rnd[i] = b ? -1 : 1;
        sr >>= 1;
        if (b) sr |= 0x4000;
    }
    int d[385];
    d[0] = 1;
    for (int i = 1; i < 385; i++) d[i] = mod[i - 1] ? -d[i - 1] : d[i - 1];
    float c[384];
    for (int i = 0; i < 384; i++) c[i] = (float)(d[i + 1] * rnd[i]);

    auto synth = [&](int shift, cf32* out) {
        std::vector<float> re(1024, 0.f), im(1024, 0.f);
        for (int i = 0; i < 384; i++) {
            int f = kP1ActiveCarriers[i] + 86 - 512 + shift; // signed carrier index
            re[(f + 1024) % 1024] = c[i];
        }
        inverseFft(I.fft1k, 10, re, im);
        float sc = 1.0f / std::sqrt(384.0f);
        for (int n = 0; n < 1024; n++) out[n] = cf32(re[n] * sc, im[n] * sc);
    };
    cf32 shifted[1024];
    synth(0, I.p1a);
    synth(1, shifted); // +1 bin  ==  multiply by exp(+j 2 pi n / 1024)
    memcpy(I.p1c, shifted, sizeof I.p1c);
    memcpy(I.p1b, shifted + 542, sizeof I.p1b);
}

T2Generator::T2Generator(const TxParams& p) : i_(new Impl), p_(p) {
    const FftMode* m = fftModeFromS2(p.s2field1);
    if (!m || p.s2field1 > 5) m = fftModeFromS2(1);
    p_.s2field1 = m->code;
    n_ = m->n;
    g_ = guardSamples(n_, p.giIdx);
    nP2_ = m->nP2;
    PilotConfig pc;
    pc.fftCode = m->code; pc.ext = p.ext && m->kExt; pc.pp = p.pp; pc.tr = p.tr; pc.giIdx = p.giIdx;
    pm_.reset(new PilotMap(pc));
    for (int pp = 0; !pm_->valid() && pp < 8; pp++) { pc.pp = pp; pm_.reset(new PilotMap(pc)); }
    p_.pp = pc.pp;
    k_ = pm_->carriers();
    i_->log2N = (int)std::lround(std::log2((double)n_));
    i_->fft1k = vDSP_create_fftsetup(10, kFFTRadix2);
    i_->fftN = vDSP_create_fftsetup(i_->log2N, kFFTRadix2);
    i_->rng.seed(p.seed);
    i_->re.resize(n_);
    i_->im.resize(n_);
    if (p.dataSymbols > 0) symbols_ = p.dataSymbols;
    else {
        double tsym = (double)(n_ + g_) * 7.0 / 64.0 * 1e-6; // seconds at 8 MHz
        symbols_ = std::max(m->nP2 + 2, (int)std::lround(0.060 / tsym));
        symbols_ = std::min(symbols_, 400);
    }
    buildP1(*i_, p.s1, (p.s2field1 << 1) | (p.mixed ? 1 : 0));
}

T2Generator::~T2Generator() {
    vDSP_destroy_fftsetup(i_->fft1k);
    vDSP_destroy_fftsetup(i_->fftN);
    delete i_;
}

void T2Generator::nextFrame(std::vector<cf32>& out) {
    Impl& I = *i_;
    out.resize(frameLength());
    cf32* o = out.data();
    memcpy(o, I.p1c, sizeof I.p1c); o += kP1CLen;
    memcpy(o, I.p1a, sizeof I.p1a); o += kP1ALen;
    memcpy(o, I.p1b, sizeof I.p1b); o += kP1BLen;

    const float a = 0.70710678f;
    const float norm = 5.0f / std::sqrt(27.0f * (float)k_);
    std::vector<uint8_t> types;
    for (int s = 0; s < symbols_; s++) {
        pm_->symbolTypes(s, symbols_, types);
        std::fill(I.re.begin(), I.re.end(), 0.f);
        std::fill(I.im.begin(), I.im.end(), 0.f);
        for (int k = 0; k < k_; k++) {
            int f = k - (k_ - 1) / 2;
            size_t bin = (size_t)((f + n_) % n_);
            switch (types[k]) {
            case kCellData: {
                uint32_t r = I.rng();
                I.re[bin] = (r & 1) ? a : -a;
                I.im[bin] = (r & 2) ? a : -a;
            } break;
            case kCellP2Papr:
            case kCellTrPapr: break;
            default: I.re[bin] = pm_->pilot(s, k, types[k]).real(); break;
            }
        }
        inverseFft(I.fftN, I.log2N, I.re, I.im);
        for (int i = 0; i < g_; i++) o[i] = cf32(I.re[n_ - g_ + i], I.im[n_ - g_ + i]) * norm;
        for (int i = 0; i < n_; i++) o[g_ + i] = cf32(I.re[i], I.im[i]) * norm;
        o += n_ + g_;
    }
}

} // namespace dect2
