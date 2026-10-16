// LBJ screens: train number, kilometre marker, locomotive ID and a log of recent warnings.
#include "app.h"
#include "dect2/lbj_rx.h"
#include "dect2/lbj_tel.h"
#include <cstdio>
#include <ctime>
#include <string>

namespace {

struct State { int sel = 0; };
State S;

const char* kHelp =
    "LBJ (列车接近预警) is the Chinese railway train proximity warning broadcast on about 821 MHz.\n\n"
    "The voice channel carries 1200 bit/s FSK data with train number, kilometre marker, locomotive identity, "
    "direction and speed. Receive only; nothing is transmitted.\n\n"
    "This build wires the mode and UI; full frame sync and BCH decode are still being implemented.";

bool live(const App& a) { return a.rx.standard == 82; }

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
    if (ImGui::SmallButton("?##lbjhelp")) ImGui::OpenPopup("##lbjhelppop");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What LBJ carries");
    if (ImGui::BeginPopup("##lbjhelppop")) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32);
        ImGui::TextUnformatted(kHelp);
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
}

void tab(App& a) {
    helpButton();
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) a.engine.lbj().clearMessages();
    if (!live(a)) {
        ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "tune to about 821.2375 MHz and start the receiver");
        return;
    }
    const dect2::LbjTelemetry& t = a.rx.lbj;
    ImGui::TextDisabled("%s", dect2::lbjSummary(t).c_str());
    ImGui::TextDisabled("FM, 1200 bit/s FSK, %.2f Msps", t.inputRate / 1e6);
    if (t.messages.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("listening (level %.0f dBFS, offset %+.0f Hz)", t.levelDb, t.cfoHz);
        return;
    }
    if (ImGui::BeginTable("##lbjlog", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupColumn("Train");
        ImGui::TableSetupColumn("Km");
        ImGui::TableSetupColumn("Loco");
        ImGui::TableSetupColumn("Dir");
        ImGui::TableSetupColumn("Speed");
        ImGui::TableSetupColumn("Seen");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < t.messages.size(); i++) {
            const auto& m = t.messages[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char l[96];
            snprintf(l, sizeof l, "%s##lbj%zu", m.train.empty() ? "-" : m.train.c_str(), i);
            if (ImGui::Selectable(l, S.sel == (int)i, ImGuiSelectableFlags_SpanAllColumns)) S.sel = (int)i;
            ImGui::TableNextColumn(); ImGui::TextUnformatted(m.kmMarker.empty() ? "-" : m.kmMarker.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(m.loco.empty() ? "-" : m.loco.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(m.direction.empty() ? "-" : m.direction.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(m.speedKmh.empty() ? "-" : m.speedKmh.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(ago(m.wallTime).c_str());
        }
        ImGui::EndTable();
    }
}

void receiver(App& a) {
    if (!live(a)) return;
    const dect2::LbjTelemetry& t = a.rx.lbj;
    ImGui::Text("%s", dect2::lbjSummary(t).c_str());
    ImGui::Text("%llu blocks, %llu decoded", (unsigned long long)t.blocks, (unsigned long long)t.blocksOk);
    ImGui::Text("input %.2f Msps, %.1f dBFS", t.inputRate / 1e6, t.levelDb);
}

void list(App&) {}
void tick(App&) {}

void status(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State");
    ImGui::SameLine();
    ImGui::TextUnformatted(live(a) ? dect2::lbjSummary(a.rx.lbj).c_str() : (a.engine.running() ? "starting" : "stopped"));
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "LBJ";
    if (live(a)) l2 = dect2::lbjSummary(a.rx.lbj);
}

} // namespace

extern const ModeUi kLbjUi;
const ModeUi kLbjUi = {
    .sideTitle = "MESSAGES",
    .tabName = "LBJ",
    .tabIcon = Ic::Radio,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .tick = tick,
};
