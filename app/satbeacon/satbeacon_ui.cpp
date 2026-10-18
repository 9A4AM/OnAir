// Satellite beacon screens: the carrier (level, C/N0, offset, drift, the other narrow lines, the spectrum of the capture) and the telemetry (the subcarrier tones,
// the spectrum of the carrier's phase modulation, the bit rate, the frames recovered with the frame length and template learned from their repetition).
// A beacon is an unmodulated or phase-modulated carrier from a geostationary satellite; the frame layout of a given satellite is not decoded, only recovered as bits.
#include "app.h"
#include "dect2/satbeacon/satbeacon_live.h"
#include "dect2/satbeacon/satbeacon_tel.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct State {
    int view = 0;                 // 0 carrier, 1 telemetry
    bool logOn = true;
    int pushedLog = -1;
};
State S;

bool live(const App& a) { return a.rx.standard == 106; }

void kvRow(const App& a, const char* k, const char* fmt, ...) {
    ImGui::TextDisabled("%s", k); kvColumn(130 * gUi);
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    ImGui::PushFont(a.mono, 0); ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(b); ImGui::PopTextWrapPos(); ImGui::PopFont();
}

std::string logFile() { return plat::dataDir() + "/satellite_beacon.csv"; }

void pushSettings(App& a) {
    const int want = S.logOn ? 1 : 0;
    if (want != S.pushedLog) { a.engine.satbeacon().setLogFile(S.logOn ? logFile() : std::string()); S.pushedLog = want; }
}

void tick(App& a) { pushSettings(a); }

std::string hex(const std::vector<uint8_t>& v) {
    std::string s;
    char b[4];
    for (uint8_t x : v) { snprintf(b, sizeof b, "%02X", x); s += b; }
    return s;
}

// a line plot of dB values (0 = the floor) with the x axis labelled in kHz from x0 to x1
void plot(const char* id, const std::vector<float>& v, double x0Khz, double x1Khz, float h, ImU32 col) {
    const float w = std::max(80.f * gUi, ImGui::GetContentRegionAvail().x);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(14, 16, 20, 255), 3.f);
    if (v.size() < 2) return;
    float mx = 10;
    for (float f : v) mx = std::max(mx, f);
    mx = std::ceil(mx / 10.f) * 10.f;
    const float top = 4 * gUi, bot = 14 * gUi;
    for (int g = 10; g < (int)mx; g += 10) {
        const float y = p.y + h - bot - (h - top - bot) * (float)g / mx;
        dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), IM_COL32(40, 44, 52, 255));
    }
    ImVec2 prev;
    for (size_t i = 0; i < v.size(); i++) {
        const float x = p.x + w * (float)i / (float)(v.size() - 1);
        const float y = p.y + h - bot - (h - top - bot) * std::max(0.f, v[i]) / mx;
        if (i) dl->AddLine(prev, ImVec2(x, y), col, 1.2f);
        prev = ImVec2(x, y);
    }
    char s[32];
    for (int k = 0; k <= 4; k++) {
        snprintf(s, sizeof s, "%.0f", x0Khz + (x1Khz - x0Khz) * k / 4.0);
        dl->AddText(ImVec2(p.x + (w - 24 * gUi) * k / 4.f, p.y + h - bot + 1), IM_COL32(150, 156, 170, 255), s);
    }
    snprintf(s, sizeof s, "%.0f dB", (double)mx);
    dl->AddText(ImVec2(p.x + 3, p.y + 1), IM_COL32(150, 156, 170, 255), s);
}

void controls(App& a) {
    const dect2::SbTelemetry& t = a.rx.satbeacon;
    if (ImGui::Button(tr("Copy"))) {
        std::string txt;
        for (auto it = t.frames.rbegin(); it != t.frames.rend(); ++it) txt += hex(it->bytes) + "\n";
        ImGui::SetClipboardText(txt.c_str());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Copies the learned frames as hex lines."));
    ImGui::SameLine();
    ImGui::Checkbox(tr("Log to file"), &S.logOn);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Appends the carrier measurements and every frame to satellite_beacon.csv in the data folder, also when nobody watches the screen."));
}

void carrierView(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? tr("starting") : tr("start the receiver to see the beacon")); return; }
    const dect2::SbTelemetry& t = a.rx.satbeacon;
    controls(a);
    if (t.state == 0) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(pal::warnAmber(), "%s", tr("No narrow carrier found. Tune so that the beacon lies inside the capture: a Ku-band beacon seen through an LNB is usually within a few hundred kHz of the centre."));
        ImGui::PopTextWrapPos();
    } else {
        const double mhz = t.centreHz > 0 ? (t.centreHz + t.cfoHz) / 1e6 : 0.0;
        if (mhz > 0) kvRow(a, tr("carrier"), "%.6f MHz (%+.1f Hz from the centre)", mhz, t.cfoHz);
        else kvRow(a, tr("carrier"), "%+.1f Hz from the centre", t.cfoHz);
        kvRow(a, tr("level"), "%.1f dBFS, C/N0 %.1f dB-Hz", (double)t.levelDb, (double)t.snrDb);
        kvRow(a, tr("Drift"), "%+.1f Hz per minute", t.driftHzPerMin);
        kvRow(a, tr("noise"), "%.1f dBFS in 1 Hz", (double)t.noiseDb);
        if (t.lineSpacingHz > 0) kvRow(a, tr("other lines"), "second carrier %.3f kHz away", t.lineSpacingHz / 1e3);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("capture spectrum (dB over the noise): the narrow lines are carriers"));
    plot("##sbrf", t.rfSpectrum, -t.inputRate / 2e3, t.inputRate / 2e3, std::max(100.f * gUi, ImGui::GetContentRegionAvail().y * 0.45f), IM_COL32(120, 170, 240, 255));
    if (ImGui::BeginTable("##sblines", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn(tr("lines")); ImGui::TableSetupColumn(tr("offset")); ImGui::TableSetupColumn(tr("level")); ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        ImGui::PushFont(a.mono, 0);
        for (const auto& l : t.lines) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%zu", (size_t)(&l - &t.lines[0]) + 1);
            ImGui::TableNextColumn(); ImGui::Text("%+.1f Hz", l.offsetHz);
            ImGui::TableNextColumn(); ImGui::Text("%.1f dB over the noise, %+.1f dB", (double)l.levelDb, (double)l.relDb);
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", l.side ? "modulation" : "");
        }
        ImGui::PopFont();
        ImGui::EndTable();
    }
}

void telemetryView(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? tr("starting") : tr("start the receiver to see the beacon")); return; }
    const dect2::SbTelemetry& t = a.rx.satbeacon;
    controls(a);
    if (t.state == 0) { ImGui::TextDisabled("%s", tr("not found")); return; }
    if (t.tones == 0) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(pal::warnAmber(), "%s", tr("No telemetry: the carrier is not phase modulated, or the tones are too weak."));
        ImGui::PopTextWrapPos();
    } else {
        if (t.tones > 1) kvRow(a, tr("subcarrier"), "%.3f kHz, two tones %.2f kHz apart, %.1f dB over the floor", t.subcarrierHz / 1e3, t.toneSpacingHz / 1e3, (double)t.subcarrierDb);
        else kvRow(a, tr("subcarrier"), "%.3f kHz, one tone, %.1f dB over the floor", t.subcarrierHz / 1e3, (double)t.subcarrierDb);
        kvRow(a, tr("bit rate"), "%.2f bit/s, %s, clock %s", t.baud, t.coding ? "bi-phase" : "NRZ", t.clockLocked ? "locked" : "searching");
        if (t.frameBits > 0) {
            kvRow(a, tr("Frame"), "%d bits (%d bytes), %s, %.0f %% of the bits repeat from frame to frame, %d never change", t.frameBits, t.frameBits / 8, t.syncName.empty() ? "no known sync word, the start is learned" : (std::string("sync ") + t.syncName).c_str(), (double)t.frameAgreement * 100, t.fixedBits);
            if (t.counterByte >= 0) {
                if (t.counterMod > 0) kvRow(a, tr("counter"), "byte %d counts up to %d: a major frame of %d minor frames", t.counterByte, t.counterMod - 1, t.counterMod);
                else kvRow(a, tr("counter"), "byte %d counts up", t.counterByte);
            }
            kvRow(a, tr("frames"), "%llu good, %llu with many errors, %llu bits received", (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad, (unsigned long long)t.bitsTotal);
            kvRow(a, tr("template"), "%s", hex(t.templateBytes).c_str());
            kvRow(a, tr("fixed bits"), "%s", hex(t.fixedMask).c_str());
        } else kvRow(a, tr("frames"), "%llu bits received", (unsigned long long)t.bitsTotal);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("phase modulation of the carrier (dB over the floor): the telemetry tones stand out"));
    plot("##sbpm", t.pmSpectrum, 0, t.pmSpanHz / 1e3, std::max(90.f * gUi, ImGui::GetContentRegionAvail().y * 0.25f), IM_COL32(240, 190, 80, 255));
    if (t.frames.empty()) return;
    if (ImGui::BeginTable("##sbframes", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX, ImVec2(0, std::max(100.f * gUi, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr("Frame")); ImGui::TableSetupColumn(tr("Errors")); ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        ImGui::PushFont(a.mono, 0);
        for (const auto& f : t.frames) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); if (f.counter >= 0) ImGui::Text("%u  (%d)", f.id, f.counter); else ImGui::Text("%u", f.id);
            ImGui::TableNextColumn(); if (f.good) ImGui::Text("%d", f.fixedErrors); else ImGui::TextColored(pal::warnAmber(), "%d", f.fixedErrors);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(hex(f.bytes).c_str());
        }
        ImGui::PopFont();
        ImGui::EndTable();
    }
}

void tab(App& a) {
    subNav("sbc", S.view, {tr("Carrier"), tr("Telemetry")});
    if (S.view == 0) carrierView(a);
    else telemetryView(a);
}

void list(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? tr("starting") : tr("stopped")); return; }
    const dect2::SbTelemetry& t = a.rx.satbeacon;
    ImGui::Text("%llu %s", (unsigned long long)t.framesTotal, tr("frames"));
    ImGui::PushFont(a.mono, 0);
    int n = 0;
    for (const auto& f : t.frames) {
        if (n++ >= 10) break;
        std::string h = hex(f.bytes);
        ImGui::TextUnformatted(h.substr(0, std::min<size_t>(h.size(), 16)).c_str());
    }
    ImGui::PopFont();
}

void receiver(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? tr("starting") : tr("stopped")); return; }
    const dect2::SbTelemetry& t = a.rx.satbeacon;
    kvRow(a, tr("state"), "%s", dect2::sbSummary(t).c_str());
    if (t.state == 0) kvRow(a, tr("beacon"), "%s", tr("not found"));
    else kvRow(a, tr("beacon"), "%s, carrier phase modulation %.0f deg rms", t.carrierLocked ? "locked" : "searching", (double)t.pmRmsDeg);
    kvRow(a, tr("input"), "%.3f Msps, carrier loop at %.0f Hz", t.inputRate / 1e6, t.workRate);
    kvRow(a, tr("log"), "%s", t.logging ? t.logPath.c_str() : tr("off"));
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", tr("A satellite beacon is a carrier, sometimes phase modulated with telemetry, that ground stations use to measure the link. The carrier is followed in frequency and level; the telemetry is recovered as raw frames and the frame length is learned from their repetition. The layout of a given satellite's frames is not decoded."));
    ImGui::PopTextWrapPos();
}

void status(App& a) {
    const bool run = a.engine.running(), on = live(a);
    const SignalStats& st = a.spec.stats;
    const AdcStatus adc = classifyAdc(st.rmsDbfs, st.peak, st.clipFraction);
    lamp("IQ", run ? (adc == AdcStatus::Overload ? 3 : (adc == AdcStatus::Good ? 1 : 2)) : 0, (int)Ic::Wave); flowNext(12 * gUi);
    lamp(tr("Carrier"), !on ? 0 : a.rx.satbeacon.carrierLocked ? 1 : 2); flowNext(10 * gUi);
    lamp(tr("Telemetry"), !on ? 0 : a.rx.satbeacon.state >= 3 ? 1 : (a.rx.satbeacon.state == 2 ? 2 : 0)); flowNext(10 * gUi);
    ImGui::TextDisabled("|"); flowNext(10 * gUi);
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(!on ? (run ? tr("starting") : tr("stopped")) : dect2::sbSummary(a.rx.satbeacon).c_str()); ImGui::PopFont();
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "Satellite beacon";
    if (live(a)) l2 = dect2::sbSummary(a.rx.satbeacon);
}

void synth(App& a, bool& changed) {
    static const char* names[] = {"Carrier, reference carrier and telemetry", "Carrier only", "Telemetry without the reference carrier"};
    int& o = a.tune.synth.modeOpt[0];
    if (o < 0 || o > 2) o = 0;
    ImGui::SetNextItemWidth(std::min(330 * gUi, ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##sbsynth", names[o])) {
        for (int i = 0; i < 3; i++) if (ImGui::Selectable(names[i], i == o)) { o = i; changed = true; }
        ImGui::EndCombo();
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", tr("The test signal is a carrier 236 kHz off the centre, drifting like an LNB, with a second carrier 545 kHz above it and 4800 bit/s bi-phase telemetry on a 48 kHz subcarrier, in frames of 256 bytes."));
    ImGui::PopTextWrapPos();
}

} // namespace

extern const ModeUi kSatbeaconUi;
const ModeUi kSatbeaconUi = {
    .sideTitle = "FRAMES",
    .tabName = "Beacon",
    .tabIcon = Ic::Signal,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .synth = synth,
    .tick = tick,
};
