// SITOR screen (work in progress). There is no FSK demodulator yet, so the SITOR decoder (core/include/dect2/hfdig/hfdig_sitor.h)
// gets no input and the panel says so. The FEC-B character table is not yet checked against ITU-R M.476.
#include "app.h"

namespace {

const char* kWarning = "Character table not yet checked against ITU-R M.476; text may be wrong.";
const char* kWaiting = "no FSK demodulator yet: decoder waiting for input";

void tab(App&) {
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "%s", kWarning);
    ImGui::Spacing();
    ImGui::TextDisabled("SITOR (CCIR 476): FEC-B 4-of-7 characters with ITA2 letters and figures, FEC-A time diversity.");
    ImGui::TextDisabled("%s", kWaiting);
}

void list(App&) {}
void tick(App&) {}

void status(App&) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State");
    ImGui::SameLine();
    ImGui::TextUnformatted(kWaiting);
}

void summary(const App&, std::string& l1, std::string& l2) {
    l1 = "SITOR (WIP, table unverified)";
    l2 = kWaiting;
}

} // namespace

extern const ModeUi kSitorUi;
const ModeUi kSitorUi = {
    .sideTitle = "TEXT",
    .tabName = "SITOR",
    .tabIcon = Ic::Radio,
    .tab = tab,
    .list = list,
    .status = status,
    .summary = summary,
    .tick = tick,
};
