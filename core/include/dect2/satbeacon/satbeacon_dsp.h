// Satellite beacon receiver parts, written for the live receiver (satbeacon_live.cpp) and tested on their own (tests/satbeacon).
// The signal class: an unmodulated or phase-modulated carrier from a geostationary satellite (a Ku or Ka band beacon, seen through an LNB),
// sometimes with telemetry on a subcarrier (here: BPSK on one or two tones around a subcarrier, sent as short frames), and often a second
// unmodulated reference carrier a few hundred kHz away.
//   LineFinder  : averaged spectrum of the whole capture, narrow lines, their level over the noise (C/N0)
//   Beacon      : the chain: carrier mixer and decimator, carrier PLL (the phase error is the PM signal), PM spectrum, subcarrier tone search,
//                 per-tone Costas loops, bit clock from the edges, bits, frame length / frame start / template learning
// Written for any sample rate from 250 kHz up; the decimated work rate is 200 to 400 kHz.
#pragma once
#include "dect2/satbeacon/satbeacon_tel.h"
#include "dect2/common/ring.h"
#include "dect2/dsp/fftutil.h"
#include <complex>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace dect2 {
namespace sb {

struct Line { double hz; double peakDb; double snrBinDb; };   // hz relative to the centre; peakDb: dBFS of a tone with this amplitude

class LineFinder {
public:
    void configure(double rate);
    void reset();
    // true when a new spectrum is ready (about once a second)
    bool push(const cf32* x, size_t n);
    const std::vector<Line>& lines() const { return lines_; }   // strongest first
    double noiseDbHz() const { return noiseDbHz_; }              // dBFS in 1 Hz
    double binHz() const { return rate_ / nfft_; }
    const std::vector<float>& plot() const { return plot_; }      // kSbSpecPts max-hold points, dB over the noise
private:
    void analyse();
    double rate_ = 0;
    int nfft_ = 0, fill_ = 0, nAvg_ = 0;
    std::unique_ptr<Fft> fft_;
    std::vector<cf32> buf_;
    std::vector<float> win_, psd_;
    std::vector<Line> lines_;
    std::vector<float> plot_;
    double noiseDbHz_ = -200, sumW_ = 1, sumW2_ = 1;
};

// A bit stream and what is learned from it: the frame length, the frame start, a template, the frames cut out, the counter byte.
// The receiver keeps two of them, one reading the chips as NRZ bits and one as bi-phase, and shows the one the chips look like.
class Channel {
public:
    void reset();
    void push(int bit);
    // the known sync words
    struct Sync { const char* name; int bits; uint32_t word; };
    static const std::vector<Sync>& syncWords();
    uint64_t total = 0;
    int frameL = 0, frameOff = 0, fixedBits = 0, syncIdx = -1, counterByte = -1, counterMod = 0;
    float agree = 0.5f;
    uint64_t ok = 0, bad = 0;
    std::vector<uint8_t> tmpl, mask;
    std::vector<float> stab;
    std::deque<SbFrame> frames;
    const std::vector<uint8_t>& recent() const { return bits_; }
private:
    void learn();
    bool findSync(const std::vector<uint8_t>& last, uint64_t first, int& L, int& off, bool& inv, int& idx) const;
    void exportFrames();
    void findCounter();
    std::vector<uint8_t> bits_;
    uint64_t base_ = 0, nextLearn_ = 2400, nextFrame_ = 0;
    bool inv_ = false;
    int folded_ = 0, prevCounter_ = -1, ctrMax_ = 0;
    bool ctrWrap_ = false;
    uint32_t frameId_ = 1;
};

class Beacon {
public:
    Beacon();
    ~Beacon();
    void configure(double inputRate);
    void reset();
    void process(const cf32* x, size_t n);
    void fill(SbTelemetry& t) const;
    double workRate() const { return wrate_; }
    const std::vector<uint8_t>& recentBits() const { return ch_[active()].recent(); }   // tests
private:
    int active() const { return biphase_ ? 1 : 0; }   // the stream the telemetry shows
    void startCarrier(double hz);
    void feedWork(cf32 y);
    void pmBlock();
    void startSub();
    void feedSub(float p);
    void soft(float s);
    void chip(float v);
    double rate_ = 0, wrate_ = 0, frontHz_ = 0, frontPh_ = 0, pllFreqHz_ = 0;
    int dec_ = 1;
    std::vector<float> taps_;
    std::vector<cf32> hist_;
    size_t hpos_ = 0;
    int phaseCnt_ = 0;
    LineFinder lf_;
    bool haveCarrier_ = false;
    double noiseDbHz_ = -200;
    // carrier loop
    double pllPh_ = 0, pllW_ = 0, kp_ = 0, ki_ = 0;
    bool lockedRaw_ = false;
    double avgRe_ = 0, avgIm_ = 0, avgAbs_ = 0, errAcc_ = 0, errN_ = 0, errStd_ = 0;
    std::deque<std::pair<double, double>> freqHist_;   // (time s, carrier Hz)
    double lastHistT_ = 0, timeS_ = 0;
    int lostCnt_ = 0, farCnt_ = 0;
    uint64_t nWork_ = 0;
    // PM spectrum
    static constexpr int kPmFft = 8192;
    Fft pmFft_{kPmFft};
    std::vector<float> pmBuf_, pmWin_, pmAcc_;
    int pmFill_ = 0, pmCount_ = 0;
    std::vector<float> pmPlot_;
    double pmNoise_ = 0;
    // subcarrier
    int nTones_ = 0;
    double subHz_ = 0, spacingHz_ = 0, toneSetT_ = 0;
    float subDb_ = 0;
    struct Sub {
        double ph = 0, cph = 0, cfr = 0;
        std::vector<cf32> hist;
        std::vector<float> taps;
        size_t hp = 0;
        int cnt = 0, M = 1;
        double fs = 0;
        bool on = false;
    } sub_;
    double ampAvg_ = 0;
    // clock
    std::vector<float> edgeBuf_;
    float prevS_ = 0;
    double baud_ = 0, clkPh_ = 0, clkB_ = 0, ampEdge_ = 0, accBit_ = 0, clkLockAvg_ = 0;   // baud_: the chip (symbol) rate
    std::complex<double> clkZ_;
    bool clkLock_ = false, clkInit_ = false;
    int64_t lastIdx_ = 0;
    // chips and bi-phase pairing
    float prevChip_ = 0;
    uint64_t chipN_ = 0;
    double pairD_[2] = {0, 0}, chipAbs_ = 0;
    bool biphase_ = false;
    Channel ch_[2];   // 0: the chips as NRZ bits, 1: bi-phase pairs
};

// the frame length of a bit stream by self-agreement; 0 when there is no repetition. agreement receives the match fraction at that length
int frameLengthOf(const std::vector<uint8_t>& bits, float* agreement);

} // namespace sb
} // namespace dect2
