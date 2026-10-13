// Carrier finder and channelizer for "decode the whole visible span": from the power spectrum of the captured span, find the carriers that sit
// on a channel raster (12.5 kHz, 25 kHz, 6.25 kHz or any step), keep each one tracked while it talks and for a holding time after it goes quiet,
// and bring each one down to a baseband stream at the rate a single-channel decoder wants. Nothing here knows a protocol.
//
//   span samples -> windowed FFT, averaged -> power in each raster cell -> noise floor (low percentile of the cells) -> threshold with
//   hysteresis (on above noise + onDb, stays above noise + offDb) and a debounce -> tracked carriers (at most maxCarriers: the active and
//   the strongest first, a newcomer must beat a tracked one by 6 dB to push it out) -> Channelizer (mixer + resampler) per carrier.
//
// Frequencies are in Hz relative to the centre of the span (0 = the middle of the samples). The raster is gridOriginHz + k * stepHz.
#pragma once
#include "resampler.h"
#include "ring.h"
#include <complex>
#include <memory>
#include <vector>

namespace dect2 {

struct CarrierCfg {
    double stepHz = 12500;          // channel raster (6250, 12500, 25000 or any)
    double gridOriginHz = 0;        // a channel centre sits at gridOriginHz + k * stepHz (relative to the span centre)
    double onDb = 9;                // a cell must be this far above the noise floor to count as a carrier
    double offDb = 5;               // and it counts as quiet only below this (hysteresis)
    double holdSec = 8;             // a carrier stays tracked this long after it went quiet
    double debounceSec = 0.3;       // it must stay above the threshold this long before it is reported
    int maxCarriers = 8;            // simultaneous carriers (the CPU budget of the decoders)
    double avgSec = 0.1;            // one spectrum estimate covers this much signal
    double skirtDb = 30;            // a cell this far below a neighbour is that neighbour's skirt, not a carrier
    double edgeFrac = 0.04;         // the outer part of the span (filter roll-off) is not searched
};

struct Carrier {
    int id = 0;                     // stable while it is tracked, never reused
    double hz = 0;                  // centre frequency relative to the span centre (the raster cell centre moved to the measured centroid)
    double cellHz = 0;              // the raster cell centre
    double levelDb = -200;          // power in the cell, dB relative to full scale (rms 1.0 over the whole span)
    double snrDb = 0;               // above the noise floor in the same cell width
    bool active = false;            // above the threshold now
    double ageSec = 0;              // since it was first reported
    double idleSec = 0;             // since it was last active (0 while active)
};

class CarrierFinder {
public:
    void configure(double spanRateHz, const CarrierCfg& cfg);
    void reset();
    void feed(const cf32* x, size_t n);
    const std::vector<Carrier>& carriers() const { return cars_; }
    // ids of carriers dropped (hold time over, or pushed out by the cap) since the last call
    std::vector<int> takeRemoved();
    double noiseDb() const { return noiseDb_; }
    uint64_t updates() const { return updates_; }
    const CarrierCfg& cfg() const { return cfg_; }
    double rate() const { return rate_; }

private:
    void update();
    CarrierCfg cfg_;
    double rate_ = 0, noiseDb_ = -200;
    int nfft_ = 0, fill_ = 0, nAvg_ = 0, avgTarget_ = 1;
    std::vector<float> win_;
    std::vector<double> acc_;
    std::vector<cf32> buf_;
    struct FftHolder;
    std::shared_ptr<FftHolder> fft_;
    struct Track { Carrier c; double aboveSec = 0; bool reported = false; };
    std::vector<Track> trk_;
    std::vector<Carrier> cars_;
    std::vector<int> removed_;
    int nextId_ = 1;
    uint64_t updates_ = 0;
    bool noiseInit_ = false;
};

// One carrier brought to baseband: mixed down by shiftHz (the carrier's frequency in the span), low-pass filtered and resampled to outRate.
class Channelizer {
public:
    bool configure(double inRate, double outRate, double shiftHz, double occupiedHz);
    void reset();
    // appends the carrier's baseband samples to `out`
    void process(const cf32* x, size_t n, std::vector<cf32>& out);
    double shiftHz() const { return shift_; }

private:
    RationalResampler rs_;
    std::vector<cf32> mix_;
    double inRate_ = 0, shift_ = 0, phase_ = 0;
};

} // namespace dect2
