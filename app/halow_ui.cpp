// Wi-Fi HaLow (802.11ah) screens: the network list (SSID, BSSID, vendor, channel, bandwidth, MCS, security as information, level, beacons, last seen),
// the live frame log with a frames-per-second plot, the region (EU 863 - 868 MHz / US 902 - 928 MHz) and channel pickers, and the channel scan.
// Receive only: nothing is ever sent, joined or answered; nothing is decrypted, the security type is only what the beacon announces.
#include "app.h"
#include "dect2/halow_proto.h"
#include "dect2/halow_rx.h"
#include "dect2/halow_tel.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

struct State {
    int view = 0;                        // 0 networks, 1 frames
    bool loaded = false, scan = false;
    int region = 0;                      // 0 EU 863 - 868 MHz, 1 US 902 - 928 MHz
    int width = 1;                       // the channel width the picker lists: 1, 2, 4 MHz
    int dwellSec = 6;
    double lastHop = 0;
    char filter[64] = "";
    float minRssi = -120;
    std::string sel;                     // BSSID clicked: the frame log shows only its frames
};
State S;

void loadState() {
    if (S.loaded) return;
    S.loaded = true;
    S.scan = plat::prefs().getB("hlScan", false);
    S.region = std::max(0, std::min(1, (int)plat::prefs().getI("hlRegion", 0)));
    S.width = std::max(1, std::min(4, (int)plat::prefs().getI("hlWidth", 1)));
    if (S.width == 3) S.width = 2;
    S.dwellSec = std::max(1, std::min(60, (int)plat::prefs().getI("hlDwell", 6)));
}

bool live(const App& a) { return a.rx.standard == 71; }   // the engine reports its standard code minus one

std::string clock(int64_t t) {
    if (t <= 0) return "-";
    const time_t tt = (time_t)t;
    struct tm g;
#ifdef _WIN32
    gmtime_s(&g, &tt);
#else
    gmtime_r(&tt, &g);
#endif
    char b[16];
    snprintf(b, sizeof b, "%02d:%02d:%02d", g.tm_hour, g.tm_min, g.tm_sec);
    return b;
}

void kvRow(const App& a, const char* k, const char* fmt, ...) {
    ImGui::TextDisabled("%s", k); kvColumn(130 * gUi);
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    ImGui::PushFont(a.mono, 0); ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(b); ImGui::PopTextWrapPos(); ImGui::PopFont();
}

std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

std::string nameOf(const dect2::HlNet& n) {
    if (n.hasSsid) return n.ssid;
    if (n.hidden) return "(hidden)";
    if (n.comp) return dect2::halow::compText(n.comp);
    return "-";
}

bool passes(const dect2::HlNet& n) {
    if (n.rssi < S.minRssi) return false;
    if (!S.filter[0]) return true;
    const std::string f = lower(S.filter);
    return lower(n.ssid).find(f) != std::string::npos || lower(n.bssid).find(f) != std::string::npos || lower(n.vendor).find(f) != std::string::npos ||
           lower(n.security).find(f) != std::string::npos;
}

void receiveNote() {
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Receive only: OnAir never transmits, probes, joins or answers a network, and nothing is decrypted. The security type is what the beacon "
                        "announces, shown for information. Sub-GHz Wi-Fi HaLow (802.11ah) is for low power sensors and cameras at long range; 1, 2 and 4 MHz channels, "
                        "863 - 868 MHz in Europe, 902 - 928 MHz in the US. A 1 or 2 MHz channel works from 2.4 Msps (an RTL-SDR), 4 MHz needs a radio with 4 Msps or more.");
    ImGui::PopTextWrapPos();
}

void filters(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Filter"); ImGui::SameLine(0, 5 * gUi);
    ImGui::SetNextItemWidth(std::min(160 * gUi, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##hlf", "SSID, BSSID, vendor, security", S.filter, sizeof S.filter);
    flowNext(10 * gUi);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Level above"); ImGui::SameLine(0, 5 * gUi);
    ImGui::SetNextItemWidth(std::min(120 * gUi, ImGui::GetContentRegionAvail().x));
    ImGui::SliderFloat("##hlr", &S.minRssi, -120, 0, "%.0f dBFS");
    flowNext(10 * gUi);
    if (!S.sel.empty()) {
        ImGui::TextColored(pal::okGreen(), "%s only", S.sel.c_str());
        ImGui::SameLine(0, 5 * gUi);
        if (ImGui::SmallButton("Show all")) S.sel.clear();
        flowNext(10 * gUi);
    }
    if (ImGui::Button("Clear") && live(a)) a.engine.halow().clearResults();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forget every network and frame heard so far");
    flowBreak();
}

void networksView(App& a) {
    receiveNote();
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "start the receiver to see the networks around you"); return; }
    filters(a);
    const dect2::HalowTelemetry& t = a.rx.halow;
    if (t.ppsHist.size() >= 2) {
        ImGui::TextDisabled("Frames per second");
        ImGui::PlotLines("##hlpps", t.ppsHist.data(), (int)t.ppsHist.size(), 0, nullptr, 0, FLT_MAX, ImVec2(ImGui::GetContentRegionAvail().x, 44 * gUi));
    }
    if (!ImGui::BeginTable("##hlnet", 12, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX,
                           ImVec2(0, ImGui::GetContentRegionAvail().y)))
        return;
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("SSID"); ImGui::TableSetupColumn("BSSID"); ImGui::TableSetupColumn("Vendor"); ImGui::TableSetupColumn("Ch"); ImGui::TableSetupColumn("MHz");
    ImGui::TableSetupColumn("Width"); ImGui::TableSetupColumn("MCS"); ImGui::TableSetupColumn("Security"); ImGui::TableSetupColumn("Beacon");
    ImGui::TableSetupColumn("Level"); ImGui::TableSetupColumn("Beacons"); ImGui::TableSetupColumn("Last seen", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    int shown = 0;
    for (const auto& n : t.nets) {
        if (!passes(n)) continue;
        shown++;
        ImGui::TableNextRow();
        ImGui::PushID(n.bssid.c_str());
        const double ago = std::max(0.0, t.timeSec - n.lastSec);
        const ImVec4 c = ago < 3 ? pal::okGreen() : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::TableNextColumn();
        if (ImGui::Selectable("##row", S.sel == n.bssid, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) S.sel = S.sel == n.bssid ? std::string() : n.bssid;
        ImGui::SameLine(0, 0);
        ImGui::TextColored(c, "%s", nameOf(n).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nclick to show only this network's frames", n.last.c_str());
        ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(n.bssid.c_str()); ImGui::PopFont();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(n.vendor.c_str());
        ImGui::TableNextColumn(); if (n.ccf) ImGui::Text("%d", n.ccf); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn(); if (n.freqMhz > 0) ImGui::Text("%.1f", n.freqMhz); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        if (n.opWidthMhz && n.opWidthMhz != n.seenWidthMhz) ImGui::Text("%d (op %d) MHz", n.seenWidthMhz, n.opWidthMhz); else ImGui::Text("%d MHz", n.seenWidthMhz);
        ImGui::TableNextColumn(); ImGui::Text("%d", n.mcs);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(n.security.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("What the beacon announces, for information. OnAir does not decrypt or test anything.");
        ImGui::TableNextColumn(); if (n.intervalTu) ImGui::Text("%d TU", n.intervalTu); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn(); ImGui::Text("%.0f", n.rssi);
        ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)n.beacons);
        ImGui::TableNextColumn(); ImGui::Text(ago < 1 ? "now" : "%.0f s ago", ago);
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (t.nets.empty()) ImGui::TextDisabled("nothing heard yet");
    else if (!shown) ImGui::TextDisabled("no network passes the filter");
}

void framesView(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "start the receiver to see the frames"); return; }
    const dect2::HalowTelemetry& t = a.rx.halow;
    const auto& log = t.log;
    if (!S.sel.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(pal::okGreen(), "Showing %s only", S.sel.c_str());
        ImGui::SameLine(0, 8 * gUi);
        if (ImGui::SmallButton("Show all")) S.sel.clear();
    }
    std::vector<size_t> rows;
    for (size_t i = 0; i < log.size(); i++) {
        if (!S.sel.empty() && log[i].bssid != S.sel) continue;
        rows.push_back(i);
    }
    if (ImGui::Button("Copy")) {
        std::string txt;
        for (size_t i : rows) {
            const auto& f = log[i];
            char b[700];
            snprintf(b, sizeof b, "%s\t%d MHz\tMCS %d\t%s\t%s\t%d\t%s\t%s\n", clock(f.wallTime).c_str(), f.widthMhz, f.mcs, f.type.c_str(), f.bssid.c_str(), f.len, f.decoded ? "ok" : "-", f.info.c_str());
            txt += b;
        }
        ImGui::SetClipboardText(txt.c_str());
    }
    ImGui::SameLine(0, 8 * gUi);
    ImGui::TextDisabled("%.0f frames/s", t.pps);
    if (!ImGui::BeginTable("##hllog", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX,
                           ImVec2(0, ImGui::GetContentRegionAvail().y)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Time UTC"); ImGui::TableSetupColumn("Width"); ImGui::TableSetupColumn("MCS"); ImGui::TableSetupColumn("Type"); ImGui::TableSetupColumn("BSSID");
    ImGui::TableSetupColumn("Len"); ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGuiListClipper clip;
    clip.Begin((int)rows.size());
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
            const auto& f = log[rows[(size_t)i]];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(clock(f.wallTime).c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d MHz", f.widthMhz);
            ImGui::TableNextColumn(); ImGui::Text("%d", f.mcs);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(f.type.c_str());
            ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(f.bssid.c_str()); ImGui::PopFont();
            ImGui::TableNextColumn(); ImGui::Text("%d", f.len);
            ImGui::TableNextColumn(); if (f.decoded) ImGui::TextUnformatted(f.info.c_str()); else ImGui::TextDisabled("%s", f.info.c_str());
        }
    ImGui::EndTable();
    if (log.empty()) ImGui::TextDisabled("no frames yet");
    else if (rows.empty()) ImGui::TextDisabled("no frame passes the selection");
}

std::vector<dect2::halow::Chan> plan() { return S.region == 0 ? dect2::halow::euChannels(S.width) : dect2::halow::usChannels(S.width); }

void hopTo(App& a, double mhz) {
    if (!a.engine.running() || !live(a)) return;   // wait until activeStd is 71 (engine stdMode 72)
    a.freqMhz = mhz;
    a.tune.centerHz = mhz * 1e6;
    a.engine.retune(a.tune);   // no reset: the tables stay
}

// ---------------------------------------------------------------- hooks

void tick(App& a) {
    loadState();
    const bool fileSrc = a.devIdx >= 0 && a.devIdx < (int)a.devices.size() && a.devices[(size_t)a.devIdx].kind == DeviceInfo::File;
    if (!a.engine.running() || !live(a) || !S.scan || fileSrc) { S.lastHop = ImGui::GetTime(); return; }
    if (ImGui::GetTime() - S.lastHop < S.dwellSec) return;
    S.lastHop = ImGui::GetTime();
    const auto ch = plan();
    if (ch.empty()) return;
    size_t next = 0;
    for (size_t i = 0; i < ch.size(); i++) if (std::fabs(ch[i].mhz - a.freqMhz) < 0.25) { next = (i + 1) % ch.size(); break; }
    hopTo(a, ch[next].mhz);
}

void tuner(App& a, bool& retune) {
    loadState();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Region");
    ImGui::SameLine(0, 5 * gUi);
    ImGui::SetNextItemWidth(std::min(150 * gUi, ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##hlreg", S.region == 0 ? "EU 863 - 868 MHz" : "US 902 - 928 MHz")) {
        if (ImGui::Selectable("EU 863 - 868 MHz", S.region == 0)) { S.region = 0; if (S.width > 2) S.width = 2; plat::prefs().setI("hlRegion", 0); plat::prefs().setI("hlWidth", S.width); plat::prefs().flush(); }
        if (ImGui::Selectable("US 902 - 928 MHz", S.region == 1)) { S.region = 1; plat::prefs().setI("hlRegion", 1); plat::prefs().flush(); }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 8 * gUi);
    ImGui::TextDisabled("Width");
    ImGui::SameLine(0, 5 * gUi);
    ImGui::SetNextItemWidth(std::min(70 * gUi, ImGui::GetContentRegionAvail().x));
    char wl[16];
    snprintf(wl, sizeof wl, "%d MHz", S.width);
    if (ImGui::BeginCombo("##hlw", wl)) {
        for (int w : {1, 2, 4}) {
            if (S.region == 0 && w == 4) continue;
            snprintf(wl, sizeof wl, "%d MHz", w);
            if (ImGui::Selectable(wl, S.width == w)) { S.width = w; plat::prefs().setI("hlWidth", w); plat::prefs().flush(); }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The channel width of the list below. A radio takes in one channel at a time (a 4 MHz channel needs 4 Msps or more);\n"
                                                  "the receiver reads 1, 2 and 4 MHz frames on whichever channel it is tuned to.");
    flowNext(10 * gUi);
    const auto ch = plan();
    int cur = 0;
    char label[48] = "other";
    for (const auto& c : ch) if (std::fabs(c.mhz - a.freqMhz) < 0.25) { cur = c.ccf; snprintf(label, sizeof label, "%d (%.1f MHz)", c.ccf, c.mhz); }
    ImGui::TextDisabled("Channel");
    ImGui::SameLine(0, 5 * gUi);
    ImGui::SetNextItemWidth(std::min(150 * gUi, ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##hlch", label)) {
        for (const auto& c : ch) {
            char b[48];
            snprintf(b, sizeof b, "%d  %.1f MHz", c.ccf, c.mhz);
            if (ImGui::Selectable(b, c.ccf == cur)) { tuneFreq(a, c.mhz); retune = true; }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("S1G channels of the region and width (centre = 863 or 902 MHz + 0.5 MHz x channel number)");
    ImGui::SameLine(0, 8 * gUi);
    if (ImGui::Checkbox("scan", &S.scan)) { plat::prefs().setB("hlScan", S.scan); plat::prefs().flush(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step through the channels of the list, listening to each for the dwell time (a radio that takes in a single channel misses what is\n"
                                                  "sent on the others meanwhile). Off: single channel.");
    if (S.scan) {
        ImGui::SameLine(0, 5 * gUi);
        ImGui::SetNextItemWidth(std::min(70 * gUi, ImGui::GetContentRegionAvail().x));
        if (ImGui::SliderInt("##hldw", &S.dwellSec, 1, 60, "%d s")) { plat::prefs().setI("hlDwell", S.dwellSec); plat::prefs().flush(); }
    }
}

void tab(App& a) {
    subNav("hlv", S.view, {"Networks", "Frames"});
    if (S.view == 0) networksView(a); else framesView(a);
}

void list(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const dect2::HalowTelemetry& t = a.rx.halow;
    if (!ImGui::BeginTable("##hll", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupColumn("Network", ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("Level");
    for (const auto& n : t.nets) {
        ImGui::TableNextRow();
        ImGui::PushID(n.bssid.c_str());
        const bool recent = t.timeSec - n.lastSec < 3;
        ImGui::TableNextColumn();
        if (ImGui::Selectable("##row", S.sel == n.bssid, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) S.sel = S.sel == n.bssid ? std::string() : n.bssid;
        ImGui::SameLine(0, 0);
        ImGui::TextColored(recent ? pal::okGreen() : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", nameOf(n).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%.1f MHz, %d MHz wide, %s", n.bssid.c_str(), n.freqMhz, n.seenWidthMhz, n.security.c_str());
        ImGui::TableNextColumn(); ImGui::TextDisabled("%.0f", n.rssi);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void receiver(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const dect2::HalowTelemetry& t = a.rx.halow;
    kvRow(a, "state", "%s", dect2::halowSummary(t).c_str());
    if (t.centerMhz > 0) kvRow(a, "tuned", "%.2f MHz%s%s", t.centerMhz, t.chanHint ? ", S1G channel " : "", t.chanHint ? std::to_string(t.chanHint).c_str() : "");
    kvRow(a, "signal", "tone SNR %.1f dB, offset %+.1f kHz", t.snrDb, t.cfoHz / 1e3);
    kvRow(a, "preambles", "%llu detected, %llu SIG fields read, %llu not readable", (unsigned long long)t.preambles, (unsigned long long)t.sigOk, (unsigned long long)t.sigBad);
    kvRow(a, "frames", "%llu good, %llu failed FCS, %llu at a rate that is only counted", (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad, (unsigned long long)t.undecoded);
    kvRow(a, "by width", "1 MHz %llu, 2 MHz %llu, 4 MHz %llu", (unsigned long long)t.perWidth[0], (unsigned long long)t.perWidth[1], (unsigned long long)t.perWidth[2]);
    kvRow(a, "input", "%.3f Msps, %.1f dBFS", t.inputRate / 1e6, t.levelDb);
    kvRow(a, "signal time", "%.1f s", t.timeSec);
    ImGui::Spacing();
    receiveNote();
}

void status(App& a) {
    const bool run = a.engine.running(), on = live(a);
    const SignalStats& st = a.spec.stats;
    const AdcStatus adc = classifyAdc(st.rmsDbfs, st.peak, st.clipFraction);
    StatusPanel panel;
    lamp("IQ", run ? (adc == AdcStatus::Overload ? 3 : (adc == AdcStatus::Good ? 1 : 2)) : 0, (int)Ic::Wave); flowNext(12 * gUi);
    lamp("Frames", !on ? 0 : a.rx.halow.state >= 1 ? 1 : 0); flowNext(10 * gUi);
    ImGui::TextDisabled("|"); flowNext(10 * gUi);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State"); ImGui::SameLine(0, 5 * gUi);
    ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(!on ? (run ? "starting" : "stopped") : dect2::halowSummary(a.rx.halow).c_str()); ImGui::PopFont();
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "Wi-Fi HaLow (802.11ah)";
    if (live(a)) l2 = dect2::halowSummary(a.rx.halow);
}

} // namespace

extern const ModeUi kHalowUi;
const ModeUi kHalowUi = {
    .sideTitle = "NETWORKS",
    .tabName = "HaLow",
    .tabIcon = Ic::Signal,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .tuner = tuner,
    .tick = tick,
};
