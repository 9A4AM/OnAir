// Landing prediction for a falling radiosonde: integrates the descent from the last position, vertical speed and horizontal drift.
// The parachute falls faster in thin air: speed(h) = speed(now) * sqrt(rho(now) / rho(h)), with an exponential atmosphere (scale height
// 7 km). The horizontal speed is kept at the last measured value, so the answer is a guide, good to a few km from 10 km up.
#pragma once
#include <cmath>

namespace dect2 {

struct SondeLanding {
    bool valid = false;
    double lat = 0, lon = 0;   // where it should come down
    double secondsLeft = 0;    // until then
};

inline SondeLanding predictSondeLanding(double lat, double lon, double altM, double vSpeedMs, double hSpeedMs, double headingDeg, double groundM = 0) {
    SondeLanding r;
    if (!(vSpeedMs < -1.0) || altM <= groundM + 10) return r;
    const double kPi = 3.14159265358979323846, kR = 6371000.0, kScale = 7000.0;
    const double vE = hSpeedMs * std::sin(headingDeg * kPi / 180.0), vN = hSpeedMs * std::cos(headingDeg * kPi / 180.0);
    double h = altM, t = 0, la = lat, lo = lon;
    const double v0 = -vSpeedMs, h0 = altM;
    while (h > groundM && t < 36000) {
        const double v = v0 * std::exp((h - h0) / (2 * kScale));   // slower in the denser air below
        const double dt = 1.0;
        h -= v * dt; t += dt;
        la += vN * dt / kR * 180.0 / kPi;
        lo += vE * dt / (kR * std::cos(la * kPi / 180.0)) * 180.0 / kPi;
    }
    r.valid = true; r.lat = la; r.lon = lo; r.secondsLeft = t;
    return r;
}

}  // namespace dect2
