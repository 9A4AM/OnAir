// Bluetooth LE screens: the device table (address and its type, name, level with a short history, last seen, packets, vendor, beacon and
// services) with name and level filters, the packet log, the advertising channel picker and the hop between 37, 38 and 39.
// Receive only: what devices broadcast openly, as a scanner app shows it; nothing is ever sent, connected to or paired with.
#include "app.h"
#include "dect2/ble_proto.h"
#include "dect2/ble_rx.h"
#include "dect2/ble_tel.h"
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
    int view = 0;                        // 0 devices, 1 packets
    bool loaded = false, hop = false;
    int hopSec = 3;
    double lastHop = 0;
    char filter[64] = "";
    float minRssi = -120;
    bool namedOnly = false;
};
State S;

void loadState() {
    if (S.loaded) return;
    S.loaded = true;
    S.hop = plat::prefs().getB("bleHop", false);
    S.hopSec = std::max(1, std::min(30, (int)plat::prefs().getI("bleHopSec", 3)));
}

bool live(const App& a) { return a.engine.running() && a.rx.standard == 52; }   // the engine reports its standard code minus one

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

bool passes(const dect2::BleDevice& d) {
    if (d.rssi < S.minRssi) return false;
    if (S.namedOnly && d.name.empty()) return false;
    if (!S.filter[0]) return true;
    const std::string f = lower(S.filter);
    return lower(d.name).find(f) != std::string::npos || lower(d.addr).find(f) != std::string::npos || lower(d.vendor).find(f) != std::string::npos ||
           lower(d.beacon).find(f) != std::string::npos;
}

const char* kindShort(int k) { static const char* const n[] = {"public", "static", "private", "non-res.", "random"}; return k >= 0 && k < 5 ? n[k] : "?"; }

void privacyNote() {
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Receive only: the advertising packets devices broadcast openly, as a scanner app sees them. OnAir never connects, pairs or "
                        "transmits. Most phones use random private addresses that change every few minutes, so one phone can show up as several devices.");
    ImGui::PopTextWrapPos();
}

void filters(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Filter"); ImGui::SameLine(0, 5 * gUi);
    ImGui::SetNextItemWidth(std::min(160 * gUi, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##blef", "name, address, vendor", S.filter, sizeof S.filter);
    flowNext(10 * gUi);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Level above"); ImGui::SameLine(0, 5 * gUi);
    ImGui::SetNextItemWidth(std::min(120 * gUi, ImGui::GetContentRegionAvail().x));
    ImGui::SliderFloat("##bler", &S.minRssi, -120, 0, "%.0f dBFS");
    flowNext(10 * gUi);
    ImGui::Checkbox("named only", &S.namedOnly);
    flowNext(10 * gUi);
    if (ImGui::Button("Clear")) a.engine.ble().clearDevices();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forget every device and packet heard so far");
    flowBreak();
}

void devicesView(App& a) {
    privacyNote();
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "start the receiver to see the devices around you"); return; }
    filters(a);
    const dect2::BleTelemetry& t = a.rx.ble;
    if (!ImGui::BeginTable("##bledev", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX,
                           ImVec2(0, ImGui::GetContentRegionAvail().y)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Address"); ImGui::TableSetupColumn("Type"); ImGui::TableSetupColumn("Name"); ImGui::TableSetupColumn("Level");
    ImGui::TableSetupColumn("Last seen"); ImGui::TableSetupColumn("Packets"); ImGui::TableSetupColumn("Vendor"); ImGui::TableSetupColumn("Beacon / services", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    int shown = 0;
    for (const auto& d : t.devices) {
        if (!passes(d)) continue;
        shown++;
        ImGui::TableNextRow();
        const double ago = std::max(0.0, t.timeSec - d.lastSec);
        const ImVec4 c = ago < 3 ? pal::okGreen() : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0); ImGui::TextColored(c, "%s", d.addr.c_str()); ImGui::PopFont();
        if (ImGui::IsItemHovered()) {
            std::string pdus;
            for (int i = 0; i < 8; i++) if (d.pduMask >> i & 1) { if (!pdus.empty()) pdus += ", "; pdus += dect2::ble::pduTypeName(i); }
            ImGui::SetTooltip("%s address\n%s%s%s\nchannels:%s%s%s\nfirst heard %s UTC%s%s", dect2::ble::addrKindName(d.kind), pdus.c_str(), d.connectable ? "\nconnectable" : "",
                              d.scanned ? "\nscanned by another device" : "", d.chanMask & 1 ? " 37" : "", d.chanMask & 2 ? " 38" : "", d.chanMask & 4 ? " 39" : "",
                              clock(d.firstWall).c_str(), d.mfrHex.empty() ? "" : "\nmanufacturer data ", d.mfrHex.c_str());
        }
        ImGui::TableNextColumn(); ImGui::TextUnformatted(kindShort(d.kind));
        ImGui::TableNextColumn(); ImGui::TextUnformatted(d.name.empty() ? "" : d.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%4.0f", d.rssi);
        if (d.rssiHist.size() >= 2) {
            ImGui::SameLine(0, 4 * gUi);
            ImGui::PlotLines("##h", d.rssiHist.data(), (int)d.rssiHist.size(), 0, nullptr, -110, -10, ImVec2(48 * gUi, ImGui::GetTextLineHeight()));
        }
        ImGui::TableNextColumn(); ImGui::Text(ago < 1 ? "now" : "%.0f s ago", ago);
        ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)d.packets);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(d.vendor.c_str());
        ImGui::TableNextColumn();
        std::string info = d.beacon;
        if (!d.services.empty()) { if (!info.empty()) info += "; "; info += d.services; }
        if (d.txPower != 127) { if (!info.empty()) info += "; "; info += "tx " + std::to_string(d.txPower) + " dBm"; }
        ImGui::TextUnformatted(info.c_str());
    }
    ImGui::EndTable();
    if (t.devices.empty()) ImGui::TextDisabled("nothing heard yet");
    else if (!shown) ImGui::TextDisabled("no device passes the filter");
}

void packetsView(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "start the receiver to see the packets"); return; }
    const auto& log = a.rx.ble.log;
    if (ImGui::Button("Copy")) {
        std::string txt;
        for (const auto& p : log) {
            char b[600];
            snprintf(b, sizeof b, "%s\t%d\t%s\t%s\t%.0f\t%s\n", clock(p.wallTime).c_str(), p.channel, dect2::ble::pduTypeName(p.type), p.addr.c_str(), p.rssi, p.info.c_str());
            txt += b;
        }
        ImGui::SetClipboardText(txt.c_str());
    }
    if (!ImGui::BeginTable("##blelog", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX,
                           ImVec2(0, ImGui::GetContentRegionAvail().y)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Time UTC"); ImGui::TableSetupColumn("Ch"); ImGui::TableSetupColumn("PDU"); ImGui::TableSetupColumn("Address");
    ImGui::TableSetupColumn("Level"); ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGuiListClipper clip;
    clip.Begin((int)log.size());
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
            const auto& p = log[(size_t)i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(clock(p.wallTime).c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d", p.channel);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(dect2::ble::pduTypeName(p.type));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s, %d bytes", dect2::ble::pduTypeText(p.type), p.len);
            ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(p.addr.c_str()); ImGui::PopFont();
            ImGui::TableNextColumn(); ImGui::Text("%.0f", p.rssi);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(p.info.c_str());
        }
    ImGui::EndTable();
    if (log.empty()) ImGui::TextDisabled("no packets yet");
}

void hopTo(App& a, int ch) {
    const double mhz = dect2::ble::channelMhz(ch);
    a.freqMhz = mhz;
    a.tune.centerHz = mhz * 1e6;
    a.engine.retune(a.tune);   // no reset: the device table stays
}

// ---------------------------------------------------------------- hooks

void tick(App& a) {
    loadState();
    if (!a.engine.running() || !S.hop || a.devices[a.devIdx].kind == DeviceInfo::File) { S.lastHop = ImGui::GetTime(); return; }
    if (ImGui::GetTime() - S.lastHop < S.hopSec) return;
    S.lastHop = ImGui::GetTime();
    const int cur = dect2::ble::advChannelOf(a.freqMhz);
    hopTo(a, cur == 37 ? 38 : cur == 38 ? 39 : 37);
}

void tuner(App& a, bool& retune) {
    loadState();
    ImGui::TextDisabled("Channel");
    const int cur = dect2::ble::advChannelOf(a.freqMhz);
    char label[32];
    if (cur) snprintf(label, sizeof label, "%d (%.0f MHz)", cur, dect2::ble::channelMhz(cur)); else snprintf(label, sizeof label, "other");
    ImGui::SameLine(0, 5 * gUi);
    ImGui::SetNextItemWidth(std::min(130 * gUi, ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##blech", label)) {
        for (int c = 37; c <= 39; c++) {
            char b[40];
            snprintf(b, sizeof b, "%d  %.0f MHz", c, dect2::ble::channelMhz(c));
            if (ImGui::Selectable(b, c == cur)) { tuneFreq(a, dect2::ble::channelMhz(c)); retune = true; }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The three advertising channels. Devices send each advert on all three in turn, so any one of them hears every device;\n"
                                                  "together they span 78 MHz, more than a radio takes in, so OnAir listens to one at a time (4 Msps).");
    ImGui::SameLine(0, 8 * gUi);
    if (ImGui::Checkbox("hop", &S.hop)) { plat::prefs().setB("bleHop", S.hop); plat::prefs().flush(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step through channels 37, 38 and 39 (some devices skip a channel, and interference differs between them)");
    if (S.hop) {
        ImGui::SameLine(0, 5 * gUi);
        ImGui::SetNextItemWidth(std::min(70 * gUi, ImGui::GetContentRegionAvail().x));
        if (ImGui::SliderInt("##blehs", &S.hopSec, 1, 30, "%d s")) { plat::prefs().setI("bleHopSec", S.hopSec); plat::prefs().flush(); }
    }
}

void tab(App& a) {
    subNav("blev", S.view, {"Devices", "Packets"});
    if (S.view == 0) devicesView(a); else packetsView(a);
}

void list(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const dect2::BleTelemetry& t = a.rx.ble;
    if (!ImGui::BeginTable("##blel", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("Level");
    for (const auto& d : t.devices) {
        if (!passes(d)) continue;
        ImGui::TableNextRow();
        const bool recent = t.timeSec - d.lastSec < 3;
        ImGui::TableNextColumn();
        if (!d.name.empty()) ImGui::TextColored(recent ? pal::okGreen() : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", d.name.c_str());
        else { ImGui::PushFont(a.mono, 0); ImGui::TextColored(recent ? pal::okGreen() : pal::grey(0.6f), "%s", d.addr.c_str()); ImGui::PopFont(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%s)\n%s", d.addr.c_str(), dect2::ble::addrKindName(d.kind), d.vendor.c_str());
        ImGui::TableNextColumn(); ImGui::TextDisabled("%.0f", d.rssi);
    }
    ImGui::EndTable();
}

void receiver(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const dect2::BleTelemetry& t = a.rx.ble;
    kvRow(a, "state", "%s", dect2::bleSummary(t).c_str());
    if (t.channel) kvRow(a, "channel", "%d (%.0f MHz)%s", t.channel, dect2::ble::channelMhz(t.channel), S.hop ? ", hopping" : "");
    kvRow(a, "signal", "eye SNR %.1f dB, deviation %.0f kHz, offset %+.1f kHz", t.snrDb, t.deviationHz / 1e3, t.cfoHz / 1e3);
    kvRow(a, "packets", "%llu good, %llu failed CRC, %llu access address hits", (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad, (unsigned long long)t.aaHits);
    kvRow(a, "by channel", "37: %llu, 38: %llu, 39: %llu", (unsigned long long)t.perChannel[0], (unsigned long long)t.perChannel[1], (unsigned long long)t.perChannel[2]);
    std::string types;
    for (int i = 0; i < 8; i++) if (t.perType[i]) { char b[64]; snprintf(b, sizeof b, "%s%s %llu", types.empty() ? "" : ", ", dect2::ble::pduTypeName(i), (unsigned long long)t.perType[i]); types += b; }
    kvRow(a, "PDU types", "%s", types.empty() ? "-" : types.c_str());
    kvRow(a, "input", "%.3f Msps, %.1f dBFS", t.inputRate / 1e6, t.levelDb);
    kvRow(a, "signal time", "%.1f s", t.timeSec);
    ImGui::Spacing();
    privacyNote();
}

void status(App& a) {
    const bool run = a.engine.running(), on = live(a);
    const SignalStats& st = a.spec.stats;
    const AdcStatus adc = classifyAdc(st.rmsDbfs, st.peak, st.clipFraction);
    StatusPanel panel;
    lamp("IQ", run ? (adc == AdcStatus::Overload ? 3 : (adc == AdcStatus::Good ? 1 : 2)) : 0, (int)Ic::Wave); flowNext(12 * gUi);
    lamp("Adverts", !on ? 0 : a.rx.ble.state >= 1 ? 1 : 0); flowNext(10 * gUi);
    ImGui::TextDisabled("|"); flowNext(10 * gUi);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State"); ImGui::SameLine(0, 5 * gUi);
    ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(!on ? (run ? "starting" : "stopped") : dect2::bleSummary(a.rx.ble).c_str()); ImGui::PopFont();
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "Bluetooth LE";
    if (live(a)) l2 = dect2::bleSummary(a.rx.ble);
}

} // namespace

extern const ModeUi kBleUi;
const ModeUi kBleUi = {
    .sideTitle = "DEVICES",
    .tabName = "Devices",
    .tabIcon = Ic::Signal,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .tuner = tuner,
    .tick = tick,
};
