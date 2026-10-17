#include "dect2/df/position.h"
#include "dect2/df/df.h"
#include <algorithm>
#include <cmath>

namespace dect2::df {

static const double D2R = kPi / 180.0;
static const double WA = 6378137.0, WF = 1 / 298.257223563, WB = WA * (1 - WF), WE2 = WF * (2 - WF);

// ---------------------------------------------------------------- geodesics
bool geodInverse(double lat1, double lon1, double lat2, double lon2, double& dist, double& a1, double* a2) {
    double U1 = std::atan((1 - WF) * std::tan(lat1 * D2R)), U2 = std::atan((1 - WF) * std::tan(lat2 * D2R));
    double L = (lon2 - lon1) * D2R, lam = L;
    double sU1 = std::sin(U1), cU1 = std::cos(U1), sU2 = std::sin(U2), cU2 = std::cos(U2);
    double sinS = 0, cosS = 1, sig = 0, cosSqA = 1, cos2Sm = 0, sinLam = 0, cosLam = 1;
    bool conv = false;
    for (int it = 0; it < 200; it++) {
        sinLam = std::sin(lam); cosLam = std::cos(lam);
        sinS = std::hypot(cU2 * sinLam, cU1 * sU2 - sU1 * cU2 * cosLam);
        if (sinS == 0) { dist = 0; a1 = 0; if (a2) *a2 = 0; return true; }
        cosS = sU1 * sU2 + cU1 * cU2 * cosLam;
        sig = std::atan2(sinS, cosS);
        double sinA = cU1 * cU2 * sinLam / sinS;
        cosSqA = 1 - sinA * sinA;
        cos2Sm = cosSqA != 0 ? cosS - 2 * sU1 * sU2 / cosSqA : 0;
        double C = WF / 16 * cosSqA * (4 + WF * (4 - 3 * cosSqA));
        double lp = lam;
        lam = L + (1 - C) * WF * sinA * (sig + C * sinS * (cos2Sm + C * cosS * (-1 + 2 * cos2Sm * cos2Sm)));
        if (std::abs(lam - lp) < 1e-12) { conv = true; break; }
    }
    if (!conv) {   // near-antipodal: fall back to the sphere
        double p1 = lat1 * D2R, p2 = lat2 * D2R;
        double h = std::sin((p2 - p1) / 2), k = std::sin(L / 2);
        double d = 2 * std::asin(std::sqrt(h * h + std::cos(p1) * std::cos(p2) * k * k));
        dist = 6371008.8 * d;
        a1 = wrap360(std::atan2(std::sin(L) * std::cos(p2), std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(L)) / D2R);
        if (a2) *a2 = wrap360(a1 + 180);
        return false;
    }
    double uSq = cosSqA * (WA * WA - WB * WB) / (WB * WB);
    double A = 1 + uSq / 16384 * (4096 + uSq * (-768 + uSq * (320 - 175 * uSq)));
    double B = uSq / 1024 * (256 + uSq * (-128 + uSq * (74 - 47 * uSq)));
    double dS = B * sinS * (cos2Sm + B / 4 * (cosS * (-1 + 2 * cos2Sm * cos2Sm) - B / 6 * cos2Sm * (-3 + 4 * sinS * sinS) * (-3 + 4 * cos2Sm * cos2Sm)));
    dist = WB * A * (sig - dS);
    a1 = wrap360(std::atan2(cU2 * std::sin(lam), cU1 * sU2 - sU1 * cU2 * std::cos(lam)) / D2R);
    if (a2) *a2 = wrap360(std::atan2(cU1 * std::sin(lam), -sU1 * cU2 + cU1 * sU2 * std::cos(lam)) / D2R);
    return true;
}

void geodDirect(double lat1, double lon1, double az1, double s, double& lat2, double& lon2) {
    double a = az1 * D2R, sA = std::sin(a), cA = std::cos(a);
    double tanU1 = (1 - WF) * std::tan(lat1 * D2R), cU1 = 1 / std::sqrt(1 + tanU1 * tanU1), sU1 = tanU1 * cU1;
    double sig1 = std::atan2(tanU1, cA), sinAl = cU1 * sA, cosSqAl = 1 - sinAl * sinAl;
    double uSq = cosSqAl * (WA * WA - WB * WB) / (WB * WB);
    double A = 1 + uSq / 16384 * (4096 + uSq * (-768 + uSq * (320 - 175 * uSq)));
    double B = uSq / 1024 * (256 + uSq * (-128 + uSq * (74 - 47 * uSq)));
    double sig = s / (WB * A), sigP = 0, cos2SigM = 0, sinSig = 0, cosSig = 1;
    for (int i = 0; i < 100; i++) {
        cos2SigM = std::cos(2 * sig1 + sig); sinSig = std::sin(sig); cosSig = std::cos(sig);
        double dS = B * sinSig * (cos2SigM + B / 4 * (cosSig * (-1 + 2 * cos2SigM * cos2SigM) - B / 6 * cos2SigM * (-3 + 4 * sinSig * sinSig) * (-3 + 4 * cos2SigM * cos2SigM)));
        sigP = sig; sig = s / (WB * A) + dS;
        if (std::abs(sig - sigP) < 1e-12) break;
    }
    cos2SigM = std::cos(2 * sig1 + sig); sinSig = std::sin(sig); cosSig = std::cos(sig);
    double tmp = sU1 * sinSig - cU1 * cosSig * cA;
    double p2 = std::atan2(sU1 * cosSig + cU1 * sinSig * cA, (1 - WF) * std::sqrt(sinAl * sinAl + tmp * tmp));
    double lam = std::atan2(sinSig * sA, cU1 * cosSig - sU1 * sinSig * cA);
    double C = WF / 16 * cosSqAl * (4 + WF * (4 - 3 * cosSqAl));
    double L = lam - (1 - C) * WF * sinAl * (sig + C * sinSig * (cos2SigM + C * cosSig * (-1 + 2 * cos2SigM * cos2SigM)));
    lat2 = p2 / D2R;
    lon2 = lon1 + L / D2R;
    lon2 = std::fmod(lon2 + 540.0, 360.0) - 180.0;
}

// ---------------------------------------------------------------- local frame
struct Frame2 {
    double lat0, lon0, mE, mN;
    explicit Frame2(double la, double lo) : lat0(la), lon0(lo) {
        double s = std::sin(la * D2R), w = 1 - WE2 * s * s;
        mN = WA * (1 - WE2) / std::pow(w, 1.5) * D2R;
        mE = WA * std::cos(la * D2R) / std::sqrt(w) * D2R;
    }
    void toEN(double la, double lo, double& e, double& n) const { e = (std::fmod(lo - lon0 + 540.0, 360.0) - 180.0) * mE; n = (la - lat0) * mN; }
    void toLL(double e, double n, double& la, double& lo) const { la = lat0 + n / mN; lo = lon0 + e / mE; lo = std::fmod(lo + 540.0, 360.0) - 180.0; }
};

static double azimuthTo(const BearingLine& l, double lat, double lon) {
    double d, a;
    geodInverse(l.lat, l.lon, lat, lon, d, a);
    return a;
}

static Ellipse ellipseFromCov(double pee, double pen, double pnn, double scale) {
    double tr = pee + pnn, det = pee * pnn - pen * pen;
    double disc = std::sqrt(std::max(0.0, tr * tr / 4 - det));
    double l1 = tr / 2 + disc, l2 = std::max(0.0, tr / 2 - disc);
    double ang = 0.5 * std::atan2(2 * pen, pnn - pee);   // angle of the major axis from north (clockwise), since x=east,y=north: tan(2a)=2 Pen/(Pnn-Pee)
    Ellipse e;
    e.semiMajorM = scale * std::sqrt(l1);
    e.semiMinorM = scale * std::sqrt(l2);
    e.orientationDeg = std::fmod(ang / D2R + 180.0, 180.0);
    return e;
}

static void finishEllipse(Fix& f, double pee, double pen, double pnn) {
    f.e95 = ellipseFromCov(pee, pen, pnn, std::sqrt(5.991));
    Ellipse s = ellipseFromCov(pee, pen, pnn, 1.0);
    f.cep50M = 0.589 * (s.semiMajorM + s.semiMinorM);   // standard approximation CEP ~ 0.589 (sigma_x + sigma_y)
}

// ---------------------------------------------------------------- estimators
static bool intersectPair(const BearingLine& a, const BearingLine& b, double& lat, double& lon) {
    Frame2 fr(0.5 * (a.lat + b.lat), a.lon + 0.5 * wrap180(b.lon - a.lon));
    double ea, na, eb, nb;
    fr.toEN(a.lat, a.lon, ea, na); fr.toEN(b.lat, b.lon, eb, nb);
    double ua = std::sin(a.bearingDeg * D2R), va = std::cos(a.bearingDeg * D2R), ub = std::sin(b.bearingDeg * D2R), vb = std::cos(b.bearingDeg * D2R);
    double den = ua * vb - va * ub;
    if (std::abs(den) < 1e-6) return false;
    double t = ((eb - ea) * vb - (nb - na) * ub) / den;
    double s = ((eb - ea) * va - (nb - na) * ua) / den;
    if (t <= 0 || s <= 0) return false;       // the lines meet behind a station
    fr.toLL(ea + t * ua, na + t * va, lat, lon);
    return true;
}

static bool stansfield(const std::vector<BearingLine>& L, const std::vector<int>& use, double& lat, double& lon, int passes = 3) {
    if (use.size() < 2) return false;
    double slat = 0, dlon = 0;
    for (int i : use) { slat += L[i].lat; dlon += wrap180(L[i].lon - L[use[0]].lon); }
    Frame2 fr(slat / use.size(), wrap180(L[use[0]].lon + dlon / use.size()));
    std::vector<double> range(use.size(), 1.0);       // pass 0 weights all lines by sigma only; later passes use the range to the estimate
    double e = 0, n = 0;
    for (int pass = 0; pass < passes; pass++) {
        double a11 = 0, a12 = 0, a22 = 0, b1 = 0, b2 = 0;
        for (size_t k = 0; k < use.size(); k++) {
            const BearingLine& l = L[use[k]];
            double se, sn; fr.toEN(l.lat, l.lon, se, sn);
            double u = std::sin(l.bearingDeg * D2R), v = std::cos(l.bearingDeg * D2R);
            double sg = std::max(l.sigmaDeg, 0.01) * D2R;
            double w = 1.0 / (range[k] * sg * range[k] * sg);
            // normal n = (v, -u); residual = n . (x - s)
            a11 += w * v * v; a12 += w * v * (-u); a22 += w * u * u;
            double c = v * se - u * sn;
            b1 += w * v * c; b2 += w * (-u) * c;
        }
        double det = a11 * a22 - a12 * a12;
        if (std::abs(det) < 1e-30) return false;
        e = (a22 * b1 - a12 * b2) / det; n = (-a12 * b1 + a11 * b2) / det;
        for (size_t k = 0; k < use.size(); k++) {
            double se, sn; fr.toEN(L[use[k]].lat, L[use[k]].lon, se, sn);
            range[k] = std::max(1.0, std::hypot(e - se, n - sn));
        }
    }
    fr.toLL(e, n, lat, lon);
    return true;
}

static Fix mlFit(const std::vector<BearingLine>& L, const std::vector<int>& use, double lat, double lon, bool gaussNewton) {
    Fix f;
    f.lat = lat; f.lon = lon;
    double lastCost = 1e300;
    double pee = 0, pen = 0, pnn = 0, cost = 0;
    for (int it = 0; it < (gaussNewton ? 30 : 1); it++) {
        Frame2 fr(f.lat, f.lon);
        double a11 = 0, a12 = 0, a22 = 0, g1 = 0, g2 = 0;
        cost = 0;
        for (int i : use) {
            const BearingLine& l = L[i];
            double se, sn; fr.toEN(l.lat, l.lon, se, sn);
            double de = -se, dn = -sn;                 // station -> estimate (estimate is the origin)
            double r2 = std::max(1.0, de * de + dn * dn);
            double sg = std::max(l.sigmaDeg, 0.01) * D2R;
            double res = angDiff(l.bearingDeg, azimuthTo(l, f.lat, f.lon)) * D2R;
            // measured - predicted; predicted theta(E,N)=atan2(de,dn): d/dE = dn/r2, d/dN = -de/r2 (moving the estimate by (E,N))
            double jE = dn / r2, jN = -de / r2, w = 1.0 / (sg * sg);
            a11 += w * jE * jE; a12 += w * jE * jN; a22 += w * jN * jN;
            g1 += w * jE * res; g2 += w * jN * res;
            cost += w * res * res;
        }
        double det = a11 * a22 - a12 * a12;
        if (std::abs(det) < 1e-30 || !std::isfinite(det)) return f;
        pee = a22 / det; pen = -a12 / det; pnn = a11 / det;
        if (!gaussNewton) break;
        double dE = pee * g1 + pen * g2, dN = pen * g1 + pnn * g2;
        double step = std::hypot(dE, dN);
        double lat2, lon2; fr.toLL(dE, dN, lat2, lon2);
        f.lat = lat2; f.lon = lon2;
        if (step < 0.01 || std::abs(lastCost - cost) < 1e-12 * (1 + cost)) break;
        lastCost = cost;
    }
    // final covariance and residual at the solution
    Frame2 fr(f.lat, f.lon);
    double a11 = 0, a12 = 0, a22 = 0, rss = 0, wsum = 0;
    for (int i : use) {
        const BearingLine& l = L[i];
        double se, sn; fr.toEN(l.lat, l.lon, se, sn);
        double r2 = std::max(1.0, se * se + sn * sn), sg = std::max(l.sigmaDeg, 0.01) * D2R;
        double jE = -sn / r2, jN = se / r2, w = 1.0 / (sg * sg);
        a11 += w * jE * jE; a12 += w * jE * jN; a22 += w * jN * jN;
        double res = angDiff(l.bearingDeg, azimuthTo(l, f.lat, f.lon));
        rss += res * res * w; wsum += w;
    }
    double det = a11 * a22 - a12 * a12;
    if (std::abs(det) < 1e-30 || !std::isfinite(det)) return f;
    finishEllipse(f, a22 / det, -a12 / det, a11 / det);
    f.rmsResidualDeg = std::sqrt(rss / wsum);
    f.ok = std::isfinite(f.lat) && std::isfinite(f.lon);
    return f;
}

static Fix fitSubset(const std::vector<BearingLine>& L, const std::vector<int>& use, FixMethod m) {
    Fix f;
    double lat, lon;
    if (!stansfield(L, use, lat, lon, m == FixMethod::Stansfield ? 4 : 2)) {
        if (use.size() >= 2 && intersectPair(L[use[0]], L[use[1]], lat, lon)) { /* ok */ } else return f;
    }
    f = mlFit(L, use, lat, lon, m == FixMethod::MaxLikelihood);
    return f;
}

Fix fixPosition(const std::vector<BearingLine>& lines, FixMethod method) {
    std::vector<int> all;
    for (size_t i = 0; i < lines.size(); i++) all.push_back((int)i);
    Fix f = fitSubset(lines, all, method);
    f.inlier.assign(lines.size(), 1);
    f.inliers = (int)lines.size();
    return f;
}

Fix fixPositionRobust(const std::vector<BearingLine>& lines, FixMethod method, double nSig, double minGate) {
    int n = (int)lines.size();
    Fix best;
    best.inlier.assign(n, 0);
    if (n < 2) return best;
    std::vector<std::pair<int, int>> pairs;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) pairs.push_back({i, j});
    if (pairs.size() > 400) {
        Rng r(12345);
        std::vector<std::pair<int, int>> sel;
        for (int k = 0; k < 400; k++) sel.push_back(pairs[(size_t)(r.uni() * pairs.size())]);
        pairs = sel;
    }
    int bestCount = 0;
    double bestScore = 1e300;
    std::vector<char> bestMask;
    for (auto [i, j] : pairs) {
        double lat, lon;
        if (!intersectPair(lines[i], lines[j], lat, lon)) continue;
        std::vector<char> mask(n, 0);
        int cnt = 0;
        double score = 0;
        for (int k = 0; k < n; k++) {
            double res = std::abs(angDiff(lines[k].bearingDeg, azimuthTo(lines[k], lat, lon)));
            double gate = std::max(nSig * lines[k].sigmaDeg, minGate);
            if (res <= gate) { mask[k] = 1; cnt++; score += (res / lines[k].sigmaDeg) * (res / lines[k].sigmaDeg); }
        }
        if (cnt > bestCount || (cnt == bestCount && score < bestScore)) { bestCount = cnt; bestScore = score; bestMask = mask; }
    }
    if (bestCount < 2) return best;
    std::vector<int> use;
    for (int k = 0; k < n; k++) if (bestMask[k]) use.push_back(k);
    best = fitSubset(lines, use, method);
    best.inlier = bestMask;
    best.inliers = bestCount;
    // refit once more with the gate evaluated at the refined solution
    if (best.ok) {
        std::vector<int> use2;
        std::vector<char> m2(n, 0);
        for (int k = 0; k < n; k++) {
            double res = std::abs(angDiff(lines[k].bearingDeg, azimuthTo(lines[k], best.lat, best.lon)));
            if (res <= std::max(nSig * lines[k].sigmaDeg, minGate)) { use2.push_back(k); m2[k] = 1; }
        }
        if (use2.size() >= 2 && use2 != use) { Fix f2 = fitSubset(lines, use2, method); if (f2.ok) { best = f2; best.inlier = m2; best.inliers = (int)use2.size(); } }
    }
    return best;
}

Fix fixWithRangePrior(const BearingLine& l, double range, double rangeSigma) {
    Fix f;
    geodDirect(l.lat, l.lon, l.bearingDeg, range, f.lat, f.lon);
    double cross = range * l.sigmaDeg * D2R;
    double th = l.bearingDeg * D2R;
    // covariance in (E,N): along-bearing variance rangeSigma^2, cross variance cross^2
    double u = std::sin(th), v = std::cos(th);
    double pa = rangeSigma * rangeSigma, pc = cross * cross;
    double pee = pa * u * u + pc * v * v, pnn = pa * v * v + pc * u * u, pen = (pa - pc) * u * v;
    finishEllipse(f, pee, pen, pnn);
    f.inlier = {1}; f.inliers = 1; f.ok = true;
    return f;
}

}   // namespace dect2::df
