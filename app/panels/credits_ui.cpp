// The Credits window (Help, Credits and licences; also the "by: ..." button in the menu bar): every project whose code, data or
// specification OnAir uses, with its author, licence and a link that opens in the browser. The table is generated from
// tools/credits_data.tsv into credits_data.h; the full notices are in THIRD_PARTY.md.
#include "../app.h"
#include "credits_data.h"
#include "dect2/engine/updater.h"
#include <cstring>

void creditsWindow(App& a) {
    if (!a.creditsOpen) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(900 * gUi, vp->WorkSize.x * 0.95f), std::min(620 * gUi, vp->WorkSize.y * 0.9f)), ImGuiCond_Appearing);
    if (!ImGui::Begin(tr("Credits and licences"), &a.creditsOpen)) { ImGui::End(); return; }
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(tr("OnAir is free software under the GNU General Public License v3 or later. It is built on the work of the people and projects below; thank you."));
    ImGui::TextDisabled("%s", tr("Click a link to open it in your browser. The full notices and open licence points are in THIRD_PARTY.md."));
    ImGui::PopTextWrapPos();
    static char filter[64] = "";
    ImGui::SetNextItemWidth(220 * gUi);
    ImGui::InputTextWithHint("##creditfilter", tr("Filter"), filter, sizeof filter);
    ImGui::Separator();
    if (ImGui::BeginTable("##credits", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr("Project"), ImGuiTableColumnFlags_WidthStretch, 2.f);
        ImGui::TableSetupColumn(tr("Author"), ImGuiTableColumnFlags_WidthStretch, 2.5f);
        ImGui::TableSetupColumn(tr("Licence"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn(tr("Used for"), ImGuiTableColumnFlags_WidthStretch, 3.f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < kCreditCount; i++) {
            const CreditEntry& c = kCredits[i];
            if (filter[0]) {
                std::string hay = std::string(c.name) + " " + c.author + " " + c.licence + " " + c.where;
                std::string f = filter;
                for (auto& ch : hay) ch = (char)tolower((unsigned char)ch);
                for (auto& ch : f) ch = (char)tolower((unsigned char)ch);
                if (hay.find(f) == std::string::npos) continue;
            }
            ImGui::TableNextRow();
            ImGui::PushID(i);
            ImGui::TableNextColumn();
            if (ImGui::Selectable(c.name, false, ImGuiSelectableFlags_None)) dect2::openUrl(c.url);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s\n\n%s", c.url, c.how, c.note);
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", c.author);
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", c.licence);
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", c.where);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
