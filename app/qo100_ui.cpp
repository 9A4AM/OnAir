// QO-100 screens: the LNB oscillator (9750, 9739, 10000, 10600 MHz or custom) with the IF of the downlink worked out, tuning to the whole
// downlink or the narrowband transponder, the beacon lock (LNB error in kHz and ppm, drift), the narrowband transponder as a zoomable
// spectrum over a waterfall (a click puts the SSB dial there) with Listen, the wideband transponder with its DATV carriers and a
// hand-over to DVB-S/S2, and the drift history. Every frequency shown is the corrected downlink frequency once the beacons are locked.
#include "app.h"
#include "results_export.h"
#include "gfx.h"
#include "plot.h"
#include "dect2/qo100_dsp.h"
#include "dect2/qo100_gen.h"
#include "dect2/qo100_rx.h"
#include "dect2/qo100_tel.h"
#include "dect2/specx_measure.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

namespace q = dect2::qo100;

struct WfImage {
    gfx::Image* img = nullptr;
    int W = 1024, H = 220;
    std::vector<uint32_t> px;
    bool dirty = false;
    void push(const std::vector<float>& db, float lo, float hi) {
        if (px.empty()) px.assign((size_t)W * H, 0xFF000000u);
        std::copy_backward(px.begin(), px.end() - W, px.end());
        const size_t n = db.size();
        for (int x = 0; x < W && n; x++) {
            size_t a = (size_t)x * n / W, b = std::max(a + 1, (size_t)(x + 1) * n / W);
            float m = -200;
            for (size_t k = a; k < b && k < n; k++) m = std::max(m, db[k]);
            px[(size_t)x] = Waterfall::jet(std::clamp((m - lo) / std::max(1.f, hi - lo), 0.f, 1.f));
        }
        dirty = true;
    }
    void draw(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float u0, float u1) {
        if (!gGfx) return;
        if (!img) img = gGfx->createImage(W, H, 0xFF000000u);
        if (img && dirty) { img->update(0, 0, W, H, px.data()); dirty = false; }
        if (img) dl->AddImage(img->texture(), p0, p1, ImVec2(u0, 0), ImVec2(u1, 1));
    }
};

struct State {
    bool loaded = false;
    int loPreset = 0;               // loPresets index, or the count = custom
    double customLoMhz = 9750;
    double dialHz = 10489.600e6;
    bool listen = false;
    int view = 0;                   // 0 narrowband, 1 wideband, 2 drift
    double viewLo = q::kNbLoHz - 50e3, viewHi = q::kNbHiHz + 50e3;
    double sliceLo = 0, sliceHi = 0;   // the narrowband spectrum's range (corrected RF)
    float lo = -110, hi = -40, wlo = -110, whi = -40, peakHold = -200, wPeakHold = -200;
    uint64_t lastSeq = 0;
    WfImage wf;
    bool dragging = false, dragMoved = false;
    float dragX = 0;
    double dragV0 = 0, dragV1 = 0;
    char dialBuf[40] = "";
};
State S;

void tip(const char* t) { if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", t); }
bool live(const App& a) { return a.rx.standard == 54; }   // the engine reports its standard code minus one
const dect2::Qo100Telemetry& T(const App& a) { return a.rx.qo100; }

double loHz() {
    int n = 0; const q::LoPreset* p = q::loPresets(n);
    return S.loPreset >= 0 && S.loPreset < n ? p[S.loPreset].loHz : S.customLoMhz * 1e6;
}
std::string mhz(double hz, int dec = 3) { char b[40]; snprintf(b, sizeof b, "%.*f MHz", dec, hz / 1e6); return b; }

void load() {
    if (S.loaded) return;
    S.loaded = true;
    auto& p = plat::prefs();
    int n = 0; q::loPresets(n);
    S.loPreset = (int)std::clamp(p.getI("qo100LoPreset", 0), 0L, (long)n);
    S.customLoMhz = p.getD("qo100LoCustom", 9750);
    S.dialHz = p.getD("qo100Dial", 10489.600e6);
    S.listen = p.getB("qo100Listen", false);
    S.view = (int)std::clamp(p.getI("qo100View", 0), 0L, 2L);
}
void save() {
    auto& p = plat::prefs();
    p.setI("qo100LoPreset", S.loPreset); p.setD("qo100LoCustom", S.customLoMhz); p.setD("qo100Dial", S.dialHz); p.setI("qo100View", S.view);
}

void setDial(double hz) { S.dialHz = std::round(hz / 10) * 10; save(); }

void tuneRf(App& a, double rfHz) { tuneFreq(a, q::ifOf(rfHz, loHz()) / 1e6); }

void openInDvbs(App& a, const dect2::Qo100Datv& d) {
    const double msym = std::round(d.symRate / 1e3) / 1e3;
    if (a.engine.running()) a.engine.stop();
    selectMode(a, 6);
    tuneFreq(a, d.ifHz / 1e6);
    dvbsSetSymbolRate(a, msym);
    startReceiver(a);
}

// LNB, IF and where the radio is
void strip(App& a) {
    int n = 0; const q::LoPreset* pr = q::loPresets(n);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("LNB LO"); flowNext(4 * gUi);
    ImGui::SetNextItemWidth(250 * gUi);
    if (ImGui::BeginCombo("##qolo", S.loPreset < n ? pr[S.loPreset].name : "Custom")) {
        for (int i = 0; i <= n; i++) if (ImGui::Selectable(i < n ? pr[i].name : "Custom", S.loPreset == i)) { S.loPreset = i; save(); }
        ImGui::EndCombo();
    }
    tip("The LNB's local oscillator: 9750 MHz for a standard (Bullseye, Octagon) Ku LNB; an LO above 10489.5 MHz mirrors the spectrum (the tool turns it back)");
    if (S.loPreset >= n) {
        flowNext(4 * gUi);
        ImGui::SetNextItemWidth(100 * gUi);
        if (ImGui::InputDouble("##qolocus", &S.customLoMhz, 0, 0, "%.3f MHz", ImGuiInputTextFlags_EnterReturnsTrue)) { S.customLoMhz = std::clamp(S.customLoMhz, 1.0, 20000.0); save(); }
        tip("The oscillator frequency of your LNB or converter");
    }
    const double lo = loHz();
    double i0 = q::ifOf(q::kDownLoHz, lo), i1 = q::ifOf(q::kDownHiHz, lo);
    if (i0 > i1) std::swap(i0, i1);
    flowNext(10 * gUi);
    ImGui::TextDisabled("downlink 10489.500 - 10500.000 MHz = IF %.3f - %.3f MHz%s", i0 / 1e6, i1 / 1e6, q::invertedLo(lo) ? " (inverted)" : "");
    flowBreak();
    if (ImGui::Button("Whole downlink")) tuneRf(a, q::kWholeCentreHz);
    tip("Tune the radio to 10494.0 MHz (both beacons and the DATV channels): needs 10 Msps");
    flowNext(6 * gUi);
    if (ImGui::Button("Narrowband only")) tuneRf(a, q::kNarrowCentreHz);
    tip("Tune the radio to 10490.1 MHz: the narrowband transponder and its beacons, 2 Msps is enough");
    flowNext(10 * gUi);
    const double radioRf = q::rfOf(a.tune.centerHz, lo);
    ImGui::TextDisabled("radio %.3f MHz = %s", a.tune.centerHz / 1e6, mhz(radioRf).c_str());
    flowNext(10 * gUi);
    sampleRateControl(a);
    flowEnd();
}

void lockLine(const App& a) {
    const auto& t = T(a);
    const int st = live(a) ? t.state : -1;
    lamp("Beacons", st == 2 ? 1 : st == 3 ? 2 : st == 1 ? 3 : 0, (int)Ic::Lock); flowNext(10 * gUi);
    if (!live(a)) ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "Start the receiver to lock onto the beacons");
    else if (st == 0) ImGui::TextColored(pal::warnAmber(), "the narrowband transponder is outside the radio's band: tune to it (Narrowband only) or raise the sample rate");
    else if (!t.corrValid) ImGui::TextDisabled("searching for the CW beacons (10489.500 and 10490.000 MHz)");
    else {
        ImGui::Text("LNB error %+.3f kHz (%+.3f ppm)", t.lnbErrHz / 1e3, t.lnbErrPpm);
        tip("The LNB's oscillator is this far from nominal (and the radio's own error with it): measured on the CW beacons, every frequency here is corrected by it");
        flowNext(10 * gUi);
        if (t.driftValid) ImGui::Text("drift %+.0f Hz/min", t.driftHzPerMin); else ImGui::TextDisabled("drift: measuring");
        flowNext(10 * gUi);
        ImGui::TextDisabled("%s, %llu measurements", st == 2 ? "locked" : "held (beacons lost)", (unsigned long long)t.measurements);
        for (int i = 0; i < 3; i++) {
            flowNext(10 * gUi);
            static const char* const kN[] = {"low CW", "PSK", "high CW"};
            const auto& b = t.beacons[i];
            if (b.found) ImGui::Text("%s %.0f dB", kN[i], b.snrDb); else ImGui::TextDisabled("%s -", kN[i]);
        }
    }
    flowEnd();
}

void gestures(float l, float r, bool hov, double& lo, double& hi, double minSpan, double maxLo, double maxHi, bool clickDial) {
    ImGuiIO& io = ImGui::GetIO();
    const double span = hi - lo;
    auto fAt = [&](float x) { return lo + (x - l) / std::max(1.f, r - l) * span; };
    if (hov) {
        const double f = fAt(io.MousePos.x);
        ImGui::SetTooltip("%s%s", mhz(f, 6).c_str(), clickDial ? "\nclick: SSB dial here, wheel: zoom, drag: pan" : "");
        if (io.MouseWheel != 0) {
            const double ns = std::clamp(span * std::pow(0.8, io.MouseWheel), minSpan, maxHi - maxLo), fr = (f - lo) / span;
            lo = f - fr * ns; hi = lo + ns;
        }
        if (ImGui::IsMouseClicked(0)) { S.dragging = true; S.dragMoved = false; S.dragX = io.MousePos.x; S.dragV0 = lo; S.dragV1 = hi; }
    }
    if (S.dragging) {
        if (std::fabs(io.MousePos.x - S.dragX) > 3) S.dragMoved = true;
        if (S.dragMoved) { const double d = (io.MousePos.x - S.dragX) / std::max(1.f, r - l) * (S.dragV1 - S.dragV0); lo = S.dragV0 - d; hi = S.dragV1 - d; }
        if (!ImGui::IsMouseDown(0)) { if (!S.dragMoved && clickDial) setDial(fAt(S.dragX)); S.dragging = false; }
    }
    const double s = hi - lo;
    if (lo < maxLo) { lo = maxLo; hi = lo + s; }
    if (hi > maxHi) { hi = maxHi; lo = std::max(maxLo, hi - s); }
}

void narrowView(App& a, float h) {
    const auto& t = T(a);
    const bool on = live(a) && !t.nb.empty();
    std::vector<double> xs, ys;
    if (on) { xs.resize(t.nb.size()); ys.resize(t.nb.size()); for (size_t k = 0; k < t.nb.size(); k++) { xs[k] = (t.nbF0Hz + k * t.nbBinHz) / 1e6; ys[k] = t.nb[k]; } }
    const float sh = std::max(80.f, h * 0.55f), wh = std::max(40.f, h - sh - ImGui::GetStyle().ItemSpacing.y);
    float l = 0, r = 0, top = 0, bot = 0; bool hov = false;
    if (plt::BeginPlot("##qonb", ImVec2(-1, sh), plt::Flags_NoLegend | plt::Flags_NoTitle | plt::Flags_NoMouseText)) {
        plt::SetupAxes("MHz", "dBFS");
        plt::SetupAxisLimits(plt::X1, S.viewLo / 1e6, S.viewHi / 1e6, plt::Cond_Always);
        plt::SetupAxisLimits(plt::Y1, S.lo, S.hi, plt::Cond_Always);
        plt::SetupAxisFormat(plt::X1, "%.3f");
        { plt::Spec s; s.FillColor = ImVec4(0.4f, 0.8f, 0.5f, S.listen ? 0.18f : 0.08f); plt::PlotVBand("USB", (S.dialHz + 300) / 1e6, (S.dialHz + 3000) / 1e6, s); }
        if (!ys.empty()) { plt::Spec s; s.LineColor = pal::accent(); plt::PlotLine("nb", xs.data(), ys.data(), (int)ys.size(), s); }
        const double bx[3] = {q::kLowBeaconHz / 1e6, q::kPskBeaconHz / 1e6, q::kHighBeaconHz / 1e6};
        { plt::Spec s; s.LineColor = ImVec4(1.f, 0.82f, 0.35f, 0.35f); plt::PlotInfLines("beacons", bx, 3, s); }
        const double dx = S.dialHz / 1e6;
        { plt::Spec s; s.LineColor = ImVec4(0.4f, 0.95f, 0.5f, 0.8f); plt::PlotInfLines("dial", &dx, 1, s); }
        if (!on) plt::PlotText(a.engine.running() ? "waiting for the narrowband transponder" : "Start the receiver", (S.viewLo + S.viewHi) / 2e6, (S.lo + S.hi) / 2);
        const ImVec2 p = plt::GetPlotPos(), sz = plt::GetPlotSize();
        l = p.x; r = p.x + sz.x; top = p.y; bot = p.y + sz.y;
        hov = plt::IsPlotHovered();
        plt::EndPlot();
    }
    (void)top; (void)bot;
    const double minLo = q::kNbLoHz - 50e3, maxHi = q::kNbHiHz + 50e3;
    if (r > l) gestures(l, r, hov, S.viewLo, S.viewHi, 3e3, minLo, maxHi, true);
    // waterfall: the image covers the published slice
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + wh), IM_COL32(8, 9, 10, 255));
    const float x0 = l > 0 ? l : p0.x, x1 = r > l ? r : p0.x + w;
    if (S.sliceHi > S.sliceLo) {
        const double sp = S.sliceHi - S.sliceLo;
        S.wf.draw(dl, ImVec2(x0, p0.y), ImVec2(x1, p0.y + wh), (float)((S.viewLo - S.sliceLo) / sp), (float)((S.viewHi - S.sliceLo) / sp));
    }
    const float dxp = (float)(x0 + (S.dialHz - S.viewLo) / (S.viewHi - S.viewLo) * (x1 - x0));
    if (dxp >= x0 && dxp <= x1) dl->AddLine(ImVec2(dxp, p0.y), ImVec2(dxp, p0.y + wh), IM_COL32(100, 240, 130, 150));
    ImGui::InvisibleButton("##qowf", ImVec2(w, wh));
    const bool whov = ImGui::IsItemHovered();
    if (whov) ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    gestures(x0, x1, whov, S.viewLo, S.viewHi, 3e3, minLo, maxHi, true);
}

void wideView(App& a, float h) {
    const auto& t = T(a);
    const bool on = live(a) && !t.wb.empty();
    std::vector<double> xs, ys;
    if (on) { xs.resize(t.wb.size()); ys.resize(t.wb.size()); for (size_t k = 0; k < t.wb.size(); k++) { xs[k] = (t.wbF0Hz + k * t.wbBinHz) / 1e6; ys[k] = t.wb[k]; } }
    const float tw = std::min(ImGui::GetContentRegionAvail().x * 0.45f, 460 * gUi);
    const float pw = ImGui::GetContentRegionAvail().x - tw - 8 * gUi;
    if (plt::BeginPlot("##qowb", ImVec2(pw, h), plt::Flags_NoLegend | plt::Flags_NoTitle)) {
        plt::SetupAxes("MHz", "dBFS");
        const double x0 = on ? xs.front() : q::kDownLoHz / 1e6, x1 = on ? xs.back() : q::kDownHiHz / 1e6;
        plt::SetupAxisLimits(plt::X1, x0, x1, plt::Cond_Always);
        plt::SetupAxisLimits(plt::Y1, S.wlo, S.whi, plt::Cond_Always);
        plt::SetupAxisFormat(plt::X1, "%.2f");
        { plt::Spec s; s.FillColor = ImVec4(0.5f, 0.6f, 1.f, 0.07f); plt::PlotVBand("wb", q::kWbLoHz / 1e6, q::kWbHiHz / 1e6, s); }
        { plt::Spec s; s.FillColor = ImVec4(1.f, 0.8f, 0.4f, 0.07f); plt::PlotVBand("nb", q::kNbLoHz / 1e6, q::kNbHiHz / 1e6, s); }
        if (!ys.empty()) { plt::Spec s; s.LineColor = pal::accent(); plt::PlotLine("wb", xs.data(), ys.data(), (int)ys.size(), s); }
        for (const auto& d : t.datv) {
            char b[48]; snprintf(b, sizeof b, "%.0f ks", d.symRate / 1e3);
            plt::PlotText(b, d.rfHz / 1e6, S.whi - 4);
        }
        if (!on) plt::PlotText(a.engine.running() ? "starting" : "Start the receiver", (x0 + x1) / 2, (S.wlo + S.whi) / 2);
        plt::EndPlot();
    }
    ImGui::SameLine();
    ImGui::BeginChild("##qodatv", ImVec2(tw, h), false);
    if (!on) ImGui::TextDisabled("no spectrum yet");
    else if (!t.wbOk) ImGui::TextDisabled("the wideband transponder (10491 - 10499 MHz) is outside the radio's band: Whole downlink, 10 Msps");
    else if (t.datv.empty()) ImGui::TextDisabled("no DATV carrier seen");
    else if (ImGui::BeginTable("##qodt", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Downlink"); ImGui::TableSetupColumn("Symbol rate"); ImGui::TableSetupColumn("SNR"); ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        int i = 0;
        for (const auto& d : t.datv) {
            ImGui::PushID(i++);
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::Text("%.3f%s", d.rfHz / 1e6, d.beacon ? " beacon" : "");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("downlink %s, at the radio %s%s\n-3 dB width %.0f kHz", mhz(d.rfHz).c_str(), mhz(d.ifHz).c_str(), t.inverted ? " (inverted spectrum)" : "", d.bw3Hz / 1e3);
            ImGui::TableNextColumn(); ImGui::Text("%.0f ks/s", d.symRate / 1e3);
            ImGui::TableNextColumn(); ImGui::Text("%.1f dB", d.snrDb);
            ImGui::TableNextColumn();
            if (d.looksDvbs) {
                if (ImGui::SmallButton("Decode with DVB-S/S2")) openInDvbs(a, d);
                tip("Switch to the DVB-S/S2 mode at this carrier's frequency and symbol rate");
            } else ImGui::TextDisabled("-");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (on && !t.datv.empty()) {
        static std::string note;
        resexp::Table x; x.cols = {"time", "frequency_hz", "mode", "beacon", "symbol_rate_sps", "snr_db", "bandwidth_3db_hz", "dvbs_like"};
        const std::string now = resexp::stamp();
        for (const auto& d : t.datv) x.add({now, resexp::num(d.rfHz, 0), "QO-100", d.beacon ? "yes" : "no", resexp::num(d.symRate, 0), resexp::num(d.snrDb, 1), resexp::num(d.bw3Hz, 0), d.looksDvbs ? "yes" : "no"});
        resexp::saveButton(x, "qo100", note, "svqo");
        if (!note.empty()) ImGui::TextDisabled("%s", note.c_str());
    }
    ImGui::EndChild();
}

void driftView(App& a, float h) {
    const auto& t = T(a);
    if (!live(a) || t.histT.empty()) { ImGui::TextDisabled("no measurement yet: the history starts when the beacons lock"); return; }
    std::vector<double> xs(t.histT.begin(), t.histT.end()), ys(t.histErr.size());
    for (size_t i = 0; i < ys.size(); i++) ys[i] = t.histErr[i] / 1e3;
    auto mm = std::minmax_element(ys.begin(), ys.end());
    const double pad = std::max(0.02, (*mm.second - *mm.first) * 0.15);
    if (plt::BeginPlot("##qodrift", ImVec2(-1, h), plt::Flags_NoLegend | plt::Flags_NoTitle)) {
        plt::SetupAxes("s", "LNB error kHz");
        plt::SetupAxisLimits(plt::X1, xs.front(), std::max(xs.front() + 1, xs.back()), plt::Cond_Always);
        plt::SetupAxisLimits(plt::Y1, *mm.first - pad, *mm.second + pad, plt::Cond_Always);
        plt::SetupAxisFormat(plt::Y1, "%.3f");
        plt::Spec s; s.LineColor = pal::accent(); s.Marker = plt::Marker_Circle; s.MarkerSize = 2;
        plt::PlotLine("err", xs.data(), ys.data(), (int)xs.size(), s);
        plt::EndPlot();
    }
}

void listenRow(App& a) {
    listenControls(a, S.listen, "qo100Listen", "qo100", "Play the upper sideband, 300 - 3000 Hz above the dial (corrected frequency), on the sound card");
    flowNext(10 * gUi);
    ImGui::TextDisabled("dial"); flowNext(4 * gUi);
    ImGui::SetNextItemWidth(150 * gUi);
    if (ImGui::InputTextWithHint("##qodial", mhz(S.dialHz, 6).c_str(), S.dialBuf, sizeof S.dialBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
        double hz;
        if (dect2::specx::parseFreq(S.dialBuf, hz)) { if (hz < 1e9) hz += 10e9; setDial(hz); }   // "489.6M" means 10489.6 MHz
        S.dialBuf[0] = 0;
    }
    tip("The USB dial (suppressed carrier) on the downlink: 10489.6M, or click the spectrum");
    flowNext(4 * gUi);
    if (ImGui::SmallButton("-100")) setDial(S.dialHz - 100);
    flowNext(2 * gUi);
    if (ImGui::SmallButton("+100")) setDial(S.dialHz + 100);
    if (live(a) && T(a).listening) { flowNext(10 * gUi); ImGui::TextDisabled("audio %.0f dBFS", T(a).audioDb); }
    flowEnd();
}

void tick(App& a) {
    load();
    auto& r = a.engine.qo100();
    r.setLo(loHz());
    r.setDialHz(S.dialHz);
    r.setListen(S.listen);
    r.setVolume(a.volume); r.setMuted(a.muted);
    const auto& t = T(a);
    if (!live(a) || t.seq == S.lastSeq) return;
    S.lastSeq = t.seq;
    if (!t.nb.empty()) {
        // a steady scale: whole 10 dB steps, moved only when the floor or the held peak really leave it (following every
        // update made the spectrum jump up and down as SSB stations came and went)
        S.peakHold = std::max(t.nbPeakDb, S.peakHold - 0.05f);
        const float lo = std::floor((t.nbFloorDb - 8) / 10) * 10, hi = std::max(lo + 50, std::ceil((S.peakHold + 5) / 10) * 10);
        if (S.lo == S.hi || std::fabs(lo - S.lo) >= 10 || hi > S.hi || S.hi - hi >= 20) { S.lo = lo; S.hi = hi; }
        S.wf.push(t.nb, t.nbFloorDb - 3, t.nbFloorDb + 35);
        S.sliceLo = t.nbF0Hz; S.sliceHi = t.nbF0Hz + t.nb.size() * t.nbBinHz;
    }
    if (!t.wb.empty()) {
        S.wPeakHold = std::max(t.wbPeakDb, S.wPeakHold - 0.05f);
        const float lo = std::floor((t.wbFloorDb - 8) / 10) * 10, hi = std::max(lo + 40, std::ceil((S.wPeakHold + 5) / 10) * 10);
        if (S.wlo == S.whi || std::fabs(lo - S.wlo) >= 10 || hi > S.whi || S.whi - hi >= 20) { S.wlo = lo; S.whi = hi; }   // steady, as above
    }
}

void tab(App& a) {
    load();
    // the strip wraps to another line and back as its texts change width: keep the tallest it has been at this width, so the
    // plots under it do not jump
    static float stripH = 0, stripW = 0;
    const float y0 = ImGui::GetCursorPosY(), w0 = ImGui::GetContentRegionAvail().x;
    if (w0 != stripW) { stripW = w0; stripH = 0; }
    strip(a);
    const float used = ImGui::GetCursorPosY() - y0;
    stripH = std::max(stripH, used);
    if (used + ImGui::GetStyle().ItemSpacing.y < stripH) ImGui::Dummy(ImVec2(1, stripH - used - ImGui::GetStyle().ItemSpacing.y));
    // a fixed two-line height: the line wraps as its numbers change width, which would move the plots under it every frame
    ImGui::BeginChild("##lock", ImVec2(0, 2 * ImGui::GetTextLineHeightWithSpacing() + 2 * gUi), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    lockLine(a);
    ImGui::EndChild();
    static const char* const kV[] = {"Narrowband", "Wideband", "Drift"};
    for (int i = 0; i < 3; i++) {
        if (i) ImGui::SameLine();
        if (ImGui::RadioButton(kV[i], S.view == i)) { S.view = i; save(); }
    }
    if (S.view == 0) { ImGui::SameLine(0, 16 * gUi); listenRow(a); }
    const float h = std::max(120.f, ImGui::GetContentRegionAvail().y - 2);
    if (S.view == 0) narrowView(a, h);
    else if (S.view == 1) wideView(a, h);
    else driftView(a, h);
}

void list(App& a) {
    load();
    const auto& t = T(a);
    if (!live(a) || t.signals.empty()) { ImGui::TextDisabled("no signals yet"); return; }
    ImGui::TextDisabled("narrowband, %s", t.corrValid ? "corrected" : "not corrected yet");
    int i = 0;
    for (const auto& s : t.signals) {
        static const char* const kB[] = {"CW beacon", "PSK beacon", "CW beacon"};
        char b[96];
        snprintf(b, sizeof b, "%.4f  %4.0f dB  %s##qs%d", s.rfHz / 1e6, s.snrDb, s.beacon >= 0 ? kB[s.beacon] : s.widthHz > 400 ? "SSB" : "carrier", i++);
        if (ImGui::Selectable(b)) { setDial(s.widthHz > 400 ? s.rfHz - 600 : s.rfHz - 700); S.view = 0; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s, %.0f Hz wide\nclick: put the dial on it", mhz(s.rfHz, 6).c_str(), s.widthHz);
    }
}

void receiver(App& a) {
    const auto& t = T(a);
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    ImGui::TextWrapped("%s", dect2::qo100Summary(t).c_str());
    ImGui::TextWrapped("Radio %s at %.3f Msps = %s - %s (LO %s%s)", mhz(t.centreIfHz).c_str(), t.rate / 1e6, mhz(t.wbF0Hz).c_str(),
                       mhz(t.wbF0Hz + t.wb.size() * t.wbBinHz).c_str(), mhz(t.loHz).c_str(), t.inverted ? ", inverted" : "");
    ImGui::TextWrapped("Narrowband %s, %.0f Hz bins; wideband %s, %.1f kHz bins", t.nbOk ? "in the band" : "outside the band", t.nbBinHz, t.wbOk ? "in the band" : "outside the band", t.wbBinHz / 1e3);
    for (int i = 0; i < 3; i++) {
        const auto& b = t.beacons[i];
        ImGui::Text("beacon %s: %s", mhz(b.nominalHz).c_str(), b.found ? "" : "not found");
        if (b.found) { ImGui::SameLine(0, 0); ImGui::Text("at %s before correction, %.1f dB", mhz(b.rfHz, 6).c_str(), b.snrDb); }
    }
    if (t.corrValid) ImGui::TextWrapped("LNB oscillator %s (nominal %+.3f kHz, %+.3f ppm)", mhz(t.loHz + t.lnbErrHz, 6).c_str(), t.lnbErrHz / 1e3, t.lnbErrPpm);
}

void status(App& a) {
    StatusPanel panel;
    const bool on = live(a);
    lamp("QO-100", on ? (T(a).state == 2 ? 1 : T(a).state == 3 ? 2 : 3) : 0, (int)Ic::Globe); flowNext(12 * gUi);
    ImGui::TextUnformatted(on ? dect2::qo100Summary(T(a)).c_str() : (a.engine.running() ? "starting" : "stopped"));
    flowEnd();
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "QO-100";
    if (live(a)) {
        const auto& t = T(a);
        char b[128];
        if (t.corrValid) snprintf(b, sizeof b, "LNB %+.3f kHz (%+.2f ppm)%s", t.lnbErrHz / 1e3, t.lnbErrPpm, t.state == 3 ? ", held" : "");
        else snprintf(b, sizeof b, "%s", t.state == 0 ? "narrowband not in the band" : "searching for the beacons");
        l2 = b;
    }
}

void synth(App& a, bool& changed) {
    dect2::SynthConfig& sc = a.tune.synth;
    { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("the beacons, a test carrier at 10489.650, four SSB stations and two DATV carriers, through an LNB 27 kHz off that drifts"); ImGui::PopTextWrapPos(); }
    bool quiet = sc.modeOpt[0] == 1;
    if (ImGui::Checkbox("Noise only", &quiet)) { sc.modeOpt[0] = quiet ? 1 : 0; changed = true; }
    tip("No signals: the beacons must not lock");
    float err = (float)((dect2::kQo100SynthErrHz + sc.modeVal[0]) / 1e3);
    ImGui::SetNextItemWidth(160 * gUi);
    if (ImGui::SliderFloat("LNB error", &err, -300, 300, "%.1f kHz")) { sc.modeVal[0] = err * 1e3 - dect2::kQo100SynthErrHz; changed = true; }
    float dr = (float)(sc.modeVal[1] != 0 ? sc.modeVal[1] : dect2::kQo100SynthDriftHzPerMin);
    ImGui::SetNextItemWidth(160 * gUi);
    if (ImGui::SliderFloat("Drift", &dr, -600, 600, "%.0f Hz/min")) { sc.modeVal[1] = dr == 0 ? 0.001 : dr; changed = true; }
}

} // namespace

extern const ModeUi kQo100Ui;
const ModeUi kQo100Ui = {
    .sideTitle = "SIGNALS",
    .tabName = "QO-100",
    .tabIcon = Ic::Globe,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .synth = synth,
    .tick = tick,
};
