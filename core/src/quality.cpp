#include "dect2/quality.h"
#include <algorithm>
#include <cmath>

namespace dect2 {

double requiredSnrDb(const PlpFec& f) {
    // rows: QPSK, 16-QAM, 64-QAM, 256-QAM; columns: code rate 1/2, 3/5, 2/3, 3/4, 4/5, 5/6
    static const double t[4][6] = {{1.0, 2.2, 3.1, 4.1, 4.7, 5.2},
                                   {6.0, 7.7, 8.9, 10.2, 11.0, 11.6},
                                   {9.2, 11.4, 13.1, 14.9, 15.9, 16.7},
                                   {12.1, 14.6, 17.0, 18.9, 20.3, 21.4}};
    const int m = std::max(0, std::min(3, f.mod));
    const int r = f.rate >= 0 && f.rate < 6 ? f.rate : 0;
    return t[m][r] + (f.rotation ? 0.0 : 0.0);
}

void QualityMeter::reset() { *this = QualityMeter(); }

void QualityMeter::update(const RxTelemetry& rx) {
    if (!rx.dataValid) { if (rx.state != 2) { rep_ = QualityReport(); rep_.label = "no lock"; } return; }
    if (rx.dataFrames != lastFrames_) {
        if (lastFrames_ != ~0ull && rx.blocksOk + rx.blocksBad >= lastOk_ + lastBad_) {
            hist_.push_back({rx.blocksOk - lastOk_, rx.blocksBad - lastBad_});
            if (hist_.size() > 12) hist_.pop_front();
        }
        lastFrames_ = rx.dataFrames; lastOk_ = rx.blocksOk; lastBad_ = rx.blocksBad;
    }
    uint64_t ok = 0, bad = 0;
    for (auto& h : hist_) { ok += h.first; bad += h.second; }
    QualityReport r;
    r.valid = true;
    r.snrDb = rx.dataSnrDb;
    r.requiredDb = rx.plpValid ? requiredSnrDb(rx.plpFec) : 8.0;
    r.marginDb = r.snrDb - r.requiredDb;
    r.fecOk = (ok + bad) ? (double)ok / (double)(ok + bad) : (rx.plpValid ? 0.0 : 1.0);
    // 0 dB of margin is the edge of reception (25%); +6 dB or more is comfortable (100%)
    const double qs = std::max(0.0, std::min(100.0, 25.0 + r.marginDb * 12.5));
    double q = qs * (rx.plpValid && !hist_.empty() ? r.fecOk * r.fecOk : 1.0);
    smooth_ = rep_.valid ? smooth_ + 0.25 * (q - smooth_) : q;
    r.percent = smooth_;
    r.label = r.percent >= 85 ? "excellent" : r.percent >= 65 ? "good" : r.percent >= 40 ? "marginal" : "poor";
    rep_ = r;
}

} // namespace dect2
