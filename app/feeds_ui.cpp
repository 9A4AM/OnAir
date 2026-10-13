// View > Data feeds: send what the decoders find to other programs (tar1090 / readsb / VRS, OpenCPN, APRS-IS, acarshub).
// Every feed is off until the user turns it on; the network work is in core (feeds.cpp).
#include "app.h"
#include <cstdio>
#include <ctime>

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
    ImGui::InputInt(tr(label), &p, 0, 0);
    port = std::max(1, std::min(65535, p));
    return ImGui::IsItemDeactivatedAfterEdit();
}

bool textField(const char* label, std::string& s, ImGuiInputTextFlags fl = 0, float w = 200) {
    char b[128];
    snprintf(b, sizeof b, "%s", s.c_str());
    ImGui::SetNextItemWidth(w * gUi);
    if (ImGui::InputText(tr(label), b, sizeof b, fl)) s = b;
    return ImGui::IsItemDeactivatedAfterEdit();
}

void statusLine(int id, bool server) {
    const dect2::FeedStatus s = dect2::Feeds::get().status(id);
    ImGui::Indent();
    if (s.state == "off") ImGui::TextDisabled("%s", tr("off"));
    else {
        const ImVec4 col = !s.lastError.empty() ? pal::warnAmber() : s.active ? pal::okGreen() : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        if (server) ImGui::TextColored(col, tr("%s, %d client%s, %llu sent, %llu dropped"), s.state.c_str(), s.clients, s.clients == 1 ? "" : "s", (unsigned long long)s.sent, (unsigned long long)s.dropped);
        else ImGui::TextColored(col, tr("%s, %llu sent, %llu dropped"), s.state.c_str(), (unsigned long long)s.sent, (unsigned long long)s.dropped);
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
    c.lan = d.getI("feedLan", 0) != 0;
    c.beastPort = (int)d.getI("feedBeastPort", c.beastPort);
    c.kissPort = (int)d.getI("feedKissPort", c.kissPort);
    c.aisHost = d.getS("feedAisHost", c.aisHost); c.aisPort = (int)d.getI("feedAisPort", c.aisPort);
    c.acarsHost = d.getS("feedAcarsHost", c.acarsHost); c.acarsPort = (int)d.getI("feedAcarsPort", c.acarsPort);
    c.vdl2Host = d.getS("feedVdl2Host", c.vdl2Host); c.vdl2Port = (int)d.getI("feedVdl2Port", c.vdl2Port);
    c.aprsCall = d.getS("feedAprsCall", ""); c.aprsPass = d.getS("feedAprsPass", "");
    c.aprsServer = d.getS("feedAprsServer", c.aprsServer); c.aprsServerPort = (int)d.getI("feedAprsServerPort", c.aprsServerPort);
    c.stationId = d.getS("feedStationId", c.stationId);
    dect2::PskReporterConfig& r = a.pskr;
    r.on = d.getB("pskrOn", false); r.call = d.getS("pskrCall", ""); r.locator = d.getS("pskrLocator", ""); r.antenna = d.getS("pskrAntenna", "");
    if (r.on) dect2::PskReporter::get().configure(r);
    bool any = false;
    for (bool o : c.on) any = any || o;
    if (any) dect2::Feeds::get().configure(c);
    dect2::ShareConfig& sc = a.share;
    sc.on = d.getB("shareOn", false); sc.port = (int)d.getI("sharePort", sc.port); sc.lan = d.getI("shareLan", 0) != 0; sc.allowControl = d.getI("shareControl", 0) != 0;
    if (sc.on) dect2::ShareServer::get().configure(sc);
}

// Remote control from the clients of "Share my radio": their frequency and gain commands (only arrive when the box is ticked)
void shareTick(App& a) {
    if (a.rxId != 1 || !a.share.on || !a.share.allowControl) return;
    bool retune = false;
    for (const dect2::ShareCommand& m : dect2::ShareServer::get().takeCommands()) {
        if (m.cmd == 0x01 && m.value > 0) { a.freqMhz = m.value / 1e6; a.tune.centerHz = a.freqMhz * 1e6; retune = true; }
        else if (m.cmd == 0x04 && a.devIdx >= 0 && a.devIdx < (int)a.devices.size() && a.devices[a.devIdx].isGeneric()) { a.tune.gainDb = m.value / 10.0; retune = true; }
        else if (m.cmd == 0x0e) { a.tune.biasTee = m.value != 0; retune = true; }
    }
    if (retune && a.engine.running()) { a.engine.retune(a.tune); a.peak.clear(); }
}

void saveFeedPrefs(const App& a) {
    plat::Prefs& d = plat::prefs();
    const dect2::FeedConfig& c = a.feeds;
    for (int i = 0; i < dect2::kNumFeeds; i++) d.setB(kKeys[i], c.on[i]);
    d.setI("feedLan", c.lan ? 1 : 0); d.setI("feedSbsPort", c.sbsPort); d.setI("feedBeastPort", c.beastPort); d.setI("feedKissPort", c.kissPort);
    d.setS("feedAisHost", c.aisHost); d.setI("feedAisPort", c.aisPort);
    d.setS("feedAcarsHost", c.acarsHost); d.setI("feedAcarsPort", c.acarsPort);
    d.setS("feedVdl2Host", c.vdl2Host); d.setI("feedVdl2Port", c.vdl2Port);
    d.setS("feedAprsCall", c.aprsCall); d.setS("feedAprsPass", c.aprsPass);
    d.setS("feedAprsServer", c.aprsServer); d.setI("feedAprsServerPort", c.aprsServerPort);
    d.setS("feedStationId", c.stationId);
    d.setB("shareOn", a.share.on); d.setI("sharePort", a.share.port); d.setI("shareLan", a.share.lan ? 1 : 0); d.setI("shareControl", a.share.allowControl ? 1 : 0);
    d.setB("pskrOn", a.pskr.on); d.setS("pskrCall", a.pskr.call); d.setS("pskrLocator", a.pskr.locator); d.setS("pskrAntenna", a.pskr.antenna);
}

void feedsWindow(App& a) {
    if (a.rxId != 1) {   // the feeds and PSK Reporter are shared by all receivers (they send what every receiver decodes): one window, RX 1's settings
        if (App* f = receiverById(1)) { f->feedsOpen = a.feedsOpen; feedsWindow(*f); a.feedsOpen = f->feedsOpen; }
        return;
    }
    if (!a.feedsOpen) return;
    ImGui::SetNextWindowSize(ImVec2(480 * gUi, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin(trw("Data feeds"), &a.feedsOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    dect2::FeedConfig& c = a.feeds;
    bool changed = false;
    ImGui::PushTextWrapPos(460 * gUi);
    ImGui::TextDisabled("%s", tr("Sends decoded data to other programs while the mode runs. Every feed is off until you turn it on."));
    ImGui::PopTextWrapPos();

    changed |= ImGui::Checkbox(tr("Let other computers connect to the TCP feeds (LAN)"), &c.lan);
    ImGui::SeparatorText(tr("ADS-B"));
    ImGui::PushID("sbs");
    changed |= ImGui::Checkbox(tr("SBS-1 BaseStation (TCP server)"), &c.on[dect2::kFeedSbs]);
    ImGui::SameLine(); changed |= portField(N_("Port"), c.sbsPort);
    statusLine(dect2::kFeedSbs, true);
    ImGui::PopID();
    ImGui::PushID("beast");
    changed |= ImGui::Checkbox(tr("Beast binary (TCP server)"), &c.on[dect2::kFeedBeast]);
    ImGui::SameLine(); changed |= portField(N_("Port"), c.beastPort);
    statusLine(dect2::kFeedBeast, true);
    ImGui::PopID();

    ImGui::SeparatorText(tr("AIS"));
    ImGui::PushID("ais");
    changed |= ImGui::Checkbox(tr("NMEA !AIVDM (UDP)"), &c.on[dect2::kFeedAis]);
    ImGui::SameLine(); changed |= textField("##host", c.aisHost, 0, 120);
    ImGui::SameLine(); changed |= portField(N_("Port"), c.aisPort);
    statusLine(dect2::kFeedAis, false);
    ImGui::PopID();

    ImGui::SeparatorText(tr("APRS"));
    ImGui::PushID("aprsis");
    changed |= ImGui::Checkbox(tr("APRS-IS (receive-only igate)"), &c.on[dect2::kFeedAprsIs]);
    ImGui::Indent();
    changed |= textField(N_("Callsign"), c.aprsCall, ImGuiInputTextFlags_CharsUppercase, 120);
    changed |= textField(N_("Passcode"), c.aprsPass, ImGuiInputTextFlags_Password | ImGuiInputTextFlags_CharsDecimal, 120);
    changed |= textField("##server", c.aprsServer, 0, 160);
    ImGui::SameLine(); changed |= portField(N_("Server"), c.aprsServerPort);
    ImGui::Unindent();
    statusLine(dect2::kFeedAprsIs, false);
    ImGui::PopID();
    ImGui::PushID("kiss");
    changed |= ImGui::Checkbox(tr("KISS (TCP server)"), &c.on[dect2::kFeedKiss]);
    ImGui::SameLine(); changed |= portField(N_("Port"), c.kissPort);
    statusLine(dect2::kFeedKiss, true);
    ImGui::PopID();

    ImGui::SeparatorText(tr("ACARS and VDL2 (JSON)"));
    ImGui::PushID("acars");
    changed |= ImGui::Checkbox(tr("ACARS, acarsdec format (UDP)"), &c.on[dect2::kFeedAcars]);
    ImGui::SameLine(); changed |= textField("##host", c.acarsHost, 0, 120);
    ImGui::SameLine(); changed |= portField(N_("Port"), c.acarsPort);
    statusLine(dect2::kFeedAcars, false);
    ImGui::PopID();
    ImGui::PushID("vdl2");
    changed |= ImGui::Checkbox(tr("VDL2, dumpvdl2 format (UDP)"), &c.on[dect2::kFeedVdl2]);
    ImGui::SameLine(); changed |= textField("##host", c.vdl2Host, 0, 120);
    ImGui::SameLine(); changed |= portField(N_("Port"), c.vdl2Port);
    statusLine(dect2::kFeedVdl2, false);
    ImGui::PopID();
    changed |= textField(N_("Station ID"), c.stationId, 0, 120);

    ImGui::SeparatorText(tr("PSK Reporter"));
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(380 * gUi);
        ImGui::TextUnformatted(tr("Sends the FT8, FT4, FT2, WSPR, JS8Call, PSK31 and CW stations you decode to pskreporter.info, so they show on its public map and statistics. "
            "A report is: the sender's callsign, frequency, SNR, mode, its locator when the message has one, and when it was heard; with your callsign, locator, antenna text and 'OnAir'. "
            "Reports are batched and sent every 5 minutes; the same station on the same band is sent once per 10 minutes. Nothing is sent until you tick the box. "
            "Spots are public: use your own valid amateur radio callsign. CW and PSK31 are only reported when the text says 'DE callsign'."));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    dect2::PskReporterConfig& r = a.pskr;
    bool pc = false;
    pc |= ImGui::Checkbox(tr("Send spots to PSK Reporter (pskreporter.info)"), &r.on);
    ImGui::Indent();
    pc |= textField(N_("Your callsign"), r.call, ImGuiInputTextFlags_CharsUppercase, 120);
    pc |= textField(N_("Your locator"), r.locator, ImGuiInputTextFlags_CharsUppercase, 90);
    pc |= textField(N_("Antenna"), r.antenna, 0, 200);
    ImGui::Unindent();
    {
        const dect2::PskReporterStatus s = dect2::PskReporter::get().status();
        ImGui::Indent();
        if (!r.on) ImGui::TextDisabled("%s", tr("off"));
        else {
            ImGui::TextColored(s.active ? pal::okGreen() : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), tr("%s, %llu spots sent in %llu packets, %llu duplicates skipped"), s.state.c_str(), (unsigned long long)s.spotsSent, (unsigned long long)s.packets, (unsigned long long)s.duplicates);
            if (s.lastSend) { char b[32]; const time_t t = (time_t)s.lastSend; strftime(b, sizeof b, "%H:%M:%S", localtime(&t)); ImGui::Text(tr("last send %s"), b); }
            if (!s.lastError.empty()) ImGui::TextColored(pal::badRed(), "%s", s.lastError.c_str());
        }
        ImGui::PushTextWrapPos(460 * gUi);
        ImGui::TextDisabled("%s", tr("Spots are public. A valid amateur radio callsign is required; the dial frequency is the tuned frequency."));
        ImGui::PopTextWrapPos();
        ImGui::Unindent();
    }
    if (pc) { dect2::PskReporter::get().configure(r); savePrefs(a); }

    ImGui::SeparatorText(tr("Share my radio (rtl_tcp server)"));
    {
        dect2::ShareConfig& sc = a.share;
        bool sch = false;
        sch |= ImGui::Checkbox(tr("Share the radio's samples as an rtl_tcp server"), &sc.on);
        ImGui::SameLine(); sch |= portField(N_("Port"), sc.port);
        ImGui::Indent();
        sch |= ImGui::Checkbox(tr("Let other computers connect (LAN)"), &sc.lan);
        sch |= ImGui::Checkbox(tr("Allow remote control (frequency and gain)"), &sc.allowControl);
        const dect2::ShareStatus ss = dect2::ShareServer::get().status();
        if (!sc.on) ImGui::TextDisabled("%s", tr("off"));
        else if (!ss.listening) ImGui::TextColored(pal::badRed(), "%s", ss.lastError.empty() ? tr("starting") : ss.lastError.c_str());
        else {
            ImGui::TextColored(pal::okGreen(), tr("listening on %s port %d, %d client(s), %.1f MB sent, %.1f MB dropped"), sc.lan ? "all addresses" : "127.0.0.1", sc.port,
                               (int)ss.clients.size(), (double)ss.sentBytes / 1e6, (double)ss.droppedBytes / 1e6);
            for (const std::string& c : ss.clients) ImGui::BulletText("%s", c.c_str());
        }
        ImGui::PushTextWrapPos(460 * gUi);
        ImGui::TextDisabled("%s", tr("A second OnAir (Add network radio, rtl_tcp), SDR# or SDR++ connects to this computer's address and the port. Samples are sent as 8-bit IQ at the rate the radio runs at. "
                                     "Without 'allow remote control' the clients cannot change the frequency or gain."));
        ImGui::PopTextWrapPos();
        ImGui::Unindent();
        if (sch) { dect2::ShareServer::get().configure(sc); savePrefs(a); }
    }

    if (changed) apply(a);
    ImGui::End();
}
