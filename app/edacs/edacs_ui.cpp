// EDACS trunking screens (work in progress): lock, good / bad frame counts, the frame list and a table of the most frequent data words.
// Control data only. The data words are shown raw: the BCH check, the Barker word and the field layout are not in public non-code sources.
#include "app.h"
#include "dect2/edacs/edacs_tel.h"
#include <cstdio>
#include <string>

namespace {

bool live(const App& a) { return a.rx.standard == 131; }   // family 130, engine standard 132, active standard 131

void tab(App& a) {
    if (!live(a)) {
        ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "tune to an EDACS control channel (9600 bit/s) and start the receiver");
        return;
    }
    const dect2::EdacsTelemetry& t = a.rx.edacs;
    ImGui::TextUnformatted("EDACS trunking (WIP): 9600 bit/s control channel, no voice");
    ImGui::Spacing();
    ImGui::Text("Lock: %s", t.locked ? "yes" : "no");
    ImGui::Text("Good frames: %llu   Bad frames: %llu", (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad);
    ImGui::TextDisabled("System ID: not decoded (field layout not established from public sources)");
    ImGui::Text("Level: %.0f dBFS, data swing %.0f Hz", t.levelDb, t.deviationHz);
    bool inv = t.inverted;
    if (ImGui::Checkbox("Invert data", &inv)) a.engine.edacs().setInverted(inv);
    ImGui::SameLine();
    ImGui::TextDisabled("(polarity is not established: flip it when the words look inverted)");
    ImGui::Spacing();
    ImGui::TextUnformatted("Data words (28 bits, raw)");
    if (ImGui::BeginTable("edword", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Word");
        ImGui::TableSetupColumn("Count");
        ImGui::TableHeadersRow();
        for (const dect2::EdacsWordCount& w : t.words) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%07X", w.word);
            ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)w.count);
        }
        ImGui::EndTable();
    }
    if (!t.dataValid) ImGui::TextDisabled("listening: no frame with agreeing copies yet");
}

void receiver(App& a) {
    if (!live(a)) return;
    const dect2::EdacsTelemetry& t = a.rx.edacs;
    ImGui::Text("%s", dect2::edacsSummary(t).c_str());
    ImGui::Text("good %llu, bad %llu, level %.1f dBFS", (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad, t.levelDb);
}

void list(App& a) {
    if (!live(a)) return;
    const dect2::EdacsTelemetry& t = a.rx.edacs;
    if (ImGui::BeginTable("edmsg", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Message A");
        ImGui::TableSetupColumn("Message B");
        ImGui::TableSetupColumn("Copy errors");
        ImGui::TableSetupColumn("Time (s)");
        ImGui::TableHeadersRow();
        for (const dect2::EdacsFrame& f : t.frames) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%07X", f.msgA);
            ImGui::TableNextColumn(); ImGui::Text("%07X", f.msgB);
            ImGui::TableNextColumn(); ImGui::Text("%d / %d", f.errA, f.errB);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", f.timeSec);
        }
        ImGui::EndTable();
    }
}

void tick(App&) {}

void status(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State");
    ImGui::SameLine();
    ImGui::TextUnformatted(live(a) ? dect2::edacsSummary(a.rx.edacs).c_str() : (a.engine.running() ? "starting" : "stopped"));
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "EDACS trunking (WIP)";
    if (live(a)) l2 = dect2::edacsSummary(a.rx.edacs);
}

} // namespace

extern const ModeUi kEdacsUi;
const ModeUi kEdacsUi = {
    .sideTitle = "FRAMES",
    .tabName = "EDACS",
    .tabIcon = Ic::Radio,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .tick = tick,
};
