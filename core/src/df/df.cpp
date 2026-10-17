// Direction finding algorithms: see core/include/dect2/df/df.h and docs/direction-finding.md
#include "dect2/df/df.h"
#include <algorithm>
#include <cmath>

namespace dect2::df {

static const double D2R = kPi / 180.0;

// ---------------------------------------------------------------- arrays
Array Array::uca(int n, double radius, double orientationDeg) {
    Array a;
    for (int i = 0; i < n; i++) {
        double th = (orientationDeg + 360.0 * i / n) * D2R;       // clockwise from north
        a.pos.push_back({radius * std::sin(th), radius * std::cos(th)});
    }
    return a;
}
Array Array::ula(int n, double spacing, double orientationDeg) {
    Array a;
    a.isUla = true; a.spacing = spacing;
    double o = orientationDeg * D2R;
    for (int i = 0; i < n; i++) {
        double d = (i - (n - 1) / 2.0) * spacing;
        a.pos.push_back({d * std::sin(o), d * std::cos(o)});
    }
    return a;
}
Array Array::custom(std::vector<std::array<double, 2>> p) { Array a; a.pos = std::move(p); return a; }
double Array::ucaRadiusForSpacing(int n, double wavelength, double sw) {
    return sw * wavelength / (2.0 * std::sin(kPi / n));
}

std::vector<cd> steering(const Array& arr, double wavelength, double bearingDeg) {
    double th = bearingDeg * D2R, ux = std::sin(th), uy = std::cos(th), k = 2.0 * kPi / wavelength;
    std::vector<cd> a(arr.n());
    for (int i = 0; i < arr.n(); i++) a[i] = std::polar(1.0, k * (arr.pos[i][0] * ux + arr.pos[i][1] * uy));
    return a;
}

// ---------------------------------------------------------------- covariance
CMat covariance(const std::vector<std::vector<cd>>& x) {
    int n = (int)x.size();
    CMat r(n);
    if (!n || x[0].empty()) return r;
    size_t t = x[0].size();
    for (int i = 0; i < n; i++)
        for (int j = i; j < n; j++) {
            cd s = 0;
            for (size_t k = 0; k < t; k++) s += x[i][k] * std::conj(x[j][k]);
            s /= (double)t;
            r(i, j) = s; r(j, i) = std::conj(s);
        }
    return r;
}
CMat forwardBackward(const CMat& r) {
    int n = r.n;
    CMat o(n);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) o(i, j) = 0.5 * (r(i, j) + std::conj(r(n - 1 - i, n - 1 - j)));
    return o;
}
CMat spatialSmooth(const CMat& r, int m) {
    int n = r.n;
    if (m <= 0 || m >= n) return r;
    int L = n - m + 1;
    CMat o(m);
    for (int s = 0; s < L; s++)
        for (int i = 0; i < m; i++)
            for (int j = 0; j < m; j++) o(i, j) += r(s + i, s + j) / (double)L;
    return o;
}

// ---------------------------------------------------------------- linear algebra
static CMat mul(const CMat& a, const CMat& b) {
    int n = a.n;
    CMat c(n);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < n; k++) {
            cd v = a(i, k);
            if (v == cd(0)) continue;
            for (int j = 0; j < n; j++) c(i, j) += v * b(k, j);
        }
    return c;
}
static CMat adj(const CMat& a) {
    CMat c(a.n);
    for (int i = 0; i < a.n; i++) for (int j = 0; j < a.n; j++) c(i, j) = std::conj(a(j, i));
    return c;
}

void eigh(const CMat& r, std::vector<double>& vals, std::vector<std::vector<cd>>& vecs) {
    int n = r.n;
    CMat a = r, v(n);
    for (int i = 0; i < n; i++) v(i, i) = 1;
    for (int sweep = 0; sweep < 60; sweep++) {
        double off = 0, diag = 0;
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) (i == j ? diag : off) += std::norm(a(i, j));
        if (off <= 1e-26 * (diag + 1e-300)) break;
        for (int p = 0; p < n - 1; p++)
            for (int q = p + 1; q < n; q++) {
                double m = std::abs(a(p, q));
                if (m < 1e-300) continue;
                cd e = a(p, q) / m;
                double th = 0.5 * std::atan2(2 * m, a(p, p).real() - a(q, q).real());
                double c = std::cos(th), s = std::sin(th);
                // J = D R, D = diag(1, conj(e)) on (p,q), R = [[c,-s],[s,c]]; A' = J^H A J
                CMat j(n);
                for (int i = 0; i < n; i++) j(i, i) = 1;
                j(p, p) = c;                     j(p, q) = -s;
                j(q, p) = std::conj(e) * s;      j(q, q) = std::conj(e) * c;
                a = mul(adj(j), mul(a, j));
                v = mul(v, j);
            }
    }
    std::vector<int> idx(n);
    for (int i = 0; i < n; i++) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int x, int y) { return a(x, x).real() > a(y, y).real(); });
    vals.assign(n, 0); vecs.assign(n, std::vector<cd>(n));
    for (int k = 0; k < n; k++) {
        vals[k] = a(idx[k], idx[k]).real();
        for (int i = 0; i < n; i++) vecs[k][i] = v(i, idx[k]);
    }
}

bool solve(CMat a, std::vector<cd> b, std::vector<cd>& x) {
    int n = a.n;
    for (int c = 0; c < n; c++) {
        int p = c;
        for (int i = c + 1; i < n; i++) if (std::abs(a(i, c)) > std::abs(a(p, c))) p = i;
        if (std::abs(a(p, c)) < 1e-300) return false;
        if (p != c) { for (int j = 0; j < n; j++) std::swap(a(p, j), a(c, j)); std::swap(b[p], b[c]); }
        for (int i = c + 1; i < n; i++) {
            cd f = a(i, c) / a(c, c);
            for (int j = c; j < n; j++) a(i, j) -= f * a(c, j);
            b[i] -= f * b[c];
        }
    }
    x.assign(n, 0);
    for (int i = n - 1; i >= 0; i--) {
        cd s = b[i];
        for (int j = i + 1; j < n; j++) s -= a(i, j) * x[j];
        x[i] = s / a(i, i);
    }
    return true;
}

static bool inverse(const CMat& a, CMat& inv) {
    int n = a.n;
    inv = CMat(n);
    for (int c = 0; c < n; c++) {
        std::vector<cd> e(n, 0), x;
        e[c] = 1;
        if (!solve(a, e, x)) return false;
        for (int i = 0; i < n; i++) inv(i, c) = x[i];
    }
    return true;
}

// ---------------------------------------------------------------- circular statistics
double wrap360(double d) { d = std::fmod(d, 360.0); if (d < 0) d += 360.0; if (d >= 360.0) d = 0; return d; }
double wrap180(double d) { d = wrap360(d); return d > 180.0 ? d - 360.0 : d; }
double angDiff(double a, double b) { double d = wrap360(a - b); return d > 180.0 ? d - 360.0 : d; }
double circResultant(const std::vector<double>& d, const std::vector<double>* w) {
    double c = 0, s = 0, tw = 0;
    for (size_t i = 0; i < d.size(); i++) { double ww = w ? (*w)[i] : 1.0; c += ww * std::cos(d[i] * D2R); s += ww * std::sin(d[i] * D2R); tw += ww; }
    return tw > 0 ? std::hypot(c, s) / tw : 0;
}
double circMean(const std::vector<double>& d, const std::vector<double>* w) {
    double c = 0, s = 0;
    for (size_t i = 0; i < d.size(); i++) { double ww = w ? (*w)[i] : 1.0; c += ww * std::cos(d[i] * D2R); s += ww * std::sin(d[i] * D2R); }
    return wrap360(std::atan2(s, c) / D2R);
}
double circStd(const std::vector<double>& d) {
    double R = std::min(1.0, std::max(1e-12, circResultant(d)));
    return std::sqrt(-2.0 * std::log(R)) / D2R;
}
void BearingAverager::add(double b, double w) {
    b_.push_back(wrap360(b)); w_.push_back(w > 0 ? w : 1e-6);
    while ((int)b_.size() > win_) { b_.erase(b_.begin()); w_.erase(w_.begin()); }
}
double BearingAverager::mean() const { return circMean(b_, &w_); }
double BearingAverager::stdDeg() const { return b_.size() < 2 ? 0 : circStd(b_); }

// ---------------------------------------------------------------- estimators
int estimateSources(const std::vector<double>& eig, int T) {
    int n = (int)eig.size(), best = 1;
    double bestV = 1e300;
    for (int k = 0; k < n - 1; k++) {
        double lg = 0, am = 0;
        for (int i = k; i < n; i++) { double e = std::max(eig[i], 1e-300); lg += std::log(e); am += e; }
        lg /= (n - k); am /= (n - k);
        double mdl = -(double)T * (n - k) * (lg - std::log(am)) + 0.5 * k * (2.0 * n - k) * std::log((double)T);
        if (mdl < bestV) { bestV = mdl; best = k; }
    }
    return std::max(1, best);
}

static void findPeaks(Result& res) {
    int g = (int)res.spectrum.size();
    std::vector<Peak> pk;
    for (int i = 0; i < g; i++) {
        double l = res.spectrum[(i + g - 1) % g], c = res.spectrum[i], r = res.spectrum[(i + 1) % g];
        if (c > l && c >= r) {
            // parabola through the log spectrum (the peaks are close to Gaussian in dB)
            double ll = std::log(std::max(l, 1e-30)), lc = std::log(std::max(c, 1e-30)), lr = std::log(std::max(r, 1e-30));
            double den = ll - 2 * lc + lr, off = den != 0 ? 0.5 * (ll - lr) / den : 0;
            double step = res.bearings.size() > 1 ? res.bearings[1] - res.bearings[0] : 1;
            pk.push_back({wrap360(res.bearings[i] + off * step), c});
        }
    }
    std::sort(pk.begin(), pk.end(), [](const Peak& a, const Peak& b) { return a.value > b.value; });
    res.peaks = pk;
}

Result estimate(const CMat& r0, const Array& arr0, double lambda, const Options& opt) {
    Result res;
    CMat r = r0;
    Array arr = arr0;
    if (opt.forwardBackward) r = forwardBackward(r);
    if (opt.smoothing > 0 && arr.isUla && opt.smoothing < r.n) {
        r = spatialSmooth(r, opt.smoothing);
        arr.pos.resize(opt.smoothing);
    }
    int n = r.n;
    std::vector<double> ev; std::vector<std::vector<cd>> vec;
    eigh(r, ev, vec);
    res.eigenvalues = ev;
    double tr = 0; for (double e : ev) tr += std::max(e, 0.0);
    res.coherence = tr > 0 ? ev[0] / tr : 0;
    int m = opt.sources > 0 ? opt.sources : estimateSources(ev, 100);
    m = std::min(std::max(m, 1), n - 1);
    res.sourcesUsed = m;

    CMat rinv;
    if (opt.method == Method::Capon) {
        CMat rl = r;
        for (int i = 0; i < n; i++) rl(i, i) += opt.loading * tr / n + 1e-300;
        if (!inverse(rl, rinv)) rinv = CMat(n);
    }
    std::vector<cd> vphase;
    if (opt.method == Method::Interferometer) {
        vphase = vec[0];
        for (auto& z : vphase) z = std::abs(z) > 1e-30 ? z / std::abs(z) : cd(1);
    }
    int grid = std::max(8, (int)std::lround(360.0 / opt.gridStepDeg));
    double step = 360.0 / grid;
    res.bearings.resize(grid); res.spectrum.resize(grid);
    double mx = 0;
    for (int g = 0; g < grid; g++) {
        double b = g * step;
        res.bearings[g] = b;
        auto a = steering(arr, lambda, b);
        double p = 0;
        switch (opt.method) {
        case Method::Bartlett: {
            cd s = 0;
            for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) s += std::conj(a[i]) * r(i, j) * a[j];
            p = s.real() / (double)(n * n);
            break; }
        case Method::Capon: {
            cd s = 0;
            for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) s += std::conj(a[i]) * rinv(i, j) * a[j];
            p = s.real() > 1e-300 ? 1.0 / s.real() : 0;
            break; }
        case Method::Music: {
            double proj = 0;
            for (int k = 0; k < m; k++) { cd d = 0; for (int i = 0; i < n; i++) d += std::conj(vec[k][i]) * a[i]; proj += std::norm(d); }
            double noise = n - proj;
            p = 1.0 / std::max(noise, 1e-9 * n);
            break; }
        case Method::Interferometer: {
            cd d = 0; for (int i = 0; i < n; i++) d += std::conj(a[i]) * vphase[i];
            p = std::norm(d) / (double)(n * n);
            break; }
        }
        res.spectrum[g] = p; mx = std::max(mx, p);
    }
    if (mx > 0) for (auto& s : res.spectrum) s /= mx;
    findPeaks(res);
    if (!res.peaks.empty()) {
        res.bearingDeg = wrap360(res.peaks[0].bearingDeg + opt.compassOffsetDeg);
        for (auto& p : res.peaks) p.bearingDeg = wrap360(p.bearingDeg + opt.compassOffsetDeg);
        double second = res.peaks.size() > 1 ? res.peaks[1].value : 0;
        res.psldB = second > 1e-4 ? std::min(40.0, 10 * std::log10(res.peaks[0].value / second)) : 40.0;
    }
    double cohN = n > 1 ? std::min(1.0, std::max(0.0, (res.coherence - 1.0 / n) / (1.0 - 1.0 / n))) : 1.0;
    res.confidence = cohN * (1.0 - std::exp(-res.psldB / 6.0));
    return res;
}

std::vector<double> rootMusic(const CMat& r, const Array& arr, double lambda, int sources) {
    // ULA axis angle: o = orientation of the array axis; phase step psi = k d cos(theta - o)
    std::vector<double> out;
    int n = r.n;
    if (!arr.isUla || n < 3 || sources < 1 || sources >= n) return out;
    double ax = arr.pos[n - 1][0] - arr.pos[0][0], ay = arr.pos[n - 1][1] - arr.pos[0][1];
    double o = std::atan2(ax, ay);                      // axis bearing (rad, clockwise from north)
    std::vector<double> ev; std::vector<std::vector<cd>> vec;
    eigh(r, ev, vec);
    // C = En En^H ; polynomial coefficient for diagonal l: c_l = sum of C(i, i+l)
    CMat c(n);
    for (int k = sources; k < n; k++)
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) c(i, j) += vec[k][i] * std::conj(vec[k][j]);
    int deg = 2 * (n - 1);
    std::vector<cd> p(deg + 1);                         // p[d] coefficient of z^d, d = (n-1) + l ; l = j - i
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) p[(n - 1) + (j - i)] += c(i, j);
    // roots by Durand-Kerner
    std::vector<cd> rt(deg);
    cd lead = p[deg];
    if (std::abs(lead) < 1e-300) return out;
    std::vector<cd> q(deg + 1);
    for (int i = 0; i <= deg; i++) q[i] = p[i] / lead;
    for (int i = 0; i < deg; i++) rt[i] = std::pow(cd(0.4, 0.9), i) * 1.0;
    for (int it = 0; it < 500; it++) {
        double ch = 0;
        for (int i = 0; i < deg; i++) {
            cd f = 0; for (int d = deg; d >= 0; d--) f = f * rt[i] + q[d];
            cd den = 1; for (int j = 0; j < deg; j++) if (j != i) den *= (rt[i] - rt[j]);
            if (std::abs(den) < 1e-300) den = 1e-300;
            cd dz = f / den; rt[i] -= dz; ch = std::max(ch, std::abs(dz));
        }
        if (ch < 1e-13) break;
    }
    std::vector<cd> inside;
    for (auto& z : rt) if (std::abs(z) <= 1.0 + 1e-9 && std::abs(z) > 1e-6) inside.push_back(z);
    std::sort(inside.begin(), inside.end(), [](cd a, cd b) { return std::abs(a) > std::abs(b); });   // closest to the unit circle first
    double k = 2 * kPi / lambda;
    for (size_t i = 0; i < inside.size() && (int)out.size() < sources; i++) {
        double psi = std::arg(inside[i]);
        double cs = psi / (k * arr.spacing);
        if (std::abs(cs) > 1) continue;
        double al = std::acos(cs);                      // angle from the axis, 0..pi
        out.push_back(wrap360((o - al) / D2R));         // the side counter-clockwise of the axis; the mirror image (o + al) is the ULA ambiguity
    }
    return out;
}

// ---------------------------------------------------------------- calibration
std::vector<cd> estimateCalibration(const CMat& r, const Array& arr, double lambda, double knownBearingDeg) {
    int n = r.n;
    std::vector<double> ev; std::vector<std::vector<cd>> vec;
    eigh(r, ev, vec);
    std::vector<cd> a = (std::isnan(knownBearingDeg) || knownBearingDeg < -360) ? std::vector<cd>(n, cd(1)) : steering(arr, lambda, knownBearingDeg);
    std::vector<cd> g(n);
    double amean = 0;
    for (int i = 0; i < n; i++) { g[i] = vec[0][i] / a[i]; amean += std::abs(g[i]); }
    amean /= n;
    cd ph = std::polar(1.0, std::arg(g[0]));
    for (auto& z : g) z = z / ph / amean;
    return g;
}
CMat applyCalibration(const CMat& r, const std::vector<cd>& g) {
    CMat o(r.n);
    for (int i = 0; i < r.n; i++) for (int j = 0; j < r.n; j++) o(i, j) = r(i, j) / (g[i] * std::conj(g[j]));
    return o;
}
void applyCalibration(std::vector<std::vector<cd>>& x, const std::vector<cd>& g) {
    for (size_t i = 0; i < x.size(); i++) for (auto& z : x[i]) z /= g[i];
}

// ---------------------------------------------------------------- beamforming
std::vector<cd> beamWeights(const Array& arr, double lambda, double look, const std::vector<double>& nulls) {
    int n = arr.n(), m = 1 + (int)nulls.size();
    std::vector<std::vector<cd>> c;
    c.push_back(steering(arr, lambda, look));
    for (double b : nulls) c.push_back(steering(arr, lambda, b));
    CMat g(m);
    for (int i = 0; i < m; i++) for (int j = 0; j < m; j++) { cd s = 0; for (int k = 0; k < n; k++) s += std::conj(c[i][k]) * c[j][k]; g(i, j) = s; }
    std::vector<cd> f(m, 0), y;
    f[0] = 1;
    std::vector<cd> w(n, 0);
    if (!solve(g, f, y)) { for (int k = 0; k < n; k++) w[k] = c[0][k] / (double)n; return w; }
    for (int k = 0; k < n; k++) for (int i = 0; i < m; i++) w[k] += c[i][k] * y[i];
    return w;
}
std::vector<cd> beamform(const std::vector<std::vector<cd>>& x, const std::vector<cd>& w) {
    std::vector<cd> y;
    if (x.empty()) return y;
    y.assign(x[0].size(), 0);
    for (size_t i = 0; i < x.size(); i++) for (size_t t = 0; t < y.size(); t++) y[t] += std::conj(w[i]) * x[i][t];
    return y;
}

// ---------------------------------------------------------------- rng
uint64_t Rng::next() {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
double Rng::uni() { return ((next() >> 11) + 0.5) / 9007199254740992.0; }
double Rng::gauss() { return std::sqrt(-2.0 * std::log(uni())) * std::cos(2 * kPi * uni()); }

}   // namespace dect2::df
