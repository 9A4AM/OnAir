// Carrier finder and channelizer: see carrierfind.h.
#include "dect2/carrierfind.h"
#include "dect2/fftutil.h"
#include <algorithm>
#include <cmath>

namespace dect2 {

struct CarrierFinder::FftHolder { explicit FftHolder(int n) : f(n) {} Fft f; };

void CarrierFinder::configure(double spanRateHz, const CarrierCfg& cfg) {
    cfg_ = cfg; rate_ = spanRateHz;
    // about 24 bins across a raster cell
    const double want = spanRateHz / std::max(1.0, cfg.stepHz / 24.0);
    nfft_ = 256; while (nfft_ < want && nfft_ < 65536) nfft_ <<= 1;
    win_.assign((size_t)nfft_, 0.f);
    for (int i = 0; i < nfft_; i++) win_[(size_t)i] = 0.5f - 0.5f * (float)std::cos(2 * M_PI * (i + 0.5) / nfft_);
    fft_ = std::make_shared<FftHolder>(nfft_);
    avgTarget_ = std::max(2, (int)std::lround(cfg.avgSec * spanRateHz / nfft_));
    reset();
}

void CarrierFinder::reset() {
    acc_.assign((size_t)nfft_, 0.0); buf_.assign((size_t)nfft_, cf32(0, 0));
    fill_ = 0; nAvg_ = 0; trk_.clear(); cars_.clear(); removed_.clear(); noiseDb_ = -200; noiseInit_ = false;
}

std::vector<int> CarrierFinder::takeRemoved() { std::vector<int> r; r.swap(removed_); return r; }

void CarrierFinder::feed(const cf32* x, size_t n) {
    if (nfft_ <= 0) return;
    std::vector<cf32> w((size_t)nfft_);
    for (size_t i = 0; i < n; i++) {
        buf_[(size_t)fill_++] = x[i];
        if (fill_ == nfft_) {
            for (int k = 0; k < nfft_; k++) w[(size_t)k] = buf_[(size_t)k] * win_[(size_t)k];
            fft_->f.forward(w.data());
            for (int k = 0; k < nfft_; k++) acc_[(size_t)k] += (double)std::norm(w[(size_t)k]);
            fill_ = 0;
            if (++nAvg_ >= avgTarget_) { update(); std::fill(acc_.begin(), acc_.end(), 0.0); nAvg_ = 0; }
        }
    }
}

void CarrierFinder::update() {
    const double dt = (double)nfft_ * avgTarget_ / rate_;
    updates_++;
    double wsum = 0; for (float v : win_) wsum += (double)v * v;
    const double norm = 1.0 / ((double)nAvg_ * wsum * (double)nfft_);   // power per bin as mean square per sample (full scale rms 1)
    const double binHz = rate_ / nfft_, step = cfg_.stepHz;
    auto binPow = [&](int k) { return acc_[(size_t)(((k % nfft_) + nfft_) % nfft_)] * norm; };   // k: signed bin, 0 = DC
    auto db = [](double p) { return 10 * std::log10(std::max(1e-20, p)); };
    // raster cells inside the searched part of the span
    const double half = rate_ / 2 * (1.0 - cfg_.edgeFrac) - step / 2;
    const int kMin = (int)std::ceil((-half - cfg_.gridOriginHz) / step), kMax = (int)std::floor((half - cfg_.gridOriginHz) / step);
    if (kMax < kMin) return;
    const int nc = kMax - kMin + 1;
    std::vector<double> cell((size_t)nc), cen((size_t)nc);
    std::vector<int> nbins((size_t)nc);
    for (int c = 0; c < nc; c++) {
        const double f = cfg_.gridOriginHz + (kMin + c) * step; cen[(size_t)c] = f;
        const int b0 = (int)std::ceil((f - step * 0.4) / binHz), b1 = (int)std::floor((f + step * 0.4) / binHz);
        double p = 0; int nb = 0;
        for (int b = b0; b <= b1; b++) { p += binPow(b); nb++; }
        cell[(size_t)c] = p; nbins[(size_t)c] = nb;
    }
    // noise floor: a low percentile of the cells (a busy band has many carriers; the quiet cells between them are the floor)
    std::vector<double> srt = cell; std::sort(srt.begin(), srt.end());
    const double fl = std::max(1e-20, srt[srt.size() / 4]) * 1.15;
    const double nDb = db(fl);
    noiseDb_ = noiseInit_ ? 0.7 * noiseDb_ + 0.3 * nDb : nDb; noiseInit_ = true;
    // candidates: cells above the threshold (lower for a tracked one) that are not the skirt of a neighbour
    auto cellOf = [&](const Track& t) { return (int)std::lround((t.c.cellHz - cfg_.gridOriginHz) / step) - kMin; };
    std::vector<char> isTracked((size_t)nc, 0), cand((size_t)nc, 0);
    for (auto& t : trk_) { const int c = cellOf(t); if (c >= 0 && c < nc) isTracked[(size_t)c] = 1; }
    for (int c = 0; c < nc; c++) {
        const double lvl = db(cell[(size_t)c]);
        if (lvl - noiseDb_ < (isTracked[(size_t)c] ? cfg_.offDb : cfg_.onDb)) continue;
        bool skirt = false;
        for (int d = -2; d <= 2 && !skirt; d++) {
            const int j = c + d;
            if (d && j >= 0 && j < nc && db(cell[(size_t)j]) > lvl + cfg_.skirtDb / (std::abs(d) == 1 ? 1.0 : 1.6)) skirt = true;
        }
        if (!skirt) cand[(size_t)c] = 1;
    }
    for (int c = 0; c < nc; c++) if (cand[(size_t)c] && !isTracked[(size_t)c]) { Track t; t.c.cellHz = t.c.hz = cen[(size_t)c]; trk_.push_back(t); }
    for (auto& t : trk_) {
        t.c.active = false;
        const int c = cellOf(t);
        if (c < 0 || c >= nc || !cand[(size_t)c]) continue;
        t.c.active = true; t.c.levelDb = db(cell[(size_t)c]); t.c.snrDb = t.c.levelDb - noiseDb_;
        // centroid of the power above the floor inside the cell: where the carrier really is
        const double perBin = fl / std::max(1, nbins[(size_t)c]);
        double sw = 0, sf = 0;
        const int b0 = (int)std::ceil((cen[(size_t)c] - step / 2) / binHz), b1 = (int)std::floor((cen[(size_t)c] + step / 2) / binHz);
        for (int b = b0; b <= b1; b++) { const double w = std::max(0.0, binPow(b) - 2 * perBin); sw += w; sf += w * b * binHz; }
        if (sw > 0) { const double cf = std::clamp(sf / sw, cen[(size_t)c] - step * 0.3, cen[(size_t)c] + step * 0.3); t.c.hz = t.reported ? 0.8 * t.c.hz + 0.2 * cf : cf; }
    }
    for (auto& t : trk_) {
        if (t.c.active) { t.aboveSec += dt; t.c.idleSec = 0; } else { t.c.idleSec += dt; if (!t.reported) t.aboveSec = 0; }
        if (!t.reported && t.aboveSec >= cfg_.debounceSec) { t.reported = true; t.c.id = nextId_++; }
        if (t.reported) t.c.ageSec += dt;
    }
    // drop: never-confirmed ones that went quiet, and confirmed ones past the hold time
    for (size_t i = trk_.size(); i-- > 0;) {
        auto& t = trk_[i];
        if (!t.reported && !t.c.active) trk_.erase(trk_.begin() + (long)i);
        else if (t.reported && t.c.idleSec > cfg_.holdSec) { removed_.push_back(t.c.id); trk_.erase(trk_.begin() + (long)i); }
    }
    // the cap: active and strong first; an old one counts 6 dB stronger than it is (a newcomer must beat it clearly), a long quiet one a little less
    std::vector<size_t> idx;
    for (size_t i = 0; i < trk_.size(); i++) if (trk_[i].reported) idx.push_back(i);
    if ((int)idx.size() > cfg_.maxCarriers) {
        auto score = [&](size_t i) { return (trk_[i].c.active ? 1000.0 : 0.0) + trk_[i].c.levelDb + (trk_[i].c.ageSec > 1.0 ? 6.0 : 0.0) - std::min(trk_[i].c.idleSec, 100.0) * 0.1; };
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return score(a) > score(b); });
        std::vector<char> drop(trk_.size(), 0);
        for (size_t k = (size_t)cfg_.maxCarriers; k < idx.size(); k++) drop[idx[k]] = 1;
        for (size_t i = trk_.size(); i-- > 0;) if (drop[i]) { removed_.push_back(trk_[i].c.id); trk_.erase(trk_.begin() + (long)i); }
    }
    cars_.clear();
    for (auto& t : trk_) if (t.reported) cars_.push_back(t.c);
    std::sort(cars_.begin(), cars_.end(), [](const Carrier& a, const Carrier& b) { return a.hz < b.hz; });
}

bool Channelizer::configure(double inRate, double outRate, double shiftHz, double occupiedHz) {
    inRate_ = inRate; shift_ = shiftHz; phase_ = 0;
    return rs_.configure(inRate, outRate, nullptr, occupiedHz);
}

void Channelizer::reset() { rs_.reset(); phase_ = 0; }

void Channelizer::process(const cf32* x, size_t n, std::vector<cf32>& out) {
    if (mix_.size() < n) mix_.resize(n);
    const double w = -2 * M_PI * shift_ / inRate_;
    const std::complex<double> rot(std::cos(w), std::sin(w));
    std::complex<double> p(std::cos(phase_), std::sin(phase_));
    for (size_t i = 0; i < n; i++) { mix_[i] = x[i] * cf32((float)p.real(), (float)p.imag()); p *= rot; if ((i & 1023) == 1023) p /= std::abs(p); }
    phase_ = std::arg(p);
    rs_.process(mix_.data(), n, out);
}

} // namespace dect2
