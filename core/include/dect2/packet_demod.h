// The radio side of AX.25 packet, shared by the APRS / Packet receiver (packet_rx.cpp) and the CubeSat telemetry receiver (cubesat_rx.cpp):
// an FM front end (mix out the signal offset, decimate to about 100 kHz, resample to exactly 96 kHz, AFC, channel filter, discriminator,
// carrier and SNR from 20 ms power blocks), the 1200 baud Bell 202 AFSK demodulator and the direct FSK slicer of G3RUH 9600 baud (and of
// other plain 2-FSK / GMSK speeds, with or without the descrambler).
#pragma once
#include "dmr_dsp.h"
#include "exact_resampler.h"
#include "packet_ax25.h"
#include "ring.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace dect2 {
namespace pktdemod {

constexpr double kMidRate = 96000.0;           // the front end's output rate
constexpr double kAudioRate = 48000.0;         // the AFSK demodulators' rate
constexpr size_t kBlock = 1920;                // power blocks of 20 ms at 96 kHz

// FM front end. process() gives the discriminator in Hz at 96 kHz; commit() afterwards moves the sample clock n96 on (the demodulators
// running on the output in between still see the clock of the block's start, as their callbacks expect).
struct FmFront {
    // pass / stop: the channel filter (Hz); clipHz: FM clicks are cut to this deviation; afcMaxHz: how far the AFC follows a carrier
    double chanPass = 9000.0, chanStop = 17000.0;
    float clipHz = 8000.f;
    double afcMaxHz = 12000.0;
    double fs = 0, offsetHz = 0, offPhase = 0;
    bool ready = false;
    std::vector<dmr::Fir<cf32>> stages;
    ExactResampler rs;
    bool rsPass = true;
    dmr::Fir<cf32> chan;
    cf32 prevY = cf32(1.f, 0.f);
    std::vector<cf32> bufA, bufB, mixBuf, o2, ch, clean, afcBuf;
    std::vector<float> disc;
    // level, carrier, AFC
    double blkSum = 0, blkDisc = 0; size_t blkN = 0;
    std::deque<double> blocks;
    double nf = 0; bool nfKnown = false;
    bool carrier = false;
    int64_t lastCarrier = -(int64_t)1e12, carrierSince = 0;
    double snrEma = 0, cfoEma = 0; bool haveSnr = false;
    double afcHz = 0, afcEst = 0, afcPhase = 0, lastBlkCfo = 0;
    double inPower = 0; int64_t inN = 0;
    int64_t n96 = 0;
    size_t pending = 0;

    // false when the rate is under minRate or cannot be brought to 96 kHz
    bool configure(double rate, double minRate);
    void setOffset(double hz) { offsetHz = hz; offPhase = 0; }
    void reset();
    const std::vector<float>& process(const cf32* x, size_t n);
    void commit() { n96 += (int64_t)pending; pending = 0; }
    double cfoHz() const { return afcHz + cfoEma; }
    float takeLevelDb() { const float l = inN ? (float)(10 * std::log10(inPower / (double)inN + 1e-20)) : -200.f; inPower = 0; inN = 0; return l; }
    bool carrierRecent(double sec) const { return carrier || (n96 - lastCarrier) < (int64_t)(sec * kMidRate); }
private:
    void blockEnd();
};

// 1200 baud demodulator: correlate the 48 kHz audio with the mark and space tones over a window of about one bit; the stronger tone is
// the level; a clock loop picks the level once per bit; the levels go to the HDLC layer (which does NRZI, so the polarity does not matter).
struct Afsk1200 {
    int len = 40;
    float gain = 1.f, inertia = 0.75f, inertiaLocked = 0.85f;
    std::vector<float> mr, mi, sr, si;
    std::vector<float> ext;
    float ph = -0.5f;
    bool lev = false, prevLev = false;
    ax25::HdlcRx hdlc;
    int64_t pos = 0;
    int snapBits_ = 0;               // after a good frame: snap the clock on edges while the next preamble arrives

    void init(int L, bool hann, float g, float inert, float inertLocked) {
        constexpr double kPi = 3.14159265358979323846;
        len = L; gain = g; inertia = inert; inertiaLocked = inertLocked;
        mr.assign((size_t)L, 0.f); mi = mr; sr = mr; si = mr;
        for (int t = 0; t < L; t++) {
            const int k = L - 1 - t;
            const double w = hann ? 0.5 - 0.5 * std::cos(2 * kPi * (k + 0.5) / L) : 1.0;
            mr[(size_t)t] = (float)(w * std::cos(2 * kPi * 1200.0 * k / kAudioRate));
            mi[(size_t)t] = (float)(w * std::sin(2 * kPi * 1200.0 * k / kAudioRate));
            sr[(size_t)t] = (float)(w * std::cos(2 * kPi * 2200.0 * k / kAudioRate));
            si[(size_t)t] = (float)(w * std::sin(2 * kPi * 2200.0 * k / kAudioRate));
        }
        reset();
    }
    void reset() {
        ext.assign((size_t)len - 1, 0.f);
        ph = -0.5f; lev = prevLev = false; pos = 0; snapBits_ = 0;
        hdlc.reset();
    }
    void process(const float* x, size_t n) {
        const size_t h = (size_t)len - 1;
        ext.resize(h + n);
        std::copy(x, x + n, ext.begin() + (ptrdiff_t)h);
        const float step = (float)(1200.0 / kAudioRate);
        for (size_t i = 0; i < n; i++) {
            const float* p = &ext[i];
            float a = 0, b = 0, c = 0, d = 0;
            for (int t = 0; t < len; t++) {
                const float v = p[t];
                a += v * mr[(size_t)t]; b += v * mi[(size_t)t];
                c += v * sr[(size_t)t]; d += v * si[(size_t)t];
            }
            lev = (a * a + b * b) > gain * gain * (c * c + d * d);
            if (lev != prevLev) {
                if (snapBits_ > 0) { ph = 0.f; snapBits_--; }
                else ph *= hdlc.flagSeen() ? inertiaLocked : inertia;
            }
            prevLev = lev;
            ph += step;
            if (ph >= 0.5f) {
                ph -= 1.f;
                hdlc.bit(lev ? 1 : 0);
                if (hdlc.takeClockResync()) { ph = 0.f; snapBits_ = 96; }
            }
            pos++;
        }
        std::copy(ext.end() - (ptrdiff_t)h, ext.end(), ext.begin());
        ext.resize(h);
    }
};

// Direct 2-FSK on the discriminator output (G3RUH 9600 baud, or GMSK / FSK at another speed): slicer threshold from tracked peaks, clock
// loop, then optionally the G3RUH descrambler (x^17 + x^12 + 1). The bits go to the HDLC layer (NRZI there), or, with bitSink set, out as
// they are (an ASM framer; then `locked` tells the clock loop to slow down inside a frame).
struct FskSlicer {
    double rate = kMidRate, baud = 9600.0;
    bool descramble = true;
    float inertia = 0.7f, inertiaLocked = 0.8f;
    float hi = 0, lo = 0, ph = -0.5f;
    bool lev = false, prevLev = false;
    uint32_t sr = 0;
    ax25::HdlcRx hdlc;
    std::function<void(int)> bitSink;
    bool locked = false;
    int64_t pos = 0;
    void init(float inert, float inertLocked) { inertia = inert; inertiaLocked = inertLocked; reset(); }
    void reset() { hi = lo = 0; ph = -0.5f; lev = prevLev = false; sr = 0; pos = 0; locked = false; hdlc.reset(); }
    void process(const float* x, size_t n) {
        const float step = (float)(baud / rate);
        for (size_t i = 0; i < n; i++) {
            const float v = x[i];
            if (v > hi) hi += 0.25f * (v - hi); else hi += 0.0015f * (v - hi);
            if (v < lo) lo += 0.25f * (v - lo); else lo += 0.0015f * (v - lo);
            lev = v > 0.5f * (hi + lo);
            if (lev != prevLev) ph *= (bitSink ? locked : hdlc.flagSeen()) ? inertiaLocked : inertia;
            prevLev = lev;
            ph += step;
            if (ph >= 0.5f) {
                ph -= 1.f;
                const uint32_t raw = lev ? 1u : 0u;
                uint32_t d = raw;
                if (descramble) {
                    d = raw ^ ((sr >> 11) & 1u) ^ ((sr >> 16) & 1u);
                    sr = ((sr << 1) | raw) & 0x1FFFFu;
                }
                if (bitSink) bitSink((int)d);
                else hdlc.bit((int)d);
            }
            pos++;
        }
    }
};

} // namespace pktdemod
} // namespace dect2
