// View > Map: a world map (the ADS-B map's tiles) to find a position by coordinates (decimal or D°M'S") or by Maidenhead locator,
// and to read a clicked position's coordinates, locator, and distance and bearing from the receiver location.
#include "app.h"
#include "adsb_map.h"
#include "dect2/maidenhead.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct State {
    bool loaded = false;
    adsbmap::View map;
    bool marker = false;
    double mLat = 0, mLon = 0;
    char coordBuf[96] = "", locBuf[16] = "";
    std::string msg;              // why the typed text was not understood
};
State S;

void load() {
    if (S.loaded) return;
    S.loaded = true;
    receiverLocation(S.map.lat, S.map.lon);
    S.map.zoom = (int)plat::prefs().getI("mapZoom", 7);
}

void place(double lat, double lon, bool centre, int zoom = 0) {
    S.marker = true; S.mLat = lat; S.mLon = lon; S.msg.clear();
    if (centre) { S.map.lat = lat; S.map.lon = lon; if (zoom) S.map.zoom = std::max(S.map.zoom, zoom); }
}

void inputs() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Coordinates");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(230 * gUi);
    if (ImGui::InputTextWithHint("##mapcoord", "54.687, 25.280 or 54 41 13 N 25 16 48 E", S.coordBuf, sizeof S.coordBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
        double la, lo;
        if (dect2::parseCoordinates(S.coordBuf, la, lo)) place(la, lo, true);
        else S.msg = "not a position: type latitude then longitude, decimal or degrees minutes seconds";
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Latitude and longitude, then Enter: 54.687, 25.280 / -33.9 151.2 / 54.687N 25.28E / 54\xC2\xB0" "41'13\"N 25\xC2\xB0" "16'48\"E");
    ImGui::SameLine(0, 12 * gUi);
    ImGui::TextDisabled("Locator");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90 * gUi);
    if (ImGui::InputTextWithHint("##maploc", "KO24ab", S.locBuf, sizeof S.locBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
        double la, lo;
        const size_t n = std::strlen(S.locBuf);
        if (dect2::fromMaidenhead(S.locBuf, la, lo)) place(la, lo, true, n <= 2 ? 0 : n <= 4 ? 7 : n <= 6 ? 11 : 14);
        else S.msg = "not a locator: 2, 4, 6 or 8 characters, e.g. KO24, KO24ab, KO24ab12";
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A Maidenhead grid locator (4, 6 or 8 characters), then Enter: the marker goes to the centre of the square");
    if (!S.msg.empty()) { ImGui::SameLine(); ImGui::TextColored(pal::warnAmber(), "%s", S.msg.c_str()); }
}

void markerInfo(App& a) {
    if (!S.marker) { ImGui::TextDisabled("Click the map, or type coordinates or a locator, to place the marker."); return; }
    char b[160];
    const std::string loc6 = dect2::toMaidenhead(S.mLat, S.mLon, 6), loc8 = dect2::toMaidenhead(S.mLat, S.mLon, 8);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.6f, %.6f", S.mLat, S.mLon);
    ImGui::SameLine(); ImGui::TextDisabled("%s %s", dect2::toDms(S.mLat, true).c_str(), dect2::toDms(S.mLon, false).c_str());
    ImGui::SameLine(0, 12 * gUi);
    ImGui::Text("%s", loc6.c_str());
    ImGui::SameLine(); ImGui::TextDisabled("(%s)", loc8.c_str());
    double rl, ro; receiverLocation(rl, ro);
    double km, brg; dect2::greatCircle(rl, ro, S.mLat, S.mLon, km, brg);
    ImGui::SameLine(0, 12 * gUi);
    if (km < 10) ImGui::Text("%.2f km", km); else ImGui::Text("%.0f km", km);
    ImGui::SameLine(); ImGui::Text("%.0f\xC2\xB0", brg);
    ImGui::SameLine(); ImGui::TextDisabled(receiverLocationSet() ? "from my location" : "from the placeholder location");
    if (ImGui::SmallButton("Copy coordinates")) { snprintf(b, sizeof b, "%.6f, %.6f", S.mLat, S.mLon); ImGui::SetClipboardText(b); }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy locator")) ImGui::SetClipboardText(loc6.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copies %s (the 6-character locator)", loc6.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Use as my location")) setReceiverLocation(a, S.mLat, S.mLon);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make this the receiver location (ADS-B ranges, the pass planner, ...)");
}

void mapView() {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, std::max(120 * gUi, ImGui::GetContentRegionAvail().y));
    adsbmap::draw(S.map, size);
    ImVec2 click;
    if (adsbmap::clickedAt(click)) { double la, lo; adsbmap::unproject(click, la, lo); lo = std::fmod(lo + 540, 360) - 180; place(la, lo, false); }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), true);
    double rl, ro; receiverLocation(rl, ro);
    const ImVec2 h = adsbmap::project(rl, ro);   // the receiver: a ringed dot
    dl->AddCircleFilled(h, 4 * gUi, IM_COL32(110, 210, 230, 255));
    dl->AddCircle(h, 8 * gUi, IM_COL32(110, 210, 230, 220), 24, 1.5f * gUi);
    dl->AddText(ImVec2(h.x + 10 * gUi, h.y - 7 * gUi), IM_COL32(110, 210, 230, 255), "me");
    if (S.marker) {   // the marker: a pin with its point on the position
        const ImVec2 q = adsbmap::project(S.mLat, S.mLon);
        const ImU32 c = IM_COL32(240, 90, 80, 255);
        dl->AddLine(h, q, IM_COL32(255, 255, 255, 110), 1 * gUi);
        dl->AddTriangleFilled(ImVec2(q.x - 4 * gUi, q.y - 10 * gUi), ImVec2(q.x + 4 * gUi, q.y - 10 * gUi), q, c);
        dl->AddCircleFilled(ImVec2(q.x, q.y - 13 * gUi), 6 * gUi, c);
        dl->AddCircleFilled(ImVec2(q.x, q.y - 13 * gUi), 2.2f * gUi, IM_COL32(255, 255, 255, 230));
    }
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 8 * gUi, p0.y + 8 * gUi));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.07f, 0.08f, 0.85f));
    if (ImGui::Button("+", ImVec2(26 * gUi, 0))) S.map.zoom = std::min(adsbmap::kMaxZoom, S.map.zoom + 1);
    ImGui::SameLine(0, 2 * gUi);
    if (ImGui::Button("-", ImVec2(26 * gUi, 0))) S.map.zoom = std::max(adsbmap::kMinZoom, S.map.zoom - 1);
    ImGui::SameLine(0, 6 * gUi);
    if (ImGui::Button("me")) { S.map.lat = rl; S.map.lon = ro; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Centre the map on my location");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 6 * gUi);
    adsbmap::tileControls("never the marker or your location");
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 8 * gUi, p0.y + size.y - ImGui::GetTextLineHeight() - 6 * gUi));
    adsbmap::legend(size.x, "click to place the marker, drag to pan, wheel to zoom");
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + size.y));
    ImGui::Dummy(ImVec2(0, 0));   // ImGui wants an item after moving the cursor
}

} // namespace

void mapWindow(App& a) {
    if (!a.mapOpen) return;
    load();
    ImGui::SetNextWindowSize(ImVec2(820 * gUi, 600 * gUi), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Map", &a.mapOpen)) { ImGui::End(); return; }
    inputs();
    markerInfo(a);
    mapView();
    static int savedZoom = -1;
    if (savedZoom != S.map.zoom) { plat::prefs().setI("mapZoom", S.map.zoom); savedZoom = S.map.zoom; }
    ImGui::End();
}
