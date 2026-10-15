// TETRAPOL screens: cells heard from D_SYSTEM_INFO on the downlink control channel. Receive only: no voice, nothing decrypted.
#include "app.h"
#include "dect2/tetrapol_rx.h"
#include "dect2/tetrapol_tel.h"
#include <cstdio>
#include <ctime>
#include <string>

namespace {

struct State { int sel = -1; };
State S;

const char* kHelp =
    "TETRAPOL network information (PAS 0001-2 / 0001-3-2), receive only.\n\n"
    "This mode is not TETRA. It reads GMSK 8 kbit/s data frames on 10 / 12.5 kHz UHF channels and shows D_SYSTEM_INFO "
    "when that broadcast is recognised: country, system, BN, cell, location area, channel and scramble.\n\n"
    "It does not decode voice, short data, identities or encrypted traffic, and it does not transmit.";

bool live(const App& a) { return a.rx.standard == 80; }

std::string ago(int64_t t) {
    if (t <= 0) return "-";
    const long d = (long)(std::time(nullptr) - t);
    char b[32];
    if (d < 60) snprintf(b, sizeof b, "%ld s", std::max(0L, d));
    else if (d < 3600) snprintf(b, sizeof b, "%ld min", d / 60);
    else snprintf(b, sizeof b, "%ld h", d / 3600);
    return b;
}

void helpButton() {
    if (ImGui::SmallButton("?##tpolhelp")) ImGui::OpenPopup("##tpolhelppop");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What this mode reads");
    if (ImGui::BeginPopup("##tpolhelppop")) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32);
        ImGui::TextUnformatted(kHelp);
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
}

void tab(App& a) {
    helpButton();
    ImGui::SameLine();
    ImGui::TextUnformatted("TETRAPOL control channel (receive only)");
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "start the receiver to hear TETRAPOL"); return; }
    const dect2::TetrapolTelemetry& t = a.rx.tetrapol;
    ImGui::TextDisabled("%s", dect2::tetrapolSummary(t).c_str());
    ImGui::TextDisabled("scramble hunt, GMSK 8 kbit/s, %d Hz channels", t.spacingHz);
    if (ImGui::BeginTable("##tpolcells", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, ImGui::GetContentRegionAvail().y * 0.55f))) {
        ImGui::TableSetupColumn("Country"); ImGui::TableSetupColumn("System"); ImGui::TableSetupColumn("Cell");
        ImGui::TableSetupColumn("LA"); ImGui::TableSetupColumn("Ch"); ImGui::TableSetupColumn("Scramble");
        ImGui::TableSetupColumn("SNR"); ImGui::TableSetupColumn("Last");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < t.cells.size(); i++) {
            const auto& c = t.cells[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", c.country);
            ImGui::TableNextColumn(); ImGui::Text("%d", c.systemId);
            ImGui::TableNextColumn(); ImGui::Text("%d", c.cellId);
            ImGui::TableNextColumn(); ImGui::Text("%d", c.locArea);
            ImGui::TableNextColumn(); ImGui::Text("%d", c.channelId);
            ImGui::TableNextColumn(); ImGui::Text("%d", c.scramble);
            ImGui::TableNextColumn(); ImGui::Text("%.1f dB", c.snrDb);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(ago(c.lastWall).c_str());
            if (ImGui::IsItemHovered()) S.sel = (int)i;
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Log");
    for (const auto& e : t.events) {
        ImGui::TextUnformatted(e.text.c_str());
        if (&e - t.events.data() > 12) break;
    }
}

void receiver(App&) {}
void list(App&) {}
void tick(App&) {}
void status(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State"); ImGui::SameLine();
    ImGui::TextUnformatted(live(a) ? dect2::tetrapolSummary(a.rx.tetrapol).c_str() : (a.engine.running() ? "starting" : "stopped"));
}
void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "TETRAPOL";
    if (live(a)) l2 = dect2::tetrapolSummary(a.rx.tetrapol);
}

} // namespace

extern const ModeUi kTetrapolUi;
const ModeUi kTetrapolUi = {
    .sideTitle = "CELLS",
    .tabName = "TETRAPOL",
    .tabIcon = Ic::Radio,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .tick = tick,
};
