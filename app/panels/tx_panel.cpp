// The guided transmit panel: a status header (Idle / Preparing / Transmitting with a pulsing red dot and the elapsed time) and the one Start / Stop button,
// then three steps: 1 Signal, 2 Settings, 3 Preview and send. Wide windows put steps 1-2 on the left and step 3 on the right; a narrow window (or the pop-out
// window) stacks them in one scrolling column. Space starts / stops and Esc stops when no text box has the keyboard. Nothing starts by itself.
#include "tx_state.h"
#include "../ui/icons.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace txui;

namespace txui {

// An inline message box with an icon and wrapped text in the colour of its kind (never a popup)
void banner(Note kind, const char* text) {
    const ImVec4 col = kind == Note::Error ? kRed : kind == Note::Warn ? pal::warnAmber() : pal::accent();
    const Ic ic = kind == Note::Error ? Ic::Warning : kind == Note::Warn ? Ic::Warning : Ic::Info;
    const float pad = 6 * gUi, iw = 18 * gUi;
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, std::max(40.f, w - 2 * pad - iw - 4 * gUi));
    const float h = std::max(ts.y, iw) + 2 * pad;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 bg = col; bg.w = 0.16f;
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(bg), 5 * gUi);
    dl->AddRect(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.7f)), 5 * gUi);
    icons::draw(ic, ImVec2(p0.x + pad + iw * 0.5f, p0.y + pad + iw * 0.5f), iw * 0.9f, ImGui::GetColorU32(col), dl);
    ImGui::SetCursorScreenPos(ImVec2(p0.x + pad + iw + 4 * gUi, p0.y + pad));
    ImGui::PushTextWrapPos(p0.x + w - pad - ImGui::GetWindowPos().x + ImGui::GetScrollX());
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + 4 * gUi));
}

// "1  Signal" in the heading colour with a round number badge, and the current choice dimmed on the same line
void sectionTitle(int number, const char* title, const char* summary) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = ImGui::GetFrameHeight() * 0.42f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetFrameHeight() * 0.5f), r, ImGui::GetColorU32(pal::accent(0.9f)));
    char n[8];
    snprintf(n, sizeof n, "%d", number);
    const ImVec2 ns = ImGui::CalcTextSize(n);
    dl->AddText(ImVec2(p.x + r - ns.x * 0.5f, p.y + ImGui::GetFrameHeight() * 0.5f - ns.y * 0.5f), IM_COL32(15, 20, 25, 255), n);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 2 * r + 8 * gUi, p.y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(pal::heading(), "%s", title);
    if (summary && *summary) { ImGui::SameLine(0, 12 * gUi); ImGui::TextDisabled("%s", summary); }
}

} // namespace txui

namespace {

enum class State { Idle, Preparing, Transmitting, Test, Carrier };

State stateOf(const TxUi& s) {
    if (s.pendingStart || s.pendingCarrier) return State::Preparing;
    if (!s.session.running()) return State::Idle;
    const TxSession::Stats st = s.session.stats();
    if (st.samples == 0) return State::Preparing;
    if (s.sessionDry) return State::Test;
    return st.carrierTune ? State::Carrier : State::Transmitting;
}

// the elapsed time as mm:ss (h:mm:ss from an hour)
std::string elapsedText(double sec) {
    const int t = (int)std::max(0.0, sec);
    char b[24];
    if (t >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
    else snprintf(b, sizeof b, "%02d:%02d", t / 60, t % 60);
    return b;
}

// the Start / Stop button of the header: one button, accent when it starts and red when it stops
bool primaryButton(bool running, bool enabled, float w) {
    const float h = ImGui::GetFrameHeight() * 1.6f;
    const char* lab = running ? tr("STOP") : tr("START TRANSMITTING");
    ImGui::BeginDisabled(!enabled && !running);
    if (running) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.10f, 0.10f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.15f, 0.15f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, pal::accent(0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal::accent(1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.08f, 0.1f, 1));
    }
    const bool hit = ImGui::Button(lab, ImVec2(w, h));
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    return hit;
}

// Space / Esc: only when no text box wants the keyboard
void shortcuts(App& a, TxUi& s, const DeviceInfo* radio, int conflict, const TxModeSpec* mode) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("##txnotice")) return;
    const bool running = s.session.running();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && (running || s.pendingStart)) stopTx(a, s, "(Esc)");
    else if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        if (running) stopTx(a, s, "(Space)");
        else if (canStart(a, s, radio, conflict, mode)) s.pendingStart = true;
    }
}

// the panel body: header, messages, steps. `inWindow`: drawn in the pop-out window
void body(App& a, bool inWindow) {
    TxUi& s = ui();
    ensureTxMode(a, s);
    loadPayloadPrefs(s);
    const bool running = s.session.running();
    const TxModeSpec* mode = txModeById(s.modeId);
    int conflict = 0;
    const DeviceInfo* radio = bindRadio(a, s, conflict);
    // Start was pressed last frame: the header showed "Preparing", now the radio is opened (it can take a moment)
    if (s.pendingStart && !running) { s.pendingStart = false; if (mode && canStart(a, s, radio, conflict, mode)) startTx(a, s, radio, mode, false); }
    if (s.pendingCarrier && !running) { s.pendingCarrier = false; if (mode && canStart(a, s, radio, conflict, mode)) startTx(a, s, radio, mode, true); }
    if (s.devAutoStart && mode && !running) { s.devAutoStart = false; s.dryRun = true; s.pendingStart = true; }   // dev flag --txdry (no RF)
    shortcuts(a, s, radio, conflict, mode);

    drawStatusHeader(a, s, radio, conflict, mode);
    if (!s.error.empty()) banner(Note::Error, s.error.c_str());
    if (!s.info.empty()) banner(Note::Info, s.info.c_str());
    if (const std::string st = s.session.stats().error; running == false && s.session.stats().failed && !st.empty() && st != s.error) banner(Note::Error, st.c_str());
    ImGui::TextDisabled("%s", tr("Space = start / stop     Esc = stop"));
    ImGui::Spacing();

    const float availW = ImGui::GetContentRegionAvail().x;
    const bool wide = availW > 900 * gUi && !inWindow;
    if (wide) {
        const float h = ImGui::GetContentRegionAvail().y;
        const float leftW = std::max(400 * gUi, availW * 0.46f);
        ImGui::BeginChild("##txleft", ImVec2(leftW, h), false);
        stepSignal(a, s, running);
        ImGui::Spacing();
        stepSettings(a, s, radio, conflict, mode, running);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##txrightcol", ImVec2(0, h), false);
        stepSend(a, s, radio, conflict, mode, running);
        ImGui::EndChild();
    } else {
        ImGui::BeginChild("##txcol", ImVec2(0, 0), false);
        stepSignal(a, s, running);
        ImGui::Spacing();
        stepSettings(a, s, radio, conflict, mode, running);
        ImGui::Spacing();
        stepSend(a, s, radio, conflict, mode, running);
        ImGui::EndChild();
    }
    txNoticePopup(s);
}

} // namespace

namespace txui {

// The status header: dot, big status word, elapsed time, signal and frequency, and the Start / Stop button
void drawStatusHeader(App& a, TxUi& s, const DeviceInfo* radio, int conflict, const TxModeSpec* mode) {
    const State st = stateOf(s);
    const bool running = s.session.running();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool narrow = w < 520 * gUi;
    const float btnW = narrow ? w : std::min(260 * gUi, w * 0.35f);
    const float h = ImGui::GetFrameHeight() * 1.9f;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* word = st == State::Idle ? tr("Idle") : st == State::Preparing ? tr("Preparing") : st == State::Test ? tr("Test (no RF)") : st == State::Carrier ? tr("Carrier tune") : tr("Transmitting");
    ImVec4 col = st == State::Idle ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : st == State::Preparing ? pal::warnAmber() : st == State::Test ? pal::accent() : kRed;
    const bool live = st == State::Transmitting || st == State::Carrier;
    const float pulse = live ? 0.5f + 0.5f * (float)std::sin(ImGui::GetTime() * 5.0) : 1.f;
    const float r = h * 0.2f;
    const ImVec2 dc(p0.x + r + 4 * gUi, p0.y + h * 0.5f);
    if (live) dl->AddCircleFilled(dc, r * (1.5f + 0.5f * pulse), ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.25f * pulse)));
    dl->AddCircleFilled(dc, r * (live ? 0.8f + 0.2f * pulse : 0.8f), ImGui::GetColorU32(col));
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 2 * r + 16 * gUi, p0.y));
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.6f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(col, "%s", word);
    ImGui::PopFont();
    if (running) {
        ImGui::SameLine(0, 14 * gUi);
        ImGui::PushFont(a.mono, 0);
        ImGui::Text("%s", elapsedText(ImGui::GetTime() - s.startedAt).c_str());
        ImGui::PopFont();
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 2 * r + 16 * gUi, p0.y + h * 0.62f));
    if (running) ImGui::TextDisabled("%s   %.6f MHz   %.2f Msps   %s", s.modeName.c_str(), s.freqSent / 1e6, s.session.stats().rateHz / 1e6, s.radioName.c_str());
    else if (mode) ImGui::TextDisabled("%s   %.6f MHz", mode->name, s.freqMhz);
    else ImGui::TextDisabled("%s", tr("Choose a signal in step 1"));
    // the button: to the right of the status when there is room, below it when the window is narrow
    if (narrow) ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + 4 * gUi));
    else ImGui::SetCursorScreenPos(ImVec2(p0.x + w - btnW, p0.y + (h - ImGui::GetFrameHeight() * 1.6f) * 0.5f));
    if (primaryButton(running || s.pendingStart, canStart(a, s, radio, conflict, mode), btnW)) {
        if (running || s.pendingStart) stopTx(a, s, "by the user");
        else s.pendingStart = true;
    }
    if (!running && !canStart(a, s, radio, conflict, mode) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", mode ? tr("Choose a radio in step 2, or switch on the test (no RF).") : tr("Choose a signal in step 1."));
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + (narrow ? ImGui::GetFrameHeight() * 1.6f + 8 * gUi : 4 * gUi)));
    ImGui::Separator();
}

} // namespace txui

// The Transmit panel in the main area; "Pop out" moves it to a floating window of its own (the main area then says so)
void txPanel(App& a) {
    TxUi& s = ui();
    if (s.popped) {
        ImGui::Dummy(ImVec2(1, 8 * gUi));
        ImGui::TextWrapped("%s", tr("The transmit panel is in its own window."));
        if (ImGui::Button(tr("Bring it back here"))) s.popped = false;
        ImGui::SetNextWindowSize(ImVec2(560 * gUi, 720 * gUi), ImGuiCond_FirstUseEver);
        bool open = true;
        if (ImGui::Begin(tr("Transmit##txpop"), &open, ImGuiWindowFlags_NoSavedSettings)) {
            body(a, true);
        }
        ImGui::End();
        if (!open) s.popped = false;
        return;
    }
    const ImVec2 tp = ImGui::GetCursorScreenPos();
    body(a, false);
    // the pop-out button sits in the top right corner of the panel
    const float bw = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(ImVec2(tp.x + ImGui::GetWindowContentRegionMax().x - bw - 2 * gUi, tp.y + 2 * gUi));
    ImGui::PushID("##txpopbtn");
    if (ImGui::InvisibleButton("##p", ImVec2(bw, bw))) s.popped = true;
    const bool hot = ImGui::IsItemHovered();
    icons::draw(Ic::Popout, ImVec2(tp.x + ImGui::GetWindowContentRegionMax().x - bw * 0.5f - 2 * gUi, tp.y + 2 * gUi + bw * 0.5f), bw * 0.6f, ImGui::GetColorU32(hot ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    if (hot) ImGui::SetTooltip("%s", tr("Pop out into a window of its own"));
    ImGui::PopID();
}
