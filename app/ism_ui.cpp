// ISM sensors screens: the device table (sortable, with a history sparkline per device), the live log, the unknown signals with a pulse
// viewer for reporting them, band presets and the privacy note.
#include "app.h"
#include "dect2/ism_rx.h"
#include "dect2/ism_tel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace {

struct State {
    int view = 0;                // 0 devices, 1 log, 2 unknown signals, 3 supported devices
    int kind = 0;                // filter: 0 all, then kKinds
    int sel = -1;                // the selected unknown signal (index in raws, newest first)
    double selSec = -1;          // its time, so the selection stays on it as new ones arrive
    bool wasRunning = false;
};
State S;

const char* const kKinds[] = {"All", "Weather", "TPMS", "Remote", "Energy", "Meter", "Sensor"};

bool live(const App& a) { return a.engine.running() && a.rx.standard == 29; }   // the engine reports its standard code minus one

std::string hhmmss(int64_t t) {
    if (t <= 0) return "-";
    const time_t tt = (time_t)t;
    struct tm g;
#ifdef _WIN32
    localtime_s(&g, &tt);
#else
    localtime_r(&tt, &g);
#endif
    char b[16];
    strftime(b, sizeof b, "%H:%M:%S", &g);
    return b;
}

std::string ago(int64_t t) {
    if (t <= 0) return "-";
    const long d = (long)(std::time(nullptr) - t);
    char b[32];
    if (d < 60) snprintf(b, sizeof b, "%ld s", std::max(0L, d));
    else if (d < 3600) snprintf(b, sizeof b, "%ld min", d / 60);
    else snprintf(b, sizeof b, "%ld h", d / 3600);
    return b;
}

std::string freqText(const dect2::IsmTelemetry& t, double hz) {
    char b[32];
    if (t.centerHz > 0) snprintf(b, sizeof b, "%.3f MHz", hz / 1e6);
    else snprintf(b, sizeof b, "%+.1f kHz", hz / 1e3);   // a file: from the dial
    return b;
}

void presets(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Band");
    struct P { const char* name; double mhz; const char* tip; };
    static const P ps[] = {{"433.92", 433.92, "433.92 MHz: weather stations, thermometers, remotes, doorbells, TPMS (Europe, Asia)"},
                           {"868.3", 868.3, "868.3 MHz: Bresser, LaCrosse IT+, Fine Offset / Ecowitt (Europe); 868.95 MHz wireless M-Bus fits in the same band"},
                           {"868.95", 868.95, "868.95 MHz: wireless M-Bus T and C mode water, heat and gas meters"},
                           {"915", 915.0, "915 MHz: weather stations and sensors in the Americas and Australia"}};
    for (const auto& p : ps) {
        flowNext(6 * gUi);
        const bool on = std::fabs(a.freqMhz - p.mhz) < 0.0005;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, pal::accent(0.45f));
        if (ImGui::SmallButton(p.name)) tuneFreq(a, p.mhz);
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tip);
    }
    ImGui::SetNextItemWidth(110 * gUi);
    ImGui::Combo("##ismkind", &S.kind, kKinds, IM_ARRAYSIZE(kKinds));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show only one kind of device.");
    ImGui::SameLine(0, 10 * gUi);
    if (ImGui::SmallButton("Clear")) a.engine.ism().clearDevices();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forget the devices, the log and the unknown signals.");
}

void privacyNote() {
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Privacy: these are other people's devices. Only what they broadcast openly is shown (model, the ID it sends, its "
                        "readings); nothing is looked up, combined or sent anywhere, and meter data is not decrypted. Tyre sensor IDs can "
                        "follow a car: do not record or share them.");
    ImGui::PopTextWrapPos();
}

bool kindShown(const std::string& k) { return S.kind == 0 || k == kKinds[S.kind]; }

void sparkline(const dect2::IsmDevice& d, float w, float h) {
    if (d.hist.size() < 2) { ImGui::Dummy(ImVec2(w, h)); return; }
    float lo = *std::min_element(d.hist.begin(), d.hist.end()), hi = *std::max_element(d.hist.begin(), d.hist.end());
    if (hi - lo < 1e-3f) { lo -= 0.5f; hi += 0.5f; }
    ImGui::PushStyleColor(ImGuiCol_PlotLines, pal::accent());
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PlotLines("##sp", d.hist.data(), (int)d.hist.size(), 0, nullptr, lo, hi, ImVec2(w, h));
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %zu readings, %.1f to %.1f %s", d.histName.c_str(), d.hist.size(), lo, hi, d.histUnit.c_str());
}

void deviceView(App& a) {
    const dect2::IsmTelemetry& t = a.rx.ism;
    std::vector<const dect2::IsmDevice*> v;
    for (const auto& d : t.devices) if (kindShown(d.kind)) v.push_back(&d);
    const ImGuiTableFlags fl = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit |
                               ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("##ismdev", 10, fl, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 165 * gUi);
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Ch");
    ImGui::TableSetupColumn("Batt");
    ImGui::TableSetupColumn("Readings", ImGuiTableColumnFlags_NoSort, 300 * gUi);
    ImGui::TableSetupColumn("History", ImGuiTableColumnFlags_NoSort, 90 * gUi);
    ImGui::TableSetupColumn("RSSI");
    ImGui::TableSetupColumn("Last seen", ImGuiTableColumnFlags_PreferSortDescending);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_PreferSortDescending);
    ImGui::TableSetupColumn("Frequency");
    ImGui::TableHeadersRow();
    if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
        if (ss->SpecsCount > 0) {
            const int col = ss->Specs[0].ColumnIndex;
            const bool asc = ss->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            std::stable_sort(v.begin(), v.end(), [&](const dect2::IsmDevice* x, const dect2::IsmDevice* y) {
                int c = 0;
                switch (col) {
                case 0: c = x->model.compare(y->model); break;
                case 1: c = x->id.compare(y->id); break;
                case 2: c = (x->channel - y->channel) + (x->channelName.compare(y->channelName)); break;
                case 3: c = x->battery - y->battery; break;
                case 6: c = x->rssiDb < y->rssiDb ? -1 : x->rssiDb > y->rssiDb; break;
                case 7: c = x->lastSec < y->lastSec ? -1 : x->lastSec > y->lastSec; break;
                case 8: c = x->count < y->count ? -1 : x->count > y->count; break;
                case 9: c = x->freqHz < y->freqHz ? -1 : x->freqHz > y->freqHz; break;
                default: break;
                }
                return asc ? c < 0 : c > 0;
            });
        }
    }
    for (size_t i = 0; i < v.size(); i++) {
        const dect2::IsmDevice& d = *v[i];
        ImGui::PushID((int)i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(d.model.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s, %s, checked by %s", d.kind.c_str(), d.fsk ? "FSK" : "OOK", d.mic.c_str());
        ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(d.id.c_str()); ImGui::PopFont();
        ImGui::TableNextColumn();
        if (!d.channelName.empty()) ImGui::TextUnformatted(d.channelName.c_str());
        else if (d.channel >= 0) ImGui::Text("%d", d.channel);
        else ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        if (d.battery == 1) ImGui::TextColored(pal::okGreen(), "ok");
        else if (d.battery == 0) ImGui::TextColored(pal::warnAmber(), "low");
        else ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        const std::string r = dect2::ismReadingText(d.fields);
        ImGui::TextUnformatted(r.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.c_str());
        ImGui::TableNextColumn();
        sparkline(d, 86 * gUi, ImGui::GetTextLineHeight());
        ImGui::TableNextColumn(); ImGui::Text("%.0f dB", d.rssiDb);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("burst power %.1f dBFS, %.1f dB over the noise", d.rssiDb, d.snrDb);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(ago(d.lastWall).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("last %s, first %s", hhmmss(d.lastWall).c_str(), hhmmss(d.firstWall).c_str());
        ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)d.count);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(freqText(t, d.freqHz).c_str());
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void logView(App& a) {
    const dect2::IsmTelemetry& t = a.rx.ism;
    if (ImGui::SmallButton("Copy")) {
        std::string txt;
        for (const auto& e : t.events) {
            char b[512];
            snprintf(b, sizeof b, "%s\t%s\t%s\t%s\t%s\t%.0f dB\n", hhmmss(e.wallTime).c_str(), e.unknown ? "unknown" : e.model.c_str(), e.id.c_str(),
                     freqText(t, e.freqHz).c_str(), e.text.c_str(), e.rssiDb);
            txt += b;
        }
        ImGui::SetClipboardText(txt.c_str());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy the log, one packet per line.");
    if (!ImGui::BeginTable("##ismlog", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV,
                           ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Time"); ImGui::TableSetupColumn("Device"); ImGui::TableSetupColumn("ID"); ImGui::TableSetupColumn("Frequency");
    ImGui::TableSetupColumn("Reading", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (const auto& e : t.events) {
        if (!e.unknown) {
            bool shown = S.kind == 0;
            for (const auto& d : t.devices) if (d.model == e.model && d.id == e.id) { shown = kindShown(d.kind); break; }
            if (!shown) continue;
        } else if (S.kind != 0) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(hhmmss(e.wallTime).c_str());
        ImGui::TableNextColumn();
        if (e.unknown) ImGui::TextColored(pal::warnAmber(), "unknown"); else ImGui::TextUnformatted(e.model.c_str());
        ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(e.id.empty() ? "-" : e.id.c_str()); ImGui::PopFont();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(freqText(t, e.freqHz).c_str());
        ImGui::TableNextColumn(); ImGui::TextUnformatted(e.text.c_str());
    }
    ImGui::EndTable();
    if (t.events.empty()) ImGui::TextDisabled("nothing heard yet");
}

// the pulses of the selected unknown signal: a trace, its timings and a line to copy for a report
void pulseViewer(App& a, const dect2::IsmRaw& r) {
    const dect2::IsmTelemetry& t = a.rx.ism;
    ImGui::Text("%s, %s %s, %zu pulses, at %s, %.0f dB over the noise", hhmmss(r.wallTime).c_str(), r.fsk ? "FSK" : "OOK", r.guess.code.c_str(),
                r.pulses.size(), freqText(t, r.freqHz).c_str(), r.snrDb);
    if (r.guess.shortUs > 0) { ImGui::SameLine(); ImGui::TextDisabled("short %.0f us, long %.0f us%s", r.guess.shortUs, r.guess.longUs, r.fsk ? "" : ""); }
    if (r.fsk) { ImGui::SameLine(); ImGui::TextDisabled("deviation +-%.0f kHz", r.fskDevHz / 1e3); }
    if (ImGui::SmallButton("Copy pulses")) {
        std::string s;
        char b[64];
        snprintf(b, sizeof b, "%s %s %.0f us/%.0f us;", r.fsk ? "FSK" : "OOK", r.guess.code.c_str(), r.guess.shortUs, r.guess.longUs);
        s += b;
        for (const auto& p : r.pulses) { snprintf(b, sizeof b, " %.0f/%.0f", p.pulseUs, p.gapUs); s += b; }
        ImGui::SetClipboardText(s.c_str());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy the line code and every pulse / gap in microseconds, to report the device.");
    // the trace: the first 60 ms or so, scaled to the width
    const float W = ImGui::GetContentRegionAvail().x, H = 60 * gUi;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, ImVec2(o.x + W, o.y + H), IM_COL32(0, 0, 0, 40));
    double total = 0;
    for (const auto& p : r.pulses) total += p.pulseUs + std::min(p.gapUs, 20000.f);
    const double span = std::max(1000.0, std::min(total, 60000.0)), sx = (W - 4) / span;
    double x = 0;
    const float yh = o.y + 8 * gUi, yl = o.y + H - 8 * gUi;
    const ImU32 c = ImGui::GetColorU32(pal::accent());
    for (const auto& p : r.pulses) {
        if (x > span) break;
        const float x0 = o.x + 2 + (float)(x * sx), x1 = o.x + 2 + (float)(std::min(span, x + p.pulseUs) * sx);
        x += p.pulseUs;
        const float x2 = o.x + 2 + (float)(std::min(span, x + std::min(p.gapUs, 20000.f)) * sx);
        x += std::min(p.gapUs, 20000.f);
        dl->AddLine(ImVec2(x0, yl), ImVec2(x0, yh), c);
        dl->AddLine(ImVec2(x0, yh), ImVec2(x1, yh), c);
        dl->AddLine(ImVec2(x1, yh), ImVec2(x1, yl), c);
        dl->AddLine(ImVec2(x1, yl), ImVec2(x2, yl), c);
    }
    ImGui::Dummy(ImVec2(W, H));
    ImGui::TextDisabled("%.1f ms shown of %.1f ms", span / 1000, total / 1000);
    // the timings
    if (ImGui::BeginChild("##isml", ImVec2(0, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders)) {
        ImGui::PushFont(a.mono, 0);
        std::string line;
        for (size_t i = 0; i < r.pulses.size(); i++) {
            char b[40];
            snprintf(b, sizeof b, "%6.0f/%-6.0f ", r.pulses[i].pulseUs, r.pulses[i].gapUs);
            line += b;
            if (i % 8 == 7) { ImGui::TextUnformatted(line.c_str()); line.clear(); }
        }
        if (!line.empty()) ImGui::TextUnformatted(line.c_str());
        ImGui::PopFont();
    }
    ImGui::EndChild();
}

void rawView(App& a) {
    const dect2::IsmTelemetry& t = a.rx.ism;
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Bursts no protocol accepted. The line code is a guess from the pulse and gap lengths (PWM, PPM, Manchester, NRZ); copy a "
                        "signal's pulses to report a device that is not decoded yet.");
    ImGui::PopTextWrapPos();
    if (t.raws.empty()) { ImGui::TextDisabled("none yet"); return; }
    if (S.selSec >= 0) { S.sel = -1; for (size_t i = 0; i < t.raws.size(); i++) if (t.raws[i].sec == S.selSec) { S.sel = (int)i; break; } }
    const float lw = std::min(320 * gUi, ImGui::GetContentRegionAvail().x * 0.4f);
    if (ImGui::BeginChild("##ismraws", ImVec2(lw, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_Borders)) {
        for (size_t i = 0; i < t.raws.size(); i++) {
            const auto& r = t.raws[i];
            char b[160];
            snprintf(b, sizeof b, "%s  %s %s  %zu p  %s##r%zu", hhmmss(r.wallTime).c_str(), r.fsk ? "FSK" : "OOK", r.guess.code.c_str(), r.pulses.size(),
                     freqText(t, r.freqHz).c_str(), i);
            if (ImGui::Selectable(b, S.sel == (int)i)) { S.sel = (int)i; S.selSec = r.sec; }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##ismpv", ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        if (S.sel >= 0 && S.sel < (int)t.raws.size()) pulseViewer(a, t.raws[S.sel]);
        else ImGui::TextDisabled("select a signal on the left");
    }
    ImGui::EndChild();
}

void supportedView() {
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("The formats follow rtl_433 (GPL-2.0); each decoder names the rtl_433 device file it was written from. A reading is "
                        "shown only when its CRC or checksum passes (or, for the few without one, when the frame repeats).");
    ImGui::PopTextWrapPos();
    if (!ImGui::BeginTable("##ismsup", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV,
                           ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Device"); ImGui::TableSetupColumn("Kind"); ImGui::TableSetupColumn("Modulation"); ImGui::TableSetupColumn("rtl_433 file", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (const auto& p : dect2::ismProtocols()) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(p.name);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(p.kind);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(p.fsk ? "FSK" : "OOK");
        ImGui::TableNextColumn(); ImGui::TextDisabled("src/devices/%s", p.file);
    }
    ImGui::EndTable();
}

// ---------------------------------------------------------------- hooks

void tick(App& a) {
    if (!a.engine.running()) { S.wasRunning = false; return; }
    S.wasRunning = true;
}

void tab(App& a) {
    presets(a);
    privacyNote();
    subNav("ismv", S.view, {"Devices", "Log", "Unknown", "Supported"});
    if (S.view == 3) { supportedView(); return; }
    if (!live(a)) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "start the receiver to hear the devices around you"); ImGui::PopTextWrapPos(); return; }
    if (S.view == 0) deviceView(a); else if (S.view == 1) logView(a); else rawView(a);
}

void list(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const dect2::IsmTelemetry& t = a.rx.ism;
    if (!ImGui::BeginTable("##ismside", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupColumn("Device"); ImGui::TableSetupColumn("Seen", ImGuiTableColumnFlags_WidthStretch);
    for (const auto& d : t.devices) {
        if (!kindShown(d.kind)) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(d.model.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("ID %s: %s", d.id.c_str(), dect2::ismReadingText(d.fields).c_str());
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", ago(d.lastWall).c_str());
    }
    ImGui::EndTable();
    if (t.devices.empty()) ImGui::TextDisabled("none yet");
}

void receiver(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const dect2::IsmTelemetry& t = a.rx.ism;
    auto kv = [&](const char* k, const char* fmt, auto... v) { ImGui::TextDisabled("%s", k); kvColumn(130 * gUi); ImGui::PushFont(a.mono, 0); ImGui::PushTextWrapPos(0); ImGui::Text(fmt, v...); ImGui::PopTextWrapPos(); ImGui::PopFont(); };
    kv("state", "%s", dect2::ismSummary(t).c_str());
    kv("bursts", "%llu detected, %llu decoded packets, %llu unknown", (unsigned long long)t.bursts, (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad);
    kv("last packet", "%.1f dB over the noise", t.snrDb);
    kv("noise floor", "%.1f dBFS per bin", t.noiseDb);
    if (t.centerHz > 0) kv("band", "%.3f to %.3f MHz", (t.centerHz - t.inputRate / 2) / 1e6, (t.centerHz + t.inputRate / 2) / 1e6);
    kv("input", "%.3f Msps, %.1f dBFS", t.inputRate / 1e6, t.levelDb);
    kv("signal time", "%.1f s", t.timeSec);
}

void status(App& a) {
    const bool run = a.engine.running(), on = live(a);
    const SignalStats& st = a.spec.stats;
    const AdcStatus adc = classifyAdc(st.rmsDbfs, st.peak, st.clipFraction);
    StatusPanel panel;
    lamp("IQ", run ? (adc == AdcStatus::Overload ? 3 : (adc == AdcStatus::Good ? 1 : 2)) : 0, (int)Ic::Wave); flowNext(12 * gUi);
    lamp("Heard", !on ? 0 : a.rx.ism.dataValid ? 1 : 0); flowNext(10 * gUi);
    ImGui::TextDisabled("|"); flowNext(10 * gUi);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State"); ImGui::SameLine(0, 5 * gUi);
    ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(!on ? (run ? "starting" : "stopped") : dect2::ismSummary(a.rx.ism).c_str()); ImGui::PopFont();
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "ISM sensors";
    if (live(a)) l2 = dect2::ismSummary(a.rx.ism);
}

} // namespace

extern const ModeUi kIsmUi;
const ModeUi kIsmUi = {
    .sideTitle = "DEVICES",
    .tabName = "Sensors",
    .tabIcon = Ic::Radio,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .tick = tick,
};
