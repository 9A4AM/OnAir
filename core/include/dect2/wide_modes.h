// Whole-span decoding for the digital voice modes (DMR, M17, D-STAR, NXDN, dPMR, P25): one WideBank per mode. The engine routes the span to the
// bank of the running mode when the switch is on, and takes its telemetry (the selected carrier in detail, every carrier's calls merged in,
// plus the carriers table) in place of the single receiver's.
#pragma once
#include "carrierfind.h"
#include "ring.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dect2 {

struct RxTelemetry;

class WideModes {
public:
    WideModes();
    ~WideModes();
    static bool supports(int stdMode);           // engine standard: 10 DMR, 32 M17, 34 D-STAR, 39 NXDN, 46 dPMR, 51 P25
    static CarrierCfg cfgFor(int stdMode);       // the raster and limits of the mode
    // Sets the span rate; gridOriginHz: the tuned channel's offset from the span centre (the raster passes through it)
    void configure(int stdMode, double spanRate, double gridOriginHz);
    void reset();
    void clearResults();
    void feed(int stdMode, const cf32* x, size_t n);
    // fills t (standard, seq, state, the mode's telemetry, t.wide) when something changed since lastSeq
    bool telemetry(int stdMode, RxTelemetry& t, uint64_t& lastSeq);
    void setSelected(int carrierId);
    void setMuted(bool m);
    size_t liveCount(int stdMode) const;
    std::vector<double> liveHz(int stdMode) const;
    void setLogCallback(std::function<void(const std::string&)> cb);
    // test hook: rate of the span the bank of this mode was configured with
    bool ready(int stdMode) const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace dect2
