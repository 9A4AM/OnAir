// P25 Phase 1 screens: the current call (unit ID, talkgroup, emergency, encryption, voice), the call log (calls, voice grants, data
// packets), the trunking system (site, adjacent sites, channel identifiers), the talkgroups with their activity, the decoded control
// messages, and a "?" about the mbelib library the voice needs (docs/CODECS.md). Receive only; encrypted calls are only reported.
#include "app.h"
#include "dect2/p25_imbe.h"
#include "dect2/p25_proto.h"
#include "dect2/p25_rx.h"
#include "dect2/p25_tel.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace {

struct State {
    int view = 0;                        // 0 now, 1 calls, 2 trunking, 3 talkgroups, 4 messages
    bool wasRunning = false;
    float pushedVol = -1; bool pushedMute = false;
};
State S;

bool live(const App& a) { return a.engine.running() && a.rx.standard == 51; }   // the engine reports its standard code minus one

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

std::string dstOf(const dect2::P25Call& c) { return (c.group ? "TG " : "") + dect2::p25Id(c.dst); }
const char* kindOf(const dect2::P25Call& c) { return c.kind == 1 ? "grant" : c.kind == 2 ? "data" : "voice"; }
std::string mhz(double hz) { if (hz <= 0) return "-"; char b[32]; snprintf(b, sizeof b, "%.5f MHz", hz / 1e6); return b; }
std::string cryptoOf(const dect2::P25Call& c) {
    char b[96];
    if (c.algId != dect2::p25::kAlgClear) snprintf(b, sizeof b, "encrypted, %s (0x%02X), key %d", dect2::p25::algName(c.algId), c.algId, c.keyId);
    else snprintf(b, sizeof b, "encrypted");
    return b;
}

void kvRow(const App& a, const char* k, const char* fmt, ...) {
    ImGui::TextDisabled("%s", k); kvColumn(130 * gUi);
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    ImGui::PushFont(a.mono, 0); ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(b); ImGui::PopTextWrapPos(); ImGui::PopFont();
}

void voiceLine() {
    ImGui::AlignTextToFramePadding();
    const bool mbe = dect2::p25ImbeAvailable();
    ImGui::TextColored(mbe ? pal::okGreen() : pal::warnAmber(), "%s", mbe ? "voice: mbelib" : "voice: needs mbelib");
    ImGui::SameLine(0, 4 * gUi);
    codecHelpButton(CodecHelp::P25Imbe);
    ImGui::SameLine(0, 10 * gUi);
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("OnAir only receives. It shows what P25 radios send in the clear: talkgroups, unit IDs, the trunking system and,\n"
                          "with your own mbelib, clear voice. Encrypted calls are only reported as encrypted; nothing is decrypted.\n"
                          "Listening to radio services is regulated in many countries: follow the rules where you live.");
}

void nowView(App& a) {
    voiceLine();
    ImGui::Spacing();
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "start the receiver to hear P25"); return; }
    const dect2::P25Telemetry& t = a.rx.p25;
    const dect2::P25Call& n = t.now;
    if (n.wallTime == 0) {
        ImGui::TextDisabled("%s", t.control ? "a control channel: see Calls and Trunking for the voice grants" : "no call heard yet");
        return;
    }
    ImGui::PushFont(a.mono, 0);
    ImGui::TextColored(n.active ? pal::okGreen() : pal::grey(0.6f), "%s  %s  ->  %s", n.active ? "ON AIR" : "last", dect2::p25Id(n.src).c_str(), dstOf(n).c_str());
    ImGui::PopFont();
    kvRow(a, "call", "%s, NAC %03X%s", n.group ? "group" : "unit to unit", n.nac < 0 ? 0 : n.nac, n.lateEntry ? ", joined late" : "");
    if (n.emergency) { ImGui::TextDisabled("note"); kvColumn(130 * gUi); ImGui::TextColored(pal::warnAmber(), "EMERGENCY"); }
    if (n.encrypted) { ImGui::TextDisabled("encryption"); kvColumn(130 * gUi); ImGui::TextColored(pal::warnAmber(), "%s: no sound (not decrypted)", cryptoOf(n).c_str()); }
    kvRow(a, "duration", "%.1f s, %u voice frames", n.durSec, n.voiceFrames);
    kvRow(a, "started", "%s UTC", clock(n.wallTime).c_str());
    kvRow(a, "voice", "%s", n.encrypted ? "-" : n.voice ? "playing (IMBE through mbelib)" : t.imbe ? "-" : "no mbelib: frames counted only");
}

void callsView(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "start the receiver to see the calls"); return; }
    const auto& log = a.rx.p25.log;
    if (ImGui::Button("Copy")) {
        std::string txt;
        for (const auto& e : log) {
            char b[400];
            snprintf(b, sizeof b, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%.1f s\n", clock(e.wallTime).c_str(), kindOf(e), dect2::p25Id(e.src).c_str(), dstOf(e).c_str(),
                     e.kind == 1 ? (dect2::p25::channelText(e.channel) + " " + mhz(e.freqHz)).c_str() : "", e.encrypted ? cryptoOf(e).c_str() : "clear", e.text.c_str(), e.durSec);
            txt += b;
        }
        ImGui::SetClipboardText(txt.c_str());
    }
    if (!ImGui::BeginTable("##p25log", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Time UTC"); ImGui::TableSetupColumn("Kind"); ImGui::TableSetupColumn("From"); ImGui::TableSetupColumn("To");
    ImGui::TableSetupColumn("Channel"); ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("Length");
    ImGui::TableHeadersRow();
    auto row = [&](const dect2::P25Call& e, bool on) {
        ImGui::TableNextRow();
        const ImVec4 c = on ? pal::okGreen() : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::TableNextColumn(); ImGui::TextColored(c, "%s", on ? "on air" : clock(e.wallTime).c_str());
        ImGui::TableNextColumn(); ImGui::TextUnformatted(kindOf(e));
        ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0); ImGui::TextColored(c, "%s", dect2::p25Id(e.src).c_str()); ImGui::PopFont();
        ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(dstOf(e).c_str()); ImGui::PopFont();
        ImGui::TableNextColumn();
        if (e.kind == 1) ImGui::Text("%s  %s", dect2::p25::channelText(e.channel).c_str(), mhz(e.freqHz).c_str()); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        if (e.emergency) { ImGui::TextColored(pal::warnAmber(), "EMERGENCY"); ImGui::SameLine(); }
        if (e.encrypted) { ImGui::TextColored(pal::warnAmber(), "%s", cryptoOf(e).c_str()); ImGui::SameLine(); }
        ImGui::TextUnformatted(e.kind == 0 ? (e.voice ? "voice played" : "") : e.text.c_str());
        ImGui::TableNextColumn(); ImGui::Text("%.1f s", e.durSec);
        if (ImGui::IsItemHovered() && e.kind == 0)
            ImGui::SetTooltip("NAC %03X, %u voice frames, %s%s", e.nac < 0 ? 0 : e.nac, e.voiceFrames, e.endSeen ? "ended with a terminator" : "signal lost", e.lateEntry ? ", joined late" : "");
    };
    if (a.rx.p25.now.active) row(a.rx.p25.now, true);
    for (const auto& e : log) row(e, false);
    ImGui::EndTable();
    if (log.empty() && !a.rx.p25.now.active) ImGui::TextDisabled("nothing heard yet");
}

void trunkingView(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const dect2::P25Telemetry& t = a.rx.p25;
    const dect2::P25Site& s = t.site;
    if (!s.valid) ImGui::TextDisabled("no system information (a trunked control channel sends it; conventional channels do not)");
    else {
        kvRow(a, "WACN / system", "%05X / %03X", s.wacn < 0 ? 0 : s.wacn, s.sysId < 0 ? 0 : s.sysId);
        kvRow(a, "RFSS / site", "%d / %d", s.rfss, s.site);
        kvRow(a, "NAC", "%03X", s.nac < 0 ? 0 : s.nac);
        kvRow(a, "control channel", "%s  %s", dect2::p25::channelText(s.channel).c_str(), mhz(s.freqHz).c_str());
        kvRow(a, "service class", "0x%02X", s.ssc);
        kvRow(a, "heard", "%s UTC", clock(s.wallTime).c_str());
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Adjacent sites");
    if (t.adjacent.empty()) ImGui::TextDisabled("  none heard");
    else if (ImGui::BeginTable("##p25adj", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("System"); ImGui::TableSetupColumn("RFSS / site"); ImGui::TableSetupColumn("Control channel"); ImGui::TableSetupColumn("Heard");
        ImGui::TableHeadersRow();
        for (const auto& j : t.adjacent) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%03X", j.sysId);
            ImGui::TableNextColumn(); ImGui::Text("%d / %d", j.rfss, j.site);
            ImGui::TableNextColumn(); ImGui::Text("%s  %s", dect2::p25::channelText(j.channel).c_str(), mhz(j.freqHz).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(clock(j.wallTime).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Channel identifiers");
    bool any = false;
    for (const auto& r : t.idens) any = any || r.valid;
    if (!any) ImGui::TextDisabled("  none heard (identifier updates turn channel numbers into frequencies)");
    else if (ImGui::BeginTable("##p25iden", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("ID"); ImGui::TableSetupColumn("Base"); ImGui::TableSetupColumn("Spacing"); ImGui::TableSetupColumn("Transmit offset"); ImGui::TableSetupColumn("Width");
        ImGui::TableHeadersRow();
        for (int i = 0; i < 16; i++) {
            const auto& r = t.idens[i];
            if (!r.valid) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d%s", i, r.vu ? " (VHF/UHF)" : "");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(mhz(r.baseHz).c_str());
            ImGui::TableNextColumn(); ImGui::Text("%.2f kHz", r.spacingHz / 1e3);
            ImGui::TableNextColumn(); ImGui::Text("%+.4f MHz", r.txOffsetHz / 1e6);
            ImGui::TableNextColumn(); ImGui::Text("%.2f kHz", r.bwHz / 1e3);
        }
        ImGui::EndTable();
    }
}

void talkgroupView(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const auto& tg = a.rx.p25.talkgroups;
    if (tg.empty()) { ImGui::TextDisabled("no talkgroups heard yet"); return; }
    if (!ImGui::BeginTable("##p25tg", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Talkgroup"); ImGui::TableSetupColumn("Grants"); ImGui::TableSetupColumn("Calls"); ImGui::TableSetupColumn("Last unit");
    ImGui::TableSetupColumn("Last channel"); ImGui::TableSetupColumn("Last heard"); ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (const auto& g : tg) {
        ImGui::TableNextRow();
        const ImVec4 c = g.active ? pal::okGreen() : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0); ImGui::TextColored(c, "%d", g.tg); ImGui::PopFont();
        ImGui::TableNextColumn(); ImGui::Text("%u", g.grants);
        ImGui::TableNextColumn(); ImGui::Text("%u", g.calls);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(dect2::p25Id(g.lastSrc).c_str());
        ImGui::TableNextColumn(); ImGui::TextUnformatted(mhz(g.lastFreqHz).c_str());
        ImGui::TableNextColumn(); ImGui::TextColored(c, "%s", g.active ? "active" : clock(g.lastWall).c_str());
        ImGui::TableNextColumn();
        if (g.emergency) { ImGui::TextColored(pal::warnAmber(), "emergency"); ImGui::SameLine(); }
        if (g.encrypted) ImGui::TextColored(pal::warnAmber(), "encrypted");
    }
    ImGui::EndTable();
}

void messageView(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "start the receiver to see messages"); return; }
    const auto& ev = a.rx.p25.events;
    ImGui::PushFont(a.mono, 0);
    for (const auto& s : ev) ImGui::TextUnformatted(s.c_str());
    ImGui::PopFont();
    if (ev.empty()) ImGui::TextDisabled("no messages yet");
}

// ---------------------------------------------------------------- hooks

void tick(App& a) {
    if (!a.engine.running()) { S.wasRunning = false; return; }
    dect2::P25Receiver& r = a.engine.p25();
    if (!S.wasRunning || a.volume != S.pushedVol || a.muted != S.pushedMute) {   // the sound controls are shared with the other modes
        r.setVolume(a.volume); r.setMuted(a.muted);
        S.pushedVol = a.volume; S.pushedMute = a.muted;
    }
    S.wasRunning = true;
}

void tab(App& a) {
    subNav("p25v", S.view, {"Now", "Calls", "Trunking", "Talkgroups", "Messages"});
    if (S.view == 0) nowView(a); else if (S.view == 1) callsView(a); else if (S.view == 2) trunkingView(a); else if (S.view == 3) talkgroupView(a); else messageView(a);
}

void list(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    if (resultsClearButton("p25")) a.engine.p25().clearResults();
    const auto& tg = a.rx.p25.talkgroups;
    if (!ImGui::BeginTable("##p25l", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupColumn("TG"); ImGui::TableSetupColumn("Heard", ImGuiTableColumnFlags_WidthStretch);
    const auto& n = a.rx.p25.now;
    if (n.active && !n.group) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0); ImGui::TextColored(pal::okGreen(), "%s", dect2::p25Id(n.src).c_str()); ImGui::PopFont();
        ImGui::TableNextColumn(); ImGui::TextColored(pal::okGreen(), "on air -> %s", dstOf(n).c_str());
    }
    for (const auto& g : tg) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0); ImGui::TextColored(g.active ? pal::okGreen() : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%d", g.tg); ImGui::PopFont();
        ImGui::TableNextColumn();
        if (g.active) ImGui::TextColored(pal::okGreen(), "active, unit %s", dect2::p25Id(g.lastSrc).c_str());
        else ImGui::TextDisabled("%s  %u grants, %u calls", clock(g.lastWall).c_str(), g.grants, g.calls);
    }
    ImGui::EndTable();
}

void receiver(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const dect2::P25Telemetry& t = a.rx.p25;
    kvRow(a, "state", "%s", dect2::p25Summary(t).c_str());
    kvRow(a, "signal", "C4FM, symbol SNR %.1f dB, +3 deviation %.0f Hz, offset %+.0f Hz%s", t.snrDb, t.deviationHz, t.cfoHz, t.inverted ? ", inverted" : "");
    kvRow(a, "NAC", "%03X", t.nac < 0 ? 0 : t.nac);
    kvRow(a, "bit errors", "%.2f %% (NID, before the BCH)", t.berPct);
    kvRow(a, "frames", "%llu, %llu with a damaged NID", (unsigned long long)t.frames, (unsigned long long)t.nidBad);
    kvRow(a, "data units", "HDU %llu, LDU1 %llu, LDU2 %llu, TDU %llu, TDULC %llu, PDU %llu", (unsigned long long)t.hdu, (unsigned long long)t.ldu1, (unsigned long long)t.ldu2,
          (unsigned long long)t.tdu, (unsigned long long)t.tdulc, (unsigned long long)t.pdu);
    kvRow(a, "TSBK", "%llu good, %llu failed CRC", (unsigned long long)t.tsbkOk, (unsigned long long)t.tsbkBad);
    kvRow(a, "link control", "%llu good, %llu failed", (unsigned long long)t.lcOk, (unsigned long long)t.lcBad);
    kvRow(a, "encryption sync", "%llu good, %llu failed; header %llu good, %llu failed", (unsigned long long)t.esOk, (unsigned long long)t.esBad, (unsigned long long)t.hduOk, (unsigned long long)t.hduBad);
    kvRow(a, "voice", "%llu frames, %llu decoded%s, %llu calls joined late", (unsigned long long)t.voiceFrames, (unsigned long long)t.voiceDecoded, t.imbe ? "" : " (mbelib not found)",
          (unsigned long long)t.lateEntries);
    if (t.imbe) kvRow(a, "mbelib", "%s", t.imbePath.c_str());
    kvRow(a, "input", "%.3f Msps, %.1f dBFS", t.inputRate / 1e6, t.levelDb);
    kvRow(a, "signal time", "%.1f s", t.timeSec);
}

void status(App& a) {
    const bool run = a.engine.running(), on = live(a);
    const SignalStats& st = a.spec.stats;
    const AdcStatus adc = classifyAdc(st.rmsDbfs, st.peak, st.clipFraction);
    StatusPanel panel;
    lamp("IQ", run ? (adc == AdcStatus::Overload ? 3 : (adc == AdcStatus::Good ? 1 : 2)) : 0, (int)Ic::Wave); flowNext(12 * gUi);
    lamp("Sync", !on ? 0 : a.rx.p25.state >= 1 ? 1 : 0); flowNext(10 * gUi);
    lamp("Control", !on ? 0 : a.rx.p25.control ? 1 : 0); flowNext(10 * gUi);
    lamp("On air", !on ? 0 : a.rx.p25.state == 2 ? 1 : 0); flowNext(10 * gUi);
    ImGui::TextDisabled("|"); flowNext(10 * gUi);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State"); ImGui::SameLine(0, 5 * gUi);
    ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(!on ? (run ? "starting" : "stopped") : dect2::p25Summary(a.rx.p25).c_str()); ImGui::PopFont();
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "P25";
    if (live(a)) l2 = dect2::p25Summary(a.rx.p25);
}

} // namespace

extern const ModeUi kP25Ui;
const ModeUi kP25Ui = {
    .sideTitle = "TALKGROUPS",
    .tabName = "P25",
    .tabIcon = Ic::Radio,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .tick = tick,
};
