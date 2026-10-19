// Spectrum analyser screens: the sweep controls (range and presets, resolution, gains, averaging), one wide chart with live, peak-hold
// and averaged traces or a persistence display, the band allocations in a strip above the chart (height from the font), a hover read-out, a marker and a delta marker,
// the waterfall of the sweeps below, and CSV export of the current sweep.
#include "dect2/common/thread_name.h"
#include "app.h"
#include "dect2/specan/burst_monitor.h"
#include "dect2/specan/specan_dsp.h"
#include "dect2/specan/specan_rx.h"
#include "dect2/specan/specan_tel.h"
#include <algorithm>
#include <cmath>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <ctime>
#include <string>
#include <vector>

namespace {

struct State {
    float loMhz = 1, hiMhz = 6000;
    int preset = 0;
    int rbw = 4;                  // index in kRbw
    int avgN = 8;
    bool showLive = true, showPeak = true, showAvg = false, persist = false, bands = true, spurs = true;
    float refDb = 0, rangeDb = 100;
    double m1 = 0, m2 = 0;        // markers, Hz (0 none)
    uint64_t seenSeq = 0;
    // waterfall and persistence (display columns)
    static constexpr int kCols = 1024, kRows = 160, kLevels = 100;
    std::vector<uint32_t> wf, ps;
    std::vector<float> hist;      // kCols x kLevels hit counts (decaying)
    gfx::Image* wfImg = nullptr;
    gfx::Image* psImg = nullptr;
    bool wfDirty = false, psDirty = false;
    std::string note;
    bool applied = false;
    // automatic CSV saving
    bool autoOn = false;
    int autoSecs = 60, autoKeep = 0;          // keep 0 = all
    char autoDir[512] = "";
    double autoNext = 0;                      // steady time of the next save
    int autoCount = 0;
    std::string autoLast;                     // text: when the last file was saved
    std::vector<std::string> autoFiles;
    std::atomic<bool> autoBusy{false};
    std::thread autoTh;
    bool prefsLoaded = false;
    // burst monitor: one level per sweep at the watched frequency
    bool monOn = false, monOpen = false;
    double monMhz = 20.1;
    float monRiseDb = 6, monMinMs = 200;
    dect2::BurstDetector mon;
    float monLast = -200, monBase = -200;
    struct MonEvent { std::time_t wall; double mhz; dect2::BurstEvent ev; };
    std::vector<MonEvent> monLog;
};
State S;
std::mutex& autoMu() { static std::mutex m; return m; }
const double kRbw[] = {2.8e3, 5e3, 10e3, 25e3, 100e3, 250e3, 500e3, 1e6};
const char* const kRbwName[] = {"2.8 kHz", "5 kHz", "10 kHz", "25 kHz", "100 kHz", "250 kHz", "500 kHz", "1 MHz"};

std::string sizeText(size_t b) {
    char t[32];
    snprintf(t, sizeof t, b >= (1u << 20) ? "%.1f MB" : "%.0f kB", b >= (1u << 20) ? b / 1048576.0 : b / 1024.0);
    return t;
}

double nowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string defaultDir() {
    const char* h = getenv("HOME");
    if (!h) h = getenv("USERPROFILE");
    return std::string(h ? h : ".") + "/SpectrumSweeps";
}

void loadAutoPrefs() {
    if (S.prefsLoaded) return;
    S.prefsLoaded = true;
    auto& p = plat::prefs();
    S.autoSecs = (int)p.getI("specanAutoSecs", 60);
    S.autoKeep = (int)p.getI("specanAutoKeep", 0);
    snprintf(S.autoDir, sizeof S.autoDir, "%s", p.getS("specanAutoDir", defaultDir()).c_str());
}

void saveAutoPrefs() {
    auto& p = plat::prefs();
    p.setI("specanAutoSecs", S.autoSecs); p.setI("specanAutoKeep", S.autoKeep); p.setS("specanAutoDir", S.autoDir);
    p.flush();
}

// One automatic save: the sweep is copied here, the file is written on a worker thread (the UI keeps running)
void autoSave(const App& a) {
    if (S.autoBusy.load()) return;
    if (S.autoTh.joinable()) S.autoTh.join();
    const auto& t = a.rx.specan;
    const dect2::SaGrid g = dect2::saMakeGrid(t.loHz, t.loHz + t.binHz * t.live.size(), t.binHz);
    char nm[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(nm, sizeof nm, "specan_%Y%m%d_%H%M%S.csv", std::localtime(&now));
    std::error_code ec;
    std::filesystem::create_directories(S.autoDir, ec);
    const std::string path = std::string(S.autoDir) + "/" + nm;
    char when[32];
    std::strftime(when, sizeof when, "%H:%M:%S", std::localtime(&now));
    S.autoBusy = true;
    S.autoTh = dect2::namedThread("onair-specan", [g, live = t.live, peak = t.peak, avg = t.avg, path, w = std::string(when)] {
        const bool ok = dect2::saCsvWriteFile(path, g, live, peak, avg);
        std::lock_guard<std::mutex> lk(autoMu());
        if (ok) { S.autoCount++; S.autoLast = w; S.autoFiles.push_back(path); S.note = "auto-saved " + path; }
        else S.note = "auto-save failed: " + path;
        S.autoBusy = false;
    });
}

bool live(const App& a) { return a.rx.standard == 43 && !a.rx.specan.live.empty(); }

void pushSettings(App& a, bool rangeChanged) {
    auto& r = a.engine.specan();
    r.setRange(S.loMhz * 1e6, S.hiMhz * 1e6);
    r.setBinHz(kRbw[S.rbw]);
    r.setAveraging(S.avgN);
    r.setRemoveSpurs(S.spurs);
    a.tune.synth.modeVal[0] = S.loMhz;
    a.tune.synth.modeVal[1] = S.hiMhz;
    // the HackRF sweeps the range it was started with: start it again over the new one
    if (rangeChanged && a.engine.running() && a.devices[a.devIdx].kind == DeviceInfo::HackRF) startReceiver(a);
}

// trace value of display column c (max over the bins it covers)
float colMax(const std::vector<float>& v, const dect2::SpecanTelemetry& t, double lo, double hi, int c, int cols) {
    if (v.empty() || t.binHz <= 0) return -200;
    const double f0 = lo + (hi - lo) * c / cols, f1 = lo + (hi - lo) * (c + 1) / cols;
    long k0 = (long)((f0 - t.loHz) / t.binHz), k1 = (long)((f1 - t.loHz) / t.binHz);
    k0 = std::max(0L, k0); k1 = std::min((long)v.size() - 1, std::max(k0, k1));
    float m = -200;
    for (long k = k0; k <= k1; k++) m = std::max(m, v[(size_t)k]);
    return m;
}

void applyMonitor() {
    dect2::BurstConfig c;
    c.riseDb = S.monRiseDb;
    c.minMs = S.monMinMs;
    S.mon = dect2::BurstDetector(c);
    S.monLast = S.monBase = -200;
}

// The level in the bin nearest the watched frequency, from this sweep, goes to the burst detector.
void feedMonitor(const dect2::SpecanTelemetry& t) {
    if (!S.monOn || t.binHz <= 0 || t.live.empty()) return;
    const double k = (S.monMhz * 1e6 - t.loHz) / t.binHz;
    if (k < 0 || k >= (double)t.live.size()) { S.monLast = -200; return; }
    const float v = t.live[(size_t)k];
    S.monLast = v;
    dect2::BurstEvent ev;
    if (S.mon.push(v, nowS() * 1000.0, ev)) {
        S.monLog.push_back({std::time(nullptr), S.monMhz, ev});
        if (S.monLog.size() > 200) S.monLog.erase(S.monLog.begin());
    }
    S.monBase = S.mon.lastBaseDb();
}

void ingest(App& a) {
    const auto& t = a.rx.specan;
    if (!live(a) || t.seq == S.seenSeq) return;
    S.seenSeq = t.seq;
    const double lo = t.loHz, hi = t.loHz + t.binHz * t.live.size();
    const int W = State::kCols;
    if ((int)S.wf.size() != W * State::kRows) { S.wf.assign((size_t)W * State::kRows, 0xFF000000u); S.ps.assign((size_t)W * State::kLevels, 0xFF000000u); S.hist.assign((size_t)W * State::kLevels, 0.f); }
    std::copy_backward(S.wf.begin(), S.wf.end() - W, S.wf.end());
    const float bot = S.refDb - S.rangeDb;
    for (auto& h : S.hist) h *= 0.92f;
    for (int c = 0; c < W; c++) {
        const float v = colMax(t.live, t, lo, hi, c, W);
        S.wf[(size_t)c] = Waterfall::jet(std::clamp((v - bot) / S.rangeDb, 0.f, 1.f));
        const int lv = (int)std::clamp((S.refDb - v) / S.rangeDb * State::kLevels, 0.f, State::kLevels - 1.f);
        S.hist[(size_t)lv * W + c] += 1.f;
    }
    for (size_t i = 0; i < S.hist.size(); i++) {
        const float h = std::min(1.f, S.hist[i] / 6.f);
        S.ps[i] = h <= 0.01f ? 0xFF000000u : Waterfall::jet(h);
    }
    S.wfDirty = S.psDirty = true;
    feedMonitor(t);
}

std::string mhzText(double hz) {
    char b[32];
    snprintf(b, sizeof b, std::fabs(hz) >= 1e9 ? "%.4f MHz" : "%.3f MHz", hz / 1e6);
    return b;
}

void controls(App& a) {
    int np = 0;
    const dect2::SaPreset* pr = dect2::saPresets(np);
    bool range = false, other = false;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Range");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150 * gUi);
    if (ImGui::BeginCombo("##sapre", S.preset >= 0 ? pr[S.preset].name : "custom")) {
        for (int i = 0; i < np; i++) if (ImGui::Selectable(pr[i].name, S.preset == i)) { S.preset = i; S.loMhz = (float)pr[i].loMhz; S.hiMhz = (float)pr[i].hiMhz; range = true; }
        ImGui::EndCombo();
    }
    flowNext(6 * gUi);
    ImGui::SetNextItemWidth(80 * gUi);
    ImGui::InputFloat("##salo", &S.loMhz, 0, 0, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit()) { S.preset = -1; range = true; }
    ImGui::SameLine(); ImGui::TextDisabled("to"); ImGui::SameLine();
    ImGui::SetNextItemWidth(80 * gUi);
    ImGui::InputFloat("MHz##sahi", &S.hiMhz, 0, 0, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit()) { S.preset = -1; range = true; }
    flowNext(10 * gUi);
    ImGui::TextDisabled("RBW"); ImGui::SameLine();
    ImGui::SetNextItemWidth(90 * gUi);
    if (ImGui::Combo("##sarbw", &S.rbw, kRbwName, 8)) other = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Resolution: the FFT bin width (the HackRF sweep goes no finer than about 2.8 kHz; the finest bins are slow over a wide range)");
    flowNext(10 * gUi);
    ImGui::TextDisabled("Avg"); ImGui::SameLine();
    ImGui::SetNextItemWidth(80 * gUi);
    if (ImGui::SliderInt("##saavg", &S.avgN, 1, 64)) other = true;
    flowNext(10 * gUi);
    if (ImGui::Checkbox("Remove spurs", &S.spurs)) other = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Take out the radio's DC spike / LO leak at the centre of every tuning step");
    flowEnd();
    // gains: the HackRF's three stages (other radios: the overall gain of the top bar)
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("LNA"); ImGui::SameLine();
    ImGui::SetNextItemWidth(90 * gUi);
    bool g = ImGui::SliderInt("##salna", &a.tune.lnaDb, 0, 40, "%d dB");
    if (g) a.tune.lnaDb = a.tune.lnaDb / 8 * 8;
    flowNext(6 * gUi);
    ImGui::TextDisabled("VGA"); ImGui::SameLine();
    ImGui::SetNextItemWidth(90 * gUi);
    if (ImGui::SliderInt("##savga", &a.tune.vgaDb, 0, 62, "%d dB")) { a.tune.vgaDb = a.tune.vgaDb / 2 * 2; g = true; }
    flowNext(6 * gUi);
    g |= ImGui::Checkbox("Amp", &a.tune.ampOn);
    if (g && a.engine.running()) a.engine.retune(a.tune);
    flowNext(12 * gUi);
    ImGui::Checkbox("Live", &S.showLive); ImGui::SameLine();
    ImGui::Checkbox("Peak", &S.showPeak); ImGui::SameLine();
    ImGui::Checkbox("Average", &S.showAvg); ImGui::SameLine();
    ImGui::Checkbox("Persistence", &S.persist); ImGui::SameLine();
    ImGui::Checkbox("Bands", &S.bands);
    flowNext(10 * gUi);
    if (ImGui::SmallButton("Reset peak")) a.engine.specan().resetPeak();
    ImGui::SameLine();
    {
        const auto& t = a.rx.specan;
        const size_t bins = t.live.size();
        if (ImGui::SmallButton("Export CSV") && live(a)) {
            char nm[64];
            const std::time_t now = std::time(nullptr);
            std::strftime(nm, sizeof nm, "sweep_%Y%m%d_%H%M%S.csv", std::localtime(&now));
            const std::string path = saveFileDialog(nm);
            if (!path.empty())
                S.note = dect2::saCsvWriteFile(path, dect2::saMakeGrid(t.loHz, t.loHz + t.binHz * bins, t.binHz), t.live, t.peak, t.avg) ? "saved " + path : "could not write " + path;
        }
        if (live(a) && bins) {
            const size_t est = dect2::saCsvEstimateBytes(bins);
            ImGui::SameLine();
            ImGui::TextDisabled("%zu bins of %.1f kHz, about %s%s", bins, t.binHz / 1e3, sizeText(est).c_str(), est > (6u << 20) ? " (large)" : "");
        }
    }
    if (!S.note.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", S.note.c_str()); }
    flowEnd();
    loadAutoPrefs();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Auto-save CSV every");
    ImGui::SameLine();
    bool pc = false;
    ImGui::SetNextItemWidth(70 * gUi);
    if (ImGui::InputInt("s##saasec", &S.autoSecs, 0, 0)) { S.autoSecs = std::max(1, S.autoSecs); pc = true; }
    flowNext(6 * gUi);
    ImGui::TextDisabled("keep"); ImGui::SameLine();
    ImGui::SetNextItemWidth(60 * gUi);
    if (ImGui::InputInt("files (0 = all)##saakeep", &S.autoKeep, 0, 0)) { S.autoKeep = std::max(0, S.autoKeep); pc = true; }
    flowNext(6 * gUi);
    ImGui::SetNextItemWidth(260 * gUi);
    if (ImGui::InputText("##saadir", S.autoDir, sizeof S.autoDir)) pc = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Folder...")) {   // pick any file name inside the wanted folder
        const std::string p = saveFileDialog("specan_folder.csv");
        if (!p.empty()) { const auto d = std::filesystem::path(p).parent_path().string(); snprintf(S.autoDir, sizeof S.autoDir, "%s", d.c_str()); pc = true; }
    }
    if (pc) saveAutoPrefs();
    flowNext(6 * gUi);
    if (ImGui::Checkbox("Auto-save on", &S.autoOn)) { S.autoNext = nowS(); if (S.autoOn) S.autoCount = 0, S.autoFiles.clear(); }
    {
        std::lock_guard<std::mutex> lk(autoMu());
        if (S.autoCount || S.autoOn) { ImGui::SameLine(); ImGui::TextDisabled("%d saved, last %s", S.autoCount, S.autoLast.empty() ? "-" : S.autoLast.c_str()); }
    }
    flowEnd();
    if (S.autoOn && live(a) && !S.autoBusy.load() && nowS() >= S.autoNext) {
        S.autoNext = nowS() + S.autoSecs;
        autoSave(a);
    }
    if (S.autoKeep > 0 && !S.autoBusy.load()) {   // trim the oldest of the files this run wrote
        std::lock_guard<std::mutex> lk(autoMu());
        while ((int)S.autoFiles.size() > S.autoKeep) { std::error_code ec; std::filesystem::remove(S.autoFiles.front(), ec); S.autoFiles.erase(S.autoFiles.begin()); }
    }
    if (range || other || !S.applied) { if (S.hiMhz < S.loMhz + 0.1f) S.hiMhz = S.loMhz + 0.1f; pushSettings(a, range); S.applied = true; }
}

void chart(App& a, float h) {
    const auto& t = a.rx.specan;
    const bool on = live(a);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float axisW = 44 * gUi, lh = ImGui::GetTextLineHeight();
    const float bandH = S.bands ? bandPlanStripHeight() : 0;
    const float l = p0.x + axisW, r = p0.x + w - 4 * gUi, top = p0.y + bandH + lh / 2 + 2;
    const float bot = p0.y + h - lh - 6 * gUi;
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(10, 11, 13, 255));
    const double lo = on ? t.loHz : S.loMhz * 1e6, hi = on ? t.loHz + t.binHz * t.live.size() : S.hiMhz * 1e6;
    auto X = [&](double hz) { return (float)(l + (hz - lo) / (hi - lo) * (r - l)); };
    auto Y = [&](float db) { return top + (S.refDb - db) / S.rangeDb * (bot - top); };
    const ImU32 grid = IM_COL32(40, 44, 50, 255), txt = IM_COL32(130, 136, 144, 255);
    char b[64];
    const float dStep = (bot - top) * 10 / S.rangeDb > lh * 1.3f ? 10.f : (bot - top) * 20 / S.rangeDb > lh * 1.3f ? 20.f : 50.f;
    for (float d = S.refDb; d >= S.refDb - S.rangeDb; d -= dStep) {
        dl->AddLine(ImVec2(l, Y(d)), ImVec2(r, Y(d)), grid);
        snprintf(b, sizeof b, "%.0f", d);
        dl->AddText(ImVec2(p0.x + 4, Y(d) - lh / 2), txt, b);
    }
    const double stepMhz = dect2::saTickStep(lo / 1e6, hi / 1e6, std::max(2, (int)((r - l) / (80 * gUi))));
    for (double f = std::ceil(lo / 1e6 / stepMhz) * stepMhz; f <= hi / 1e6 + 1e-9; f += stepMhz) {
        const float x = X(f * 1e6);
        dl->AddLine(ImVec2(x, top), ImVec2(x, bot), grid);
        snprintf(b, sizeof b, stepMhz < 0.1 ? "%.3f" : stepMhz < 1 ? "%.1f" : "%.0f", f);
        const float tw = ImGui::CalcTextSize(b).x;
        dl->AddText(ImVec2(std::clamp(x - tw / 2, p0.x, p0.x + w - tw - 2), bot + 3 * gUi), txt, b);
    }
    if (S.bands) drawBandPlanStrip(dl, l, r, p0.y, bandH, lo / 1e6, hi / 1e6);
    if (!on) {
        dl->AddText(ImVec2(l + 10, top + 10), txt, a.engine.running() ? "sweeping ..." : "start the receiver to sweep");
    } else {
        const int cols = std::max(2, (int)(r - l));
        if (S.persist && S.psImg) dl->AddImage(S.psImg->texture(), ImVec2(l, top), ImVec2(r, bot));
        auto trace = [&](const std::vector<float>& v, ImU32 c) {
            std::vector<ImVec2> pts((size_t)cols);
            for (int i = 0; i < cols; i++) pts[(size_t)i] = ImVec2(l + i, std::clamp(Y(colMax(v, t, lo, hi, i, cols)), top, bot));
            dl->AddPolyline(pts.data(), cols, c, 0, 1.2f * gUi);
        };
        if (S.showAvg) trace(t.avg, IM_COL32(120, 230, 120, 255));
        if (S.showPeak) trace(t.peak, IM_COL32(255, 170, 60, 200));
        if (S.showLive && !S.persist) trace(t.live, IM_COL32(110, 190, 255, 255));
        auto val = [&](double hz) { const long k = (long)((hz - t.loHz) / t.binHz); return k >= 0 && k < (long)t.live.size() ? t.live[(size_t)k] : -200.f; };
        auto marker = [&](double hz, ImU32 c, const char* nm) {
            if (hz <= 0 || hz < lo || hz > hi) return;
            const float x = X(hz), y = std::clamp(Y(val(hz)), top, bot);
            dl->AddLine(ImVec2(x, top), ImVec2(x, bot), c);
            dl->AddTriangleFilled(ImVec2(x - 5, y - 9), ImVec2(x + 5, y - 9), ImVec2(x, y), c);
            dl->AddText(ImVec2(x + 4, top + 2), c, nm);
        };
        marker(S.m1, IM_COL32(255, 240, 100, 255), "M1");
        marker(S.m2, IM_COL32(255, 120, 200, 255), "M2");
        float yy = top + 2;
        if (S.m1 > 0) { snprintf(b, sizeof b, "M1 %s  %.1f dBFS", mhzText(S.m1).c_str(), val(S.m1)); dl->AddText(ImVec2(r - 260 * gUi, yy), IM_COL32(255, 240, 100, 255), b); yy += lh; }
        if (S.m1 > 0 && S.m2 > 0) {
            snprintf(b, sizeof b, "M2-M1 %+.3f MHz  %+.1f dB", (S.m2 - S.m1) / 1e6, val(S.m2) - val(S.m1));
            dl->AddText(ImVec2(r - 260 * gUi, yy), IM_COL32(255, 120, 200, 255), b);
        }
    }
    ImGui::InvisibleButton("##sachart", ImVec2(w, h));
    if (on && ImGui::IsItemHovered()) {
        const float mx = ImGui::GetIO().MousePos.x;
        const double hz = lo + (mx - l) / std::max(1.f, r - l) * (hi - lo);
        if (mx >= l && mx <= r && ImGui::GetIO().MousePos.y >= top) {
            const long k = (long)((hz - t.loHz) / t.binHz);
            const float v = k >= 0 && k < (long)t.live.size() ? t.live[(size_t)k] : -200;
            const float pk = k >= 0 && k < (long)t.peak.size() ? t.peak[(size_t)k] : -200;
            const dect2::SaBand* bd = dect2::saAllocationAt(hz / 1e6);
            ImGui::SetTooltip("%s\n%.1f dBFS (peak %.1f)%s%s%s\nclick: marker 1, shift-click: marker 2, right-click: clear", mhzText(hz).c_str(), v, pk,
                              bd ? "\n" : "", bd ? bd->name : "", bd ? (std::string(" - ") + bd->use).c_str() : "");
            if (ImGui::IsMouseClicked(0)) { if (ImGui::GetIO().KeyShift) S.m2 = hz; else S.m1 = hz; }
            if (ImGui::IsMouseClicked(1)) S.m1 = S.m2 = 0;
        }
        if (ImGui::GetIO().MouseWheel != 0) S.refDb = std::clamp(S.refDb + ImGui::GetIO().MouseWheel * 5, -100.f, 30.f);
    }
}

void waterfall(App& a, float h) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(6, 7, 8, 255));
    if (live(a) && S.wfImg) dl->AddImage(S.wfImg->texture(), ImVec2(p0.x + 44 * gUi, p0.y), ImVec2(p0.x + w - 4 * gUi, p0.y + h));
    ImGui::Dummy(ImVec2(w, h));
}

// Burst monitor: watch one frequency sweep by sweep; a rise of Rise dB above the running median for at least Minimum ms is logged.
void monitor(App& a) {
    if (!ImGui::CollapsingHeader("Burst monitor (solar and Jupiter bursts)")) return;
    bool changed = ImGui::Checkbox("Watch", &S.monOn);
    ImGui::SameLine();
    changed |= ImGui::InputDouble("MHz", &S.monMhz, 0.01, 1.0, "%.3f");
    ImGui::SameLine();
    changed |= ImGui::SliderFloat("Rise (dB)", &S.monRiseDb, 1.f, 30.f, "%.0f");
    ImGui::SameLine();
    changed |= ImGui::InputFloat("Minimum (ms)", &S.monMinMs, 50.f, 500.f, "%.0f");
    if (changed) applyMonitor();
    if (!live(a)) ImGui::TextDisabled("Needs a running sweep that covers the watched frequency.");
    else ImGui::Text("Level %.1f dB, median %.1f dB, %s", S.monLast, S.monBase, S.mon.inBurst() ? "in a burst" : "quiet");
    ImGui::TextDisabled("One level per sweep, so the time resolution is one sweep. Sweeps are read while this tab is open.");
    if (ImGui::SmallButton("Clear log")) S.monLog.clear();
    ImGui::SameLine();
    ImGui::TextDisabled("Ended        MHz       Peak dB  Median dB  Duration ms");
    for (size_t i = S.monLog.size(); i-- > 0;) {
        const auto& e = S.monLog[i];
        char when[16] = "";
        std::strftime(when, sizeof when, "%H:%M:%S", std::localtime(&e.wall));
        ImGui::Text("%s  %10.3f  %8.1f  %9.1f  %11.0f", when, e.mhz, e.ev.peakDb, e.ev.baseDb, e.ev.durationMs);
    }
}

void tab(App& a) {
    ingest(a);
    if (gGfx && !S.wf.empty()) {
        if (!S.wfImg) S.wfImg = gGfx->createImage(State::kCols, State::kRows, 0xFF000000u);
        if (!S.psImg) S.psImg = gGfx->createImage(State::kCols, State::kLevels, 0xFF000000u);
        if (S.wfDirty) { S.wfImg->update(0, 0, State::kCols, State::kRows, S.wf.data()); S.wfDirty = false; }
        if (S.psDirty) { S.psImg->update(0, 0, State::kCols, State::kLevels, S.ps.data()); S.psDirty = false; }
    }
    controls(a);
    monitor(a);
    const float avail = ImGui::GetContentRegionAvail().y;
    chart(a, std::max(120 * gUi, avail * 0.62f));
    waterfall(a, std::max(40 * gUi, ImGui::GetContentRegionAvail().y - 2));
}

void list(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "sweeping" : "stopped"); return; }
    // the strongest bins of the sweep, one per allocation band
    const auto& t = a.rx.specan;
    std::vector<std::pair<float, size_t>> pk;
    for (size_t k = 1; k + 1 < t.live.size(); k++)
        if (t.live[k] > t.live[k - 1] && t.live[k] >= t.live[k + 1]) pk.push_back({t.live[k], k});
    std::sort(pk.rbegin(), pk.rend());
    int shown = 0;
    for (const auto& p : pk) {
        if (shown >= 15) break;
        const double hz = t.loHz + (p.second + 0.5) * t.binHz;
        const dect2::SaBand* bd = dect2::saAllocationAt(hz / 1e6);
        char l[96];
        snprintf(l, sizeof l, "%10.3f  %6.1f  %s##sp%d", hz / 1e6, p.first, bd ? bd->name : "", shown);
        if (ImGui::Selectable(l)) S.m1 = hz;
        shown++;
    }
}

void receiver(App& a) {
    const auto& t = a.rx.specan;
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "sweeping" : "stopped"); return; }
    ImGui::Text("%s", dect2::specanSummary(t).c_str());
    ImGui::TextWrapped("%s, %d tuning steps per sweep, %zu bins of %.1f kHz", t.native ? "HackRF native sweep (20 MHz steps)" : "stepped retuning", t.steps, t.live.size(), t.binHz / 1e3);
    ImGui::TextWrapped("%llu sweeps, %llu with gaps, input %.2f Msps", (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad, t.inputRate / 1e6);
}

void status(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State"); ImGui::SameLine();
    ImGui::TextUnformatted(live(a) ? dect2::specanSummary(a.rx.specan).c_str() : (a.engine.running() ? "sweeping" : "stopped"));
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "Spectrum analyser";
    if (live(a)) l2 = dect2::specanSummary(a.rx.specan);
}

} // namespace

extern const ModeUi kSpecanUi;
const ModeUi kSpecanUi = {
    .sideTitle = "PEAKS",
    .tabName = "Analyser",
    .tabIcon = Ic::Radio,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
};
