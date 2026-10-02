// Dark "instrument" theme modelled on the DecDRM / modem-tool references.
#pragma once
#include "imgui.h"
#include "implot.h"

// One accent colour for data and highlights; red / amber / green appear only where they mean a status.
namespace pal {
inline ImVec4 accent(float a = 1.f) { return ImVec4(0.36f, 0.66f, 0.86f, a); }
inline ImVec4 okGreen() { return ImVec4(0.40f, 0.76f, 0.52f, 1); }
inline ImVec4 warnAmber() { return ImVec4(0.90f, 0.70f, 0.28f, 1); }
inline ImVec4 badRed() { return ImVec4(0.88f, 0.38f, 0.34f, 1); }
inline ImVec4 grey(float a = 1.f) { return ImVec4(0.62f, 0.67f, 0.73f, a); }
}

inline void applyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 4; s.TabRounding = 5; s.ChildRounding = 5; s.GrabRounding = 4; s.PopupRounding = 5; s.ScrollbarRounding = 6;
    s.FramePadding = ImVec2(7, 3); s.ItemSpacing = ImVec2(7, 4); s.WindowPadding = ImVec2(10, 8); s.CellPadding = ImVec2(6, 2);
    s.FrameBorderSize = 0; s.ChildBorderSize = 1; s.TabBarBorderSize = 1; s.ScrollbarSize = 10;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]      = ImVec4(0.078f, 0.086f, 0.098f, 1);
    c[ImGuiCol_ChildBg]       = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]       = ImVec4(0.10f, 0.115f, 0.13f, 0.98f);
    c[ImGuiCol_Border]        = ImVec4(0.18f, 0.20f, 0.23f, 1);
    c[ImGuiCol_FrameBg]       = ImVec4(0.115f, 0.13f, 0.15f, 1);
    c[ImGuiCol_FrameBgHovered]= ImVec4(0.15f, 0.17f, 0.20f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.17f, 0.20f, 0.24f, 1);
    c[ImGuiCol_Button]        = ImVec4(0.14f, 0.16f, 0.185f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.34f, 0.50f, 1);
    c[ImGuiCol_ButtonActive]  = ImVec4(0.10f, 0.42f, 0.66f, 1);
    c[ImGuiCol_Header]        = ImVec4(0.14f, 0.20f, 0.28f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.30f, 0.44f, 1);
    c[ImGuiCol_HeaderActive]  = ImVec4(0.10f, 0.42f, 0.66f, 1);
    c[ImGuiCol_Tab]           = ImVec4(0.10f, 0.115f, 0.135f, 1);
    c[ImGuiCol_TabHovered]    = ImVec4(0.18f, 0.32f, 0.47f, 1);
    c[ImGuiCol_TabSelected]   = ImVec4(0.10f, 0.42f, 0.66f, 1);
    c[ImGuiCol_TabDimmed]     = ImVec4(0.10f, 0.115f, 0.135f, 1);
    c[ImGuiCol_TabDimmedSelected] = ImVec4(0.12f, 0.30f, 0.46f, 1);
    c[ImGuiCol_SliderGrab]    = ImVec4(0.55f, 0.62f, 0.70f, 1);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.75f, 1.0f, 1);
    c[ImGuiCol_CheckMark]     = ImVec4(0.45f, 0.78f, 1.0f, 1);
    c[ImGuiCol_Separator]     = ImVec4(0.18f, 0.20f, 0.23f, 1);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_Text]          = ImVec4(0.88f, 0.90f, 0.92f, 1);
    c[ImGuiCol_TextDisabled]  = ImVec4(0.50f, 0.55f, 0.60f, 1);

    ImPlotStyle& p = ImPlot::GetStyle();
    p.Colors[ImPlotCol_PlotBg]     = ImVec4(0.012f, 0.014f, 0.018f, 1);
    p.Colors[ImPlotCol_FrameBg]    = ImVec4(0, 0, 0, 0);
    p.Colors[ImPlotCol_PlotBorder] = ImVec4(0.22f, 0.25f, 0.28f, 1);
    p.Colors[ImPlotCol_AxisGrid]   = ImVec4(0.60f, 0.64f, 0.70f, 0.20f);
    p.Colors[ImPlotCol_AxisText]   = ImVec4(0.68f, 0.72f, 0.77f, 1);
    p.PlotPadding = ImVec2(6, 6);
    p.PlotBorderSize = 1;
}
