// Offline DVB-T2-like signal generator used for testing and the built-in synthetic source.
// Produces genuine P1 symbols (S1/S2 signalling) followed by P2/data symbols whose cells are random QPSK.
// Never transmits anything - it only writes samples to memory/files.
#pragma once
#include "ring.h"
#include "t2.h"
#include <random>
#include <vector>

namespace dect2 {

struct TxParams {
    int s1 = 0;          // 0 = T2-Base SISO
    int s2field1 = 1;    // FFT size code, see FftMode
    bool mixed = false;  // S2 field 2
    int giIdx = 4;       // see guardName()
    bool ext = false;    // extended carrier mode
    int dataSymbols = 0; // symbols per frame after P1 (incl. P2); 0 = auto (~60 ms)
    uint32_t seed = 1;
};

class T2Generator {
public:
    explicit T2Generator(const TxParams& p);
    ~T2Generator();
    const TxParams& params() const { return p_; }
    int fftN() const { return n_; }
    int guard() const { return g_; }
    int carriers() const { return k_; }
    int symbolsPerFrame() const { return symbols_; }
    size_t frameLength() const { return (size_t)kP1Len + (size_t)symbols_ * (n_ + g_); }
    void nextFrame(std::vector<cf32>& out); // one full frame: P1 + symbols

    struct Impl;

private:
    Impl* i_;
    TxParams p_;
    int n_, g_, k_, symbols_;
};

} // namespace dect2
