// Hydrogen line toolkit: calibration, RFI flagging, Gaussian fits, the longitude-velocity map (see astro/hline.h).
// References: Y-factor method (Friis; any radio-astronomy text, e.g. Rohlfs & Wilson, Tools of Radio Astronomy);
// spectral kurtosis (Nita & Gary 2010, MNRAS 406 L60) with an empirical noise level; Levenberg-Marquardt (Marquardt 1963);
// tangent-point method for the rotation curve (Gunn, Knapp & Tremaine 1979; Burton 1988 in Galactic and Extragalactic Radio Astronomy).
#include "dect2/astro/hline.h"
#include <algorithm>
#include <cmath>

namespace dect2 {
namespace astro {
namespace hl {

namespace {
double medianOf(std::vector<double> v) {
    if (v.empty()) return 0;
    const size_t m = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + (long)m, v.end());
    double r = v[m];
    if (v.size() % 2 == 0) { r = 0.5 * (r + *std::max_element(v.begin(), v.begin() + (long)m)); }
    return r;
}
double robustSigma(const std::vector<double>& v) {   // 1.4826 MAD
    if (v.empty()) return 0;
    const double m = medianOf(v);
    std::vector<double> d(v.size());
    for (size_t i = 0; i < v.size(); i++) d[i] = std::fabs(v[i] - m);
    return 1.4826 * medianOf(d);
}
} // namespace

YFactor yFactor(double pHot, double pCold, double tHotK, double tColdK) {
    YFactor r;
    if (pCold <= 0 || pHot <= pCold || tHotK <= tColdK) return r;
    r.y = pHot / pCold;
    r.trx = (tHotK - r.y * tColdK) / (r.y - 1);
    r.gainPerK = (pHot - pCold) / (tHotK - tColdK);
    r.ok = true;
    return r;
}

YFactorSpec yFactorSpectrum(const std::vector<double>& hot, const std::vector<double>& cold, double tHotK, double tColdK) {
    YFactorSpec r;
    if (hot.size() != cold.size() || hot.empty()) return r;
    r.trx.resize(hot.size()); r.gainPerK.resize(hot.size());
    std::vector<double> good;
    for (size_t i = 0; i < hot.size(); i++) {
        const YFactor y = yFactor(hot[i], cold[i], tHotK, tColdK);
        r.trx[i] = y.ok ? y.trx : 0; r.gainPerK[i] = y.ok ? y.gainPerK : 0;
        if (y.ok) good.push_back(y.trx);
    }
    r.ok = !good.empty();
    r.trxMedian = medianOf(good);
    return r;
}

std::vector<double> antennaTemperature(const std::vector<double>& on, const std::vector<double>& off, double tsysK) {
    std::vector<double> o(on.size(), 0.0);
    if (on.size() != off.size()) return o;
    for (size_t i = 0; i < on.size(); i++) o[i] = off[i] > 0 ? tsysK * (on[i] - off[i]) / off[i] : 0;
    return o;
}

std::vector<double> antennaTemperaturePower(const std::vector<double>& p, const std::vector<double>& off, const std::vector<double>& g) {
    std::vector<double> o(p.size(), 0.0);
    if (p.size() != off.size() || p.size() != g.size()) return o;
    for (size_t i = 0; i < p.size(); i++) o[i] = g[i] > 0 ? (p[i] - off[i]) / g[i] : 0;
    return o;
}

std::vector<double> dbAboveBaseline(const std::vector<double>& y, const std::vector<double>& b) {
    std::vector<double> o(y.size(), 0.0);
    for (size_t i = 0; i < y.size() && i < b.size(); i++) o[i] = (y[i] > 0 && b[i] > 0) ? 10 * std::log10(y[i] / b[i]) : 0;
    return o;
}

std::vector<double> medianFilter(const std::vector<double>& y, int w) {
    const int n = (int)y.size(), h = std::max(0, w / 2);
    std::vector<double> o((size_t)n), win;
    for (int i = 0; i < n; i++) {
        win.assign(y.begin() + std::max(0, i - h), y.begin() + std::min(n, i + h + 1));
        o[(size_t)i] = medianOf(win);
    }
    return o;
}

std::vector<double> perBinGain(const std::vector<double>& flat, int smoothW) {
    const std::vector<double> m = medianFilter(flat, smoothW);
    std::vector<double> g(flat.size(), 1.0);
    for (size_t i = 0; i < flat.size(); i++) if (m[i] > 0) g[i] = flat[i] / m[i];
    return g;
}

std::vector<double> applyGain(const std::vector<double>& s, const std::vector<double>& gain) {
    std::vector<double> o(s);
    for (size_t i = 0; i < s.size() && i < gain.size(); i++) if (gain[i] > 0) o[i] = s[i] / gain[i];
    return o;
}

std::vector<double> frequencySwitchFold(const std::vector<double>& a, const std::vector<double>& b, int s) {
    const int n = (int)a.size();
    std::vector<double> r((size_t)n, 0.0), f((size_t)n, 0.0);
    if (b.size() != a.size()) return f;
    for (int i = 0; i < n; i++) r[(size_t)i] = b[(size_t)i] > 0 ? (a[(size_t)i] - b[(size_t)i]) / b[(size_t)i] : 0;
    for (int i = 0; i < n; i++) {
        const int j = i + s;
        f[(size_t)i] = (j >= 0 && j < n) ? 0.5 * (r[(size_t)i] - r[(size_t)j]) : r[(size_t)i];
    }
    return f;
}

// ----------------------------------------------------------------------------------------------------------------------- RFI
std::vector<bool> flagRobust(const std::vector<double>& s, const RfiParams& p) {
    std::vector<bool> f(s.size(), false);
    if (s.size() < 8) return f;
    const std::vector<double> base = medianFilter(s, p.medianW | 1);
    std::vector<double> res(s.size());
    for (size_t i = 0; i < s.size(); i++) res[i] = s[i] - base[i];
    const double sg = robustSigma(res), m = medianOf(res);
    if (sg <= 0) return f;
    for (size_t i = 0; i < s.size(); i++) f[i] = std::fabs(res[i] - m) > p.sigma * sg;
    return f;
}

std::vector<bool> flagKurtosis(const std::vector<std::vector<double>>& rows, int nacc, const RfiParams& p) {
    const size_t M = rows.size();
    if (M < 4) return {};
    const size_t n = rows[0].size();
    std::vector<double> sk(n, 1.0);
    for (size_t c = 0; c < n; c++) {
        double s1 = 0, s2 = 0;
        for (size_t t = 0; t < M; t++) { const double v = rows[t][c]; s1 += v; s2 += v * v; }
        if (s1 > 0) sk[c] = ((double)M * std::max(1, nacc) + 1) / (M - 1) * (M * s2 / (s1 * s1) - 1);
    }
    const double med = medianOf(sk), sg = robustSigma(sk);
    std::vector<bool> f(n, false);
    if (sg <= 0) return f;
    for (size_t c = 0; c < n; c++) f[c] = std::fabs(sk[c] - med) > p.skLimit * sg;
    return f;
}

std::vector<double> maskInterpolate(const std::vector<double>& s, const std::vector<bool>& flag) {
    std::vector<double> o(s);
    const int n = (int)s.size();
    int good = 0;
    for (int i = 0; i < n; i++) if (i < (int)flag.size() ? !flag[(size_t)i] : true) good++;
    if (!good) return o;
    auto bad = [&](int i) { return i < (int)flag.size() && flag[(size_t)i]; };
    for (int i = 0; i < n; i++) {
        if (!bad(i)) continue;
        int a = i, b = i;
        while (a >= 0 && bad(a)) a--;
        while (b < n && bad(b)) b++;
        if (a < 0) o[(size_t)i] = s[(size_t)b];
        else if (b >= n) o[(size_t)i] = s[(size_t)a];
        else o[(size_t)i] = s[(size_t)a] + (s[(size_t)b] - s[(size_t)a]) * (i - a) / (double)(b - a);
    }
    return o;
}

// ------------------------------------------------------------------------------------------------------------- Gaussian fit
double GaussComp::area() const { return amp * sigma * 2.5066282746310002; }

namespace {
// solve A x = b (n x n, Gauss-Jordan with partial pivoting); also gives the inverse when inv != nullptr
bool solve(std::vector<double> A, std::vector<double> b, int n, std::vector<double>& x, std::vector<double>* inv) {
    std::vector<double> I((size_t)n * n, 0.0);
    for (int i = 0; i < n; i++) I[(size_t)i * n + i] = 1;
    for (int c = 0; c < n; c++) {
        int pv = c;
        for (int r = c + 1; r < n; r++) if (std::fabs(A[(size_t)r * n + c]) > std::fabs(A[(size_t)pv * n + c])) pv = r;
        if (std::fabs(A[(size_t)pv * n + c]) < 1e-300) return false;
        if (pv != c) { for (int k = 0; k < n; k++) { std::swap(A[(size_t)pv * n + k], A[(size_t)c * n + k]); std::swap(I[(size_t)pv * n + k], I[(size_t)c * n + k]); } std::swap(b[(size_t)pv], b[(size_t)c]); }
        const double d = A[(size_t)c * n + c];
        for (int k = 0; k < n; k++) { A[(size_t)c * n + k] /= d; I[(size_t)c * n + k] /= d; }
        b[(size_t)c] /= d;
        for (int r = 0; r < n; r++) {
            if (r == c) continue;
            const double f = A[(size_t)r * n + c];
            if (f == 0) continue;
            for (int k = 0; k < n; k++) { A[(size_t)r * n + k] -= f * A[(size_t)c * n + k]; I[(size_t)r * n + k] -= f * I[(size_t)c * n + k]; }
            b[(size_t)r] -= f * b[(size_t)c];
        }
    }
    x = b;
    if (inv) *inv = I;
    return true;
}

double model(const std::vector<double>& p, int nc, bool off, double x) {
    double y = off ? p[(size_t)nc * 3] : 0;
    for (int k = 0; k < nc; k++) { const double d = (x - p[(size_t)k * 3 + 1]) / p[(size_t)k * 3 + 2]; y += p[(size_t)k * 3] * std::exp(-0.5 * d * d); }
    return y;
}
double chi2(const std::vector<double>& p, int nc, bool off, const std::vector<double>& x, const std::vector<double>& y) {
    double s = 0;
    for (size_t i = 0; i < x.size(); i++) { const double r = y[i] - model(p, nc, off, x[i]); s += r * r; }
    return s;
}
} // namespace

FitResult fitGaussians(const std::vector<double>& x, const std::vector<double>& y, std::vector<GaussComp> start, bool fitOffset) {
    FitResult res;
    const int nc = (int)start.size(), np = nc * 3 + (fitOffset ? 1 : 0), N = (int)x.size();
    if (nc == 0 || N != (int)y.size() || N <= np) return res;
    std::vector<double> p((size_t)np);
    for (int k = 0; k < nc; k++) { p[(size_t)k * 3] = start[(size_t)k].amp; p[(size_t)k * 3 + 1] = start[(size_t)k].centre; p[(size_t)k * 3 + 2] = std::max(1e-9, std::fabs(start[(size_t)k].sigma)); }
    if (fitOffset) p[(size_t)nc * 3] = 0;
    double lambda = 1e-3, cur = chi2(p, nc, fitOffset, x, y);
    std::vector<double> JtJ, Jtr, J((size_t)np);
    auto build = [&](const std::vector<double>& q) {
        JtJ.assign((size_t)np * np, 0.0); Jtr.assign((size_t)np, 0.0);
        for (int i = 0; i < N; i++) {
            const double xi = x[(size_t)i];
            for (int k = 0; k < nc; k++) {
                const double A = q[(size_t)k * 3], c = q[(size_t)k * 3 + 1], s = q[(size_t)k * 3 + 2], d = (xi - c) / s, g = std::exp(-0.5 * d * d);
                J[(size_t)k * 3] = g; J[(size_t)k * 3 + 1] = A * g * d / s; J[(size_t)k * 3 + 2] = A * g * d * d / s;
            }
            if (fitOffset) J[(size_t)nc * 3] = 1;
            const double r = y[(size_t)i] - model(q, nc, fitOffset, xi);
            for (int a = 0; a < np; a++) { Jtr[(size_t)a] += J[(size_t)a] * r; for (int b = 0; b < np; b++) JtJ[(size_t)a * np + b] += J[(size_t)a] * J[(size_t)b]; }
        }
    };
    int it = 0;
    for (; it < 200; it++) {
        build(p);
        std::vector<double> A = JtJ, dx;
        for (int a = 0; a < np; a++) A[(size_t)a * np + a] *= (1 + lambda);
        if (!solve(A, Jtr, np, dx, nullptr)) { lambda *= 10; if (lambda > 1e12) break; continue; }
        std::vector<double> q = p;
        for (int a = 0; a < np; a++) q[(size_t)a] += dx[(size_t)a];
        for (int k = 0; k < nc; k++) q[(size_t)k * 3 + 2] = std::max(1e-9, std::fabs(q[(size_t)k * 3 + 2]));
        const double c2 = chi2(q, nc, fitOffset, x, y);
        if (c2 < cur) {
            const double gain = cur - c2;
            p = q; cur = c2; lambda = std::max(1e-12, lambda * 0.3);
            if (gain < 1e-12 * std::max(1e-30, cur)) { it++; break; }
        } else { lambda *= 10; if (lambda > 1e12) break; }
    }
    build(p);
    std::vector<double> dummy, cov;
    if (!solve(JtJ, Jtr, np, dummy, &cov)) return res;
    const double s2 = cur / (N - np);
    res.rms = std::sqrt(cur / N);
    res.iterations = it;
    for (int k = 0; k < nc; k++) {
        GaussComp g;
        g.amp = p[(size_t)k * 3]; g.centre = p[(size_t)k * 3 + 1]; g.sigma = p[(size_t)k * 3 + 2];
        auto C = [&](int a, int b) { return cov[(size_t)(k * 3 + a) * np + (k * 3 + b)] * s2; };
        g.dAmp = std::sqrt(std::max(0.0, C(0, 0))); g.dCentre = std::sqrt(std::max(0.0, C(1, 1))); g.dSigma = std::sqrt(std::max(0.0, C(2, 2)));
        const double sq = 2.5066282746310002, ga = g.sigma * sq, gs = g.amp * sq;
        g.dArea = std::sqrt(std::max(0.0, ga * ga * C(0, 0) + gs * gs * C(2, 2) + 2 * ga * gs * C(0, 2)));
        res.comps.push_back(g);
    }
    if (fitOffset) { res.offset = p[(size_t)nc * 3]; res.dOffset = std::sqrt(std::max(0.0, cov[(size_t)(np - 1) * np + (np - 1)] * s2)); }
    res.ok = true;
    return res;
}

std::vector<GaussComp> guessComponents(const std::vector<double>& x, const std::vector<double>& y, int maxComps, double snr) {
    std::vector<GaussComp> out;
    const int n = (int)y.size();
    if (n < 16 || x.size() != y.size()) return out;
    const int w = 5;
    std::vector<double> r((size_t)n);
    for (int i = 0; i < n; i++) { double s = 0; int c = 0; for (int k = std::max(0, i - w / 2); k <= std::min(n - 1, i + w / 2); k++) { s += y[(size_t)k]; c++; } r[(size_t)i] = s / c; }
    const double base = medianOf(y);
    std::vector<double> d((size_t)n - 1);
    for (int i = 0; i + 1 < n; i++) d[(size_t)i] = (y[(size_t)i + 1] - y[(size_t)i]) / 1.41421356;
    const double noise = robustSigma(d) / std::sqrt((double)w);
    for (double& v : r) v -= base;
    for (int c = 0; c < maxComps; c++) {
        int k = (int)(std::max_element(r.begin(), r.end()) - r.begin());
        const double pk = r[(size_t)k];
        if (pk < snr * noise) break;
        int a = k, b = k;
        while (a > 0 && r[(size_t)a] > pk / 2) a--;
        while (b < n - 1 && r[(size_t)b] > pk / 2) b++;
        double fwhm = std::fabs(x[(size_t)b] - x[(size_t)a]);
        if (fwhm <= 0) fwhm = 2 * std::fabs(x[1] - x[0]);
        GaussComp g; g.amp = pk; g.centre = x[(size_t)k]; g.sigma = fwhm / 2.354820045;
        out.push_back(g);
        for (int i = 0; i < n; i++) { const double t = (x[(size_t)i] - g.centre) / g.sigma; r[(size_t)i] -= pk * std::exp(-0.5 * t * t); }
    }
    return out;
}

// ------------------------------------------------------------------------------------------------------------------- l-v map
void LvMap::add(double lDeg, const std::vector<double>& v, const std::vector<double>& t) {
    const int nvg = nv();
    std::vector<std::pair<double, double>> pts;
    for (size_t i = 0; i < v.size() && i < t.size(); i++) pts.push_back({v[i], t[i]});
    std::sort(pts.begin(), pts.end());
    std::vector<double> row((size_t)nvg, 0.0);
    size_t j = 0;
    for (int g = 0; g < nvg && pts.size() > 1; g++) {
        const double vg = vLo + g * vStep;
        if (vg < pts.front().first || vg > pts.back().first) continue;
        while (j + 2 < pts.size() && pts[j + 1].first < vg) j++;
        const double v0 = pts[j].first, v1 = pts[j + 1].first;
        const double f = v1 > v0 ? (vg - v0) / (v1 - v0) : 0;
        row[(size_t)g] = pts[j].second + f * (pts[j + 1].second - pts[j].second);
    }
    l.push_back(lDeg);
    rows.push_back(std::move(row));
}

std::vector<Terminal> terminalVelocities(const LvMap& m, double sigma, int minWidth) {
    std::vector<Terminal> out;
    for (size_t r = 0; r < m.rows.size(); r++) {
        Terminal t;
        t.l = m.l[r];
        const auto& row = m.rows[r];
        const int n = (int)row.size();
        const double med = medianOf(row);
        std::vector<double> dev(row);
        for (double& v : dev) v -= med;
        t.noise = robustSigma(row);
        const double thr = med + sigma * t.noise;
        const double ll = std::fmod(std::fmod(t.l, 360.0) + 360.0, 360.0);
        const bool q1 = ll > 0 && ll < 90, q4 = ll > 270 && ll < 360;
        if ((q1 || q4) && t.noise > 0) {
            int run = 0;
            for (int s = 0; s < n; s++) {
                const int i = q1 ? n - 1 - s : s;
                if (row[(size_t)i] > thr) {
                    if (++run >= minWidth) { t.vTerm = m.vLo + (q1 ? i + minWidth - 1 : i - minWidth + 1) * m.vStep; t.ok = true; break; }
                } else run = 0;
            }
        }
        out.push_back(t);
    }
    return out;
}

std::vector<RotPoint> rotationCurve(const std::vector<Terminal>& t, double r0, double v0) {
    std::vector<RotPoint> o;
    for (const auto& e : t) {
        if (!e.ok) continue;
        const double s = std::fabs(std::sin(e.l * M_PI / 180));
        o.push_back({r0 * s, std::fabs(e.vTerm) + v0 * s});
    }
    std::sort(o.begin(), o.end(), [](const RotPoint& a, const RotPoint& b) { return a.rKpc < b.rKpc; });
    return o;
}

} // namespace hl
} // namespace astro
} // namespace dect2
