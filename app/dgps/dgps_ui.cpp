// DGPS beacon screens (work in progress): the station ID, carrier offset, level, frame count and the message types seen.
#include "app.h"
#include "dect2/dgps/dgps_engine.h"
#include <cstdio>
#include <string>

namespace {

bool live(const App& a) { return a.rx.standard == 121; }   // family 120, engine standard 122, active standard 121

std::string typesText(const dect2::DgpsTelemetry& t) {
    if (t.typesSeen.empty()) return "-";
    std::string s;
    for (int ty : t.typesSeen) {
        if (!s.empty()) s += ", ";
        s += std::to_string(ty);
    }
    return s;
}

void tab(App& a) {
    if (!live(a)) {
        ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "tune to about 300 kHz (283.5 - 325 kHz) and start the receiver");
        return;
    }
    const dect2::DgpsTelemetry& t = a.rx.dgps;
    ImGui::TextUnformatted("DGPS beacon (WIP): MSK at 200 bit/s, RTCM SC-104 messages");
    ImGui::Spacing();
    if (t.dataValid) ImGui::Text("Station ID: %d", t.stationId);
    else ImGui::TextDisabled("Station ID: -");
    ImGui::Text("Carrier offset: %+.1f Hz", t.cfoHz);
    ImGui::Text("Level: %.0f dBFS", t.levelDb);
    ImGui::Text("Frames: %llu", (unsigned long long)t.blocksOk);
    ImGui::Text("Message types seen: %s", typesText(t).c_str());
    if (t.dataValid) ImGui::Text("Last frame: type %d, Z-count %d (0.6 s units), health %d", t.lastType, t.zcount, t.health);
    ImGui::Spacing();
    ImGui::TextDisabled("Input %.0f Hz, centre %.4f MHz", t.inputRate, t.centerHz / 1e6);
    if (!t.dataValid) ImGui::TextDisabled("listening: no frame with a good header yet");
}

void receiver(App& a) {
    if (!live(a)) return;
    const dect2::DgpsTelemetry& t = a.rx.dgps;
    ImGui::Text("%s", dect2::dgpsSummary(t).c_str());
    ImGui::Text("frames %llu, level %.1f dBFS, offset %+.1f Hz", (unsigned long long)t.blocksOk, t.levelDb, t.cfoHz);
}

void list(App&) {}
void tick(App&) {}

void status(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State");
    ImGui::SameLine();
    ImGui::TextUnformatted(live(a) ? dect2::dgpsSummary(a.rx.dgps).c_str() : (a.engine.running() ? "starting" : "stopped"));
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "DGPS beacon (WIP)";
    if (live(a)) l2 = dect2::dgpsSummary(a.rx.dgps);
}

} // namespace

extern const ModeUi kDgpsUi;
const ModeUi kDgpsUi = {
    .sideTitle = "MESSAGES",
    .tabName = "DGPS",
    .tabIcon = Ic::Radio,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .tick = tick,
};
