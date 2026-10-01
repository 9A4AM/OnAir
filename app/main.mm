// DecT2 — DVB-T2 receiver for macOS. Phase 0 shell: sources, spectrum, waterfall, status, log.
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_metal.h"
#include "implot.h"

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include "dect2/engine.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

using namespace dect2;

// ------------------------------------------------------------------ waterfall texture

struct Waterfall {
    static constexpr int W = 1024, H = 512;
    id<MTLTexture> tex = nil;
    std::vector<uint32_t> lut;
    int writeRow = 0; // newest row; older rows follow at writeRow+1 ... wrapping
    int filled = 0;
    float minDb = -110, maxDb = -40;

    void init(id<MTLDevice> dev) {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:W height:H mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead;
        tex = [dev newTextureWithDescriptor:d];
        std::vector<uint32_t> black((size_t)W * H, 0xFF000000u);
        [tex replaceRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0 withBytes:black.data() bytesPerRow:W * 4];
        lut.resize(256);
        for (int i = 0; i < 256; i++) lut[i] = jet(i / 255.0f);
    }
    static uint32_t jet(float v) {
        auto c = [](float x) { return std::min(1.f, std::max(0.f, x)); };
        float r = c(1.5f - std::fabs(4 * v - 3)), g = c(1.5f - std::fabs(4 * v - 2)), b = c(1.5f - std::fabs(4 * v - 1));
        // fade the very bottom to black so the noise floor is calm
        float fade = c(v * 6.0f);
        return 0xFF000000u | ((uint32_t)(b * fade * 255) << 16) | ((uint32_t)(g * fade * 255) << 8) | (uint32_t)(r * fade * 255);
    }
    void push(const std::vector<float>& dbfs) {
        std::vector<uint32_t> row(W);
        size_t n = dbfs.size();
        for (int x = 0; x < W; x++) {
            size_t a = (size_t)x * n / W, b = std::max(a + 1, (size_t)(x + 1) * n / W);
            float m = -200;
            for (size_t k = a; k < b; k++) m = std::max(m, dbfs[k]);
            float v = (m - minDb) / (maxDb - minDb);
            row[x] = lut[(int)(std::min(1.f, std::max(0.f, v)) * 255)];
        }
        writeRow = (writeRow + H - 1) % H;
        [tex replaceRegion:MTLRegionMake2D(0, writeRow, W, 1) mipmapLevel:0 withBytes:row.data() bytesPerRow:W * 4];
        filled = std::min(H, filled + 1);
    }
};

static std::string gForceTab; // dev: --tab <name> selects a tab for screenshots
static bool tabItem(const char* name) {
    ImGuiTabItemFlags fl = 0;
    if (!gForceTab.empty() && gForceTab == name) { fl = ImGuiTabItemFlags_SetSelected; gForceTab.clear(); }
    return ImGui::BeginTabItem(name, nullptr, fl);
}

// ------------------------------------------------------------------ app state

struct BwChoice { const char* label; double mhz; double nativeMsps; };
static const BwChoice kBw[] = {
    {"8 MHz", 8, 64.0 / 7}, {"7 MHz", 7, 8.0}, {"6 MHz", 6, 48.0 / 7}, {"5 MHz", 5, 40.0 / 7}, {"1.7 MHz", 1.7, 131.0 / 71},
};

struct App {
    Engine engine;
    std::vector<DeviceInfo> devices; // [0]=synthetic, [1]=file, then HackRFs
    int devIdx = 0;
    TuneSettings tune;
    FileOptions file;
    int bwIdx = 0;
    double freqMhz = 522.0;
    bool autoScroll = true;
    int tab = 0;
    SpectrumFrame spec;
    std::vector<float> smooth, peak;
    uint64_t lastSeq = 0;
    Waterfall wf;
    float yMin = -110, yMax = -30;
    bool peakHold = true;
    double lastFrameT = 0, frameDt = 1.0 / 30;
    ImFont* mono = nullptr;
    std::string hackrfErr;
    RxTelemetry rx;
    uint64_t rxSeq = 0;
    std::deque<float> hCfo, hSnr, hTiming;
    bool rxSeen = false;
};

static void refreshDevices(App& a) {
    a.devices.clear();
    DeviceInfo s; s.kind = DeviceInfo::Synthetic; s.name = "Synthetic test signal (DVB-T2 8K, 8 MHz)"; a.devices.push_back(s);
    DeviceInfo f; f.kind = DeviceInfo::File; f.name = "IQ recording file…"; a.devices.push_back(f);
    std::string err;
    for (auto& d : listHackrfDevices(err)) a.devices.push_back(d);
    a.hackrfErr = err;
    a.engine.log("device scan: " + std::to_string(a.devices.size() - 2) + " HackRF found");
}

static void applyBandwidth(App& a) {
    a.tune.sampleRate = std::min(20e6, kBw[a.bwIdx].nativeMsps * 2e6);
    a.tune.basebandFilterHz = 0;
    a.tune.bandwidthMhz = kBw[a.bwIdx].mhz;
}

static std::string openFileDialog() {
    NSOpenPanel* p = [NSOpenPanel openPanel];
    p.canChooseFiles = YES; p.canChooseDirectories = NO; p.allowsMultipleSelection = NO;
    if ([p runModal] == NSModalResponseOK) return std::string(p.URL.path.UTF8String);
    return {};
}

static void loadPrefs(App& a) {
    NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
    if ([d objectForKey:@"freqMhz"]) a.freqMhz = [d doubleForKey:@"freqMhz"];
    if ([d objectForKey:@"lna"]) a.tune.lnaDb = (int)[d integerForKey:@"lna"];
    if ([d objectForKey:@"vga"]) a.tune.vgaDb = (int)[d integerForKey:@"vga"];
    a.tune.ampOn = [d boolForKey:@"amp"];
    a.bwIdx = std::min((int)(sizeof kBw / sizeof *kBw) - 1, (int)[d integerForKey:@"bw"]);
    NSString* fp = [d stringForKey:@"filePath"];
    if (fp) a.file.path = fp.UTF8String;
}

static void savePrefs(const App& a) {
    NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
    [d setDouble:a.freqMhz forKey:@"freqMhz"];
    [d setInteger:a.tune.lnaDb forKey:@"lna"];
    [d setInteger:a.tune.vgaDb forKey:@"vga"];
    [d setBool:a.tune.ampOn forKey:@"amp"];
    [d setInteger:a.bwIdx forKey:@"bw"];
    [d setObject:[NSString stringWithUTF8String:a.file.path.c_str()] forKey:@"filePath"];
}

// ------------------------------------------------------------------ UI pieces

static void lamp(const char* label, int state /*0 grey 1 green 2 amber 3 red*/) {
    ImVec4 c = state == 1 ? ImVec4(0.30f, 0.85f, 0.35f, 1) : state == 2 ? ImVec4(0.95f, 0.70f, 0.15f, 1)
             : state == 3 ? ImVec4(0.90f, 0.25f, 0.25f, 1) : ImVec4(0.30f, 0.32f, 0.35f, 1);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float r = 5;
    dl->AddCircleFilled(ImVec2(p.x + r + 1, p.y + ImGui::GetTextLineHeight() * 0.5f), r, ImGui::ColorConvertFloat4ToU32(c));
    ImGui::Dummy(ImVec2(r * 2 + 4, ImGui::GetTextLineHeight()));
    ImGui::SameLine(0, 0);
    ImGui::TextDisabled("%s", label);
}

static void toolbar(App& a) {
    bool running = a.engine.running();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.f, 1), "DecT2");
    ImGui::SameLine(0, 14);
    ImGui::TextDisabled("source");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300);
    ImGui::BeginDisabled(running);
    if (ImGui::BeginCombo("##src", a.devices[a.devIdx].name.c_str())) {
        for (int i = 0; i < (int)a.devices.size(); i++)
            if (ImGui::Selectable(a.devices[i].name.c_str(), i == a.devIdx)) a.devIdx = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) refreshDevices(a), a.devIdx = std::min(a.devIdx, (int)a.devices.size() - 1);
    ImGui::EndDisabled();

    bool isHw = a.devices[a.devIdx].kind == DeviceInfo::HackRF;
    bool isFile = a.devices[a.devIdx].kind == DeviceInfo::File;

    ImGui::SameLine(0, 14);
    ImGui::TextDisabled("freq");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    bool retune = false;
    ImGui::InputDouble("##freq", &a.freqMhz, 0, 0, "%.3f MHz");
    if (ImGui::IsItemDeactivatedAfterEdit()) retune = true;
    ImGui::SameLine();
    ImGui::TextDisabled("BW");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::BeginDisabled(running);
    if (ImGui::BeginCombo("##bw", kBw[a.bwIdx].label)) {
        for (int i = 0; i < (int)(sizeof kBw / sizeof *kBw); i++)
            if (ImGui::Selectable(kBw[i].label, i == a.bwIdx)) a.bwIdx = i;
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    ImGui::SameLine(0, 14);
    ImGui::BeginDisabled(!isHw);
    ImGui::TextDisabled("LNA");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    int lna = a.tune.lnaDb;
    ImGui::SliderInt("##lna", &lna, 0, 40, "%d dB");
    a.tune.lnaDb = (lna + 4) / 8 * 8; // hardware steps are 8 dB
    if (ImGui::IsItemDeactivatedAfterEdit()) retune = true;
    ImGui::SameLine();
    ImGui::TextDisabled("VGA");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    int vga = a.tune.vgaDb;
    ImGui::SliderInt("##vga", &vga, 0, 62, "%d dB");
    a.tune.vgaDb = (vga + 1) / 2 * 2; // 2 dB steps
    if (ImGui::IsItemDeactivatedAfterEdit()) retune = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Amp", &a.tune.ampOn)) retune = true;
    ImGui::EndDisabled();
    if (!isHw && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Gain controls apply to HackRF only");

    ImGui::SameLine(0, 14);
    if (!running) {
        if (ImGui::Button("  Start  ")) {
            a.tune.centerHz = a.freqMhz * 1e6;
            applyBandwidth(a);
            if (isFile && a.file.path.empty()) a.engine.log("choose an IQ file first");
            else {
                a.engine.start(a.devices[a.devIdx], a.tune, a.file);
                savePrefs(a);
                a.smooth.clear(); a.peak.clear(); a.lastSeq = 0;
            }
        }
    } else if (ImGui::Button("  Stop  ")) a.engine.stop();

    if (retune && running) {
        a.tune.centerHz = a.freqMhz * 1e6;
        a.engine.log("retune " + std::to_string(a.freqMhz) + " MHz");
        a.engine.retune(a.tune);
        a.peak.clear();
        savePrefs(a);
    }

    if (a.devices[a.devIdx].kind == DeviceInfo::Synthetic) {
        SynthConfig& sc = a.tune.synth;
        bool ch = false;
        ImGui::TextDisabled("synthetic");
        ImGui::SameLine();
        static const struct { const char* n; int code; } fm[] = {{"2K", 0}, {"8K", 1}, {"4K", 2}, {"1K", 3}, {"16K", 4}, {"32K", 5}};
        const char* cur = "?";
        for (auto& f : fm) if (f.code == sc.tx.s2field1) cur = f.n;
        ImGui::SetNextItemWidth(60);
        if (ImGui::BeginCombo("##sfft", cur)) {
            for (auto& f : fm) if (ImGui::Selectable(f.n, f.code == sc.tx.s2field1)) { sc.tx.s2field1 = f.code; ch = true; }
            ImGui::EndCombo();
        }
        ImGui::SameLine(); ImGui::TextDisabled("GI"); ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        if (ImGui::BeginCombo("##sgi", guardName(sc.tx.giIdx))) {
            for (int g = 0; g < kNumGi; g++) if (ImGui::Selectable(guardName(g), g == sc.tx.giIdx)) { sc.tx.giIdx = g; ch = true; }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("ext", &sc.tx.ext)) ch = true;
        ImGui::SameLine(); ImGui::TextDisabled("SNR"); ImGui::SameLine(); ImGui::SetNextItemWidth(90);
        float snr = (float)sc.snrDb; if (ImGui::SliderFloat("##ssnr", &snr, 0, 40, "%.0f dB")) { sc.snrDb = snr; ch = true; }
        ImGui::SameLine(); ImGui::TextDisabled("CFO"); ImGui::SameLine(); ImGui::SetNextItemWidth(110);
        float cfo = (float)(sc.cfoHz / 1e3); if (ImGui::SliderFloat("##scfo", &cfo, -40, 40, "%.2f kHz")) { sc.cfoHz = cfo * 1e3; ch = true; }
        ImGui::SameLine(); ImGui::TextDisabled("SRO"); ImGui::SameLine(); ImGui::SetNextItemWidth(90);
        float sro = (float)sc.sroPpm; if (ImGui::SliderFloat("##ssro", &sro, -50, 50, "%.0f ppm")) { sc.sroPpm = sro; ch = true; }
        ImGui::SameLine(); ImGui::TextDisabled("echo"); ImGui::SameLine(); ImGui::SetNextItemWidth(90);
        float ec = (float)sc.echoDb; if (ImGui::SliderFloat("##sec", &ec, 0, 20, ec == 0 ? "off" : "-%.0f dB")) { sc.echoDb = ec; ch = true; }
        if (ch && running) a.engine.retune(a.tune);
    }
    if (isFile && !running) {
        ImGui::TextDisabled("file");
        ImGui::SameLine();
        if (ImGui::SmallButton("Open…")) { auto p = openFileDialog(); if (!p.empty()) { a.file.path = p; a.file.format = guessFormat(p); } }
        ImGui::SameLine();
        ImGui::TextUnformatted(a.file.path.empty() ? "(none)" : a.file.path.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        const char* fm[] = {"cs8", "cu8", "cf32"};
        int fi = (int)a.file.format;
        if (ImGui::Combo("##ff", &fi, fm, 3)) a.file.format = (FileFormat)fi;
        ImGui::SameLine();
        ImGui::TextDisabled("rate");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        double msps = a.file.sampleRate / 1e6;
        if (ImGui::InputDouble("##frate", &msps, 0, 0, "%.4f Msps")) a.file.sampleRate = msps * 1e6;
        ImGui::SameLine();
        ImGui::Checkbox("loop", &a.file.loop);
    }
}

static void statusBar(App& a) {
    bool run = a.engine.running();
    const SignalStats& st = a.spec.stats;
    int iq = !run ? 0 : (st.clipFraction > 0.01f ? 3 : (st.clipFraction > 0.0005f || st.rmsDbfs < -45) ? 2 : 1);
    lamp("IQ", iq); ImGui::SameLine(0, 12);
    {
        const RxTelemetry& rx = a.rx;
        double frameS = rx.frameMs > 0 ? rx.frameMs / 1e3 : 0.5;
        int p1s = !run || !a.rxSeen ? 0 : (rx.p1.valid && rx.secSinceP1 < std::max(1.0, 3 * frameS) ? 1 : 2);
        int gis = !run ? 0 : rx.state == 2 ? 1 : rx.state == 1 ? 2 : 0;
        lamp("P1", p1s); ImGui::SameLine(0, 12);
        lamp("GI", gis); ImGui::SameLine(0, 12);
    }
    const char* later[] = {"L1-pre", "L1-post", "Frame", "LDPC", "BCH", "TS", "Video", "Audio"};
    for (auto* l : later) { lamp(l, 0); ImGui::SameLine(0, 12); }
    ImGui::TextDisabled("|"); ImGui::SameLine();
    ImGui::PushFont(a.mono, 0);
    if (run) {
        ImGui::Text("fs %.3f Msps   level %.1f dBFS   peak %.2f   clip %.3f%%   DC %+.3f/%+.3f   dropped %llu",
                    a.engine.sampleRate() / 1e6, st.rmsDbfs, st.peak, st.clipFraction * 100, st.dcI, st.dcQ,
                    (unsigned long long)a.engine.droppedSamples());
    } else ImGui::TextDisabled("stopped");
    ImGui::PopFont();
    if (run && a.rx.state == 2) {
        ImGui::PushFont(a.mono, 0);
        ImGui::Text("%s %s GI %s   CFO %+.1f Hz   CP-SNR %.1f dB   SRO %+.1f ppm", s1Name(a.rx.p1.s1),
                    fftModeFromSize(a.rx.fftN) ? fftModeFromSize(a.rx.fftN)->name : "?", guardName(a.rx.giIdx), a.rx.cfoHz, a.rx.cpSnrDb, a.rx.sroPpm);
        ImGui::PopFont();
    }
    if (run && a.devices[a.devIdx].kind == DeviceInfo::HackRF) {
        if (st.clipFraction > 0.0005f) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.25f, 1), " ADC CLIPPING - reduce gain"); }
        else if (st.rmsDbfs < -45) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.15f, 1), " low ADC level (<6 bits used) - raise gain; 8-bit ADC limits SNR"); }
    }
}

static void ingestSpectrum(App& a) {
    SpectrumFrame f;
    if (!a.engine.latestSpectrum(f, a.lastSeq)) return;
    a.lastSeq = f.seq;
    a.spec = f;
    size_t n = f.dbfs.size();
    if (a.smooth.size() != n) { a.smooth = f.dbfs; a.peak = f.dbfs; }
    for (size_t i = 0; i < n; i++) {
        a.smooth[i] += 0.35f * (f.dbfs[i] - a.smooth[i]);
        a.peak[i] = std::max(a.peak[i] - 0.15f, f.dbfs[i]);
    }
    double t = glfwGetTime();
    if (a.lastFrameT > 0) a.frameDt = a.frameDt * 0.9 + (t - a.lastFrameT) * 0.1;
    a.lastFrameT = t;
    a.wf.push(f.dbfs);
}

static void ingestRx(App& a) {
    RxTelemetry t;
    if (!a.engine.latestRx(t, a.rxSeq)) return;
    a.rxSeq = t.seq;
    if (t.p1Count > 0) a.rxSeen = true;
    a.rx = std::move(t);
    auto push = [](std::deque<float>& d, float v) { d.push_back(v); if (d.size() > 600) d.pop_front(); };
    if (a.rx.state == 2) {
        push(a.hCfo, (float)a.rx.cfoHz);
        push(a.hSnr, a.rx.cpSnrDb);
        push(a.hTiming, a.rx.timingErr);
    }
}

static std::vector<double> xs(const App& a) {
    size_t n = a.smooth.size();
    std::vector<double> x(n);
    double fs = a.engine.sampleRate() > 0 ? a.engine.sampleRate() : a.tune.sampleRate;
    double c = a.freqMhz;
    for (size_t i = 0; i < n; i++) x[i] = c + ((double)i / n - 0.5) * fs / 1e6;
    return x;
}

static void spectrumPlot(App& a, ImVec2 size) {
    if (ImPlot::BeginPlot("##spec", size, ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("frequency (MHz)", "power (dBFS/bin)");
        double fs = (a.engine.sampleRate() > 0 ? a.engine.sampleRate() : a.tune.sampleRate) / 1e6;
        ImPlot::SetupAxisLimits(ImAxis_X1, a.freqMhz - fs / 2, a.freqMhz + fs / 2, ImPlotCond_Once);
        ImPlot::SetupAxisLimits(ImAxis_Y1, a.yMin, a.yMax, ImPlotCond_Once);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%.2f");
        if (!a.smooth.empty()) {
            auto x = xs(a);
            // channel overlay: 8 MHz occupied band around the centre
            double bw = kBw[a.bwIdx].mhz;
            double xo[2] = {a.freqMhz - bw / 2 * 0.95, a.freqMhz + bw / 2 * 0.95};
            double yo[2] = {a.yMax, a.yMax};
            ImPlotSpec band; band.FillColor = ImVec4(0.15f, 0.55f, 0.20f, 0.18f); band.LineColor = ImVec4(0, 0, 0, 0);
            ImPlot::PlotShaded("band", xo, yo, 2, a.yMin, band);
            if (a.peakHold) {
                ImPlotSpec ps; ps.LineColor = ImVec4(0.9f, 0.7f, 0.2f, 0.45f); ps.LineWeight = 1.0f;
                std::vector<double> yp(a.peak.begin(), a.peak.end());
                ImPlot::PlotLine("peak", x.data(), yp.data(), (int)x.size(), ps);
            }
            ImPlotSpec ss; ss.LineColor = ImVec4(0.45f, 0.65f, 1.0f, 1); ss.LineWeight = 1.3f;
            std::vector<double> ysm(a.smooth.begin(), a.smooth.end());
            ImPlot::PlotLine("spectrum", x.data(), ysm.data(), (int)x.size(), ss);
            double cx[2] = {a.freqMhz, a.freqMhz}, cy[2] = {a.yMin, a.yMax};
            ImPlotSpec cs; cs.LineColor = ImVec4(1, 0.8f, 0.2f, 0.6f);
            ImPlot::PlotLine("centre", cx, cy, 2, cs);
        }
        ImPlot::EndPlot();
    }
}

static void waterfallPlot(App& a, ImVec2 size) {
    const int H = Waterfall::H;
    double secs = H * a.frameDt;
    if (ImPlot::BeginPlot("##wf", size, ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("frequency (MHz)", "seconds ago");
        double fs = (a.engine.sampleRate() > 0 ? a.engine.sampleRate() : a.tune.sampleRate) / 1e6;
        double x0 = a.freqMhz - fs / 2, x1 = a.freqMhz + fs / 2;
        ImPlot::SetupAxisLimits(ImAxis_X1, x0, x1, ImPlotCond_Once);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -secs, 0, ImPlotCond_Once);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%.2f");
        int w = a.wf.writeRow;
        double secA = (H - w) * a.frameDt;
        ImPlot::PlotImage("a", (ImTextureID)(__bridge void*)a.wf.tex, ImPlotPoint(x0, -secA), ImPlotPoint(x1, 0), ImVec2(0, (float)w / H), ImVec2(1, 1));
        if (w > 0)
            ImPlot::PlotImage("b", (ImTextureID)(__bridge void*)a.wf.tex, ImPlotPoint(x0, -secs), ImPlotPoint(x1, -secA), ImVec2(0, 0), ImVec2(1, (float)w / H));
        ImPlot::EndPlot();
    }
}

static void histogramPlot(App& a, ImVec2 size) {
    if (ImPlot::BeginPlot("##hist", size, ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("|sample| (fraction of full scale)", "count");
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, 1, ImPlotCond_Once);
        float h[64], x[64];
        for (int i = 0; i < 64; i++) { h[i] = (float)std::max<uint32_t>(1, a.spec.stats.hist[i]); x[i] = (i + 0.5f) / 64; }
        ImPlotSpec bs; bs.FillColor = ImVec4(0.45f, 0.65f, 1.0f, 0.8f);
        ImPlot::PlotBars("adc", x, h, 64, 1.0 / 64, bs);
        ImPlot::EndPlot();
    }
}

static void scatter(const char* id, const std::vector<cf32>& pts, ImVec2 size, double lim, ImVec4 col) {
    if (ImPlot::BeginPlot(id, size, ImPlotFlags_NoLegend | ImPlotFlags_NoTitle | ImPlotFlags_Equal)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
        ImPlot::SetupAxisLimits(ImAxis_X1, -lim, lim, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -lim, lim, ImPlotCond_Always);
        if (!pts.empty()) {
            ImPlotSpec sp;
            sp.Marker = ImPlotMarker_Circle; sp.MarkerSize = 1.6f; sp.Stride = sizeof(cf32);
            sp.MarkerFillColor = col; sp.MarkerLineColor = col; sp.LineColor = col;
            const float* d = reinterpret_cast<const float*>(pts.data());
            ImPlot::PlotScatter("pts", d, d + 1, (int)pts.size(), sp);
        }
        ImPlot::EndPlot();
    }
}

static void historyPlot(const char* id, const char* ylabel, const std::deque<float>& h, ImVec2 size) {
    if (ImPlot::BeginPlot(id, size, ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("samples (~30/s)", ylabel, 0, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, 600, ImPlotCond_Always);
        if (!h.empty()) {
            std::vector<float> v(h.begin(), h.end());
            ImPlot::PlotLine("h", v.data(), (int)v.size());
        }
        ImPlot::EndPlot();
    }
}

static void syncTab(App& a) {
    const RxTelemetry& rx = a.rx;
    float h = ImGui::GetContentRegionAvail().y;
    ImGui::TextDisabled("P1 detector - C-A-B correlation (peak > 0.30 triggers an S1/S2 decode)");
    if (ImPlot::BeginPlot("##p1", ImVec2(-1, h * 0.33f), ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("time (ms)", "correlation");
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.05, ImPlotCond_Always);
        if (!rx.p1Trace.empty() && rx.nativeRate > 0) {
            std::vector<float> xv(rx.p1Trace.size());
            double dt = kTraceDecim / rx.nativeRate * 1e3;
            for (size_t i = 0; i < xv.size(); i++) xv[i] = (float)((i - (double)xv.size()) * dt);
            ImPlot::SetupAxisLimits(ImAxis_X1, xv.front(), 0, ImPlotCond_Always);
            ImPlot::PlotLine("m", xv.data(), rx.p1Trace.data(), (int)xv.size());
            float tx[2] = {xv.front(), 0}, ty[2] = {0.30f, 0.30f};
            ImPlotSpec ts; ts.LineColor = ImVec4(0.9f, 0.7f, 0.2f, 0.7f);
            ImPlot::PlotLine("thr", tx, ty, 2, ts);
        }
        ImPlot::EndPlot();
    }
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::TextDisabled("guard-interval score (mean cyclic-prefix correlation over 3 symbols)");
    if (ImPlot::BeginPlot("##gi", ImVec2(w * 0.38f, -1), ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes(nullptr, "score", ImPlotAxisFlags_NoGridLines, 0);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.05, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_X1, -0.6, kNumGi - 0.4, ImPlotCond_Always);
        double pos[kNumGi]; const char* lab[kNumGi];
        for (int i = 0; i < kNumGi; i++) { pos[i] = i; lab[i] = guardName(i); }
        ImPlot::SetupAxisTicks(ImAxis_X1, pos, kNumGi, lab);
        float sc[kNumGi], x[kNumGi];
        for (int i = 0; i < kNumGi; i++) { sc[i] = rx.giScore[i]; x[i] = (float)i; }
        ImPlotSpec bs; bs.FillColor = ImVec4(0.45f, 0.65f, 1.0f, 0.85f);
        ImPlot::PlotBars("gi", x, sc, kNumGi, 0.7, bs);
        ImPlot::EndPlot();
    }
    ImGui::SameLine();
    ImGui::BeginChild("hist", ImVec2(0, -1));
    float hh = ImGui::GetContentRegionAvail().y / 3 - 4;
    historyPlot("##hc", "CFO (Hz)", a.hCfo, ImVec2(-1, hh));
    historyPlot("##hs", "CP-SNR (dB)", a.hSnr, ImVec2(-1, hh));
    historyPlot("##ht", "timing (samples)", a.hTiming, ImVec2(-1, hh));
    ImGui::EndChild();
}

static void signallingTab(App& a) {
    const RxTelemetry& rx = a.rx;
    ImGui::PushFont(a.mono, 0);
    auto row = [](const char* k, const char* fmt, ...) {
        char b[160];
        va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        ImGui::TextDisabled("%-22s", k); ImGui::SameLine(); ImGui::TextUnformatted(b);
    };
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), "P1 preamble");
    if (rx.p1.valid) {
        const FftMode* fm = fftModeFromS2(rx.p1.s2field1);
        row("S1", "%d  %s", rx.p1.s1, s1Name(rx.p1.s1));
        row("S2 field 1", "%d  FFT %s", rx.p1.s2field1, fm ? fm->name : "?");
        row("S2 field 2", "%d  %s", rx.p1.mixed, rx.p1.mixed ? "mixed (T2 + FEF/other frames)" : "all frames same preamble type");
        row("sequence correlation", "%.2f", rx.p1.conf);
        row("C-A-B correlation", "%.2f", rx.p1.metric);
        row("P1 carrier CFO", "%+.1f Hz", rx.p1.cfoHz);
        row("P1 detections", "%llu", (unsigned long long)rx.p1Count);
        row("last P1", "%.2f s ago", rx.secSinceP1);
    } else ImGui::TextDisabled("no P1 decoded yet");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), "OFDM / frame (blind, before L1-pre)");
    if (rx.state >= 1) {
        row("FFT size", "%d", rx.fftN);
        row("carriers (normal)", "%d", rx.carriers);
        if (rx.giIdx >= 0) {
            row("guard interval", "%s  (%d samples)  margin %.2fx", guardName(rx.giIdx), rx.guard, rx.giMargin);
            row("symbol duration", "%.1f us", (rx.fftN + rx.guard) / rx.nativeRate * 1e6);
        }
        if (rx.frameMs > 0) {
            row("frame length", "%.2f ms  =  P1 + %d symbols", rx.frameMs, rx.symbolsPerFrame);
            row("sample clock offset", "%+.1f ppm", rx.sroPpm);
        }
        row("CFO (tracked)", "%+.1f Hz", rx.cfoHz);
        row("CP correlation", "%.3f  (~%.1f dB SNR)", rx.cpCorr, rx.cpSnrDb);
        row("symbols processed", "%llu", (unsigned long long)rx.symbols);
    }
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), "L1-pre / L1-post");
    ImGui::TextDisabled("(phase 2: pilots, channel estimation and L1 decoding)");
    ImGui::PopFont();
}

static void constellationsTab(App& a) {
    float w = ImGui::GetContentRegionAvail().x / 3 - 8;
    float h = std::min(ImGui::GetContentRegionAvail().y - 40, w);
    ImGui::BeginGroup();
    ImGui::TextDisabled("P1 carriers (DBPSK, differential)");
    scatter("##c1", a.rx.p1Const, ImVec2(w, h), 2.5, ImVec4(0.95f, 0.7f, 0.2f, 0.9f));
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("OFDM cells, inter-symbol differential");
    scatter("##c2", a.rx.cells, ImVec2(w, h), 3.0, ImVec4(0.35f, 0.85f, 0.45f, 0.8f));
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("OFDM cells, raw (no channel correction)");
    scatter("##c3", a.rx.rawCells, ImVec2(w, h), 3.0, ImVec4(0.45f, 0.65f, 1.0f, 0.7f));
    ImGui::EndGroup();
    ImGui::TextDisabled("Raw cells are rotated by the channel and timing, so they form a ring until pilot-based channel estimation (phase 2). "
                        "The differential view cancels the channel: tight clusters = good lock.");
}

static void rightPanel(App& a) {
    ImGui::TextDisabled("Services");
    for (int i = 0; i < 4; i++) {
        ImGui::BeginChild(("svc" + std::to_string(i)).c_str(), ImVec2(0, 34), ImGuiChildFlags_Borders);
        ImGui::TextDisabled("%d  —", i);
        ImGui::EndChild();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Video");
    ImGui::BeginChild("video", ImVec2(0, 180), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("(live video — phase 5)");
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::TextDisabled("Outputs");
    ImGui::TextDisabled("File ○   UDP ○   Live ○   (phase 4–5)");
    ImGui::Spacing();
    ImGui::TextDisabled("ADC occupancy");
    histogramPlot(a, ImVec2(-1, 150));
}

static void logPanel(App& a) {
    ImGui::TextDisabled("Log");
    ImGui::SameLine();
    size_t total = 0;
    auto lines = a.engine.logSnapshot(total);
    ImGui::TextDisabled("%zu lines", total);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        std::string all;
        for (auto& l : lines) all += l + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) a.engine.clearLog();
    ImGui::BeginChild("logtext", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushFont(a.mono, 0);
    for (auto& l : lines) ImGui::TextUnformatted(l.c_str());
    ImGui::PopFont();
    if (a.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

static void drawUI(App& a, ImVec2 disp) {
    ingestSpectrum(a);
    ingestRx(a);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(disp);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

    toolbar(a);
    ImGui::Separator();
    statusBar(a);
    ImGui::Separator();

    float logH = 150, rightW = 330;
    float mainH = ImGui::GetContentRegionAvail().y - logH - 6;
    ImGui::BeginChild("main", ImVec2(disp.x - rightW - 16, mainH));
    if (ImGui::BeginTabBar("tabs")) {
        if (tabItem("Overview")) {
            float h = ImGui::GetContentRegionAvail().y;
            spectrumPlot(a, ImVec2(-1, h * 0.5f));
            waterfallPlot(a, ImVec2(-1, -1));
            ImGui::EndTabItem();
        }
        if (tabItem("Spectrum")) {
            ImGui::Checkbox("peak hold", &a.peakHold);
            ImGui::SameLine(); ImGui::SetNextItemWidth(160);
            ImGui::DragFloatRange2("dB range", &a.yMin, &a.yMax, 1, -160, 20, "%.0f", "%.0f");
            spectrumPlot(a, ImVec2(-1, -1));
            ImGui::EndTabItem();
        }
        if (tabItem("Waterfall")) {
            ImGui::SetNextItemWidth(220);
            ImGui::DragFloatRange2("colour range (dB)", &a.wf.minDb, &a.wf.maxDb, 1, -160, 20, "%.0f", "%.0f");
            waterfallPlot(a, ImVec2(-1, -1));
            ImGui::EndTabItem();
        }
        if (tabItem("Sync")) { syncTab(a); ImGui::EndTabItem(); }
        if (tabItem("Signalling")) { signallingTab(a); ImGui::EndTabItem(); }
        if (tabItem("Constellations")) { constellationsTab(a); ImGui::EndTabItem(); }
        const char* later[] = {"Channel", "Impulse response", "SNR per carrier",
                               "FEC", "Frame map", "TS", "History", "Outputs"};
        for (auto* t : later)
            if (tabItem(t)) { ImGui::TextDisabled("Not implemented yet (see PROJECT_BRIEF.md, development plan)."); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("right", ImVec2(0, mainH));
    rightPanel(a);
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::BeginChild("log", ImVec2(0, 0));
    logPanel(a);
    ImGui::EndChild();
    ImGui::End();
}

// ------------------------------------------------------------------ main

int main(int argc, char** argv) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1500, 900, "DecT2", nullptr, nullptr);
    if (!window) return 1;

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> queue = [device newCommandQueue];

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    applyTheme();

    App app;
    ImFont* ui = io.Fonts->AddFontFromFileTTF("/System/Library/Fonts/Helvetica.ttc", 14.0f);
    app.mono = io.Fonts->AddFontFromFileTTF("/System/Library/Fonts/Menlo.ttc", 13.0f);
    if (!ui) ui = io.Fonts->AddFontDefault();
    if (!app.mono) app.mono = ui;

    ImGui_ImplGlfw_InitForOther(window, true);
    ImGui_ImplMetal_Init(device);

    NSWindow* nswin = glfwGetCocoaWindow(window);
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    nswin.contentView.layer = layer;
    nswin.contentView.wantsLayer = YES;
    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor new];

    app.wf.init(device);
    loadPrefs(app);
    app.engine.log("DecT2 phase 0 - IQ sources, spectrum, waterfall");
    refreshDevices(app);
    for (int i = 0; i < (int)app.devices.size(); i++)
        if (app.devices[i].kind == DeviceInfo::HackRF) { app.devIdx = i; break; }
    if (app.devices.size() == 2) app.engine.log("no HackRF connected - use the synthetic signal or an IQ file");

    bool autostart = false;
    const char* shotPath = nullptr;
    int shotFrame = 0;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--autostart") autostart = true;
        if (std::string(argv[i]) == "--shot" && i + 1 < argc) shotPath = argv[++i];
        if (std::string(argv[i]) == "--tab" && i + 1 < argc) gForceTab = argv[++i];
        if (std::string(argv[i]) == "--fft" && i + 1 < argc) app.tune.synth.tx.s2field1 = atoi(argv[++i]);
        if (std::string(argv[i]) == "--gi" && i + 1 < argc) app.tune.synth.tx.giIdx = atoi(argv[++i]);
        if (std::string(argv[i]) == "--cfo" && i + 1 < argc) app.tune.synth.cfoHz = atof(argv[++i]);
        if (std::string(argv[i]) == "--snr" && i + 1 < argc) app.tune.synth.snrDb = atof(argv[++i]);
    }
    layer.framebufferOnly = NO;
    double last = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        @autoreleasepool {
            glfwPollEvents();
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            if (w == 0 || h == 0) { glfwWaitEventsTimeout(0.1); continue; }
            layer.drawableSize = CGSizeMake(w, h);
            id<CAMetalDrawable> drawable = [layer nextDrawable];
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.04, 0.045, 0.05, 1);
            rpd.colorAttachments[0].texture = drawable.texture;
            rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
            rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rpd];

            ImGui_ImplMetal_NewFrame(rpd);
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            if (autostart) {
                autostart = false;
                app.devIdx = 0; app.tune.centerHz = app.freqMhz * 1e6; applyBandwidth(app);
                app.engine.start(app.devices[0], app.tune, app.file);
            }
            double now = glfwGetTime();
            last = now;
            drawUI(app, io.DisplaySize);
            ImGui::Render();
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cb, enc);
            [enc endEncoding];
            [cb presentDrawable:drawable];
            [cb commit];
            if (shotPath && ++shotFrame == 400) {
                [cb waitUntilCompleted];
                id<MTLTexture> t = drawable.texture;
                NSUInteger tw = t.width, th = t.height;
                std::vector<uint8_t> px(tw * th * 4);
                [t getBytes:px.data() bytesPerRow:tw * 4 fromRegion:MTLRegionMake2D(0, 0, tw, th) mipmapLevel:0];
                for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]); // BGRA -> RGBA
                unsigned char* planes[1] = {px.data()};
                NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes pixelsWide:tw pixelsHigh:th bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:tw * 4 bitsPerPixel:32];
                [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithUTF8String:shotPath] atomically:YES];
                glfwSetWindowShouldClose(window, 1);
            }
        }
    }
    app.engine.stop();
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
