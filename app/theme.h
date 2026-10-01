// Dark "instrument" theme modelled on the DecDRM / modem-tool references.
#pragma once
#include "imgui.h"
#include "implot.h"

inline void applyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 3; s.TabRounding = 3; s.ChildRounding = 3; s.GrabRounding = 3;
    s.FramePadding = ImVec2(6, 3); s.ItemSpacing = ImVec2(6, 4); s.WindowPadding = ImVec2(8, 6);
    s.FrameBorderSize = 0; s.ChildBorderSize = 1;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]      = ImVec4(0.067f, 0.075f, 0.082f, 1);
    c[ImGuiCol_ChildBg]       = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Border]        = ImVec4(0.17f, 0.19f, 0.21f, 1);
    c[ImGuiCol_FrameBg]       = ImVec4(0.12f, 0.135f, 0.15f, 1);
    c[ImGuiCol_FrameBgHovered]= ImVec4(0.16f, 0.18f, 0.20f, 1);
    c[ImGuiCol_Button]        = ImVec4(0.14f, 0.16f, 0.18f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.36f, 0.52f, 1);
    c[ImGuiCol_ButtonActive]  = ImVec4(0.12f, 0.45f, 0.70f, 1);
    c[ImGuiCol_Tab]           = ImVec4(0.10f, 0.115f, 0.13f, 1);
    c[ImGuiCol_TabHovered]    = ImVec4(0.20f, 0.36f, 0.52f, 1);
    c[ImGuiCol_TabSelected]   = ImVec4(0.10f, 0.42f, 0.66f, 1);
    c[ImGuiCol_SliderGrab]    = ImVec4(0.55f, 0.60f, 0.66f, 1);
    c[ImGuiCol_CheckMark]     = ImVec4(0.45f, 0.75f, 1.0f, 1);
    c[ImGuiCol_Text]          = ImVec4(0.86f, 0.88f, 0.90f, 1);
    c[ImGuiCol_TextDisabled]  = ImVec4(0.52f, 0.56f, 0.60f, 1);

    ImPlotStyle& p = ImPlot::GetStyle();
    p.Colors[ImPlotCol_PlotBg]     = ImVec4(0.02f, 0.02f, 0.025f, 1);
    p.Colors[ImPlotCol_FrameBg]    = ImVec4(0, 0, 0, 0);
    p.Colors[ImPlotCol_PlotBorder] = ImVec4(0.25f, 0.27f, 0.30f, 1);
    p.Colors[ImPlotCol_AxisGrid]   = ImVec4(0.45f, 0.48f, 0.52f, 0.25f);
    p.Colors[ImPlotCol_AxisText]   = ImVec4(0.70f, 0.74f, 0.78f, 1);
    p.PlotPadding = ImVec2(6, 6);
}
