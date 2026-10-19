// The panels of the three "Kris custom modes" that only exist as specifications so far: KAVB (video), KAPP (packets) and KAFT (file transfer).
// Each shows what the mode is, its width, the planned profiles (speed against range, from the draft specification v0.1) and that nothing is
// built yet. The transmit section is in place for later, with its controls disabled. No receiver and no engine code belongs to these modes.
#include "app.h"
#include <string>
#include <vector>

namespace {

struct Wip {
    const char* name;
    const char* purpose;
    const char* width;
    std::vector<const char*> profiles;   // planned profiles, short lines (speed versus range)
};

const Wip& kavb() {
    static const Wip w = {"KAVB", "Amateur digital video: 720p30 up to 1080p60 with stereo audio, the KAAL station beacon (callsign, position, quality, bio text) "
                                   "and the V-shaped sweep signature.", "1 to 2 MHz wide",
        {"A  1.0 MHz  16-QAM 3/4   about 2.1 Mbit/s   720p30            needs about 11-12 dB Es/N0",
         "B  1.5 MHz  16-QAM 3/4   about 3.2 Mbit/s   720p60 or 1080p30  needs about 11-12 dB Es/N0",
         "C  2.0 MHz  16-QAM 3/4   about 4.2 Mbit/s   1080p30           needs about 11-12 dB Es/N0",
         "D  2.0 MHz  64-QAM 3/4   about 6.3 Mbit/s   1080p60           needs about 17-19 dB Es/N0",
         "Robust: QPSK 1/2, about 0.35 Mbit/s, for weak links"}};
    return w;
}
const Wip& kapp() {
    static const Wip w = {"KAPP", "Amateur packet data in bursts with selective acknowledgement: from short messages to bulk data, the profile follows the link quality.",
                          "100 kHz wide (80 ksym/s)",
        {"0  BPSK 1/4      about 19 kbit/s net    needs about -5.7 dB Es/N0 (longest range)",
         "2  QPSK 1/2      about 78 kbit/s net    needs about +1.5 dB",
         "5  16-QAM 3/4    about 233 kbit/s net   needs about +10 dB",
         "7  256-QAM 3/4   about 465 kbit/s net   needs about +19.5 dB",
         "8  1024-QAM 5/6  about 646 kbit/s net   needs about +26.6 dB (short, strong paths)"}};
    return w;
}
const Wip& kaft() {
    static const Wip w = {"KAFT", "Amateur file transfer with a session handshake, resume, compression and automatic profile selection by the measured link quality.",
                          "200 kHz, 500 kHz or 1 MHz wide",
        {"P0  BPSK 1/4       31 / 86 / 165 kbit/s net     (200 kHz / 500 kHz / 1 MHz)   longest range",
         "P2  QPSK 1/2       134 / 353 / 677 kbit/s net",
         "P4  16-QAM 3/4     411 / 1067 / 2054 kbit/s net",
         "P6  256-QAM 3/4    827 / 2139 / 4123 kbit/s net",
         "P9  1024-QAM 9/10  1244 / 3213 / 6194 kbit/s net   fastest, strongest signals only"}};
    return w;
}

void panel(App& a, const Wip& w) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(pal::heading(), "%s", w.name);
    ImGui::TextUnformatted(w.purpose);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::TextDisabled("Width");
    kvColumn(130 * gUi);
    ImGui::TextUnformatted(w.width);
    ImGui::Spacing();
    ImGui::TextColored(pal::heading(), "%s", "Planned profiles (speed versus range, draft specification v0.1)");
    for (const char* p : w.profiles) {
        ImGui::PushFont(a.mono, 0);
        ImGui::TextUnformatted(p);
        ImGui::PopFont();
    }
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.4f, 1), "%s", "Work in progress: no receiver or transmitter yet.");
    ImGui::Separator();
    ImGui::TextColored(pal::heading(), "%s", "Transmit");
    ImGui::BeginDisabled(true);
    static char cs[16] = "", text[104] = "";
    static bool beacon = true;
    ImGui::SetNextItemWidth(160 * gUi);
    ImGui::InputText("Callsign##wip", cs, sizeof cs);
    ImGui::SetNextItemWidth(260 * gUi);
    ImGui::InputText("Bio text##wip", text, sizeof text);
    ImGui::Checkbox("Send the beacon##wip", &beacon);
    ImGui::Button("Start transmitting##wip");
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", "Work in progress: no receiver or transmitter yet.");
}

void tabKavb(App& a) { panel(a, kavb()); }
void tabKapp(App& a) { panel(a, kapp()); }
void tabKaft(App& a) { panel(a, kaft()); }
void sumKavb(const App&, std::string& l1, std::string& l2) { l1 = "KAVB (WIP)"; l2 = "video, 1-2 MHz: no receiver yet"; }
void sumKapp(const App&, std::string& l1, std::string& l2) { l1 = "KAPP (WIP)"; l2 = "packets, 100 kHz: no receiver yet"; }
void sumKaft(const App&, std::string& l1, std::string& l2) { l1 = "KAFT (WIP)"; l2 = "files, 200 kHz - 1 MHz: no receiver yet"; }

void statusWip(App& a) { ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("State"); ImGui::SameLine(); ImGui::TextUnformatted(a.engine.running() ? "work in progress: no receiver yet" : "stopped"); }

} // namespace

extern const ModeUi kKavbUi;
const ModeUi kKavbUi = {.sideTitle = "PROFILES", .tabName = "KAVB", .tabIcon = Ic::Tv, .tab = tabKavb, .status = statusWip, .summary = sumKavb};
extern const ModeUi kKappUi;
const ModeUi kKappUi = {.sideTitle = "PROFILES", .tabName = "KAPP", .tabIcon = Ic::Signal, .tab = tabKapp, .status = statusWip, .summary = sumKapp};
extern const ModeUi kKaftUi;
const ModeUi kKaftUi = {.sideTitle = "PROFILES", .tabName = "KAFT", .tabIcon = Ic::Signal, .tab = tabKaft, .status = statusWip, .summary = sumKaft};
