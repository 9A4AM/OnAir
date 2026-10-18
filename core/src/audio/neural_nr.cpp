// Neural noise remover: see dect2/audio/neural_nr.h. Rate path: x (rate r) -> zero-stuff + Kaiser low-pass to 48 kHz -> 480-sample frames
// -> RNNoise -> low-pass + decimate back to r -> FIFO primed with one frame's worth of zeros (so a sample comes out for every sample in).
#include "dect2/audio/neural_nr.h"
#include "dect2/common/window.h"
extern "C" {
#include "rnnoise.h"
}
#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

namespace dect2 {

namespace {
constexpr int kFrame = 480;     // RNNoise: 10 ms at 48 kHz
constexpr double kPiD = 3.14159265358979323846;
constexpr int kHalfWidth = 8;   // resampling filter half-width in samples of the audio rate
constexpr int kFifo = 4096;     // power of two

double bessel0(double x) { return dect2::window::besselI0(x, 40, 0.0); }
std::mutex gCreateMu;   // rnnoise builds shared tables the first time a state is made
}

struct NeuralNr::Impl {
    int L = 1, M = 0, T = 1, P = 0, D = 0;
    DenoiseState* st = nullptr;
    std::vector<float> h;        // low-pass, unity DC gain, T taps
    std::vector<float> xh;       // input history at the audio rate (twice, so a window never wraps)
    int xpos = 0;
    std::vector<float> dh;       // 48 kHz history for the decimator
    int dpos = 0;
    int dphase = 0;
    float fb[kFrame], fo[kFrame];
    int fi = 0;
    float fifo[kFifo];
    unsigned rd = 0, wr = 0;     // monotonically increasing; masked on access
    std::vector<float> dly;      // dry path delay line, D samples
    int dlyPos = 0;
    float sPrev = 1.f;
    bool wasOn = false;

    void clear() {
        std::fill(xh.begin(), xh.end(), 0.f); std::fill(dh.begin(), dh.end(), 0.f);
        std::fill(dly.begin(), dly.end(), 0.f);
        xpos = dpos = dphase = fi = dlyPos = 0;
        std::fill(fb, fb + kFrame, 0.f);
        rd = wr = 0;
        for (int i = 0; i < P; i++) fifo[wr++ & (kFifo - 1)] = 0.f;
    }
    void pushDown(float v, float* outFloat, bool& have) {   // one 48 kHz sample in; a rate-r sample out every L-th
        if (L == 1) { *outFloat = v; have = true; return; }
        dh[(size_t)dpos] = v; dh[(size_t)(dpos + T)] = v;
        if (++dpos >= T) dpos = 0;
        have = false;
        const bool pick = dphase == 0;   // the first of each L samples: input sample q sits at 48 kHz index qL + 2*M*L + 480
        if (++dphase >= L) dphase = 0;
        if (!pick) return;
        const float* w = &dh[(size_t)dpos];   // the last T samples, oldest first
        float a = 0;
        for (int k = 0; k < T; k++) a += h[(size_t)k] * w[k];
        *outFloat = a; have = true;
    }
    void frame(float* vadOut) {
        *vadOut = rnnoise_process_frame(st, fo, fb);
        for (int i = 0; i < kFrame; i++) {
            float y; bool have;
            pushDown(fo[i] * (1.f / 32768.f), &y, have);
            if (have) fifo[wr++ & (kFifo - 1)] = y;
        }
    }
};

bool NeuralNr::rateSupported(int r) { return r >= 8000 && r <= 48000 && 48000 % r == 0; }

NeuralNr::NeuralNr(int sampleRateHz) : p_(new Impl), rate_(sampleRateHz) {
    ok_ = rateSupported(sampleRateHz);
    if (!ok_) return;
    Impl& m = *p_;
    m.L = 48000 / sampleRateHz;
    m.M = m.L == 1 ? 0 : kHalfWidth;
    m.T = 2 * m.M * m.L + 1;
    m.P = kFrame / m.L;
    m.D = 2 * m.M + m.P + (kFrame - 1 + m.L / 2) / m.L;   // filters + the priming zeros + RNNoise's own delay (479 samples at 48 kHz, measured)
    lat_ = m.D;
    if (m.L > 1) {
        m.h.resize((size_t)m.T);
        const double fc = 0.45 / m.L, beta = 8.0, c = (m.T - 1) * 0.5, i0b = bessel0(beta);
        double sum = 0;
        std::vector<double> t((size_t)m.T);
        for (int k = 0; k < m.T; k++) {
            const double x = k - c, r = x / c;
            const double sinc = x == 0 ? 2 * fc : std::sin(2 * kPiD * fc * x) / (kPiD * x);
            t[(size_t)k] = sinc * bessel0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / i0b;
            sum += t[(size_t)k];
        }
        for (int k = 0; k < m.T; k++) m.h[(size_t)k] = (float)(t[(size_t)k] / sum);
        m.xh.assign((size_t)(2 * (2 * m.M + 1)), 0.f);
        m.dh.assign((size_t)(2 * m.T), 0.f);
    }
    m.dly.assign((size_t)m.D, 0.f);
    {
        std::lock_guard<std::mutex> lk(gCreateMu);
        m.st = rnnoise_create(nullptr);
    }
    if (!m.st) { ok_ = false; return; }
    m.clear();
}

NeuralNr::~NeuralNr() {
    if (p_ && p_->st) {
        std::lock_guard<std::mutex> lk(gCreateMu);
        rnnoise_destroy(p_->st);
    }
}

void NeuralNr::setStrength(float s) { str_.store(std::clamp(s, 0.f, 1.f), std::memory_order_relaxed); }

void NeuralNr::reset() { if (ok_) { p_->clear(); p_->sPrev = str_.load(std::memory_order_relaxed); } }

void NeuralNr::process(float* x, size_t n) {
    if (!ok_ || !x || n == 0) return;
    Impl& m = *p_;
    const bool on = on_.load(std::memory_order_relaxed);
    if (!on) { m.wasOn = false; return; }   // bypass: the samples are not touched
    if (!m.wasOn) { m.clear(); m.sPrev = str_.load(std::memory_order_relaxed); m.wasOn = true; }
    const float s1 = str_.load(std::memory_order_relaxed);
    const float s0 = m.sPrev;
    const float ds = (s1 - s0) / (float)n;
    float s = s0, vad = vad_.load(std::memory_order_relaxed);
    const int L = m.L, M = m.M, nx = 2 * M + 1;
    for (size_t i = 0; i < n; i++) {
        const float in = x[i];
        if (L == 1) {
            m.fb[m.fi++] = in * 32768.f;
        } else {
            m.xh[(size_t)m.xpos] = in; m.xh[(size_t)(m.xpos + nx)] = in;
            if (++m.xpos >= nx) m.xpos = 0;
            const float* w = &m.xh[(size_t)m.xpos];   // the last nx inputs, oldest first: w[nx-1] is the newest
            for (int p = 0; p < L; p++) {
                float a = 0;
                for (int k = 0; k < nx; k++) {         // tap index p + k*L against the input k samples back
                    const int ti = p + k * L;
                    if (ti >= m.T) break;
                    a += m.h[(size_t)ti] * w[nx - 1 - k];
                }
                m.fb[m.fi++] = a * (float)L * 32768.f;
            }
        }
        if (m.fi >= kFrame) { m.fi = 0; m.frame(&vad); }
        float wet = 0;
        if (m.wr != m.rd) wet = m.fifo[m.rd++ & (kFifo - 1)];
        const float dry = m.dly[(size_t)m.dlyPos];     // x, D samples ago
        m.dly[(size_t)m.dlyPos] = in;
        if (++m.dlyPos >= m.D) m.dlyPos = 0;
        s += ds;
        float y = dry + s * (wet - dry);
        x[i] = std::clamp(y, -1.f, 1.f);
    }
    m.sPrev = s1;
    vad_.store(vad, std::memory_order_relaxed);
}

} // namespace dect2
