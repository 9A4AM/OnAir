// Several receivers at once: see receivers.h and docs/multi-sdr.md.
#include "receivers.h"
#include "adsb/adsb_acars.h"
#include "dect2/engine/device_claims.h"
#include <memory>
#ifndef _WIN32
#include <sys/resource.h>
#endif

float gCpuPct = 0;

namespace {
std::unique_ptr<App> gExtra[kMaxReceivers - 1];   // RX 2 ... ; a closed receiver leaves its place empty (the numbers stay)
App* gFirst = nullptr;
int gSel = 1, gAudio = 1;
std::vector<int> gCloseQ;
bool gWarnAsk = false;

struct PrefScope {
    int old;
    explicit PrefScope(int r) : old(plat::gPrefRx) { plat::gPrefRx = r; }
    ~PrefScope() { plat::gPrefRx = old; }
};

int openMask() {
    int m = 0;
    for (int i = 0; i < kMaxReceivers - 1; i++) if (gExtra[i]) m |= 1 << (i + 2);
    return m;
}
void saveOpen() { plat::prefs().setI("rxOpen", openMask()); }

const char* modeName(const App& a) {
    const SubMode* sm = curSub(a);
    for (int i = 0; i < kNumModes; i++) {
        if (kModes[i].family != a.family) continue;
        if (sm ? (kModes[i].subId && !strcmp(kModes[i].subId, sm->id)) : !kModes[i].subId) return kModes[i].name;
    }
    return "";
}

void sampleCpu() {
#ifndef _WIN32
    static double lastCpu = 0, lastWall = 0;
    const double wall = ImGui::GetTime();
    if (wall - lastWall < 1.0) return;
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    const double cpu = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6;
    if (lastWall > 0) gCpuPct = (float)((cpu - lastCpu) / (wall - lastWall) * 100);
    lastCpu = cpu; lastWall = wall;
#endif
}

App* create(int id) {
    if (id < 2 || id > kMaxReceivers || gExtra[id - 2]) return nullptr;
    App& first = *gFirst;
    auto up = std::make_unique<App>();
    App& a = *up;
    a.rxId = id;
    a.engine.setReceiverId(id);
    a.ui = first.ui; a.mono = first.mono;
    a.wf.init(gGfx);
    a.tune.synth.demoTv = true;
    a.engine.player().setHardwareDecode(plat::prefs().getB("hwVideo", true));
    {
        PrefScope ps(id);
        loadPrefs(a);
    }
    a.newUi = first.newUi; a.lightUi = first.lightUi; a.highContrast = first.highContrast; a.uiTheme = first.uiTheme; a.uiVariant = first.uiVariant; a.touchUi = first.touchUi; a.touchScale = first.touchScale;   // one look for the whole window
    a.volume = first.volume; a.muted = first.rxForcedMute ? first.rxSavedMuted : first.muted;
    a.engine.setResultsDir(plat::dataDir() + "/results_rx" + std::to_string(id));
    a.engine.log("receiver RX " + std::to_string(id));
    refreshDevices(a);
    {   // the radio it had, else the first one nobody has, else the test signal
        int pick = 0;
        PrefScope ps(id);
        const std::string name = plat::prefs().getS("devName", "");
        for (int i = 2; i < (int)a.devices.size() && pick == 0; i++)
            if (!name.empty() && a.devices[i].name == name && !deviceHolder(a.devices[i])) pick = i;
        for (int i = 2; i < (int)a.devices.size() && pick == 0; i++)
            if (a.devices[i].isRadio() && !deviceHolder(a.devices[i])) pick = i;
        a.devIdx = pick;
    }
    gExtra[id - 2] = std::move(up);
    return gExtra[id - 2].get();
}

void drawWarning(App& first) {
    if (gWarnAsk) { ImGui::OpenPopup("Add receiver##rxwarn"); gWarnAsk = false; }
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Add receiver##rxwarn", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    static bool dontShow = false;
    ImGui::PushTextWrapPos(430 * gUi);
    ImGui::TextUnformatted(tr("Running several radios at the same time needs a lot of processing power. Performance may limit what is possible: with too many or too fast receivers samples are dropped and decoding gets worse."));
    ImGui::Spacing();
    ImGui::TextUnformatted(tr("A high-end computer is recommended. Lower sample rates help."));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Text(tr("CPU load now: %.0f%% (100%% = one core)"), gCpuPct);
    ImGui::Checkbox(tr("Don't show again"), &dontShow);
    ImGui::Spacing();
    if (ImGui::Button(tr("Add receiver"))) {
        if (dontShow) plat::prefs().setB("rxWarnDone", true);
        if (App* n = addReceiver()) (void)n;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    (void)first;
}

void strip(App& first, ImVec2 disp, float h) {
    ImGui::SetNextWindowPos(ImVec2(0, disp.y - h));
    ImGui::SetNextWindowSize(ImVec2(disp.x, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * gUi, 3 * gUi));
    ImGui::Begin("##rxstrip", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    int closeId = 0;
    for (App* r : receiversAll()) {
        ImGui::PushID(r->rxId);
        const bool sel = r->rxId == gSel;
        char lab[160];
        snprintf(lab, sizeof lab, "RX %d  %s  %.3f MHz", r->rxId, modeName(*r), r->freqMhz);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, pal::accent(0.55f));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::Button(lab)) gSel = r->rxId;
        if (sel) ImGui::PopStyleColor();
        if (r->engine.running()) ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5 * gUi, p.y + 5 * gUi), 3 * gUi, IM_COL32(60, 210, 90, 255));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r->engine.running() ? tr("Receiving") : tr("Stopped"));
        ImGui::SameLine(0, 2 * gUi);
        const bool snd = r->rxId == gAudio;
        if (snd) ImGui::PushStyleColor(ImGuiCol_Button, pal::accent(0.55f));
        if (ImGui::SmallButton(snd ? tr("sound") : tr("mute"))) gAudio = r->rxId;
        if (snd) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Only one receiver plays its sound at a time: click to hear this one"));
        if (r->rxId > 1) {
            ImGui::SameLine(0, 2 * gUi);
            if (ImGui::SmallButton("x")) closeId = r->rxId;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Close this receiver: it stops and lets go of its radio"));
        }
        ImGui::SameLine(0, 10 * gUi);
        ImGui::PopID();
    }
    {
        const bool room = (int)receiversAll().size() < kMaxReceivers;
        ImGui::BeginDisabled(!room);
        if (ImGui::Button("+")) {
            if (plat::prefs().getB("rxWarnDone", false)) addReceiver(); else gWarnAsk = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", room ? tr("Add a receiver: another radio with its own mode") : tr("The most receivers are open"));
    }
    {
        char b[48];
        snprintf(b, sizeof b, "CPU %.0f%%", gCpuPct);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), disp.x - ImGui::CalcTextSize(b).x - 14 * gUi));
        ImGui::TextColored(gCpuPct > 250 * (float)receiversAll().size() / 2 ? pal::warnAmber() : ImVec4(0.6f, 0.6f, 0.6f, 1), "%s", b);
    }
    ImGui::End();
    if (closeId) closeReceiver(closeId);
    (void)first;
}
}  // namespace

App* receiverById(int id) {
    if (id == 1) return gFirst;
    return id >= 2 && id <= kMaxReceivers ? gExtra[id - 2].get() : nullptr;
}

std::vector<App*> receiversAll() {
    std::vector<App*> v;
    if (gFirst) v.push_back(gFirst);
    for (auto& e : gExtra) if (e) v.push_back(e.get());
    return v;
}

App* addReceiver() {
    for (int id = 2; id <= kMaxReceivers; id++) if (!gExtra[id - 2]) {
        App* a = create(id);
        if (a) { gSel = id; saveOpen(); }
        return a;
    }
    return nullptr;
}

void closeReceiver(int id) {
    if (id >= 2) gCloseQ.push_back(id);
}

void receiversInit(App& first) {
    gFirst = &first;
    first.rxId = 1;
    first.engine.setReceiverId(1);
    const int m = (int)plat::prefs().getI("rxOpen", 0);
    for (int id = 2; id <= kMaxReceivers; id++) if (m & (1 << id)) create(id);
}

bool receiverModeBusy(const App& a, std::string* why) {
    for (App* b : receiversAll()) {
        if (b == &a || !b->engine.running() || b->family != a.family || b->sub != a.sub) continue;
        if (why) *why = "RX " + std::to_string(b->rxId) + " already uses this mode: per-receiver state for this mode is not separated yet";
        return true;
    }
    return false;
}

void receiversFrame(App& first, ImVec2 disp) {
    gFirst = &first;
    for (int id : gCloseQ) {
        App* r = receiverById(id);
        if (!r) continue;
        { PrefScope ps(id); savePrefs(*r); }
        r->engine.stop();
        r->engine.flushResults();
        gExtra[id - 2].reset();
        if (gSel == id) gSel = 1;
        if (gAudio == id) gAudio = gSel;
        saveOpen();
    }
    gCloseQ.clear();
    sampleCpu();
    if (!receiverById(gSel)) gSel = 1;
    if (!receiverById(gAudio)) gAudio = gSel;
    const float h = ImGui::GetFrameHeight() + 10 * gUi;
    const ImVec2 d(disp.x, disp.y - h);
    for (App* r : receiversAll()) {
        PrefScope ps(r->rxId);
        const bool play = r->rxId == gAudio;
        if (!play && !r->rxForcedMute) { r->rxSavedMuted = r->muted; r->muted = true; r->rxForcedMute = true; }
        else if (play && r->rxForcedMute) { r->muted = r->rxSavedMuted; r->rxForcedMute = false; }
        drawUI(*r, d, r->rxId == gSel);
        aeroToolsTick(*r);
    }
    strip(first, disp, h);
    drawWarning(first);
    acarslog::tick(first);
    mapSendTick(first);
}

void receiversShutdown(App& first) {
    mapSendShutdown();
    for (int id = 2; id <= kMaxReceivers; id++) if (App* r = gExtra[id - 2].get()) {
        { PrefScope ps(id); savePrefs(*r); }
        r->engine.stop();
        r->engine.flushResults();
    }
    for (auto& e : gExtra) e.reset();
    (void)first;
}
