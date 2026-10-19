// DME screens (work in progress): the channel X or Y of the beacon, the pulse-pair rate, the level and the ident letters read so far.
#include "app.h"
#include "dect2/dme/dme_tel.h"
#include <cstdio>
#include <string>

namespace {

bool live(const App& a) { return a.rx.standard == 122; }   // family 121, engine standard 123, active standard 122

void tab(App& a) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Work in progress. The receiver finds the pulse pairs of the beacon in the level of the IQ. The channel is X (12 us pair spacing) or Y (36 us). "
                        "The ident is Morse keyed into the pair rate (about 1350 pairs per second while keyed). Receive only.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }

    const dect2::DmeTelemetry& t = a.rx.dme;
    ImGui::PushFont(a.mono, 0);
    ImGui::SetWindowFontScale(2.4f);
    const std::string id = t.ident.empty() ? std::string("...") : t.ident;
    ImGui::TextUnformatted(id.c_str());
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopFont();
    ImGui::Spacing();

    auto row = [](const char* k, const char* v) {
        ImGui::TextDisabled("%s", k);
        kvColumn(130 * gUi);
        ImGui::TextUnformatted(v);
    };
    char b[160];
    snprintf(b, sizeof b, "%s", dect2::dmeChannelName(t.channel));
    row("channel", b);
    snprintf(b, sizeof b, "%.0f pairs/s", t.pairRatePps);
    row("pair rate", b);
    snprintf(b, sizeof b, "%.1f dB", (double)t.levelDb);
    row("level", b);
    snprintf(b, sizeof b, "%s", t.identKeyed ? "keyed" : "not keyed");
    row("ident", b);
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "DME (WIP)";
    if (live(a)) l2 = dect2::dmeSummary(a.rx.dme);
}

void status(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State");
    ImGui::SameLine();
    ImGui::TextUnformatted(live(a) ? dect2::dmeSummary(a.rx.dme).c_str() : (a.engine.running() ? "starting" : "stopped"));
}

} // namespace

extern const ModeUi kDmeUi;
const ModeUi kDmeUi = {
    .sideTitle = "IDENTS",
    .tabName = "DME",
    .tabIcon = Ic::Signal,
    .tab = tab,
    .status = status,
    .summary = summary,
};
