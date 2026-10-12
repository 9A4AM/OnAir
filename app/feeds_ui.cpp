// View > Data feeds: send what the decoders find to other programs (tar1090 / readsb / VRS, OpenCPN, APRS-IS, acarshub).
// Every feed is off until the user turns it on; the network work is in core (feeds.cpp).
#include "app.h"
#include <cstdio>

namespace {
const char* kKeys[dect2::kNumFeeds] = {"feedSbs", "feedBeast", "feedAis", "feedAprsIs", "feedKiss", "feedAcars", "feedVdl2"};

void apply(App& a) {
    dect2::Feeds::get().configure(a.feeds);
    savePrefs(a);
}

// a port field; true when the edit is finished
bool portField(const char* label, int& port) {
    ImGui::SetNextItemWidth(90 * gUi);
    int p = port;
    ImGui::InputInt(label, &p, 0, 0);
    port = std::max(1, std::min(65535, p));
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool textField(const char* label, std::string& s, ImGuiInputTextFlags fl = 0, float w = 200) {
    char b[128];
    snprintf(b, sizeof b, "%s", s.c_str());
    ImGui::SetNextItemWidth(w * gUi);
    if (ImGui::InputText(label, b, sizeof b, fl)) s = b;
    return ImGui::IsItemDeactivatedAfterEdit();
}

void statusLine(int id, bool server) {
    const dect2::FeedStatus s = dect2::Feeds::get().status(id);
    ImGui::Indent();
    if (s.state == "off") ImGui::TextDisabled("off");
    else {
        const ImVec4 col = !s.lastError.empty() ? pal::warnAmber() : s.active ? pal::okGreen() : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        if (server) ImGui::TextColored(col, "%s, %d client%s, %llu sent, %llu dropped", s.state.c_str(), s.clients, s.clients == 1 ? "" : "s", (unsigned long long)s.sent, (unsigned long long)s.dropped);
        else ImGui::TextColored(col, "%s, %llu sent, %llu dropped", s.state.c_str(), (unsigned long long)s.sent, (unsigned long long)s.dropped);
        if (!s.lastError.empty()) { ImGui::PushTextWrapPos(460 * gUi); ImGui::TextColored(pal::badRed(), "%s", s.lastError.c_str()); ImGui::PopTextWrapPos(); }
    }
    ImGui::Unindent();
}
} // namespace

void loadFeedPrefs(App& a) {
    plat::Prefs& d = plat::prefs();
    dect2::FeedConfig& c = a.feeds;
    for (int i = 0; i < dect2::kNumFeeds; i++) c.on[i] = d.getB(kKeys[i], false);
    c.sbsPort = (int)d.getI("feedSbsPort", c.sbsPort);
    c.beastPort = (int)d.getI("feedBeastPort", c.beastPort);
    c.kissPort = (int)d.getI("feedKissPort", c.kissPort);
    c.aisHost = d.getS("feedAisHost", c.aisHost); c.aisPort = (int)d.getI("feedAisPort", c.aisPort);
    c.acarsHost = d.getS("feedAcarsHost", c.acarsHost); c.acarsPort = (int)d.getI("feedAcarsPort", c.acarsPort);
    c.vdl2Host = d.getS("feedVdl2Host", c.vdl2Host); c.vdl2Port = (int)d.getI("feedVdl2Port", c.vdl2Port);
    c.aprsCall = d.getS("feedAprsCall", ""); c.aprsPass = d.getS("feedAprsPass", "");
    c.aprsServer = d.getS("feedAprsServer", c.aprsServer); c.aprsServerPort = (int)d.getI("feedAprsServerPort", c.aprsServerPort);
    c.stationId = d.getS("feedStationId", c.stationId);
    bool any = false;
    for (bool o : c.on) any = any || o;
    if (any) dect2::Feeds::get().configure(c);
}

void saveFeedPrefs(const App& a) {
    plat::Prefs& d = plat::prefs();
    const dect2::FeedConfig& c = a.feeds;
    for (int i = 0; i < dect2::kNumFeeds; i++) d.setB(kKeys[i], c.on[i]);
    d.setI("feedSbsPort", c.sbsPort); d.setI("feedBeastPort", c.beastPort); d.setI("feedKissPort", c.kissPort);
    d.setS("feedAisHost", c.aisHost); d.setI("feedAisPort", c.aisPort);
    d.setS("feedAcarsHost", c.acarsHost); d.setI("feedAcarsPort", c.acarsPort);
    d.setS("feedVdl2Host", c.vdl2Host); d.setI("feedVdl2Port", c.vdl2Port);
    d.setS("feedAprsCall", c.aprsCall); d.setS("feedAprsPass", c.aprsPass);
    d.setS("feedAprsServer", c.aprsServer); d.setI("feedAprsServerPort", c.aprsServerPort);
    d.setS("feedStationId", c.stationId);
}

void feedsWindow(App& a) {
    if (!a.feedsOpen) return;
    ImGui::SetNextWindowSize(ImVec2(480 * gUi, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("Data feeds", &a.feedsOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    dect2::FeedConfig& c = a.feeds;
    bool changed = false;
    ImGui::PushTextWrapPos(460 * gUi);
    ImGui::TextDisabled("Sends decoded data to other programs while the mode runs. Every feed is off until you turn it on.");
    ImGui::PopTextWrapPos();

    ImGui::SeparatorText("ADS-B");
    ImGui::PushID("sbs");
    changed |= ImGui::Checkbox("SBS-1 BaseStation (TCP server)", &c.on[dect2::kFeedSbs]);
    ImGui::SameLine(); changed |= portField("Port", c.sbsPort);
    statusLine(dect2::kFeedSbs, true);
    ImGui::PopID();
    ImGui::PushID("beast");
    changed |= ImGui::Checkbox("Beast binary (TCP server)", &c.on[dect2::kFeedBeast]);
    ImGui::SameLine(); changed |= portField("Port", c.beastPort);
    statusLine(dect2::kFeedBeast, true);
    ImGui::PopID();

    ImGui::SeparatorText("AIS");
    ImGui::PushID("ais");
    changed |= ImGui::Checkbox("NMEA !AIVDM (UDP)", &c.on[dect2::kFeedAis]);
    ImGui::SameLine(); changed |= textField("##host", c.aisHost, 0, 120);
    ImGui::SameLine(); changed |= portField("Port", c.aisPort);
    statusLine(dect2::kFeedAis, false);
    ImGui::PopID();

    ImGui::SeparatorText("APRS");
    ImGui::PushID("aprsis");
    changed |= ImGui::Checkbox("APRS-IS (receive-only igate)", &c.on[dect2::kFeedAprsIs]);
    ImGui::Indent();
    changed |= textField("Callsign", c.aprsCall, ImGuiInputTextFlags_CharsUppercase, 120);
    changed |= textField("Passcode", c.aprsPass, ImGuiInputTextFlags_Password | ImGuiInputTextFlags_CharsDecimal, 120);
    changed |= textField("##server", c.aprsServer, 0, 160);
    ImGui::SameLine(); changed |= portField("Server", c.aprsServerPort);
    ImGui::Unindent();
    statusLine(dect2::kFeedAprsIs, false);
    ImGui::PopID();
    ImGui::PushID("kiss");
    changed |= ImGui::Checkbox("KISS (TCP server)", &c.on[dect2::kFeedKiss]);
    ImGui::SameLine(); changed |= portField("Port", c.kissPort);
    statusLine(dect2::kFeedKiss, true);
    ImGui::PopID();

    ImGui::SeparatorText("ACARS and VDL2 (JSON)");
    ImGui::PushID("acars");
    changed |= ImGui::Checkbox("ACARS, acarsdec format (UDP)", &c.on[dect2::kFeedAcars]);
    ImGui::SameLine(); changed |= textField("##host", c.acarsHost, 0, 120);
    ImGui::SameLine(); changed |= portField("Port", c.acarsPort);
    statusLine(dect2::kFeedAcars, false);
    ImGui::PopID();
    ImGui::PushID("vdl2");
    changed |= ImGui::Checkbox("VDL2, dumpvdl2 format (UDP)", &c.on[dect2::kFeedVdl2]);
    ImGui::SameLine(); changed |= textField("##host", c.vdl2Host, 0, 120);
    ImGui::SameLine(); changed |= portField("Port", c.vdl2Port);
    statusLine(dect2::kFeedVdl2, false);
    ImGui::PopID();
    changed |= textField("Station ID", c.stationId, 0, 120);

    if (changed) apply(a);
    ImGui::End();
}
