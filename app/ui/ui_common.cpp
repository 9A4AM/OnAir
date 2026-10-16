// Helpers shared by several screens (see ui_common.h): clock text, key / value rows, the status-line IQ lamp and State label,
// the map's zoom buttons and the wrapped-text tooltip.
#include "ui_common.h"
#include <cstdarg>
#include <ctime>

std::string clockText(int64_t t) {
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

void kvRow(const App& a, const char* k, const char* fmt, ...) {
    ImGui::TextDisabled("%s", k); kvColumn(130 * gUi);
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    ImGui::PushFont(a.mono, 0); ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(b); ImGui::PopTextWrapPos(); ImGui::PopFont();
}

void statusIqLamp(const App& a) {
    const bool run = a.engine.running();
    const SignalStats& st = a.spec.stats;
    const AdcStatus adc = classifyAdc(st.rmsDbfs, st.peak, st.clipFraction);
    lamp("IQ", run ? (adc == AdcStatus::Overload ? 3 : (adc == AdcStatus::Good ? 1 : 2)) : 0, (int)Ic::Wave); flowNext(12 * gUi);
}

void statusStateLabel(const App& a, bool live, const std::string& summaryText) {
    const bool run = a.engine.running();
    ImGui::TextDisabled("|"); flowNext(10 * gUi);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State"); ImGui::SameLine(0, 5 * gUi);
    ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(!live ? (run ? "starting" : "stopped") : summaryText.c_str()); ImGui::PopFont();
}

void mapCornerButtons(adsbmap::View& map, bool& follow, const char* tileNote) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.07f, 0.08f, 0.85f));
    if (ImGui::Button("+", ImVec2(26 * gUi, 0))) { map.zoom = std::min(adsbmap::kMaxZoom, map.zoom + 1); follow = false; }
    ImGui::SameLine(0, 2 * gUi);
    if (ImGui::Button("-", ImVec2(26 * gUi, 0))) { map.zoom = std::max(adsbmap::kMinZoom, map.zoom - 1); follow = false; }
    ImGui::SameLine(0, 6 * gUi);
    if (ImGui::Button("fit")) follow = true;
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 6 * gUi);
    adsbmap::tileControls(tileNote);
}

void wrapTooltip(float width, const char* text) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(width);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}
