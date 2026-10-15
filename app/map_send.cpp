// Server > Send data to map server: opted-in POST of decoded positions to map.on-air.dev /ingest.
// Only positions the signal itself sent (no TLE catalogue, receiver GNSS, RID operator, FLARM stealth, cell towers, synthetic test signal).
#include "app.h"
#include "receivers.h"
#ifndef ONAIR_VERSION
#define ONAIR_VERSION "dev"
#endif
#include "dect2/flarm_proto.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr const char* kDefaultOrigin = "https://map.on-air.dev";
constexpr double kMinInterval = 4.0;
constexpr double kHeartbeatSec = 60.0;
constexpr double kRetrySec = 8.0;
constexpr int kFailPopupAfter = 3;
constexpr size_t kMaxTracks = 250;
constexpr double kIngestMaxAdsbPosAgeSec = 30.0;   // same as adsb_map acStaleSec: do not publish a frozen CPR fix

std::atomic<bool> gBusy{false};
std::mutex gThMu;
std::thread gTh;
bool gAskConsent = false;
bool gAskConnecting = false;
bool gPingNow = false;
enum { kOff = 0, kConnecting = 1, kOk = 2, kRetry = 3 };
std::atomic<int> gPhase{kOff};
std::atomic<int> gLastN{0};
std::atomic<uint64_t> gTotalN{0};
std::atomic<bool> gNeedRetryPopup{false};
std::atomic<int> gFails{0};
std::atomic<int64_t> gLastOkMs{0};
std::atomic<int64_t> gLastTryMs{0};
bool gToastRetry = false;
bool gTxHold = false;                // a transmission is in progress, or just ended: do not post, and do not let the user turn sending back on
bool gTxPausedSend = false;          // the connection was on when transmit started (restored only if the radio never keyed)
int64_t gToastTxUntil = 0;

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

App& rootApp(App& a) {
    if (App* r = receiverById(1)) return *r;
    return a;
}

bool validPos(double lat, double lon) {
    if (!std::isfinite(lat) || !std::isfinite(lon)) return false;
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return false;
    if (lat == 0 && lon == 0) return false;
    return true;
}

std::string jsonEsc(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

std::string makeStationId() {
    static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::string s(20, '0');
    for (char& c : s) c = abc[rd() % 62];
    return s;
}

bool stationOk(const std::string& s) {
    if (s.size() < 8 || s.size() > 64) return false;
    for (unsigned char c : s) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

void ensureStation(App& a) {
    if (stationOk(a.mapStationId)) return;
    a.mapStationId = makeStationId();
    savePrefs(a);
}

std::string originOf(const App& a) {
    std::string o = a.mapServer;
    if (o.empty()) o = plat::basePrefs().getS("mapServer", "");
    if (o.empty()) o = kDefaultOrigin;
    while (!o.empty() && o.back() == '/') o.pop_back();
    return o;
}

struct Track {
    const char* kind = "";
    std::string id, label, sub, extra, src, cat;
    double lat = 0, lon = 0;
    bool hasHeard = false;
    double heardAt = 0;              // signal time of the last position (0 when not known)
    bool hasAlt = false, hasHdg = false, hasSpd = false, hasVrate = false, hasSquawk = false;
    double alt = 0, hdg = 0, spd = 0, vrate = 0, age = 0;
    int squawk = 0;
    std::vector<std::pair<double, double>> path;
};

struct RunGate {
    uint64_t epoch = 0;
    bool primed = false;
    std::unordered_map<std::string, double> heard;   // last hear at the start of this run; missing = not present then
};
std::unordered_map<int, RunGate> gRun;
RunGate* gG = nullptr;

void addTrack(std::vector<Track>& v, std::unordered_set<std::string>& seen, Track t) {
    if (!t.kind || !*t.kind || t.id.empty() || !validPos(t.lat, t.lon)) return;
    if (t.kind && std::strcmp(t.kind, "aircraft") == 0 && t.src == "adsb" && t.age > kIngestMaxAdsbPosAgeSec) return;
    if (t.id.size() > 80) t.id.resize(80);
    const std::string key = std::string(t.kind) + ":" + t.id;
    const double ht = t.hasHeard ? t.heardAt : -1;
    if (gG && !gG->primed) { gG->heard[key] = ht; return; }   // first look this run: remember leftovers, send nothing yet
    if (gG && gG->primed) {
        auto it = gG->heard.find(key);
        if (it != gG->heard.end()) {
            if (ht < 0 || ht <= it->second + 0.2) return;   // not heard again since the receiver started
            t.path.clear();                                 // saved trail is not this run
        }
    }
    if (!seen.insert(key).second) return;
    if (v.size() >= kMaxTracks) return;
    v.push_back(std::move(t));
}

void fromMarkers(std::vector<Track>& v, std::unordered_set<std::string>& seen, const std::vector<dect2::MapMarker>& ms, double now) {
    for (const dect2::MapMarker& m : ms) {
        Track t;
        t.id = m.id;
        t.label = m.label.empty() ? m.id : m.label;
        if (m.layer == dect2::LayerAdsb) { t.kind = "aircraft"; t.sub = "ADS-B"; t.src = "adsb"; if (m.hasAlt) { t.hasAlt = true; t.alt = m.altM / 0.3048; } }
        else if (m.layer == dect2::LayerAis) { t.kind = "vessel"; t.sub = "AIS"; if (m.hasAlt) { t.hasAlt = true; t.alt = m.altM; } }
        else if (m.layer == dect2::LayerAprs) { t.kind = "aprs"; t.sub = "APRS"; if (m.hasAlt) { t.hasAlt = true; t.alt = m.altM; } }
        else if (m.layer == dect2::LayerSonde) { t.kind = "sonde"; t.sub = "sonde"; if (m.hasAlt) { t.hasAlt = true; t.alt = m.altM; } }
        else continue;
        t.lat = m.lat; t.lon = m.lon;
        t.age = m.ageSec;
        t.hasHeard = true;
        t.heardAt = now - m.ageSec;
        if (m.hasSpeed) { t.hasSpd = true; t.spd = m.speedKt; }
        if (m.headingDeg >= 0) { t.hasHdg = true; t.hdg = m.headingDeg; }
        if (!m.detail.empty()) t.extra = m.detail.substr(0, 120);
        for (const auto& p : m.track) if (validPos(p.first, p.second)) t.path.push_back(p);
        addTrack(v, seen, std::move(t));
    }
}

void collectFrom(App& rx, std::vector<Track>& v, std::unordered_set<std::string>& seen) {
    if (rx.engine.syntheticSource()) return;   // the built-in test signal is not traffic
    if (rx.devIdx >= 0 && rx.devIdx < (int)rx.devices.size() && rx.devices[(size_t)rx.devIdx].kind == DeviceInfo::Synthetic) return;
    if (!rx.engine.running()) return;          // saved lists are not live traffic
    RunGate& g = gRun[rx.rxId];
    const uint64_t ep = rx.engine.startEpoch();
    if (g.epoch != ep) { g.heard.clear(); g.primed = false; g.epoch = ep; }
    gG = &g;
    // Same lists the UI shows (a.rx), not a second snapshot from the decoder.
    dect2::AdsbTelemetry adsb;
    if (rx.engine.adsb().telemetry(adsb, 0)) {
        fromMarkers(v, seen, dect2::markersFromAdsb(adsb), adsb.timeSec);
        for (Track& t : v) {
            if (t.kind && std::strcmp(t.kind, "aircraft") == 0 && t.src == "adsb") {
                for (const auto& a : adsb.aircraft) {
                    char id[16]; snprintf(id, sizeof id, "%06X", a.icao & 0xFFFFFF);
                    if (t.id != id) continue;
                    t.cat = a.category;
                    if (a.hasSquawk) { t.hasSquawk = true; t.squawk = a.squawk; }
                    if (a.hasVrate) { t.hasVrate = true; t.vrate = a.vrateFpm; }
                    t.age = a.posAgeSec > 0 ? a.posAgeSec : a.ageSec;
                    break;
                }
            }
        }
    } else fromMarkers(v, seen, dect2::markersFromAdsb(rx.rx.adsb), rx.rx.adsb.timeSec);
    fromMarkers(v, seen, dect2::markersFromAis(rx.rx.ais), rx.rx.ais.timeSec);
    fromMarkers(v, seen, dect2::markersFromAprs(rx.rx.packet), rx.rx.packet.timeSec);
    fromMarkers(v, seen, dect2::markersFromSonde(rx.rx.sonde), rx.rx.sonde.streamTimeS);

    dect2::AcarsTelemetry acars;
    if (rx.engine.acars().telemetry(acars, 0)) {
        for (const auto& a : acars.aircraft) {
            if (!a.hasPos) continue;
            Track t; t.kind = "aircraft"; t.sub = "ACARS"; t.src = "acars";
            t.id = a.reg.empty() ? a.flight : a.reg; t.label = a.flight.empty() ? t.id : a.flight;
            t.lat = a.lat; t.lon = a.lon; t.age = acars.nowSec - a.lastHeardSec;
            t.hasHeard = true; t.heardAt = a.lastHeardSec;
            if (a.hasAlt) { t.hasAlt = true; t.alt = a.altFt; }
            if (a.hasTrack) { t.hasHdg = true; t.hdg = a.trackDeg; }
            if (a.hasSpeed) { t.hasSpd = true; t.spd = a.speedKt; }
            t.cat = a.posKind;
            for (const auto& p : a.track) t.path.push_back({p.lat, p.lon});
            addTrack(v, seen, std::move(t));
        }
    }
    dect2::AeroTelemetry aero;
    if (rx.engine.aero().telemetry(aero, 0)) {
        for (const auto& a : aero.aircraft) {
            if (!a.hasPos) continue;
            Track t; t.kind = "aircraft"; t.sub = "Inmarsat Aero"; t.src = "sat";
            t.id = a.aesId ? [&] { char b[16]; snprintf(b, sizeof b, "%06X", a.aesId); return std::string(b); }() : a.registration;
            t.label = a.flight.empty() ? (a.registration.empty() ? t.id : a.registration) : a.flight;
            t.lat = a.lat; t.lon = a.lon; t.age = aero.timeSec - a.posTime;
            t.hasHeard = true; t.heardAt = a.posTime;
            if (a.hasAlt) { t.hasAlt = true; t.alt = a.altFt; }
            if (a.hasTrack) { t.hasHdg = true; t.hdg = a.trackDeg; }
            if (a.hasSpeed) { t.hasSpd = true; t.spd = a.speedKt; }
            t.cat = a.posKind;
            for (const auto& p : a.track) t.path.push_back({p.lat, p.lon});
            addTrack(v, seen, std::move(t));
        }
    }
    dect2::HfdlTelemetry hfdl;
    if (rx.engine.hfdl().telemetry(hfdl, 0)) {
        for (const auto& a : hfdl.aircraft) {
            if (!a.hasPos) continue;
            Track t; t.kind = "aircraft"; t.sub = "HFDL"; t.src = "hfdl";
            char id[16]; snprintf(id, sizeof id, "%06X", a.icao ? a.icao : a.key);
            t.id = a.flight.empty() ? (a.reg.empty() ? id : a.reg) : a.flight;
            t.label = t.id; t.lat = a.lat; t.lon = a.lon; t.age = hfdl.nowSec - a.lastHeardSec;
            t.hasHeard = true; t.heardAt = a.lastHeardSec;
            if (a.hasAlt) { t.hasAlt = true; t.alt = a.altFt; }
            t.cat = a.posKind;
            for (const auto& p : a.track) t.path.push_back({p.lat, p.lon});
            addTrack(v, seen, std::move(t));
        }
    }
    dect2::Vdl2Telemetry vdl;
    if (rx.engine.vdl2().telemetry(vdl, 0)) {
        for (const auto& a : vdl.aircraft) {
            if (!a.hasPos) continue;
            Track t; t.kind = "aircraft"; t.sub = "VDL2"; t.src = "acars";
            char id[16]; snprintf(id, sizeof id, "%06X", a.icao);
            t.id = a.flight.empty() ? (a.reg.empty() ? id : a.reg) : a.flight;
            t.label = t.id; t.lat = a.lat; t.lon = a.lon; t.age = vdl.nowSec - a.lastHeardSec;
            t.hasHeard = true; t.heardAt = a.lastHeardSec;
            if (a.hasAlt) { t.hasAlt = true; t.alt = a.altFt; }
            t.cat = a.posKind;
            for (const auto& p : a.track) t.path.push_back({p.lat, p.lon});
            addTrack(v, seen, std::move(t));
        }
    }
    dect2::FlarmTelemetry flarm;
    if (rx.engine.flarm().telemetry(flarm, 0)) {
        for (const auto& a : flarm.aircraft) {
            if (!a.hasPos) continue;
            Track t; t.kind = "flarm"; t.sub = "FLARM"; t.src = "flarm";
            char id[16]; snprintf(id, sizeof id, "%06X", a.id & 0xFFFFFF);
            t.id = id; t.label = dect2::flarm::aircraftTypeName(a.acType);
            t.lat = a.lat; t.lon = a.lon; t.age = a.ageSec;
            t.hasHeard = true; t.heardAt = flarm.timeSec - a.ageSec;
            t.hasAlt = true; t.alt = a.altM / 0.3048;
            t.hasHdg = true; t.hdg = a.trackDeg; t.hasSpd = true; t.spd = a.speedKmh / 1.852;
            t.cat = dect2::flarm::aircraftTypeName(a.acType);
            for (const auto& p : a.track) t.path.push_back({p.lat, p.lon});
            addTrack(v, seen, std::move(t));
        }
    }

    for (const dect2::OrbcommSat& s : rx.rx.orbcomm.sats) {
        if (!s.hasPos) continue;
        Track t; t.kind = "sat"; t.sub = "Orbcomm";
        t.id = std::to_string(s.id); t.label = t.id;
        t.lat = s.lat; t.lon = s.lon; t.hasAlt = true; t.alt = s.altKm;
        t.hasHeard = true; t.heardAt = s.posTime > 0 ? s.posTime : s.lastHeard;
        addTrack(v, seen, std::move(t));
    }

    for (const dect2::IridiumSatInfo& s : rx.rx.iridium.sats) {
        if (!s.hasPos || s.altKm < 100) continue;   // IRA sub-point of the satellite, not a beam centre on the ground
        Track t; t.kind = "sat"; t.sub = "Iridium";
        t.id = std::to_string(s.id); t.label = t.id;
        t.lat = s.lat; t.lon = s.lon; t.hasAlt = true; t.alt = s.altKm;
        t.hasHeard = true; t.heardAt = s.lastHeard;
        addTrack(v, seen, std::move(t));
    }

    for (const dect2::RidDrone& d : rx.rx.rid.drones) {
        if (!d.hasPos) continue;   // not the operator location
        Track t; t.kind = "drone"; t.sub = "Remote ID";
        t.id = !d.serial.empty() ? d.serial : d.mac;
        t.label = t.id;
        t.lat = d.lat; t.lon = d.lon;
        t.hasHeard = true; t.heardAt = d.lastSec;
        if (d.altGeo > -1000) { t.hasAlt = true; t.alt = d.altGeo; }
        if (d.direction >= 0) { t.hasHdg = true; t.hdg = d.direction; }
        if (d.speed >= 0) { t.hasSpd = true; t.spd = d.speed * 1.943844; }
        addTrack(v, seen, std::move(t));
    }

    for (const dect2::DjiDrone& d : rx.rx.dji.drones) {
        const double la = d.last.lat(), lo = d.last.lon();
        if (!validPos(la, lo)) continue;   // drone position only, not home or the pilot
        Track t; t.kind = "drone"; t.sub = "DJI";
        t.id = d.serial.empty() ? "dji" : d.serial; t.label = t.id;
        t.lat = la; t.lon = lo; t.hasAlt = true; t.alt = d.last.altitudeM();
        t.hasHeard = true; t.heardAt = d.lastSec;
        addTrack(v, seen, std::move(t));
    }

    for (const dect2::DscCall& c : rx.rx.marine.dsc) {
        if (!c.hasPos || !c.eccOk) continue;
        Track t; t.kind = "dsc"; t.sub = "DSC";
        t.id = !c.fromMmsi.empty() ? c.fromMmsi : c.distressMmsi;
        t.label = t.id; t.lat = c.lat; t.lon = c.lon;
        t.hasHeard = true; t.heardAt = c.rxSec;
        if (!c.natureName.empty()) t.extra = c.natureName;
        addTrack(v, seen, std::move(t));
    }

    const dect2::DatawellTelemetry& dw = rx.rx.datawell;
    if (dw.sys.havePos && validPos(dw.sys.lat, dw.sys.lon)) {
        Track t; t.kind = "buoy"; t.sub = "Datawell";
        t.id = !dw.channel.empty() ? dw.channel : "datawell";
        t.label = t.id; t.lat = dw.sys.lat; t.lon = dw.sys.lon;
        t.hasHeard = true; t.heardAt = dw.timeSec;
        addTrack(v, seen, std::move(t));
    }
    g.primed = true;
    gG = nullptr;
}

std::string buildJson(const std::string& station, const std::vector<Track>& tracks) {
    std::string j = "{\"station\":\"" + jsonEsc(station) + "\",\"tracks\":[";
    bool first = true;
    for (const Track& t : tracks) {
        if (!first) j += ',';
        first = false;
        j += "{\"kind\":\""; j += t.kind; j += "\",\"id\":\""; j += jsonEsc(t.id);
        j += "\",\"label\":\""; j += jsonEsc(t.label.empty() ? t.id : t.label);
        j += "\",\"sub\":\""; j += jsonEsc(t.sub);
        char b[96];
        snprintf(b, sizeof b, "\",\"lat\":%.6f,\"lon\":%.6f", t.lat, t.lon);
        j += b;
        if (t.hasAlt) { snprintf(b, sizeof b, ",\"alt\":%.2f", t.alt); j += b; }
        if (t.hasHdg) { snprintf(b, sizeof b, ",\"heading\":%.1f", t.hdg); j += b; }
        if (t.hasSpd) { snprintf(b, sizeof b, ",\"speed\":%.2f", t.spd); j += b; }
        if (t.hasVrate) { snprintf(b, sizeof b, ",\"vrate\":%.0f", t.vrate); j += b; }
        if (t.hasSquawk) { snprintf(b, sizeof b, ",\"squawk\":%04d", t.squawk); j += b; }
        snprintf(b, sizeof b, ",\"age\":%.1f", t.age); j += b;
        if (!t.src.empty()) { j += ",\"src\":\""; j += jsonEsc(t.src); j += '"'; }
        if (!t.cat.empty()) { j += ",\"cat\":\""; j += jsonEsc(t.cat); j += '"'; }
        if (!t.extra.empty()) { j += ",\"extra\":\""; j += jsonEsc(t.extra); j += '"'; }
        if (!t.path.empty()) {
            j += ",\"path\":[";
            size_t n = 0;
            const size_t start = t.path.size() > 60 ? t.path.size() - 60 : 0;
            for (size_t i = start; i < t.path.size(); i++) {
                if (n++) j += ',';
                snprintf(b, sizeof b, "[%.5f,%.5f]", t.path[i].first, t.path[i].second);
                j += b;
            }
            j += ']';
        }
        j += '}';
    }
    j += "]}";
    return j;
}

void startPost(const std::string& url, const std::string& body, int n) {
    if (gBusy.exchange(true)) return;
    std::lock_guard<std::mutex> lk(gThMu);
    if (gTh.joinable()) gTh.join();
    const std::string ua = std::string("OnAir/") + ONAIR_VERSION + " (map ingest)";
    gTh = std::thread([url, body, ua, n] {
        const bool ok = plat::postJson(url, body, ua);
        const int64_t t = nowMs();
        gLastTryMs.store(t, std::memory_order_relaxed);
        if (ok) {
            gPhase.store(kOk, std::memory_order_relaxed);
            gLastN.store(n, std::memory_order_relaxed);
            gTotalN.fetch_add((uint64_t)std::max(0, n), std::memory_order_relaxed);
            gLastOkMs.store(t, std::memory_order_relaxed);
            gFails.store(0, std::memory_order_relaxed);
        } else {
            const int nfail = gFails.fetch_add(1, std::memory_order_relaxed) + 1;
            const int64_t lastOk = gLastOkMs.load(std::memory_order_relaxed);
            const bool outage = nfail >= kFailPopupAfter || lastOk == 0 || (t - lastOk) > 20000;
            if (outage) {
                gPhase.store(kRetry, std::memory_order_relaxed);
                if (nfail == kFailPopupAfter || lastOk == 0) gNeedRetryPopup.store(true, std::memory_order_relaxed);
            }
        }
        fprintf(stderr, "map ingest %s n=%d (%zu bytes) %s\n", ok ? "ok" : "failed", n, body.size(), url.c_str());
        gBusy = false;
    });
}

void beginSend(App& s) {
    ensureStation(s);
    s.mapSend = true;
    savePrefs(s);
    gPhase = kConnecting;
    gAskConnecting = true;
    gToastRetry = false;
    gFails.store(0, std::memory_order_relaxed);
    gNeedRetryPopup.store(false, std::memory_order_relaxed);
    gPingNow = true;
}

void forgetHeard() {
    for (auto& kv : gRun) { kv.second.heard.clear(); kv.second.primed = false; }
}

void stopSend(App& s) {
    s.mapSend = false;
    savePrefs(s);
    gPhase = kOff;
    gAskConnecting = false;
    gToastRetry = false;
    gNeedRetryPopup.store(false, std::memory_order_relaxed);
    gPingNow = false;
}

void consentPopup(App& a) {
    App& s = rootApp(a);
    if (gAskConsent) { ImGui::OpenPopup("##mapconsent"); gAskConsent = false; }
    ImGui::SetNextWindowSizeConstraints(ImVec2(420 * gUi, 0), ImVec2(520 * gUi, 400 * gUi));
    if (!ImGui::BeginPopupModal("##mapconsent", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushTextWrapPos(480 * gUi);
    ImGui::TextUnformatted(tr("When this is on, positions decoded from the signals you receive are sent to map.on-air.dev so they can appear on the public map. Catalogue satellite orbits, your receiver location, drone operator locations, privacy-flagged FLARM aircraft, and the built-in test signal are not sent. You can turn this off at any time from Server."));
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 8 * gUi));
    if (ImGui::Button(tr("Send positions"), ImVec2(160 * gUi, 0))) {
        ensureStation(s);
        s.mapConsent = true;
        beginSend(s);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"), ImVec2(100 * gUi, 0))) {
        stopSend(s);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void mapToast(const char* text) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp) return;
    const ImVec2 sz = ImGui::CalcTextSize(text);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 72 * gUi), ImGuiCond_Always, ImVec2(0.5f, 0.f));
    ImGui::SetNextWindowSize(ImVec2(sz.x + 36 * gUi, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.10f, 0.94f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4 * gUi);
    if (ImGui::Begin("##maptoast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextUnformatted(text);
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void connectingPopup() {
    if (gAskConnecting) gAskConnecting = false;
    if (gPhase.load(std::memory_order_relaxed) == kConnecting) mapToast(tr("Connecting to server…"));
}

void retryPopup() {
    const int ph = gPhase.load(std::memory_order_relaxed);
    if (gNeedRetryPopup.exchange(false)) gToastRetry = true;
    if (ph == kOk || ph == kOff) gToastRetry = false;
    if (gToastRetry && ph == kRetry) mapToast(tr("Retrying connecting with server"));
}

void txOffToast() {
    if (gToastTxUntil == 0 || nowMs() >= gToastTxUntil) return;
    mapToast(tr("The connection to the map server has been turned off. It cannot be turned back on while you are transmitting."));
}

void mapPopups(App& a) {
    consentPopup(a);
    connectingPopup();
    retryPopup();
    txOffToast();
}

void sendToggle(App& a, bool menu) {
    App& s = rootApp(a);
    if (gTxHold || txActive()) {
        ImGui::BeginDisabled(true);
        if (menu) ImGui::MenuItem(tr("Send data to map server"), nullptr, false);
        else { bool v = false; ImGui::Checkbox(tr("Send data to map server"), &v); }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tr("Turned off while transmitting, so a signal from this radio is not sent to the map."));
        return;
    }
    const bool on = s.mapSend;
    bool hit = false;
    if (menu) hit = ImGui::MenuItem(tr("Send data to map server"), nullptr, on);
    else {
        bool v = on;
        hit = ImGui::Checkbox(tr("Send data to map server"), &v) && v != on;
    }
    if (!hit) return;
    if (!on) {
        if (!s.mapConsent) gAskConsent = true;
        else beginSend(s);
    } else stopSend(s);
}

} // namespace

void mapSendServerMenu(App& a) {
    if (!ImGui::BeginMenu(tr("Server"))) { mapPopups(a); return; }
    sendToggle(a, true);
    ImGui::TextDisabled("%s", tr("The map is at map.on-air.dev"));
    ImGui::EndMenu();
    mapPopups(a);
}

void mapSendButton(App& a) {
    if (ImGui::SmallButton(tr("Server"))) ImGui::OpenPopup("##serverpop");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Send decoded positions to the public map"));
    if (ImGui::BeginPopup("##serverpop")) {
        sendToggle(a, false);
        ImGui::TextDisabled("%s", tr("The map is at map.on-air.dev"));
        ImGui::EndPopup();
    }
    mapPopups(a);
}

bool mapFamilySends(int family) {
    switch (family) {
    case 11:  // ADS-B
    case 13:  // radiosonde
    case 14:  // AIS
    case 15:  // marine (DSC)
    case 16:  // ACARS
    case 18:  // Inmarsat Aero
    case 19:  // Iridium
    case 24:  // APRS
    case 27:  // VDL2
    case 35:  // HFDL
    case 44:  // Orbcomm
    case 56:  // Remote ID
    case 66:  // FLARM
    case 68:  // DJI
    case 74:  // Datawell
        return true;
    default: return false;
    }
}

std::string mapSendStatusLine() {
    const int ph = gPhase.load(std::memory_order_relaxed);
    if (ph == kOff) return {};
    char b[80];
    if (ph == kConnecting) return tr("connecting");
    if (ph == kRetry) return tr("retrying");
    const int n = gLastN.load(std::memory_order_relaxed);
    const uint64_t tot = gTotalN.load(std::memory_order_relaxed);
    if (n > 0) snprintf(b, sizeof b, "%s  %d sent", tr("connected"), n);
    else if (tot > 0) snprintf(b, sizeof b, "%s  %llu sent", tr("connected"), (unsigned long long)tot);
    else snprintf(b, sizeof b, "%s", tr("connected"));
    return b;
}

void mapSendTick(App& a) {
    App& s = rootApp(a);
    mapPopups(a);
    if (txActive() || gTxHold) {
        if (s.mapSend) mapPauseForTransmit(s);
        if (txActive()) return;
        gTxHold = false;
        gTxPausedSend = false;
        gToastTxUntil = 0;
        forgetHeard();
        return;
    }
    if (!s.mapSend || !s.mapConsent) {
        if (gPhase.load(std::memory_order_relaxed) != kOff && !s.mapSend) gPhase = kOff;
        return;
    }
    if (gPhase.load(std::memory_order_relaxed) == kOff) gPhase = kConnecting;
    if (gBusy) return;
    ensureStation(s);
    const int64_t now = nowMs();
    const int ph = gPhase.load(std::memory_order_relaxed);
    const int64_t lastOk = gLastOkMs.load(std::memory_order_relaxed);
    const int64_t lastTry = gLastTryMs.load(std::memory_order_relaxed);
    const bool needHb = lastOk == 0 || (now - lastOk) >= (int64_t)(kHeartbeatSec * 1000);
    const bool retryWait = (ph == kConnecting || ph == kRetry) && lastTry != 0 && (now - lastTry) < (int64_t)(kRetrySec * 1000);
    if (retryWait && !gPingNow) return;

    std::vector<Track> tracks;
    std::unordered_set<std::string> seen;
    for (App* r : receiversAll()) if (r) collectFrom(*r, tracks, seen);
    const bool ping = gPingNow || needHb || (tracks.empty() && (ph == kConnecting || ph == kRetry));
    if (!gPingNow && !needHb && !tracks.empty()) {
        static int64_t lastSend = 0;
        if (lastSend != 0 && (now - lastSend) < (int64_t)(kMinInterval * 1000)) return;
        lastSend = now;
    } else if (!ping) return;
    gPingNow = false;
    startPost(originOf(s) + "/ingest", buildJson(s.mapStationId, tracks), (int)tracks.size());
}

bool mapPauseForTransmit(App& a) {
    App& s = rootApp(a);
    gTxHold = true;
    if (!s.mapSend || !s.mapConsent) return false;
    gTxPausedSend = true;
    stopSend(s);
    gToastTxUntil = nowMs() + 8000;
    return true;
}

void mapTransmitAbort(App& a) {
    const bool restore = gTxPausedSend;
    gTxHold = false;
    gTxPausedSend = false;
    gToastTxUntil = 0;
    if (restore && rootApp(a).mapConsent) beginSend(rootApp(a));
}

void mapTransmitEnded() {
    gTxHold = false;
    gTxPausedSend = false;
    gToastTxUntil = 0;
    forgetHeard();
}

void mapSendShutdown() {
    std::thread t;
    { std::lock_guard<std::mutex> lk(gThMu); if (gTh.joinable()) t = std::move(gTh); }
    if (t.joinable()) t.join();
    gBusy = false;
}
