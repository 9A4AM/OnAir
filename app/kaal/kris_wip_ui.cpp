// The panel of the one "Kris custom mode" that only exists as a specification so far: KAVB (video). KAPP and KAFT have their own screens (app/kapp, app/kaft).
// It shows what the mode is, its width, the planned profiles (speed against range, from the draft specification v0.1) and that nothing is
// built yet. The transmit section is in place for later, with its controls disabled. No receiver and no engine code belongs to this mode.
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
void sumKavb(const App&, std::string& l1, std::string& l2) { l1 = "KAVB (WIP)"; l2 = "video, 1-2 MHz: no receiver yet"; }

void statusWip(App& a) { ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("State"); ImGui::SameLine(); ImGui::TextUnformatted(a.engine.running() ? "work in progress: no receiver yet" : "stopped"); }

} // namespace

extern const ModeUi kKavbUi;
const ModeUi kKavbUi = {.sideTitle = "PROFILES", .tabName = "KAVB", .tabIcon = Ic::Tv, .tab = tabKavb, .summary = sumKavb};
