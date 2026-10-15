// HF ALE screens: the common ALE channels (HFN / amateur, upper sideband dial), the nets known, Listen, then three views: the event
// log (UTC, frequency, from, to, call type, AMD text and commands, BER, SNR), the stations heard (address, last heard, count), and
// every word decoded (type, characters, vote and Golay errors).
#include "app.h"
#include "dect2/ale_proto.h"
#include "dect2/ale_rx.h"
#include "dect2/ale_tel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace {

struct State {
    bool wasRunning = false, loaded = false;
    bool listen = false; int pushedListen = -1; float pushedVol = -1; bool pushedMute = false;   // "Listen": off until switched on
    int view = 0;                        // 0 events, 1 stations, 2 words
    int demod = dect2::HfDemodAuto;      // the front end's demodulator (HfDemod)
    char nets[128] = "";
    std::string pushedNets = "\x01";
};
State S;

bool live(const App& a) { return a.rx.standard == 50; }   // the engine reports its standard code minus one

void load() {
    if (S.loaded) return;
    S.loaded = true;
    S.listen = plat::prefs().getB("aleListen", false);
    S.demod = hfDemodLoad("aleDemod");
    const std::string n = plat::prefs().getS("aleNets", "");
    snprintf(S.nets, sizeof S.nets, "%s", n.c_str());
}

void utcText(double t, char* out, size_t n) {
    const time_t tt = (time_t)std::floor(t);
    struct tm g {};
#ifdef _WIN32
    gmtime_s(&g, &tt);
#else
    gmtime_r(&tt, &g);
#endif
    strftime(out, n, "%H:%M:%S", &g);
}

std::vector<std::string> netList() {
    std::vector<std::string> v;
    std::string cur;
    for (const char* p = S.nets;; p++) {
        const char c = *p;
        if (c == 0 || c == ',' || c == ' ') { if (!cur.empty()) v.push_back(cur); cur.clear(); if (!c) break; continue; }
        cur += (char)((c >= 'a' && c <= 'z') ? c - 32 : c);
    }
    return v;
}

ImVec4 typeColour(const std::string& t) {
    if (t == "Sounding") return ImVec4(0.60f, 0.70f, 0.78f, 1);
    if (t == "Response" || t == "Net response") return ImVec4(0.55f, 0.85f, 0.60f, 1);
    if (t == "Ack") return ImVec4(0.40f, 0.95f, 0.55f, 1);
    if (t == "Net" || t == "Group" || t == "AllCall" || t == "AnyCall") return ImVec4(0.95f, 0.75f, 0.45f, 1);
    return ImVec4(0.92f, 0.94f, 0.96f, 1);
}

void presets(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Channel"); flowNext(6 * gUi);
    int n = 0;
    const dect2::AlePreset* p = dect2::alePresets(n);
    for (int i = 0; i < n; i++) {
        const bool here = std::fabs(a.freqMhz - p[i].mhz) < 0.002;
        if (here) ImGui::PushStyleColor(ImGuiCol_Button, pal::accent(0.45f));
        char b[40]; snprintf(b, sizeof b, "%.3f##alep%d", p[i].mhz, i);
        if (ImGui::Button(b)) tuneFreq(a, p[i].mhz);
        if (here) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s ALE channel, %.3f MHz dial (upper sideband)", p[i].name, p[i].mhz);
        flowNext(4 * gUi);
    }
    ImGui::TextDisabled("|"); flowNext(8 * gUi);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Nets"); flowNext(4 * gUi);
    ImGui::SetNextItemWidth(110 * gUi);
    if (ImGui::InputTextWithHint("##alenets", "HFN, ...", S.nets, sizeof S.nets)) plat::prefs().setS("aleNets", S.nets);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Net addresses you know, comma separated: calls to them are shown as net calls at once.\nOthers are found when a station other than the one called answers.");
    flowNext(8 * gUi);
    listenControls(a, S.listen, "aleListen", "ale", "Play the sideband (dial + 200 Hz to 3.8 kHz) on the sound card; decoding is the same either way");
    flowNext(8 * gUi);
    if (ImGui::Button("Clear##ale")) a.engine.ale().clearLog();
    flowEnd();
}

void eventsView(App& a) {
    const auto& ev = a.rx.ale.events;
    if (!ImGui::BeginTable("##alee", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("UTC"); ImGui::TableSetupColumn("MHz"); ImGui::TableSetupColumn("From"); ImGui::TableSetupColumn("To"); ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn("BER"); ImGui::TableSetupColumn("dB"); ImGui::TableSetupColumn("AMD / commands", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGui::PushFont(a.mono, 0);
    char b[32];
    for (const auto& e : ev) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); utcText(e.utc, b, sizeof b); ImGui::TextDisabled("%s", b);
        ImGui::TableNextColumn(); if (e.dialHz > 0) ImGui::Text("%.3f", e.dialHz / 1e6); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.36f, 1), "%s", e.from.empty() ? "?" : e.from.c_str());
        ImGui::TableNextColumn(); ImGui::TextUnformatted(e.to.empty() ? "-" : e.to.c_str());
        ImGui::TableNextColumn(); ImGui::TextColored(typeColour(e.type), "%s", e.type.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s, %d words, %s, tone offset %+.0f Hz", e.type.c_str(), e.words, e.tis ? "TIS: an answer requested" : "TWS: no answer", e.cfoHz);
        ImGui::TableNextColumn(); ImGui::Text("%.1f%%", 100 * e.ber);
        ImGui::TableNextColumn(); ImGui::Text("%+.0f", e.snrDb);
        ImGui::TableNextColumn();
        ImGui::PushTextWrapPos(0);
        if (!e.amd.empty()) ImGui::TextColored(ImVec4(0.70f, 0.90f, 1.0f, 1), "%s", e.amd.c_str());
        if (!e.cmd.empty()) ImGui::TextDisabled("%s", e.cmd.c_str());
        if (e.amd.empty() && e.cmd.empty()) ImGui::TextDisabled(" ");
        ImGui::PopTextWrapPos();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::PopFont();
    ImGui::EndTable();
    if (ev.empty()) ImGui::TextDisabled("no calls yet");
}

void stationsView(App& a) {
    const auto& st = a.rx.ale.stations;
    if (!ImGui::BeginTable("##ales", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Address"); ImGui::TableSetupColumn("Last heard"); ImGui::TableSetupColumn("MHz"); ImGui::TableSetupColumn("Count"); ImGui::TableSetupColumn("dB");
    ImGui::TableSetupColumn("Last", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGui::PushFont(a.mono, 0);
    char b[32];
    for (const auto& s : st) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.36f, 1), "%s", s.address.c_str());
        ImGui::TableNextColumn(); utcText(s.lastUtc, b, sizeof b); ImGui::TextDisabled("%s", b);
        ImGui::TableNextColumn(); if (s.lastDialHz > 0) ImGui::Text("%.3f", s.lastDialHz / 1e6); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn(); ImGui::Text("%d", s.count);
        ImGui::TableNextColumn(); ImGui::Text("%+.0f", s.snrDb);
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", s.lastType.c_str());
    }
    ImGui::PopFont();
    ImGui::EndTable();
    if (st.empty()) ImGui::TextDisabled("no stations yet");
}

void wordsView(App& a) {
    const auto& w = a.rx.ale.words;
    if (!ImGui::BeginTable("##alew", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("UTC"); ImGui::TableSetupColumn("Type"); ImGui::TableSetupColumn("Text"); ImGui::TableSetupColumn("Vote"); ImGui::TableSetupColumn("Golay", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGui::PushFont(a.mono, 0);
    char b[32];
    for (const auto& x : w) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); utcText(x.utc, b, sizeof b); ImGui::TextDisabled("%s", b);
        ImGui::TableNextColumn(); ImGui::Text("%s", dect2::ale::typeName(x.type));
        ImGui::TableNextColumn(); ImGui::Text("\"%s\"", x.text.c_str());
        ImGui::TableNextColumn(); ImGui::Text("%d", x.voteErrs);
        ImGui::TableNextColumn(); ImGui::Text("%d", x.golayErrs);
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::PopFont();
    ImGui::EndTable();
    if (w.empty()) ImGui::TextDisabled("no words yet");
}

// ---------------------------------------------------------------- hooks

void tick(App& a) {
    load();
    if (!a.engine.running()) { S.wasRunning = false; return; }
    dect2::AleReceiver& r = a.engine.ale();
    if (!S.wasRunning || S.pushedNets != S.nets) { r.setNetAddresses(netList()); S.pushedNets = S.nets; }
    if (!S.wasRunning || (int)S.listen != S.pushedListen) { r.setListen(S.listen); S.pushedListen = S.listen; }
    r.setDemod(dect2::hfDemodResolve(S.demod, a.freqMhz));   // Auto follows the dial
    if (!S.wasRunning || a.volume != S.pushedVol || a.muted != S.pushedMute) {   // the sound controls are shared with the other modes
        r.setVolume(a.volume); r.setMuted(a.muted);
        S.pushedVol = a.volume; S.pushedMute = a.muted;
    }
    S.wasRunning = true;
}

void tuner(App& a, bool&) {
    load();
    hfDemodControl(a, S.demod, "aleDemod", "ale");
}

void tab(App& a) {
    load();
    presets(a);
    ImGui::Spacing();
    subNav("alev", S.view, {"Events", "Stations", "Words"});
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    if (S.view == 0) eventsView(a); else if (S.view == 1) stationsView(a); else wordsView(a);
}

void list(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    if (!ImGui::BeginTable("##alel", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupColumn("Address"); ImGui::TableSetupColumn("Heard", ImGuiTableColumnFlags_WidthStretch);
    char b[32];
    for (const auto& s : a.rx.ale.stations) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(s.address.c_str()); ImGui::PopFont();
        ImGui::TableNextColumn(); utcText(s.lastUtc, b, sizeof b); ImGui::TextDisabled("%s, %dx", b, s.count);
    }
    ImGui::EndTable();
    if (a.rx.ale.stations.empty()) ImGui::TextDisabled("no stations yet");
}

void receiver(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const dect2::AleTelemetry& t = a.rx.ale;
    auto kv = [&](const char* k, const char* fmt, auto... v) { ImGui::TextDisabled("%s", k); kvColumn(130 * gUi); ImGui::PushFont(a.mono, 0); ImGui::PushTextWrapPos(0); ImGui::Text(fmt, v...); ImGui::PopTextWrapPos(); ImGui::PopFont(); };
    kv("state", "%s", dect2::aleSummary(t).c_str());
    kv("word sync", "%s", t.state == 2 ? "in sync (392 ms words)" : "searching");
    kv("words", "%llu decoded, %llu missed in sync", (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad);
    kv("last word", "%+.1f dB in 3 kHz, BER %.1f%%", t.snrDb, 100 * t.ber);
    kv("tone offset", "%+.1f Hz (AFC)", t.cfoHz);
    kv("clock", "%s", t.timeMode ? "the recording's start time" : "system clock (UTC)");
    kv("input", "%.3f Msps, %.1f dBFS", t.inputRate / 1e6, t.levelDb);
}

void status(App& a) {
    const bool run = a.engine.running(), on = live(a);
    const SignalStats& st = a.spec.stats;
    const AdcStatus adc = classifyAdc(st.rmsDbfs, st.peak, st.clipFraction);
    StatusPanel panel;
    lamp("IQ", run ? (adc == AdcStatus::Overload ? 3 : (adc == AdcStatus::Good ? 1 : 2)) : 0, (int)Ic::Wave); flowNext(12 * gUi);
    lamp("Sync", !on ? 0 : a.rx.ale.state == 2 ? 1 : 0); flowNext(10 * gUi);
    lamp("Words", !on ? 0 : a.rx.ale.dataValid ? 1 : 0); flowNext(10 * gUi);
    ImGui::TextDisabled("|"); flowNext(10 * gUi);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State"); ImGui::SameLine(0, 5 * gUi);
    ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(!on ? (run ? "starting" : "stopped") : dect2::aleSummary(a.rx.ale).c_str()); ImGui::PopFont();
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "HF ALE";
    if (live(a)) l2 = dect2::aleSummary(a.rx.ale);
}

} // namespace

extern const ModeUi kAleUi;
const ModeUi kAleUi = {
    .sideTitle = "STATIONS",
    .tabName = "ALE",
    .tabIcon = Ic::Radio,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .tuner = tuner,
    .tick = tick,
};
