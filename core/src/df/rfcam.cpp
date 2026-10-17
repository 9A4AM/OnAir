// RF camera maths, see rfcam.h. Written from scratch (pinhole camera model, textbook windowed DFT sub-band covariance).
#include "dect2/df/rfcam.h"
#include "dect2/dsp/fftutil.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dect2::df {

static const double D2R = kPi / 180.0;

static double focalPx(const View& v) { return (v.w * 0.5) / std::tan(0.5 * v.hfovDeg * D2R); }

bool azToX(const View& v, double azDeg, double& x) {
    const double d = wrap180(azDeg - v.centerAzDeg);
    if (v.proj == Projection::Equirect) {
        if (v.hfovDeg >= 360.0 - 1e-9) { x = v.w * 0.5 + d / 360.0 * v.w; return true; }
        if (std::fabs(d) > v.hfovDeg * 0.5) return false;
        x = v.w * 0.5 + d / v.hfovDeg * v.w;
        return true;
    }
    if (std::fabs(d) >= 90.0) return false;
    x = v.w * 0.5 + focalPx(v) * std::tan(d * D2R);
    if (x < 0 || x > v.w) return false;
    return true;
}

double xToAz(const View& v, double x) {
    if (v.proj == Projection::Equirect) return wrap360(v.centerAzDeg + (x - v.w * 0.5) / v.w * (v.hfovDeg >= 360.0 - 1e-9 ? 360.0 : v.hfovDeg));
    return wrap360(v.centerAzDeg + std::atan((x - v.w * 0.5) / focalPx(v)) / D2R);
}

bool calibrateView(View& v, double x1, double az1, double x2, double az2) {
    if (std::fabs(x1 - x2) < 2 || v.w <= 0) return false;
    const double da = angDiff(az2, az1);
    if (std::fabs(da) < 0.01) return false;
    if (v.proj == Projection::Equirect) {
        if ((x2 - x1) * da < 0) return false;              // columns must grow with azimuth
        const double degPerPx = da / (x2 - x1);
        v.hfovDeg = std::fabs(degPerPx) * v.w;
        v.centerAzDeg = wrap360(az1 - (x1 - v.w * 0.5) * degPerPx);
        return v.hfovDeg > 0 && v.hfovDeg <= 360.0 + 1e-6;
    }
    // Rectilinear: x - w/2 = f tan(az - c). Scan the centre azimuth c; f from point 1, residual at point 2, then refine.
    const double u1 = x1 - v.w * 0.5, u2 = x2 - v.w * 0.5;
    auto resid = [&](double c, double& f) {
        const double t1 = std::tan(wrap180(az1 - c) * D2R), t2 = std::tan(wrap180(az2 - c) * D2R);
        if (std::fabs(t1) < 1e-6) { f = 0; return 1e9; }
        f = u1 / t1;
        if (f <= 0) return 1e9;
        return std::fabs(f * t2 - u2);
    };
    double best = 1e18, bc = 0, f = 0;
    for (int i = 0; i < 3600; i++) {
        const double c = az1 - 90 + i * 0.05;
        // keep both landmarks in front of the lens
        if (std::fabs(wrap180(az1 - c)) >= 89 || std::fabs(wrap180(az2 - c)) >= 89) continue;
        double ff; const double r = resid(c, ff);
        if (r < best) { best = r; bc = c; }
    }
    if (best > 1e17) return false;
    double lo = bc - 0.05, hi = bc + 0.05;
    for (int i = 0; i < 40; i++) {
        const double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
        double a, b;
        if (resid(m1, a) < resid(m2, b)) hi = m2; else lo = m1;
    }
    bc = 0.5 * (lo + hi);
    if (resid(bc, f) > 1.0) return false;                  // no pinhole camera fits the two landmarks
    v.centerAzDeg = wrap360(bc);
    v.hfovDeg = 2 * std::atan((v.w * 0.5) / f) / D2R;
    return v.hfovDeg > 1 && v.hfovDeg < 180;
}

// ---- sub-band analysis
CamResult analyse(const Frame& x, const Array& arr, double centerHz, double fs, const CamOptions& opt) {
    CamResult out;
    const int n = (int)x.size();
    if (n < 2 || n != arr.n() || x[0].empty() || fs <= 0) return out;
    int M = 16;
    while (M < opt.fftSize && M < 4096) M <<= 1;
    const size_t T = x[0].size();
    const int blocks = (int)(T / (size_t)M);
    if (blocks < 1) return out;
    double lo = opt.filterLoHz, hi = opt.filterHiHz;
    if (lo >= hi) { lo = -fs * 0.5; hi = fs * 0.5; }
    lo = std::max(lo, -fs * 0.5); hi = std::min(hi, fs * 0.5);
    const int nb = std::clamp(opt.bands, 1, 64);
    const double bw = (hi - lo) / nb, binHz = fs / M;
    // windowed spectra: X[ch][block][bin]
    std::vector<float> win(M);
    for (int i = 0; i < M; i++) win[i] = 0.5f - 0.5f * (float)std::cos(2 * kPi * (i + 0.5) / M);
    Fft fft(M);
    std::vector<std::vector<cf32>> spec((size_t)n * blocks, std::vector<cf32>(M));
    for (int c = 0; c < n; c++)
        for (int b = 0; b < blocks; b++) {
            auto& s = spec[(size_t)c * blocks + b];
            for (int i = 0; i < M; i++) s[i] = cf32((float)x[c][(size_t)b * M + i].real() * win[i], (float)x[c][(size_t)b * M + i].imag() * win[i]);
            fft.forward(s.data());
        }
    const double lam = kC / centerHz;
    for (int k = 0; k < nb; k++) {
        BandResult br;
        br.band = {lo + k * bw, lo + (k + 1) * bw};
        br.freqHz = centerHz + 0.5 * (br.band.loHz + br.band.hiHz);
        std::vector<int> bins;
        for (int q = 0; q < M; q++) {
            const double f = (q < M / 2 ? q : q - M) * binHz;
            if (f >= br.band.loHz && f < br.band.hiHz) bins.push_back(q);
        }
        if (bins.empty()) continue;
        CMat r(n);
        for (int b = 0; b < blocks; b++)
            for (int q : bins)
                for (int i = 0; i < n; i++) {
                    const cf32 a = spec[(size_t)i * blocks + b][q];
                    for (int j = 0; j < n; j++) {
                        const cf32 c = spec[(size_t)j * blocks + b][q];
                        r(i, j) += cd(a.real(), a.imag()) * std::conj(cd(c.real(), c.imag()));
                    }
                }
        const double cnt = (double)blocks * (double)bins.size();
        for (auto& v : r.a) v /= cnt;
        Options o = opt.df;
        o.compassOffsetDeg = opt.df.compassOffsetDeg;
        br.res = estimate(r, arr, lam, o);
        const auto& ev = br.res.eigenvalues;
        if (ev.size() >= 3) {
            std::vector<double> rest(ev.begin() + 1, ev.end());
            std::sort(rest.begin(), rest.end());
            const double noise = std::max(rest[rest.size() / 2], 1e-30);
            br.snrDb = 10 * std::log10(std::max(ev[0], 1e-30) / noise);
        }
        br.weight = std::clamp((br.snrDb - opt.minSnrDb) / 20.0, 0.0, 1.0);
        out.bands.push_back(std::move(br));
    }
    if (out.bands.empty()) return out;
    // grid is the same for every band; compass applied
    const BandResult* ref = nullptr;
    for (auto& b : out.bands) if (!b.res.bearings.empty()) { ref = &b; break; }
    if (!ref) return out;
    out.bearings.resize(ref->res.bearings.size());
    for (size_t g = 0; g < out.bearings.size(); g++) out.bearings[g] = wrap360(ref->res.bearings[g] + opt.df.compassOffsetDeg);
    out.spectrum.assign(out.bearings.size(), 0.0);
    for (auto& b : out.bands) {
        if (b.weight <= 0 || b.res.spectrum.size() != out.spectrum.size()) continue;
        for (size_t g = 0; g < out.spectrum.size(); g++) out.spectrum[g] = std::max(out.spectrum[g], b.weight * b.res.spectrum[g]);
        for (size_t i = 0; i < b.res.peaks.size() && (int)i < std::max(1, opt.df.sources); i++)
            out.peaks.push_back({b.res.peaks[i].bearingDeg, b.freqHz, b.weight * b.res.peaks[i].value});
    }
    std::sort(out.peaks.begin(), out.peaks.end(), [](const CamPeak& a, const CamPeak& b) { return a.strength > b.strength; });
    return out;
}

// ---- heat map
double displayLevel(double v, double floorDb) {
    if (v <= 0) return 0;
    return std::clamp((10 * std::log10(v) + floorDb) / floorDb, 0.0, 1.0);
}

uint32_t heatColor(double t, double maxAlpha) {
    t = std::clamp(t, 0.0, 1.0);
    // blue (0.0) -> cyan (0.35) -> yellow (0.65) -> red (1.0)
    static const double st[4][3] = {{0.1, 0.2, 1.0}, {0.0, 0.9, 1.0}, {1.0, 0.95, 0.1}, {1.0, 0.1, 0.05}};
    static const double at[4] = {0.0, 0.35, 0.65, 1.0};
    int i = 0;
    while (i < 2 && t > at[i + 1]) i++;
    const double u = (t - at[i]) / (at[i + 1] - at[i]);
    uint32_t c[3];
    for (int k = 0; k < 3; k++) c[k] = (uint32_t)std::lround(255 * (st[i][k] + (st[i + 1][k] - st[i][k]) * u));
    const uint32_t a = (uint32_t)std::lround(255 * maxAlpha * std::sqrt(t));
    return (a << 24) | (c[2] << 16) | (c[1] << 8) | c[0];
}

double sampleSpectrum(const std::vector<double>& b, const std::vector<double>& s, double az) {
    const size_t n = b.size();
    if (n < 2 || s.size() != n) return 0;
    // the grid is evenly spaced over the circle, possibly rotated by the compass offset
    const double step = 360.0 / (double)n;
    double p = wrap360(az - b[0]) / step;
    size_t i0 = (size_t)std::floor(p) % n, i1 = (i0 + 1) % n;
    const double u = p - std::floor(p);
    return s[i0] * (1 - u) + s[i1] * u;
}

void blendHeat(std::vector<uint32_t>& px, const View& v, const std::vector<double>& b, const std::vector<double>& s, const HeatOptions& o) {
    if ((int)px.size() != v.w * v.h || b.empty()) return;
    for (int xx = 0; xx < v.w; xx++) {
        const double lvl = displayLevel(sampleSpectrum(b, s, xToAz(v, xx + 0.5)), o.floorDb);
        if (lvl < o.threshold) continue;
        const uint32_t hc = heatColor(lvl, o.maxAlpha);
        const uint32_t a = hc >> 24;
        for (int y = 0; y < v.h; y++) {
            uint32_t& d = px[(size_t)y * v.w + xx];
            uint32_t out = d & 0xFF000000u;
            for (int sh = 0; sh <= 16; sh += 8) {
                const uint32_t dc = (d >> sh) & 0xFF, sc = (hc >> sh) & 0xFF;
                out |= ((dc * (255 - a) + sc * a + 127) / 255) << sh;
            }
            d = out;
        }
    }
}

// ---- markers
std::string formatFreq(double hz) {
    char b[48];
    if (std::fabs(hz) >= 1e6) snprintf(b, sizeof b, "%.3f MHz", hz / 1e6);
    else snprintf(b, sizeof b, "%.1f kHz", hz / 1e3);
    return b;
}

std::vector<Marker> layoutMarkers(const std::vector<CamPeak>& peaks, const View& v, const MarkerOptions& o) {
    std::vector<CamPeak> sorted = peaks;
    std::sort(sorted.begin(), sorted.end(), [](const CamPeak& a, const CamPeak& b) { return a.strength > b.strength; });
    std::vector<Marker> out;
    std::vector<std::vector<std::pair<double, double>>> rows((size_t)std::max(o.labelRows, 1));   // occupied x intervals per label row
    for (const CamPeak& p : sorted) {
        if (p.strength < o.minStrength || (int)out.size() >= o.maxMarkers) continue;
        bool dup = false;
        for (const Marker& m : out) if (std::fabs(angDiff(m.bearingDeg, p.bearingDeg)) < o.mergeDeg) { dup = true; break; }
        if (dup) continue;
        double x;
        if (!azToX(v, p.bearingDeg, x)) continue;
        Marker m;
        m.x = x; m.y = v.h * o.yFrac; m.bearingDeg = p.bearingDeg; m.freqHz = p.freqHz; m.strength = p.strength;
        m.label = formatFreq(p.freqHz);
        const double l0 = x - o.labelWidthPx * 0.5, l1 = x + o.labelWidthPx * 0.5;
        for (size_t r = 0; r < rows.size(); r++) {
            bool free = true;
            for (auto& iv : rows[r]) if (l0 < iv.second && l1 > iv.first) { free = false; break; }
            if (free) { rows[r].push_back({l0, l1}); m.labelRow = (int)r; break; }
        }
        out.push_back(std::move(m));
    }
    return out;
}

}   // namespace dect2::df
