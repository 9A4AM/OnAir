// Hydrogen line toolkit: time, coordinates, the observer's velocity (LSR correction), pointing and visibility (see astro/hline.h).
// References (formulas only, no code taken):
//  J. Meeus, Astronomical Algorithms, 2nd ed.: ch. 7 (Julian day), 12 (sidereal time), 13 (coordinates), 16 (refraction), 21 (precession IAU 1976),
//   25 (low-accuracy Sun; the Earth's orbital elements), 15 (rise, transit, set).
//  The Astronomical Almanac, "low precision formulae for the Moon" (about 0.3 deg).
//  IAU 1958 galactic system, positions of its pole in the ICRS from the Hipparcos catalogue (ESA SP-1200): 192.85948, +27.12825, 122.93192.
//  Standard solar motion (the "IAU" LSR): 20 km/s towards RA 18h, Dec +30 (B1900) = 18h03m50.29s +30d00m16.8s (J2000); Delhaye 1965, Kerr & Lynden-Bell 1986.
//  Dynamical LSR: Schoenrich, Binney & Dehnen 2010, MNRAS 403, 1829: (U, V, W) = (11.1, 12.24, 7.25) km/s.
//  Earth rotation speed 0.4651 km/s at the equator: omega = 7.2921159e-5 rad/s, a = 6378.137 km, flattening 1/298.257.
#include "dect2/astro/hline.h"
#include <algorithm>
#include <cmath>

namespace dect2 {
namespace astro {
namespace hl {

namespace {
constexpr double kD2R = M_PI / 180.0;
double wrap360(double a) { a = std::fmod(a, 360.0); return a < 0 ? a + 360.0 : a; }
double wrap180(double a) { a = wrap360(a); return a > 180 ? a - 360 : a; }
struct V3 { double x, y, z; };
V3 unit(const Eq& e) { const double a = e.raDeg * kD2R, d = e.decDeg * kD2R; return {std::cos(d) * std::cos(a), std::cos(d) * std::sin(a), std::sin(d)}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
}

const Eq kCasA{(23 + 23 / 60.0 + 24.0 / 3600) * 15, 58 + 48 / 60.0 + 54.0 / 3600};
const Eq kCygA{(19 + 59 / 60.0 + 28.356 / 3600) * 15, 40 + 44 / 60.0 + 2.10 / 3600};
const Eq kSgrA{(17 + 45 / 60.0 + 40.0383 / 3600) * 15, -(29 + 0 / 60.0 + 28.069 / 3600)};

double julianDate(const Utc& t) {
    int y = t.y, m = t.mo;
    if (m <= 2) { y--; m += 12; }
    const int a = y / 100, b = 2 - a + a / 4;
    const double day = t.d + (t.h + (t.mi + t.s / 60.0) / 60.0) / 24.0;
    return std::floor(365.25 * (y + 4716)) + std::floor(30.6001 * (m + 1)) + day + b - 1524.5;
}

Utc fromJulianDate(double jd) {
    const double j = jd + 0.5, z = std::floor(j), f = j - z;
    double a = z;
    if (z >= 2299161) { const double al = std::floor((z - 1867216.25) / 36524.25); a = z + 1 + al - std::floor(al / 4); }
    const double b = a + 1524, c = std::floor((b - 122.1) / 365.25), d = std::floor(365.25 * c), e = std::floor((b - d) / 30.6001);
    const double dayf = b - d - std::floor(30.6001 * e) + f;
    Utc u;
    u.d = (int)std::floor(dayf);
    u.mo = (int)(e < 14 ? e - 1 : e - 13);
    u.y = (int)(u.mo > 2 ? c - 4716 : c - 4715);
    double sec = (dayf - u.d) * 86400.0;
    sec = std::round(sec * 1000) / 1000;
    u.h = (int)(sec / 3600); sec -= u.h * 3600;
    u.mi = (int)(sec / 60); sec -= u.mi * 60;
    u.s = sec;
    return u;
}

double gmstDeg(double jd) {
    const double T = (jd - 2451545.0) / 36525.0;
    return wrap360(280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * T * T - T * T * T / 38710000.0);
}
double lmstDeg(double jd, double lon) { return wrap360(gmstDeg(jd) + lon); }

Gal galacticFromJ2000(const Eq& e) {
    const double ag = 192.85948 * kD2R, dg = 27.12825 * kD2R, ln = 122.93192 * kD2R;
    const double a = e.raDeg * kD2R, d = e.decDeg * kD2R;
    const double sb = std::sin(d) * std::sin(dg) + std::cos(d) * std::cos(dg) * std::cos(a - ag);
    const double l = ln - std::atan2(std::cos(d) * std::sin(a - ag), std::sin(d) * std::cos(dg) - std::cos(d) * std::sin(dg) * std::cos(a - ag));
    return {wrap360(l / kD2R), std::asin(std::clamp(sb, -1.0, 1.0)) / kD2R};
}
Eq j2000FromGalactic(const Gal& g) {
    const double ag = 192.85948 * kD2R, dg = 27.12825 * kD2R, ln = 122.93192 * kD2R;
    const double l = g.lDeg * kD2R, b = g.bDeg * kD2R;
    const double sd = std::sin(b) * std::sin(dg) + std::cos(b) * std::cos(dg) * std::cos(ln - l);
    const double a = ag + std::atan2(std::cos(b) * std::sin(ln - l), std::sin(b) * std::cos(dg) - std::cos(b) * std::sin(dg) * std::cos(ln - l));
    return {wrap360(a / kD2R), std::asin(std::clamp(sd, -1.0, 1.0)) / kD2R};
}

Eq precessJ2000ToDate(const Eq& e, double jd) {
    const double t = (jd - 2451545.0) / 36525.0, as = kD2R / 3600.0;
    const double zeta = (2306.2181 * t + 0.30188 * t * t + 0.017998 * t * t * t) * as;
    const double z = (2306.2181 * t + 1.09468 * t * t + 0.018203 * t * t * t) * as;
    const double th = (2004.3109 * t - 0.42665 * t * t - 0.041833 * t * t * t) * as;
    const double a = e.raDeg * kD2R, d = e.decDeg * kD2R;
    const double A = std::cos(d) * std::sin(a + zeta);
    const double B = std::cos(th) * std::cos(d) * std::cos(a + zeta) - std::sin(th) * std::sin(d);
    const double C = std::sin(th) * std::cos(d) * std::cos(a + zeta) + std::cos(th) * std::sin(d);
    return {wrap360((std::atan2(A, B) + z) / kD2R), std::asin(std::clamp(C, -1.0, 1.0)) / kD2R};
}

Hor horizontal(const Eq& e, double jd, double lat, double lon, bool refr) {
    const double H = (lmstDeg(jd, lon) - e.raDeg) * kD2R, phi = lat * kD2R, d = e.decDeg * kD2R;
    const double A = std::atan2(std::sin(H), std::cos(H) * std::sin(phi) - std::tan(d) * std::cos(phi));
    double h = std::asin(std::clamp(std::sin(phi) * std::sin(d) + std::cos(phi) * std::cos(d) * std::cos(H), -1.0, 1.0)) / kD2R;
    if (refr && h > -1.0) h += (1.02 / std::tan((h + 10.3 / (h + 5.11)) * kD2R)) / 60.0;
    return {wrap360(A / kD2R + 180.0), h};
}

// ---------------------------------------------------------------------------------------------------------------- Sun, Moon, Earth
namespace {
struct SunInfo { double lonTrue, lonApp, eps, rAu, nu, e; };
SunInfo sunInfo(double jd) {
    const double T = (jd - 2451545.0) / 36525.0;
    const double L0 = 280.46646 + 36000.76983 * T + 0.0003032 * T * T;
    const double M = 357.52911 + 35999.05029 * T - 0.0001537 * T * T;
    const double e = 0.016708634 - 0.000042037 * T - 0.0000001267 * T * T;
    const double Mr = M * kD2R;
    const double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * std::sin(Mr) + (0.019993 - 0.000101 * T) * std::sin(2 * Mr) + 0.000289 * std::sin(3 * Mr);
    const double om = (125.04 - 1934.136 * T) * kD2R;
    SunInfo s;
    s.lonTrue = L0 + C;
    s.lonApp = s.lonTrue - 0.00569 - 0.00478 * std::sin(om);
    s.eps = 23.439291 - 0.0130042 * T + 0.00256 * std::cos(om);
    s.nu = M + C;
    s.e = e;
    s.rAu = 1.000001018 * (1 - e * e) / (1 + e * std::cos(s.nu * kD2R));
    return s;
}
Eq eclToEq(double lonDeg, double latDeg, double epsDeg) {
    const double l = lonDeg * kD2R, b = latDeg * kD2R, e = epsDeg * kD2R;
    return {wrap360(std::atan2(std::sin(l) * std::cos(e) - std::tan(b) * std::sin(e), std::cos(l)) / kD2R),
            std::asin(std::clamp(std::sin(b) * std::cos(e) + std::cos(b) * std::sin(e) * std::sin(l), -1.0, 1.0)) / kD2R};
}
Eq moonEq(double jd, double* distKm) {
    const double T = (jd - 2451545.0) / 36525.0;
    auto s = [&](double a, double b) { return std::sin((a + b * T) * kD2R); };
    auto c = [&](double a, double b) { return std::cos((a + b * T) * kD2R); };
    const double lon = 218.32 + 481267.881 * T + 6.29 * s(135.0, 477198.87) - 1.27 * s(259.3, -413335.36) + 0.66 * s(235.7, 890534.22)
                       + 0.21 * s(269.9, 954397.74) - 0.19 * s(357.5, 35999.05) - 0.11 * s(186.5, 966404.03);
    const double lat = 5.13 * s(93.3, 483202.02) + 0.28 * s(228.2, 960400.89) - 0.28 * s(318.3, 6003.15) - 0.17 * s(217.6, -407332.21);
    const double par = 0.9508 + 0.0518 * c(135.0, 477198.87) + 0.0095 * c(259.3, -413335.36) + 0.0078 * c(235.7, 890534.22) + 0.0028 * c(269.9, 954397.74);
    if (distKm) *distKm = 6378.14 / std::sin(par * kD2R);
    return eclToEq(lon, lat, 23.439291 - 0.0130042 * T);
}
} // namespace

void earthVelocity(double jd, double v[3]) {
    const SunInfo s = sunInfo(jd);
    const double theta = (s.lonTrue + 180.0) * kD2R;     // heliocentric longitude of the Earth
    const double nu = s.nu * kD2R, e = s.e;
    const double na = 0.9856474 * kD2R / 86400.0 * 1.000001018 * 149597870.7;   // n a, km/s
    const double k = na / std::sqrt(1 - e * e);
    const double vr = k * e * std::sin(nu), vt = k * (1 + e * std::cos(nu));
    const double vx = vr * std::cos(theta) - vt * std::sin(theta), vy = vr * std::sin(theta) + vt * std::cos(theta);
    const double ce = std::cos(s.eps * kD2R), se = std::sin(s.eps * kD2R);
    v[0] = vx; v[1] = vy * ce; v[2] = vy * se;           // equatorial of date
}

Eq solarApexJ2000(Lsr lsr, double* speed) {
    if (lsr == Lsr::Kinematic) {
        if (speed) *speed = 20.0;
        return {(18 + 3 / 60.0 + 50.29 / 3600) * 15, 30 + 0 / 60.0 + 16.8 / 3600};
    }
    const double U = 11.1, V = 12.24, W = 7.25;
    const V3 gx = unit(j2000FromGalactic({0, 0})), gy = unit(j2000FromGalactic({90, 0})), gz = unit(j2000FromGalactic({0, 90}));
    const V3 s{U * gx.x + V * gy.x + W * gz.x, U * gx.y + V * gy.y + W * gz.y, U * gx.z + V * gy.z + W * gz.z};
    const double sp = std::sqrt(dot(s, s));
    if (speed) *speed = sp;
    return {wrap360(std::atan2(s.y, s.x) / kD2R), std::asin(s.z / sp) / kD2R};
}

ObserverVelocity observerVelocity(const Eq& src, double jd, double lat, double lon, Lsr lsr) {
    const Eq sd = precessJ2000ToDate(src, jd);
    const V3 u = unit(sd);
    ObserverVelocity o;
    // rotation: speed at the latitude (ellipsoid, sea level) times the cosine of the declination and sine of the hour angle, east-moving
    const double phi = lat * kD2R, om = 7.2921159e-5, a = 6378.137, f2 = 0.00669437999;
    const double radius = a / std::sqrt(1 - f2 * std::sin(phi) * std::sin(phi)) * std::cos(phi);
    const double H = (lmstDeg(jd, lon) - sd.raDeg) * kD2R;
    o.rotation = -om * radius * std::cos(sd.decDeg * kD2R) * std::sin(H);
    double ev[3];
    earthVelocity(jd, ev);
    o.orbit = ev[0] * u.x + ev[1] * u.y + ev[2] * u.z;
    double sp = 0;
    const Eq ap = solarApexJ2000(lsr, &sp);
    o.sun = sp * dot(unit(precessJ2000ToDate(ap, jd)), u);
    o.total = o.rotation + o.orbit + o.sun;
    return o;
}

// -------------------------------------------------------------------------------------------------------------------- pointing
const char* bodyName(Body b) {
    switch (b) {
    case Body::Galactic: return "Galactic position";
    case Body::Sun: return "Sun";
    case Body::Moon: return "Moon";
    case Body::CasA: return "Cassiopeia A";
    case Body::CygA: return "Cygnus A";
    case Body::SgrA: return "Sagittarius A*";
    default: return "Custom (RA / Dec)";
    }
}

Eq bodyPosition(Body b, double jd, const Eq& target, double* dist) {
    if (dist) *dist = 0;
    switch (b) {
    case Body::Sun: { const SunInfo s = sunInfo(jd); if (dist) *dist = s.rAu * 149597870.7; return eclToEq(s.lonApp, 0, s.eps); }
    case Body::Moon: return moonEq(jd, dist);
    case Body::CasA: return precessJ2000ToDate(kCasA, jd);
    case Body::CygA: return precessJ2000ToDate(kCygA, jd);
    case Body::SgrA: return precessJ2000ToDate(kSgrA, jd);
    default: return precessJ2000ToDate(target, jd);   // Galactic: the caller passes the J2000 equivalent of l, b
    }
}

Hor bodyHorizontal(Body b, double jd, double lat, double lon, const Eq& target, bool refr) {
    double d = 0;
    const Eq e = bodyPosition(b, jd, target, &d);
    Hor h = horizontal(e, jd, lat, lon, false);
    if (b == Body::Moon && d > 0) h.elDeg -= std::asin(6378.14 / d) / kD2R * std::cos(h.elDeg * kD2R);   // topocentric parallax in altitude
    if (refr && h.elDeg > -1.0) h.elDeg += (1.02 / std::tan((h.elDeg + 10.3 / (h.elDeg + 5.11)) * kD2R)) / 60.0;
    return h;
}

Transit events(Body b, double jd0, double lat, double lon, const Eq& target, double horizon) {
    Transit t;
    const double step = 5.0 / 1440.0;
    auto elOf = [&](double jd) { return bodyHorizontal(b, jd, lat, lon, target, false).elDeg - horizon; };
    auto haOf = [&](double jd) { return wrap180(lmstDeg(jd, lon) - bodyPosition(b, jd, target).raDeg); };
    auto bisect = [&](auto f, double lo, double hi) { const double flo = f(lo); for (int i = 0; i < 40; i++) { const double m = 0.5 * (lo + hi); ((f(m) > 0) == (flo > 0)) ? lo = m : hi = m; } return 0.5 * (lo + hi); };
    double prevEl = elOf(jd0), prevHa = haOf(jd0);
    for (int i = 1; i <= 288; i++) {
        const double jd = jd0 + i * step, el = elOf(jd), ha = haOf(jd);
        if (!t.transits && prevHa < 0 && ha >= 0 && ha - prevHa < 90) {
            t.transits = true;
            t.transitJd = bisect(haOf, jd - step, jd);
            t.transitElDeg = bodyHorizontal(b, t.transitJd, lat, lon, target, false).elDeg;
        }
        if (prevEl < 0 && el >= 0 && t.riseJd == 0) { t.rises = true; t.riseJd = bisect(elOf, jd - step, jd); }
        if (prevEl >= 0 && el < 0 && t.setJd == 0) { t.rises = true; t.setJd = bisect(elOf, jd - step, jd); }
        prevEl = el; prevHa = ha;
    }
    return t;
}

std::vector<double> visibility(Body b, double jd0, double lat, double lon, const Eq& target, int steps) {
    std::vector<double> o((size_t)steps);
    for (int i = 0; i < steps; i++) o[(size_t)i] = bodyHorizontal(b, jd0 + (double)i / steps, lat, lon, target, false).elDeg;
    return o;
}

} // namespace hl
} // namespace astro
} // namespace dect2
