#include "dect2/geo/maplayers.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dect2 {
namespace {
constexpr double kPi = 3.14159265358979323846, kEarthKm = 6371.0;
std::string hex(uint32_t v, int w) { char b[16]; snprintf(b, sizeof b, "%0*X", w, v); return b; }
}

std::string ageText(double s) {
    char b[48];
    if (s < 0) s = 0;
    if (s < 60) snprintf(b, sizeof b, "%.0f s", s);
    else if (s < 3600) snprintf(b, sizeof b, "%.0f min", s / 60);
    else snprintf(b, sizeof b, "%d h %d min", (int)(s / 3600), (int)std::fmod(s / 60, 60));
    return b;
}

std::vector<MapMarker> markersFromAdsb(const AdsbTelemetry& t) {
    std::vector<MapMarker> v;
    for (const AdsbAircraft& a : t.aircraft) {
        if (!a.hasPos) continue;
        MapMarker m; m.layer = LayerAdsb; m.id = hex(a.icao & 0xFFFFFF, 6);
        m.label = a.callsign.empty() ? m.id : a.callsign;
        m.lat = a.lat; m.lon = a.lon;
        if (a.hasAlt) { m.hasAlt = true; m.altM = a.altFt * 0.3048; }
        if (a.hasSpeed) { m.hasSpeed = true; m.speedKt = a.speedKt; }
        if (a.hasHeading) m.headingDeg = a.headingDeg;
        m.ageSec = a.posAgeSec > 0 ? a.posAgeSec : a.ageSec;
        m.detail = a.category.empty() ? (a.ground ? "on the ground" : "") : "category " + a.category;
        for (const auto& p : a.track) m.track.push_back({p.lat, p.lon});
        v.push_back(std::move(m));
    }
    return v;
}

std::vector<MapMarker> markersFromAis(const AisTelemetry& t) {
    std::vector<MapMarker> v;
    for (const AisVessel& s : t.vessels) {
        if (!s.hasPos) continue;
        MapMarker m; m.layer = LayerAis; m.id = std::to_string(s.mmsi);
        m.label = s.name.empty() ? m.id : s.name;
        m.lat = s.lat; m.lon = s.lon;
        if (s.sog >= 0) { m.hasSpeed = true; m.speedKt = s.sog; }
        m.headingDeg = s.heading >= 0 ? s.heading : (s.cog >= 0 ? s.cog : -1);
        m.ageSec = s.ageSec;
        m.detail = s.destination.empty() ? s.callsign : "to " + s.destination;
        if (s.altitudeM >= 0) { m.hasAlt = true; m.altM = s.altitudeM; }
        for (const auto& p : s.track) m.track.push_back({p.lat, p.lon});
        v.push_back(std::move(m));
    }
    return v;
}

std::vector<MapMarker> markersFromAprs(const PacketTelemetry& t) {
    std::vector<MapMarker> v;
    for (const PacketStation& s : t.stations) {
        if (!s.hasPos) continue;
        MapMarker m; m.layer = LayerAprs; m.id = s.call; m.label = s.call;
        m.lat = s.lat; m.lon = s.lon;
        if (s.hasAlt) { m.hasAlt = true; m.altM = s.altM; }
        if (s.hasSpeed) { m.hasSpeed = true; m.speedKt = s.speedKnots; }
        if (s.hasCourse) m.headingDeg = s.courseDeg;
        m.ageSec = std::max(0.0, t.timeSec - s.lastHeardSec);
        m.detail = s.comment;
        v.push_back(std::move(m));
    }
    return v;
}

std::vector<MapMarker> markersFromSonde(const SondeTelemetry& t) {
    std::vector<MapMarker> v;
    for (const SondeInfo& s : t.sondes) {
        if (!s.hasPos) continue;
        MapMarker m; m.layer = LayerSonde; m.id = s.serial; m.label = s.serial;
        m.lat = s.lat; m.lon = s.lon; m.hasAlt = true; m.altM = s.altM;
        if (s.hasVel) { m.hasSpeed = true; m.speedKt = s.hSpeed * 1.943844; m.headingDeg = s.headingDeg; }
        m.ageSec = s.lastHeardS;
        m.detail = s.type + (s.hasVel && s.vSpeed != 0 ? (s.vSpeed < 0 ? ", falling" : ", rising") : "");
        for (const auto& p : s.track) m.track.push_back({p.lat, p.lon});
        v.push_back(std::move(m));
    }
    return v;
}

double footprintRadiusKm(double altKm, double minElDeg) {
    if (altKm <= 0) return 0;
    const double e = minElDeg * kPi / 180.0;
    const double c = kEarthKm / (kEarthKm + altKm) * std::cos(e);
    return kEarthKm * (std::acos(std::min(1.0, c)) - e);
}

bool satelliteMapPoint(const Sgp4& s, double unixSec, double pastMin, double futureMin, double stepMin, SatMapPoint& out, double minElDeg) {
    out = SatMapPoint();
    if (!s.ok() || stepMin <= 0) return false;
    out.name = s.tle().name; out.norad = s.tle().norad;
    if (!subPoint(s, unixSec, out.lat, out.lon, out.altKm)) return false;
    out.footprintKm = footprintRadiusKm(out.altKm, minElDeg);
    const int a = (int)std::floor(pastMin / stepMin), b = (int)std::ceil(futureMin / stepMin);
    for (int i = -a; i <= b; i++) {
        double la, lo, al;
        if (!subPoint(s, unixSec + i * stepMin * 60.0, la, lo, al)) continue;
        if (i <= 0) out.nowIndex = out.track.size();
        out.track.push_back({la, lo});
    }
    return true;
}

std::vector<std::pair<double, double>> circlePoints(double lat, double lon, double radiusKm, int n) {
    std::vector<std::pair<double, double>> v;
    const double d = radiusKm / kEarthKm, la = lat * kPi / 180, lo = lon * kPi / 180;
    for (int i = 0; i < n; i++) {
        const double b = 2 * kPi * i / n;
        const double la2 = std::asin(std::sin(la) * std::cos(d) + std::cos(la) * std::sin(d) * std::cos(b));
        const double lo2 = lo + std::atan2(std::sin(b) * std::sin(d) * std::cos(la), std::cos(d) - std::sin(la) * std::sin(la2));
        v.push_back({la2 * 180 / kPi, std::fmod(std::fmod(lo2 * 180 / kPi + 180, 360) + 360, 360) - 180});
    }
    return v;
}

}
