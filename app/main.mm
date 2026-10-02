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
#include "dect2/scanner.h"
#include "dect2/gain.h"
#include "dect2/channel.h"
#include "dect2/quality.h"
#include "dect2/teletext.h"
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

static id<MTLDevice> gDevice;
static id<MTLCommandQueue> gQueue;
static GLFWwindow* gWindow = nullptr;
static int gWinX = 100, gWinY = 100, gWinW = 1500, gWinH = 900;
static void toggleFullscreen() {
    if (!gWindow) return;
    if (glfwGetWindowMonitor(gWindow)) { glfwSetWindowMonitor(gWindow, nullptr, gWinX, gWinY, gWinW, gWinH, 0); return; }
    glfwGetWindowPos(gWindow, &gWinX, &gWinY); glfwGetWindowSize(gWindow, &gWinW, &gWinH);
    GLFWmonitor* m = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = glfwGetVideoMode(m);
    glfwSetWindowMonitor(gWindow, m, 0, 0, vm->width, vm->height, vm->refreshRate);
}

static NSString* const kYuvShader = @R"MSL(
#include <metal_stdlib>
using namespace metal;
kernel void nv12(texture2d<float, access::read> Y [[texture(0)]], texture2d<float, access::read> UV [[texture(1)]],
                 texture2d<float, access::write> out [[texture(2)]], constant float3& prm [[buffer(0)]], uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= out.get_width() || gid.y >= out.get_height()) return;
    float y = Y.read(gid).r;
    if (prm.z > 0.5) { // interlaced: replace combed pixels (a line that differs from both neighbours in the same direction) by the line average
        const uint H = out.get_height();
        float ya = Y.read(uint2(gid.x, gid.y > 0 ? gid.y - 1 : gid.y)).r;
        float yb = Y.read(uint2(gid.x, gid.y + 1 < H ? gid.y + 1 : gid.y)).r;
        float comb = (y - ya) * (y - yb);
        float w = clamp((comb - 0.0008) / 0.004, 0.0, 1.0);
        y = mix(y, 0.5 * (ya + yb), w);
    }
    float2 c = UV.read(gid / 2).rg;
    if (prm.y < 0.5) { y = (y - 16.0 / 255.0) * (255.0 / 219.0); c = (c - 128.0 / 255.0) * (255.0 / 224.0); }
    else c -= 0.5;
    float3 rgb;
    if (prm.x > 0.5) rgb = float3(y + 1.5748 * c.y, y - 0.1873 * c.x - 0.4681 * c.y, y + 1.8556 * c.x);
    else rgb = float3(y + 1.402 * c.y, y - 0.344136 * c.x - 0.714136 * c.y, y + 1.772 * c.x);
    out.write(float4(saturate(rgb), 1.0), gid);
}
)MSL";

struct VideoTex {
    id<MTLTexture> tex = nil, yTex = nil, uvTex = nil;
    id<MTLComputePipelineState> pso = nil;
    int w = 0, h = 0;
    uint64_t seq = 0;
    bool deint = true;
    void update(Player& pl, id<MTLCommandQueue> q) {
        auto f = pl.videoFrame(seq);
        if (!f || f->w <= 0) return;
        const bool yuv = f->rgba.empty();
        if (!tex || w != f->w || h != f->h) {
            MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:f->w height:f->h mipmapped:NO];
            d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
            tex = [gDevice newTextureWithDescriptor:d];
            MTLTextureDescriptor* dy = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:f->w height:f->h mipmapped:NO];
            yTex = [gDevice newTextureWithDescriptor:dy];
            MTLTextureDescriptor* du = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm width:f->w / 2 height:f->h / 2 mipmapped:NO];
            uvTex = [gDevice newTextureWithDescriptor:du];
            w = f->w; h = f->h;
        }
        if (!yuv) { [tex replaceRegion:MTLRegionMake2D(0, 0, f->w, f->h) mipmapLevel:0 withBytes:f->rgba.data() bytesPerRow:f->w * 4]; return; }
        if (!pso) {
            NSError* err = nil;
            id<MTLLibrary> lib = [gDevice newLibraryWithSource:kYuvShader options:nil error:&err];
            pso = lib ? [gDevice newComputePipelineStateWithFunction:[lib newFunctionWithName:@"nv12"] error:&err] : nil;
            if (!pso) { NSLog(@"yuv shader: %@", err); return; }
        }
        [yTex replaceRegion:MTLRegionMake2D(0, 0, f->w, f->h) mipmapLevel:0 withBytes:f->y.data() bytesPerRow:f->w];
        [uvTex replaceRegion:MTLRegionMake2D(0, 0, f->w / 2, f->h / 2) mipmapLevel:0 withBytes:f->uv.data() bytesPerRow:f->w];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
        [e setComputePipelineState:pso];
        float prm[4] = {f->bt709 ? 1.f : 0.f, f->fullRange ? 1.f : 0.f, (f->interlaced && deint) ? 1.f : 0.f, 0.f};
        [e setBytes:prm length:sizeof prm atIndex:0];
        [e setTexture:yTex atIndex:0]; [e setTexture:uvTex atIndex:1]; [e setTexture:tex atIndex:2];
        [e dispatchThreads:MTLSizeMake(f->w, f->h, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [e endEncoding];
        [cb commit]; // the render pass later on the same queue sees the converted texture
    }
    void draw(ImVec2 box) {
        if (!tex) return;
        float ar = (float)w / h;
        ImVec2 sz = box;
        if (box.x / box.y > ar) sz.x = box.y * ar; else sz.y = box.x / ar;
        ImVec2 p = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(p.x + (box.x - sz.x) * 0.5f, p.y + (box.y - sz.y) * 0.5f));
        ImGui::Image((ImTextureID)(__bridge void*)tex, sz);
        ImGui::SetCursorPos(p);
    }
};

struct SavedChannel {
    double freqMhz = 0, bwMhz = 8;
    std::string name, mode;
    float snrDb = 0;
    int nServices = 0;
    bool favourite = false;
};

struct App {
    Engine engine;
    VideoTex video;
    float volume = 1.f;
    bool muted = false, subsOn = true;
    int playReq = -1;
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
    TsSnapshot ts;
    BbStats bb;
    double tsT = 0;
    OutputConfig out;
    int selService = -1;     // service id used for outputs (-1 = whole multiplex)
    char udpHost[64] = "127.0.0.1";
    char filePath[512] = "";
    std::deque<float> hCfo, hSnr, hTiming;
    struct HistSample { float t, snr, mer, loss, cfo, sro, level, clip, quality; };
    std::deque<HistSample> hist;      // 4 samples per second, last 15 minutes
    double histT = 0;
    uint64_t histOk = 0, histBad = 0;
    bool rxSeen = false;
    int computeMode = 2; // 0 CPU, 1 GPU, 2 auto
    bool popOut = false, videoOnly = false;
    int histWindow = 60;
    int ttxPage = 100;
    int plpSel = -1; // -1 = automatic
    std::map<int, std::vector<EpgEvent>> epg;
    double epgT = -10;
    int guideSid = -1, guideEvent = -1;
    bool fakeEpg = false; // --fakeepg: made-up programmes, for checking the layout when the mux sends none
    char favName[64] = "";
    MultipathDetector mpd;
    QualityMeter quality;
    std::vector<SavedChannel> channels; // DVB-T2 muxes found by the scanner (remembered)
    bool scanWas = false;
    double scanHarvestT = 0;
    bool agcOn = false;
    AutoGain agc;
    GainSweep sweep;
    bool sweepRetune = false;
    Scanner scanner;
    ScanConfig scanCfg;
    int scanPreset = 0;
    bool scanWasRunning = false;
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
    // HackRF Pro: the tuned centre is only exact at <= 10 Msps and at 20 Msps, so use 10 Msps (8 for narrow channels)
    a.tune.sampleRate = kBw[a.bwIdx].mhz >= 7 ? 10e6 : 8e6;
    a.tune.basebandFilterHz = 0;
    a.tune.bandwidthMhz = kBw[a.bwIdx].mhz;
}

static std::string openFileDialog() {
    NSOpenPanel* p = [NSOpenPanel openPanel];
    p.canChooseFiles = YES; p.canChooseDirectories = NO; p.allowsMultipleSelection = NO;
    if ([p runModal] == NSModalResponseOK) return std::string(p.URL.path.UTF8String);
    return {};
}

static std::string saveFileDialog(const char* name) {
    NSSavePanel* p = [NSSavePanel savePanel];
    p.nameFieldStringValue = [NSString stringWithUTF8String:name];
    if ([p runModal] == NSModalResponseOK) return std::string(p.URL.path.UTF8String);
    return {};
}

static void loadPrefs(App& a) {
    NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
    if ([d objectForKey:@"freqMhz"]) a.freqMhz = [d doubleForKey:@"freqMhz"];
    if ([d objectForKey:@"lna"]) a.tune.lnaDb = (int)[d integerForKey:@"lna"];
    if ([d objectForKey:@"vga"]) a.tune.vgaDb = (int)[d integerForKey:@"vga"];
    a.tune.ampOn = [d boolForKey:@"amp"];
    if ([d objectForKey:@"compute"]) a.computeMode = (int)[d integerForKey:@"compute"];
    a.bwIdx = std::min((int)(sizeof kBw / sizeof *kBw) - 1, (int)[d integerForKey:@"bw"]);
    NSString* fp = [d stringForKey:@"filePath"];
    if (fp) a.file.path = fp.UTF8String;
    NSString* op = [d stringForKey:@"outPath"];
    if (op) snprintf(a.filePath, sizeof a.filePath, "%s", op.UTF8String);
    NSString* oh = [d stringForKey:@"udpHost"];
    if (oh) snprintf(a.udpHost, sizeof a.udpHost, "%s", oh.UTF8String);
    if ([d objectForKey:@"udpPort"]) a.out.port = (int)[d integerForKey:@"udpPort"];
    a.out.rtp = [d boolForKey:@"udpRtp"];
    a.out.dropNull = [d boolForKey:@"dropNull"];
    a.channels.clear();
    for (NSDictionary* c in [d arrayForKey:@"channels"]) {
        SavedChannel sc;
        sc.freqMhz = [c[@"f"] doubleValue]; sc.bwMhz = [c[@"bw"] doubleValue];
        sc.name = [c[@"name"] UTF8String] ?: ""; sc.mode = [c[@"mode"] UTF8String] ?: "";
        sc.snrDb = [c[@"snr"] floatValue]; sc.nServices = [c[@"n"] intValue]; sc.favourite = [c[@"fav"] boolValue];
        if (sc.freqMhz > 0) a.channels.push_back(sc);
    }
}

static void savePrefs(const App& a) {
    NSUserDefaults* d = NSUserDefaults.standardUserDefaults;
    [d setDouble:a.freqMhz forKey:@"freqMhz"];
    [d setInteger:a.tune.lnaDb forKey:@"lna"];
    [d setInteger:a.tune.vgaDb forKey:@"vga"];
    [d setBool:a.tune.ampOn forKey:@"amp"];
    {
        NSMutableArray* arr = [NSMutableArray array];
        for (auto& c : a.channels)
            [arr addObject:@{@"f": @(c.freqMhz), @"bw": @(c.bwMhz), @"name": [NSString stringWithUTF8String:c.name.c_str()], @"mode": [NSString stringWithUTF8String:c.mode.c_str()], @"snr": @(c.snrDb), @"n": @(c.nServices), @"fav": @(c.favourite)}];
        [d setObject:arr forKey:@"channels"];
    }
    [d setInteger:a.computeMode forKey:@"compute"];
    [d setInteger:a.bwIdx forKey:@"bw"];
    [d setObject:[NSString stringWithUTF8String:a.file.path.c_str()] forKey:@"filePath"];
    [d setObject:[NSString stringWithUTF8String:a.filePath] forKey:@"outPath"];
    [d setObject:[NSString stringWithUTF8String:a.udpHost] forKey:@"udpHost"];
    [d setInteger:a.out.port forKey:@"udpPort"];
    [d setBool:a.out.rtp forKey:@"udpRtp"];
    [d setBool:a.out.dropNull forKey:@"dropNull"];
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
    if (ImGui::IsItemDeactivatedAfterEdit()) retune = true, a.agcOn = false;
    ImGui::SameLine();
    ImGui::TextDisabled("VGA");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    int vga = a.tune.vgaDb;
    ImGui::SliderInt("##vga", &vga, 0, 62, "%d dB");
    a.tune.vgaDb = (vga + 1) / 2 * 2; // 2 dB steps
    if (ImGui::IsItemDeactivatedAfterEdit()) retune = true, a.agcOn = false;
    ImGui::SameLine();
    if (ImGui::Checkbox("Amp", &a.tune.ampOn)) retune = true, a.agcOn = false;
    ImGui::SameLine();
    if (ImGui::Checkbox("AGC", &a.agcOn)) { a.agc.reset(); }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Keep the ADC level in a healthy window (about -16 dBFS rms, no clipping) by adjusting LNA, VGA and amp.\nTouching a gain control turns it off.");
    ImGui::SameLine();
    if (a.sweep.active()) {
        if (ImGui::Button("Stop tune")) { a.sweep = GainSweep(); }
    } else if (ImGui::Button("Auto-tune") && running) {
        a.sweep.start(ImGui::GetTime());
        a.agcOn = false;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Gain helper: tries LNA/VGA/amp combinations for about 40 s and keeps the one with the best SNR that does not clip.\nNeeds a signal the receiver can lock to.");
    ImGui::EndDisabled();
    if (!isHw && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Gain controls apply to HackRF only");

    ImGui::SameLine(0, 14);
    if (!running) {
        if (ImGui::Button("  Start  ")) {
            a.tune.centerHz = a.freqMhz * 1e6;
            applyBandwidth(a);
            if (isFile) { const double r = guessSampleRate(a.file.path); if (r > 0) a.file.sampleRate = r; } // the name says the rate
            if (isFile && a.file.path.empty()) a.engine.log("choose an IQ file first");
            else {
                a.engine.setComputeMode(a.computeMode);
                a.engine.start(a.devices[a.devIdx], a.tune, a.file);
                savePrefs(a);
                a.smooth.clear(); a.peak.clear(); a.lastSeq = 0;
            }
        }
    } else if (ImGui::Button("  Stop  ")) a.engine.stop();

    ImGui::SameLine(0, 14);
    {
        static const char* modes[] = {"CPU", "GPU", "Auto"};
        char lbl[48];
        const bool gpuNow = a.rx.plpOnGpu;
        snprintf(lbl, sizeof lbl, "%s", a.computeMode == 2 ? (gpuNow ? "Auto (GPU)" : "Auto (CPU)") : modes[a.computeMode]);
        ImGui::TextDisabled("decode");
        ImGui::SameLine(0, 6);
        ImGui::SetNextItemWidth(110);
        if (ImGui::BeginCombo("##compute", lbl)) {
            for (int i = 0; i < 3; i++) {
                const bool dis = i == 1 && !a.rx.gpuAvailable && running;
                if (dis) ImGui::BeginDisabled();
                if (ImGui::Selectable(modes[i], a.computeMode == i)) { a.computeMode = i; a.engine.setComputeMode(i); savePrefs(a); }
                if (dis) ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("LDPC decoding backend.\nCPU: NEON on all cores. GPU: Metal compute.\nAuto: CPU until it falls behind real time, then GPU.");
    }

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
        ImGui::SameLine(); ImGui::SetNextItemWidth(62);
        {
            char ppl[8]; snprintf(ppl, sizeof ppl, "PP%d", sc.tx.pp + 1);
            if (ImGui::BeginCombo("##spp", ppl)) {
                for (int q = 0; q < 8; q++) { snprintf(ppl, sizeof ppl, "PP%d", q + 1); if (ImGui::Selectable(ppl, q == sc.tx.pp)) { sc.tx.pp = q; ch = true; } }
                ImGui::EndCombo();
            }
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("TR", &sc.tx.tr)) ch = true;
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
        if (ImGui::SmallButton("Open…")) { auto p = openFileDialog(); if (!p.empty()) { a.file.path = p; a.file.format = guessFormat(p); const double r = guessSampleRate(p); if (r > 0) a.file.sampleRate = r; } }
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
    int iq = 0;
    if (run) { const AdcStatus as = classifyAdc(st.rmsDbfs, st.peak, st.clipFraction); iq = as == AdcStatus::Overload ? 3 : (as == AdcStatus::Good ? 1 : 2); }
    lamp("IQ", iq); ImGui::SameLine(0, 12);
    {
        const RxTelemetry& rx = a.rx;
        double frameS = rx.frameMs > 0 ? rx.frameMs / 1e3 : 0.5;
        int p1s = !run || !a.rxSeen ? 0 : (rx.p1.valid && rx.secSinceP1 < std::max(1.0, 3 * frameS) ? 1 : 2);
        int gis = !run ? 0 : rx.state == 2 ? 1 : rx.state == 1 ? 2 : 0;
        lamp("P1", p1s); ImGui::SameLine(0, 12);
        lamp("GI", gis); ImGui::SameLine(0, 12);
    }
    {
        const RxTelemetry& rx = a.rx;
        int l1p = !run ? 0 : rx.l1preOk ? 1 : rx.l1preGood > 0 ? 2 : rx.chValid ? 3 : 0;
        int l1q = !run ? 0 : rx.l1postOk ? 1 : rx.l1postGood > 0 ? 2 : rx.l1preOk ? 3 : 0;
        lamp("L1-pre", l1p); ImGui::SameLine(0, 12);
        lamp("L1-post", l1q); ImGui::SameLine(0, 12);
    }
    {
        int fs = !run ? 0 : a.rx.dataValid ? 1 : a.rx.l1preOk ? 2 : 0;
        lamp("Frame", fs); ImGui::SameLine(0, 12);
    }
    {
        const RxTelemetry& rx = a.rx;
        double okFrac = (rx.blocksOk + rx.blocksBad) ? (double)rx.blocksOk / (rx.blocksOk + rx.blocksBad) : 0;
        int fec = !run || !rx.plpValid || rx.plpFrames == 0 ? 0 : okFrac > 0.995 ? 1 : okFrac > 0.5 ? 2 : 3;
        lamp("LDPC", fec); ImGui::SameLine(0, 12);
        lamp("BCH", fec); ImGui::SameLine(0, 12);
    }
    {
        int tsl = !run ? 0 : a.ts.services.empty() ? 0 : (a.ts.ccErrors > 0 && a.bb.framesLost > 0.02 * (a.bb.frames + 1)) ? 2 : 1;
        lamp("TS", tsl); ImGui::SameLine(0, 12);
    }
    {
        const PlayerStats ps = a.engine.player().stats();
        const bool pl = run && ps.active;
        lamp("Video", !pl || !ps.hasVideo ? 0 : (ps.shown > 0 && ps.videoQueue > 2) ? 1 : 2); ImGui::SameLine(0, 12);
        lamp("Audio", !pl || !ps.hasAudio ? 0 : (ps.audioBufferMs > 150) ? 1 : 2); ImGui::SameLine(0, 12);
    }
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
        ImGui::Text("%s %s GI %s%s   CFO %+.1f Hz   SRO %+.1f ppm   SNR %.1f dB%s", s1Name(a.rx.p1.s1),
                    fftModeFromSize(a.rx.fftN) ? fftModeFromSize(a.rx.fftN)->name : "?", guardName(a.rx.giIdx), a.rx.extCarriers ? " ext" : "", a.rx.cfoHz, a.rx.sroPpm,
                    a.rx.dataValid ? a.rx.dataSnrDb : a.rx.cpSnrDb, a.rx.dataValid ? "" : " (CP)");
        if (a.rx.l1preOk) ImGui::SameLine(), ImGui::Text("  PP%d  cell %d", a.rx.l1pre.pilotPattern + 1, a.rx.l1pre.cellId);
        ImGui::PopFont();
    }
    if (run && a.mpd.report().level != MultipathLevel::Unknown) {
        const MultipathReport& mr = a.mpd.report();
        const ImVec4 col = mr.level == MultipathLevel::None ? ImVec4(0.5f, 0.55f, 0.6f, 1) : mr.level == MultipathLevel::Mild ? ImVec4(0.95f, 0.8f, 0.3f, 1)
                         : mr.level == MultipathLevel::Likely ? ImVec4(0.95f, 0.6f, 0.2f, 1) : ImVec4(0.95f, 0.35f, 0.25f, 1);
        ImGui::SameLine(); ImGui::TextColored(col, "  multipath: %s", multipathName(mr.level));
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(mr.headline.c_str());
            for (auto& r : mr.reasons) ImGui::BulletText("%s", r.c_str());
            ImGui::EndTooltip();
        }
    }
    if (run && a.devices[a.devIdx].kind != DeviceInfo::File) {
        const AdcStatus as = classifyAdc(st.rmsDbfs, st.peak, st.clipFraction);
        if (a.sweep.active()) {
            ImGui::SameLine(); ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), " gain helper %d/%d ...", std::max(0, a.sweep.current()) + 1, (int)a.sweep.entries().size());
        } else if (as == AdcStatus::Overload) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.25f, 1), " ADC OVERLOAD - %s", a.agcOn ? "AGC is lowering the gain" : "reduce the gain (or enable AGC)"); }
        else if (as == AdcStatus::High) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.15f, 1), " ADC level high"); }
        else if (as == AdcStatus::Low) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.15f, 1), " ADC level low - raise the gain; 8 bits limit the SNR"); }
        else if (as == AdcStatus::NoSignal) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.15f, 1), " ADC sees almost nothing - antenna / gain?"); }
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
    double now = glfwGetTime();
    if (a.engine.running() && now - a.tsT > 0.25) { a.ts = a.engine.tsSnapshot(); a.bb = a.engine.bbStats(); a.tsT = now; }
    RxTelemetry t;
    if (!a.engine.latestRx(t, a.rxSeq)) return;
    a.rxSeq = t.seq;
    if (t.p1Count > 0) a.rxSeen = true;
    if (t.dataFrames < a.rx.dataFrames) { a.mpd.reset(); a.quality.reset(); } // receiver was restarted or retuned
    a.rx = std::move(t);
    a.mpd.update(a.rx);
    a.quality.update(a.rx);
    {
        const double nowT = glfwGetTime();
        if (a.engine.running() && nowT - a.histT >= 0.25) {
            App::HistSample h;
            h.t = (float)nowT;
            h.snr = a.rx.dataValid ? a.rx.dataSnrDb : NAN;
            h.mer = a.rx.plpMerDb > 0 && a.rx.plpMerDb < 90 ? (float)a.rx.plpMerDb : NAN;
            const uint64_t dOk = a.rx.blocksOk >= a.histOk ? a.rx.blocksOk - a.histOk : 0, dBad = a.rx.blocksBad >= a.histBad ? a.rx.blocksBad - a.histBad : 0;
            h.loss = (dOk + dBad) ? 100.f * (float)dBad / (float)(dOk + dBad) : NAN;
            a.histOk = a.rx.blocksOk; a.histBad = a.rx.blocksBad;
            h.cfo = a.rx.state == 2 ? (float)a.rx.cfoHz : NAN;
            h.sro = a.rx.state == 2 ? (float)a.rx.sroPpm : NAN;
            h.level = a.spec.stats.rmsDbfs; h.clip = a.spec.stats.clipFraction * 100.f;
            h.quality = a.quality.report().valid ? (float)a.quality.report().percent : NAN;
            a.hist.push_back(h);
            if (a.hist.size() > 3600) a.hist.pop_front();
            a.histT = nowT;
        }
    }
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

static void historyTab(App& a) {
    if (a.hist.empty()) { ImGui::TextDisabled("history fills while the receiver runs (4 samples per second, last 15 minutes)"); return; }
    const double nowT = glfwGetTime();
    {
        static const int wins[] = {30, 60, 300, 900};
        static const char* names[] = {"30 s", "1 min", "5 min", "15 min"};
        ImGui::TextDisabled("window"); ImGui::SameLine();
        for (int i = 0; i < 4; i++) { if (i) ImGui::SameLine(); if (ImGui::RadioButton(names[i], a.histWindow == wins[i])) a.histWindow = wins[i]; }
    }
    const float h = (ImGui::GetContentRegionAvail().y - 30) / 4.f;
    auto series = [&](const char* id, const char* ylab, std::initializer_list<std::pair<const char*, float App::HistSample::*>> ser, ImVec2 size, double ymin, double ymax, bool fixed) {
        if (!ImPlot::BeginPlot(id, size, ImPlotFlags_NoTitle)) return;
        ImPlot::SetupAxes("seconds ago", ylab, 0, fixed ? 0 : ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, -a.histWindow, 0, ImPlotCond_Always);
        if (fixed) ImPlot::SetupAxisLimits(ImAxis_Y1, ymin, ymax, ImPlotCond_Once);
        std::vector<float> xs(a.hist.size());
        for (size_t i = 0; i < xs.size(); i++) xs[i] = (float)(a.hist[i].t - nowT);
        int k = 0;
        static const ImVec4 cols[3] = {ImVec4(0.45f, 0.75f, 1, 1), ImVec4(0.95f, 0.7f, 0.2f, 1), ImVec4(0.4f, 0.85f, 0.5f, 1)};
        for (auto& s : ser) {
            std::vector<float> ys(a.hist.size());
            for (size_t i = 0; i < ys.size(); i++) ys[i] = a.hist[i].*(s.second);
            ImPlotSpec sp; sp.LineColor = cols[k++ % 3];
            ImPlot::PlotLine(s.first, xs.data(), ys.data(), (int)xs.size(), sp);
        }
        ImPlot::EndPlot();
    };
    series("##h1", "quality (%)", {{"quality", &App::HistSample::quality}}, ImVec2(-1, h), 0, 100, true);
    series("##h2", "SNR / MER (dB)", {{"data SNR", &App::HistSample::snr}, {"MER", &App::HistSample::mer}}, ImVec2(-1, h), 0, 0, false);
    series("##h3", "FEC block loss (%)", {{"lost blocks", &App::HistSample::loss}}, ImVec2(-1, h), 0, 100, true);
    series("##h4", "ADC level (dBFS)", {{"rms", &App::HistSample::level}}, ImVec2(-1, h), -60, 0, true);
    ImGui::TextDisabled("CFO and SRO are on the Sync tab. Gaps in a line mean the receiver was not locked.");
}

static void frameMapTab(App& a) {
    const RxTelemetry& rx = a.rx;
    if (!rx.l1preOk) { ImGui::TextDisabled("the frame structure appears once L1-pre has been decoded"); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const L1Pre& p = rx.l1pre;
    static const int nP2tab[6] = {8, 2, 4, 16, 1, 1};
    const int fftCode = p.s2 >> 1;
    const int nP2 = fftCode >= 0 && fftCode < 6 ? nP2tab[fftCode] : 1;
    const int nData = p.numDataSyms;
    ImGui::TextDisabled("T2 frame: P1 + %d P2 symbol%s + %d data symbols (%s, GI %s)  -  %.1f ms", nP2, nP2 == 1 ? "" : "s", nData, fftModeFromSize(rx.fftN) ? fftModeFromSize(rx.fftN)->name : "?", guardName(rx.giIdx), rx.frameMs);
    ImVec2 o = ImGui::GetCursorScreenPos();
    const float W = ImGui::GetContentRegionAvail().x - 10, H = 34;
    const double symUnits = 1.0;             // widths in OFDM symbols; P1 is about half a long symbol
    const double p1u = 0.5, total = p1u + nP2 + nData;
    float x = o.x;
    auto seg = [&](double units, ImU32 col, const char* label) {
        const float w = (float)(units / total * W);
        dl->AddRectFilled(ImVec2(x, o.y), ImVec2(x + w - 1, o.y + H), col, 2.f);
        if (w > 28) { ImVec2 ts = ImGui::CalcTextSize(label); dl->AddText(ImVec2(x + (w - ts.x) / 2, o.y + (H - ts.y) / 2), IM_COL32(15, 18, 22, 255), label); }
        x += w;
    };
    (void)symUnits;
    seg(p1u, IM_COL32(240, 180, 60, 255), "P1");
    seg(nP2, IM_COL32(230, 120, 190, 255), "P2");
    seg(nData, IM_COL32(90, 200, 120, 255), "data symbols (PLP cells, pilots, edge pilots)");
    ImGui::Dummy(ImVec2(W, H + 6));
    if (rx.plpValid) ImGui::TextDisabled("PLP %d: %d FEC blocks per frame (%s, rate index %d%s), time interleaver %d", rx.plpId, rx.plpBlocks, rx.plpFec.mod == 0 ? "QPSK" : rx.plpFec.mod == 1 ? "16-QAM" : rx.plpFec.mod == 2 ? "64-QAM" : "256-QAM", rx.plpFec.rate, rx.plpFec.rotation ? ", rotated" : "", p.numDataSyms > 0 ? 0 : 0);
    ImGui::Separator();
    ImGui::TextDisabled("FEC block map: one row per decoded frame (newest at the bottom), one cell per FEC block. Green = decoded, red = failed.");
    const auto& bm = rx.blockMap;
    if (bm.empty()) { ImGui::TextDisabled("no decoded frames yet"); return; }
    size_t nb = 0;
    for (auto& f : bm) nb = std::max(nb, f.size());
    ImVec2 g = ImGui::GetCursorScreenPos();
    const float availW = ImGui::GetContentRegionAvail().x - 80, availH = ImGui::GetContentRegionAvail().y - 4;
    const float cw = nb ? std::max(1.f, availW / (float)nb) : 1.f;
    const float ch = std::max(2.f, std::min(10.f, availH / (float)bm.size()));
    for (size_t r = 0; r < bm.size(); r++) {
        const float y = g.y + r * ch;
        int ok = 0;
        for (size_t b = 0; b < bm[r].size(); b++) {
            ok += bm[r][b];
            dl->AddRectFilled(ImVec2(g.x + b * cw, y), ImVec2(g.x + (b + 1) * cw, y + ch - 1), bm[r][b] ? IM_COL32(60, 170, 90, 255) : IM_COL32(220, 60, 55, 255));
        }
        const float pct = bm[r].empty() ? 0.f : 100.f * ok / (float)bm[r].size();
        char t[16]; snprintf(t, sizeof t, "%.0f%%", pct);
        if (ch >= 8) dl->AddText(ImVec2(g.x + nb * cw + 6, y - 2), pct < 99.5f ? IM_COL32(240, 140, 120, 255) : IM_COL32(140, 150, 160, 255), t);
    }
    ImGui::Dummy(ImVec2(availW + 70, ch * bm.size()));
}

static ImU32 ttxColour(int c, float alpha = 1.f) {
    static const ImVec4 col[8] = {ImVec4(0, 0, 0, 1), ImVec4(1, 0.1f, 0.1f, 1), ImVec4(0.1f, 1, 0.1f, 1), ImVec4(1, 1, 0.1f, 1),
                                  ImVec4(0.2f, 0.3f, 1, 1), ImVec4(1, 0.2f, 1, 1), ImVec4(0.1f, 1, 1, 1), ImVec4(1, 1, 1, 1)};
    ImVec4 v = col[c & 7]; v.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(v);
}

static void teletextTab(App& a) {
    TeletextDecoder& tx = a.engine.teletext();
    const Player& pl = a.engine.player();
    if (tx.pid() < 0) {
        ImGui::TextDisabled(pl.selected() < 0 ? "Play a service (Player tab) - teletext is read from the playing service." : "The playing service has no teletext stream.");
        return;
    }
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("page", &a.ttxPage, 0, 0);
    a.ttxPage = std::max(100, std::min(899, a.ttxPage));
    ImGui::SameLine(); if (ImGui::SmallButton("<")) a.ttxPage = std::max(100, a.ttxPage - 1);
    ImGui::SameLine(); if (ImGui::SmallButton(">")) a.ttxPage = std::min(899, a.ttxPage + 1);
    ImGui::SameLine(); if (ImGui::SmallButton("100")) a.ttxPage = 100;
    ImGui::SameLine();
    {
        auto pages = tx.pages();
        ImGui::TextDisabled("%zu pages received, %llu packets", pages.size(), (unsigned long long)tx.packets());
        if (!pages.empty()) {
            ImGui::SameLine();
            std::string l = "available: ";
            int n = 0;
            for (int p : pages) { if (n++ >= 14) { l += "..."; break; } l += std::to_string(p) + " "; }
            ImGui::TextDisabled("%s", l.c_str());
        }
    }
    TtxPage pg;
    if (!tx.page(a.ttxPage, pg)) { ImGui::TextDisabled("page %d has not been received yet (pages repeat every few seconds)", a.ttxPage); return; }
    TtxGrid grid;
    ttxRender(pg, grid);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float cw = std::max(6.f, std::min(avail.x / 40.f, avail.y / 25.f * 0.55f)), ch = cw / 0.55f;
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, ImVec2(o.x + cw * 40, o.y + ch * 25), IM_COL32(0, 0, 0, 255));
    const bool flashOn = std::fmod(ImGui::GetTime(), 1.0) < 0.6;
    for (int r = 0; r < 25; r++)
        for (int c = 0; c < 40; c++) {
            const TtxCell& cell = grid[r][c];
            const ImVec2 p0(o.x + c * cw, o.y + r * ch), p1(p0.x + cw, p0.y + ch);
            if (cell.bg) dl->AddRectFilled(p0, p1, ttxColour(cell.bg));
            if (cell.flash && !flashOn) continue;
            if (cell.mosaic) {
                const float gx = cell.separated ? 1.f : 0.f;
                for (int k = 0; k < 6; k++) {
                    if (!(cell.sextants & (1 << k))) continue;
                    const int cx = k & 1, cy = k >> 1;
                    dl->AddRectFilled(ImVec2(p0.x + cx * cw / 2 + gx, p0.y + cy * ch / 3 + gx), ImVec2(p0.x + (cx + 1) * cw / 2 - gx, p0.y + (cy + 1) * ch / 3 - gx), ttxColour(cell.fg));
                }
            } else if (cell.ch > ' ') {
                char buf[5] = {0};
                uint32_t u = cell.ch;
                if (u < 0x80) buf[0] = (char)u;
                else if (u < 0x800) { buf[0] = (char)(0xC0 | (u >> 6)); buf[1] = (char)(0x80 | (u & 0x3F)); }
                else { buf[0] = (char)(0xE0 | (u >> 12)); buf[1] = (char)(0x80 | ((u >> 6) & 0x3F)); buf[2] = (char)(0x80 | (u & 0x3F)); }
                ImFont* f = a.mono;
                const float fs = cell.dh ? ch * 2.0f * 0.8f : ch * 0.8f;
                if (cell.dh == 1) { dl->PushClipRect(p0, p1, true); dl->AddText(f, fs, ImVec2(p0.x, p0.y), ttxColour(cell.fg), buf); dl->PopClipRect(); }
                else if (cell.dh == 2) { dl->PushClipRect(p0, p1, true); dl->AddText(f, fs, ImVec2(p0.x, p0.y - ch), ttxColour(cell.fg), buf); dl->PopClipRect(); }
                else dl->AddText(f, fs, ImVec2(p0.x, p0.y), ttxColour(cell.fg), buf);
            }
        }
    char title[64];
    snprintf(title, sizeof title, "P%d%s", pg.number, pg.subtitle ? "  (subtitle page)" : "");
    dl->AddText(ImVec2(o.x + 2, o.y + 1), IM_COL32(255, 255, 255, 255), title);
    ImGui::Dummy(ImVec2(cw * 40, ch * 25));
}

// ---- programme guide helpers
static int64_t utcNowOf(const App& a) { return a.ts.utcNow ? a.ts.utcNow : (int64_t)time(nullptr); }
static std::string fmtLocal(int64_t utc, const char* f) {
    time_t t = (time_t)utc; struct tm m; localtime_r(&t, &m);
    char b[48]; strftime(b, sizeof b, f, &m);
    return b;
}
static const EpgEvent* epgCurrent(const App& a, int sid, const EpgEvent** next = nullptr) {
    auto it = a.epg.find(sid);
    if (it == a.epg.end()) return nullptr;
    const int64_t now = utcNowOf(a);
    const EpgEvent* cur = nullptr;
    if (next) *next = nullptr;
    for (size_t i = 0; i < it->second.size(); i++) {
        const EpgEvent& e = it->second[i];
        if (e.start <= now && now < e.end()) { cur = &e; if (next && i + 1 < it->second.size()) *next = &it->second[i + 1]; break; }
        if (e.start > now) { if (next && !*next) *next = &e; break; }
    }
    return cur;
}
static const char* genreName(int g) {
    static const char* n[] = {"", "Movie / drama", "News / current affairs", "Show / game show", "Sports", "Children's / youth", "Music / ballet / dance", "Arts / culture", "Social / political / economics", "Education / science", "Leisure / hobbies"};
    return g >= 1 && g <= 10 ? n[g] : "";
}

static void guideTab(App& a) {
    const int64_t now = utcNowOf(a);
    if (a.ts.services.empty()) { ImGui::TextDisabled("waiting for the service list..."); return; }
    size_t total = 0;
    for (auto& kv : a.epg) total += kv.second.size();
    if (total == 0) {
        ImGui::TextWrapped("No programme information received yet. Broadcasters send it in the EIT tables, which repeat every few seconds (now/next) to a few minutes (schedule). "
                           "Some multiplexes send none at all: the 522 MHz Sharjah mux transmits empty EIT sections.");
        ImGui::TextDisabled("clock: %s%s", fmtLocal(now, "%a %d %b %H:%M").c_str(), a.ts.utcNow ? " (from the broadcast time signal)" : " (this computer's clock)");
        return;
    }
    if (a.guideSid < 0 || !a.epg.count(a.guideSid)) { a.guideSid = a.engine.player().selected() >= 0 && a.epg.count(a.engine.player().selected()) ? a.engine.player().selected() : a.epg.begin()->first; }
    ImGui::BeginChild("gsv", ImVec2(260, 0), ImGuiChildFlags_Borders);
    for (auto& sv : a.ts.services) {
        auto it = a.epg.find(sv.id);
        if (it == a.epg.end()) continue;
        const EpgEvent* nx = nullptr;
        const EpgEvent* cur = epgCurrent(a, sv.id, &nx);
        ImGui::PushID(sv.id);
        if (ImGui::Selectable(sv.name.empty() ? std::to_string(sv.id).c_str() : sv.name.c_str(), a.guideSid == sv.id)) { a.guideSid = sv.id; a.guideEvent = -1; }
        ImGui::TextDisabled("  %s", cur ? (fmtLocal(cur->start, "%H:%M") + " " + cur->title).c_str() : "-");
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("gev", ImVec2(0, 0));
    const auto& evs = a.epg[a.guideSid];
    ImGui::TextDisabled("%zu events   local time %s", evs.size(), fmtLocal(now, "%a %d %b %H:%M").c_str());
    const float detailH = 150;
    ImGui::BeginChild("evlist", ImVec2(0, -detailH), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("evt", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("time", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("length", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("programme");
        std::string lastDay;
        for (size_t i = 0; i < evs.size(); i++) {
            const EpgEvent& e = evs[i];
            if (e.end() < now - 1800) continue; // hide what ended more than half an hour ago
            const bool cur = e.start <= now && now < e.end();
            const std::string day = fmtLocal(e.start, "%a %d %b");
            if (day != lastDay) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), "%s", day.c_str()); lastDay = day; }
            ImGui::TableNextRow();
            if (cur) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 110, 60, 110));
            ImGui::TableNextColumn();
            char lbl[64]; snprintf(lbl, sizeof lbl, "%s - %s##%d", fmtLocal(e.start, "%H:%M").c_str(), fmtLocal(e.end(), "%H:%M").c_str(), e.eventId);
            if (ImGui::Selectable(lbl, a.guideEvent == e.eventId, ImGuiSelectableFlags_SpanAllColumns)) a.guideEvent = e.eventId;
            ImGui::TableNextColumn(); ImGui::Text("%d min", (e.duration + 30) / 60);
            ImGui::TableNextColumn();
            if (cur) { const float f = (float)(now - e.start) / (float)e.duration; ImGui::Text("%s", e.title.c_str()); ImGui::SameLine(); ImGui::TextDisabled("  now, %.0f%%", f * 100); }
            else ImGui::TextUnformatted(e.title.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::BeginChild("evdet", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const EpgEvent* sel = nullptr;
    for (auto& e : evs) if (e.eventId == a.guideEvent) sel = &e;
    if (!sel) sel = epgCurrent(a, a.guideSid);
    if (sel) {
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.4f, 1), "%s", sel->title.c_str());
        ImGui::TextDisabled("%s - %s  (%d min)%s%s", fmtLocal(sel->start, "%a %H:%M").c_str(), fmtLocal(sel->end(), "%H:%M").c_str(), (sel->duration + 30) / 60, genreName(sel->genre)[0] ? "   " : "", genreName(sel->genre));
        if (!sel->text.empty()) ImGui::TextWrapped("%s", sel->text.c_str());
        if (!sel->extended.empty()) ImGui::TextWrapped("%s", sel->extended.c_str());
    } else ImGui::TextDisabled("select a programme");
    ImGui::EndChild();
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
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), "L1-pre   (decoded %llu, failed %llu, LDPC iterations %d)", (unsigned long long)rx.l1preGood, (unsigned long long)rx.l1preBad, rx.l1Iters);
    if (rx.l1preGood > 0) {
        const L1Pre& p = rx.l1pre;
        static const char* paprN[] = {"off", "ACE", "TR", "ACE + TR"};
        static const char* modN[] = {"BPSK", "QPSK", "16-QAM", "64-QAM"};
        static const char* verN[] = {"1.1.1", "1.2.1", "1.3.1"};
        row("TYPE", "%d  %s", p.type, p.type == 0 ? "TS" : p.type == 1 ? "GSE/GS" : "TS + GS");
        row("carrier mode", "%s", p.bwtExt ? "extended" : "normal");
        row("S1 / S2", "%d / %d (FFT %s%s)", p.s1, p.s2, fftModeFromS2(p.s2 >> 1) ? fftModeFromS2(p.s2 >> 1)->name : "?", (p.s2 & 1) ? ", FEF present" : "");
        row("guard interval", "%s", guardName(p.guardInterval));
        row("PAPR", "%s", p.papr < 4 ? paprN[p.papr] : "reserved");
        row("L1-post", "%s, rate 1/2, %s FEC, %d cells, %d info bits%s", p.l1Mod < 4 ? modN[p.l1Mod] : "?", p.l1Fec == 0 ? "16K" : "64K", p.postSize, p.postInfoSize, p.postScrambled ? ", scrambled" : "");
        row("pilot pattern", "PP%d", p.pilotPattern + 1);
        row("cell / network / system", "%d / 0x%04X / 0x%04X", p.cellId, p.networkId, p.systemId);
        row("T2 frames per super-frame", "%d", p.numFrames);
        row("data symbols per frame", "%d", p.numDataSyms);
        row("T2 version", "%s%s", p.version < 3 ? verN[p.version] : "?", p.lite ? "  (T2-Lite)" : "");
        row("RF channels", "%d (current %d)  regen %d  tx-id %d", p.numRf, p.curRf, p.regen, p.txIdAvail);
    } else ImGui::TextDisabled("not decoded yet");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), "L1-post   (decoded %llu, failed %llu)", (unsigned long long)rx.l1postGood, (unsigned long long)rx.l1postBad);
    if (rx.l1postGood > 0) {
        const L1Post& q = rx.l1post;
        static const char* cod[] = {"1/2", "3/5", "2/3", "3/4", "4/5", "5/6", "1/3", "2/5"};
        static const char* pmod[] = {"QPSK", "16-QAM", "64-QAM", "256-QAM"};
        static const char* ptype[] = {"common", "data type 1", "data type 2"};
        row("sub-slices / PLPs / aux", "%d / %d / %d", q.subSlices, q.numPlp, q.numAux);
        for (auto& rf : q.rf) row("RF frequency", "%.3f MHz  (index %d)", rf.freq / 1e6, rf.idx);
        row("frame index", "%d   change counter %d", q.frameIdx, q.changeCounter);
        for (size_t i = 0; i < q.plps.size(); i++) {
            const L1PlpConf& c = q.plps[i];
            char hdr[32]; snprintf(hdr, sizeof hdr, "PLP %d", c.id);
            row(hdr, "%s, %s, rate %s, %s, %s FEC, rotation %s", c.type < 3 ? ptype[c.type] : "?", c.mod < 4 ? pmod[c.mod] : "?", c.cod < 8 ? cod[c.cod] : "?",
                c.payloadType == 3 ? "TS" : c.payloadType == 0 ? "GFPS" : c.payloadType == 1 ? "GCS" : "GSE", c.fecType == 1 ? "64K" : "16K", c.rotation ? "on" : "off");
            row("", "max %d FEC blocks, TI %d (%s), group %d, interval %d, mode %d", c.numBlocksMax, c.timeIlLength, c.timeIlType ? "multi-frame" : "one frame", c.groupId, c.frameInterval, c.plpMode);
            if (i < q.dyn.size()) row("", "start %d, %d blocks in this frame", q.dyn[i].start, q.dyn[i].numBlocks);
        }
    } else ImGui::TextDisabled("%s", rx.l1preGood ? "not decoded (L1-pre must be valid first)" : "not decoded yet");
    ImGui::PopFont();
}

static void constellationsTab(App& a) {
    float w = ImGui::GetContentRegionAvail().x / 4 - 8;
    float h = std::min(ImGui::GetContentRegionAvail().y - 40, w);
    ImGui::BeginGroup();
    ImGui::TextDisabled("P1 carriers (DBPSK, differential)");
    scatter("##c1", a.rx.p1Const, ImVec2(w, h), 2.5, ImVec4(0.95f, 0.7f, 0.2f, 0.9f));
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("P2 cells, equalised (L1 + PLP)");
    scatter("##ceq", a.rx.eqCells, ImVec2(w, h), 2.0, ImVec4(0.9f, 0.45f, 0.75f, 0.8f));
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (!a.rx.plpConst.empty()) ImGui::TextDisabled("PLP %d symbols, derotated, MER %.1f dB", a.rx.plpId, a.rx.plpMerDb);
    else ImGui::TextDisabled("Data cells, equalised (frame %llu)", (unsigned long long)a.rx.dataFrames);
    if (!a.rx.plpConst.empty()) {
        // PLP cells that belong to correctly decoded FEC blocks; points far from their transmitted constellation point are red
        std::vector<cf32> good, bad;
        for (size_t i = 0; i < a.rx.plpConst.size(); i++) (a.rx.plpConstErr.size() > i && a.rx.plpConstErr[i] ? bad : good).push_back(a.rx.plpConst[i]);
        if (ImPlot::BeginPlot("##c2", ImVec2(w, h), ImPlotFlags_NoLegend | ImPlotFlags_NoTitle | ImPlotFlags_Equal)) {
            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
            ImPlot::SetupAxisLimits(ImAxis_X1, -1.5, 1.5, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -1.5, 1.5, ImPlotCond_Always);
            auto draw = [&](const char* id, const std::vector<cf32>& v, ImVec4 col) {
                if (v.empty()) return;
                ImPlotSpec sp; sp.Marker = ImPlotMarker_Circle; sp.MarkerSize = 1.4f; sp.Stride = sizeof(cf32);
                sp.MarkerFillColor = col; sp.MarkerLineColor = col; sp.LineColor = col;
                const float* d = reinterpret_cast<const float*>(v.data());
                ImPlot::PlotScatter(id, d, d + 1, (int)v.size(), sp);
            };
            {   // the transmitted (unrotated) constellation points
                const int M = 2 * (a.rx.plpFec.mod + 1);
                std::vector<cf32> grid;
                for (unsigned l = 0; l < (1u << M); l++) grid.push_back(qamPoint(a.rx.plpFec.mod, false, l));
                ImPlotSpec gs; gs.Marker = ImPlotMarker_Cross; gs.MarkerSize = 3.f; gs.Stride = sizeof(cf32);
                gs.MarkerFillColor = gs.MarkerLineColor = gs.LineColor = ImVec4(1, 1, 1, 0.45f);
                const float* g = reinterpret_cast<const float*>(grid.data());
                ImPlot::PlotScatter("ideal", g, g + 1, (int)grid.size(), gs);
            }
            draw("ok", good, ImVec4(0.35f, 0.85f, 0.45f, 0.55f));
            draw("err", bad, ImVec4(0.95f, 0.35f, 0.3f, 0.8f));
            ImPlot::EndPlot();
        }
    } else
    scatter("##c2", a.rx.eqData, ImVec2(w, h), 1.8, ImVec4(0.35f, 0.85f, 0.45f, 0.6f));
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("OFDM cells, raw");
    scatter("##c3", a.rx.rawCells, ImVec2(w, h), 3.0, ImVec4(0.45f, 0.65f, 1.0f, 0.7f));
    ImGui::EndGroup();
    if (a.rx.dataValid)
        ImGui::TextDisabled("Pilot pattern PP%d (dx %d, dy %d). Data SNR %.1f dB. The PLP panel shows the cells of FEC blocks that decoded cleanly; "
                            "red points are cells whose error exceeded the decision distance.", a.rx.dataPp + 1, a.rx.dataDx, a.rx.dataDy, a.rx.dataSnrDb);
    else
        ImGui::TextDisabled("Raw cells are rotated by the channel and timing (a ring). Equalised views appear once L1-pre has been decoded.");
}

static void channelTab(App& a) {
    const RxTelemetry& rx = a.rx;
    if (!rx.chValid) { ImGui::TextDisabled("no channel estimate yet (needs a decoded P1 and a P2 symbol)"); return; }
    {
        const MultipathReport& mr = a.mpd.report();
        ImGui::TextColored(mr.level == MultipathLevel::None ? ImVec4(0.4f, 0.85f, 0.5f, 1) : mr.level == MultipathLevel::Mild ? ImVec4(0.95f, 0.8f, 0.3f, 1) : ImVec4(0.95f, 0.5f, 0.25f, 1),
                           "Multipath analysis: %s", mr.headline.c_str());
        for (auto& r : mr.reasons) { ImGui::SameLine(0, 0); ImGui::NewLine(); ImGui::TextDisabled("  - %s", r.c_str()); }
        ImGui::Separator();
    }
    float h = ImGui::GetContentRegionAvail().y;
    double fnMhz = rx.nativeRate / 1e6, binMhz = fnMhz / rx.fftN;
    std::vector<float> xs(rx.chMagDb.size());
    for (size_t i = 0; i < xs.size(); i++) xs[i] = (float)(a.freqMhz + ((double)(i * rx.chDecim) - (rx.chCarriers - 1) / 2.0) * binMhz);
    ImGui::TextDisabled("channel magnitude |H(f)| from P2 pilots (relative, dB) - %d carriers%s", rx.chCarriers, rx.extCarriers ? ", extended mode" : "");
    if (ImPlot::BeginPlot("##chm", ImVec2(-1, h * 0.5f), ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("frequency (MHz)", "dB", 0, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%.2f");
        ImPlot::SetupAxisLimits(ImAxis_X1, xs.front(), xs.back(), ImPlotCond_Always);
        ImPlotSpec sp; sp.LineColor = ImVec4(0.45f, 0.75f, 1, 1);
        ImPlot::PlotLine("mag", xs.data(), rx.chMagDb.data(), (int)xs.size(), sp);
        ImPlot::EndPlot();
    }
    ImGui::TextDisabled("channel phase (radians); a steady slope is timing offset, curvature is echoes");
    if (ImPlot::BeginPlot("##chp", ImVec2(-1, -1), ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("frequency (MHz)", "rad");
        ImPlot::SetupAxisFormat(ImAxis_X1, "%.2f");
        ImPlot::SetupAxisLimits(ImAxis_X1, xs.front(), xs.back(), ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -3.3, 3.3, ImPlotCond_Always);
        ImPlotSpec sp; sp.LineColor = ImVec4(0.95f, 0.7f, 0.2f, 1); sp.Marker = ImPlotMarker_Circle; sp.MarkerSize = 1.2f;
        sp.LineWeight = 0.0f;
        ImPlot::PlotScatter("ph", xs.data(), rx.chPhase.data(), (int)xs.size(), sp);
        ImPlot::EndPlot();
    }
}

static void impulseTab(App& a) {
    const RxTelemetry& rx = a.rx;
    if (!rx.chValid || rx.irDb.empty()) { ImGui::TextDisabled("no channel estimate yet"); return; }
    std::vector<float> xs(rx.irDb.size());
    double usPerSample = 1e6 / rx.nativeRate;
    for (size_t i = 0; i < xs.size(); i++) xs[i] = (float)((rx.irTauMin + (double)i) * usPerSample);
    ImGui::TextDisabled("power-delay profile (dB rel. strongest path). Shaded: guard interval (%.1f us)", rx.guard * usPerSample);
    if (ImPlot::BeginPlot("##ir", ImVec2(-1, -1), ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("delay (us)", "dB");
        ImPlot::SetupAxisLimits(ImAxis_Y1, -80, 5, ImPlotCond_Once);
        ImPlot::SetupAxisLimits(ImAxis_X1, xs.front(), xs.back(), ImPlotCond_Once);
        double gx[2] = {0, rx.guard * usPerSample}, gy[2] = {5, 5};
        ImPlotSpec gs; gs.FillColor = ImVec4(0.15f, 0.55f, 0.20f, 0.18f); gs.LineColor = ImVec4(0, 0, 0, 0);
        ImPlot::PlotShaded("gi", gx, gy, 2, -80.0, gs);
        ImPlotSpec sp; sp.LineColor = ImVec4(0.45f, 0.75f, 1, 1);
        ImPlot::PlotLine("pdp", xs.data(), rx.irDb.data(), (int)xs.size(), sp);
        ImPlot::EndPlot();
    }
}

static void snrTab(App& a) {
    const RxTelemetry& rx = a.rx;
    if (rx.snrDb.empty()) { ImGui::TextDisabled("per-carrier SNR appears once a full frame has been received"); if (rx.chValid) ImGui::Text("SNR estimate: %.1f dB", rx.p2SnrDb); return; }
    double binMhz = rx.nativeRate / 1e6 / rx.fftN;
    std::vector<float> xs(rx.snrDb.size());
    for (size_t i = 0; i < xs.size(); i++) xs[i] = (float)(a.freqMhz + ((double)(i * rx.snrStep) - (rx.chCarriers - 1) / 2.0) * binMhz);
    ImGui::Text("pilot-derived SNR across the channel (scattered pilots, smoothed over %d points); mean %.1f dB", 21, rx.p2SnrDb);
    if (ImPlot::BeginPlot("##snrc", ImVec2(-1, -1), ImPlotFlags_NoLegend | ImPlotFlags_NoTitle)) {
        ImPlot::SetupAxes("frequency (MHz)", "SNR (dB)", 0, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisFormat(ImAxis_X1, "%.2f");
        ImPlot::SetupAxisLimits(ImAxis_X1, xs.front(), xs.back(), ImPlotCond_Always);
        ImPlotSpec sp; sp.LineColor = ImVec4(0.35f, 0.85f, 0.45f, 1);
        ImPlot::PlotLine("snr", xs.data(), rx.snrDb.data(), (int)xs.size(), sp);
        ImPlot::EndPlot();
    }
}

static void fecTab(App& a) {
    const RxTelemetry& rx = a.rx;
    ImGui::PushFont(a.mono, 0);
    auto row = [](const char* k, const char* fmt, ...) {
        char b[200];
        va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        ImGui::TextDisabled("%-26s", k); ImGui::SameLine(); ImGui::TextUnformatted(b);
    };
    if (!rx.plpValid) {
        ImGui::TextDisabled("waiting for L1-post (PLP configuration)...");
        if (rx.plpSkipped) ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.2f, 1), "%d frame(s) skipped: PLP uses inter-frame time interleaving or lies outside the received cells", rx.plpSkipped);
        ImGui::PopFont();
        return;
    }
    static const char* modN[] = {"QPSK", "16-QAM", "64-QAM", "256-QAM"};
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), "PLP %d", rx.plpId);
    row("FEC frame / code rate", "%s, %s", rx.plpFec.shortFrame ? "short (16200)" : "normal (64800)", rateName(rx.plpFec.rate));
    row("constellation", "%s%s", modN[rx.plpFec.mod & 3], rx.plpFec.rotation ? ", rotated + cyclic Q delay" : "");
    row("FEC blocks per frame", "%d", rx.plpBlocks);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), "decoder (all frames since start)");
    double tot = (double)(rx.blocksOk + rx.blocksBad);
    row("frames decoded", "%llu  (dropped, decoder busy: %llu)", (unsigned long long)rx.plpFrames, (unsigned long long)rx.plpFramesDropped);
    row("FEC blocks OK / failed", "%llu / %llu  (%.2f%% good)", (unsigned long long)rx.blocksOk, (unsigned long long)rx.blocksBad, tot ? 100.0 * rx.blocksOk / tot : 0.0);
    row("BB headers with valid CRC", "%llu", (unsigned long long)rx.headersOk);
    row("BCH bit errors corrected", "%llu", (unsigned long long)rx.plpBchCorrected);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), "last frame");
    row("MER", "%.1f dB", rx.plpMerDb);
    row("BER before LDPC", "%.2e", rx.plpPreBer);
    row("LDPC iterations (average)", "%.1f", rx.plpIters);
    row("decode time", "%.0f ms for %.0f ms of signal (%s)", rx.plpDecodeMs, rx.frameMs, rx.plpOnGpu ? "GPU" : "CPU");
    if (rx.plpHeaderUpl) row("BB header", "UPL %d bits, DFL %d bits, SYNCD %d", rx.plpHeaderUpl, rx.plpHeaderDfl, rx.plpHeaderSyncd);
    ImGui::PopFont();
}

static const char* fmtKbps(char* b, size_t n, double k) { if (k >= 1000) snprintf(b, n, "%.2f Mbit/s", k / 1000); else snprintf(b, n, "%.0f kbit/s", k); return b; }

static void tsTab(App& a) {
    const TsSnapshot& ts = a.ts;
    ImGui::PushFont(a.mono, 0);
    char b[64], b2[64];
    ImGui::Text("network \"%s\"  onid 0x%04X  tsid 0x%04X   %s", ts.networkName.c_str(), ts.onid, ts.tsid, ts.utc.c_str());
    ImGui::Text("multiplex %s (null packets %s)   packets %llu   continuity errors %llu   TEI %llu",
                fmtKbps(b, sizeof b, ts.muxKbps), fmtKbps(b2, sizeof b2, ts.nullKbps), (unsigned long long)ts.totalPackets, (unsigned long long)ts.ccErrors, (unsigned long long)ts.teiPackets);
    ImGui::Text("BB frames %llu (lost %llu)  mode %s  ISSYI %d NPD %d  resyncs %llu  CRC-8 errors %llu", (unsigned long long)a.bb.frames, (unsigned long long)a.bb.framesLost,
                a.bb.hem ? "high-efficiency" : "normal", a.bb.issyi, a.bb.npd, (unsigned long long)a.bb.resyncs, (unsigned long long)a.bb.crcErrors);
    ImGui::PopFont();
    ImGui::Spacing();
    if (ImGui::BeginTable("pids", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp, ImVec2(0, -1))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("content");
        ImGui::TableSetupColumn("packets", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("rate", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("CC err", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("scr", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableHeadersRow();
        for (auto& p : ts.pids) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("0x%04X", p.pid);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(p.label.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)p.packets);
            ImGui::TableNextColumn(); { char r[32]; ImGui::TextUnformatted(fmtKbps(r, sizeof r, p.kbps)); }
            ImGui::TableNextColumn(); if (p.ccErrors) ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.3f, 1), "%llu", (unsigned long long)p.ccErrors); else ImGui::TextDisabled("0");
            ImGui::TableNextColumn(); if (p.scrambled) ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.2f, 1), "yes"); else ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
    }
}

static void applyOutputs(App& a) {
    a.out.host = a.udpHost;
    a.out.path = a.filePath;
    a.out.serviceId = a.selService;
    a.engine.setOutputs(a.out);
    savePrefs(a);
}


static void scanTab(App& a) {
    ScanProgress pr = a.scanner.progress();
    auto res = a.scanner.results();
    int hw = -1;
    for (int i = 0; i < (int)a.devices.size(); i++) if (a.devices[i].kind == DeviceInfo::HackRF) hw = i;
    if (hw < 0) { ImGui::TextDisabled("Scanning needs a HackRF."); return; }
    ImGui::BeginDisabled(pr.running);
    const char* presets[] = {"UHF 474-858 MHz (8 MHz)", "VHF III 174-230 MHz (7 MHz)", "Custom"};
    ImGui::SetNextItemWidth(240);
    if (ImGui::Combo("##range", &a.scanPreset, presets, 3)) {
        if (a.scanPreset == 0) { a.scanCfg.startMHz = 474; a.scanCfg.stopMHz = 858; a.scanCfg.stepMHz = 8; a.scanCfg.bwMhz = 8; }
        if (a.scanPreset == 1) { a.scanCfg.startMHz = 177.5; a.scanCfg.stopMHz = 226.5; a.scanCfg.stepMHz = 7; a.scanCfg.bwMhz = 7; }
    }
    ImGui::SameLine(); ImGui::TextUnformatted("from"); ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(80); ImGui::InputDouble("##from", &a.scanCfg.startMHz, 0, 0, "%.1f");
    ImGui::SameLine(); ImGui::TextUnformatted("to"); ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(80); ImGui::InputDouble("##to", &a.scanCfg.stopMHz, 0, 0, "%.1f");
    ImGui::SameLine(); ImGui::TextUnformatted("step"); ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(60); ImGui::InputDouble("##step", &a.scanCfg.stepMHz, 0, 0, "%.1f");
    ImGui::SameLine(); ImGui::TextUnformatted("bw"); ImGui::SameLine(0, 4); ImGui::SetNextItemWidth(60); ImGui::InputDouble("##bw", &a.scanCfg.bwMhz, 0, 0, "%.0f");
    ImGui::Checkbox("read service names (slower, ~9 s per mux)", &a.scanCfg.identifyServices);
    ImGui::EndDisabled();
    if (a.scanCfg.stepMHz < 1) a.scanCfg.stepMHz = 1;
    if (!pr.running) {
        if (ImGui::Button("  Start scan  ")) {
            a.scanWasRunning = a.engine.running();
            if (a.scanWasRunning) a.engine.stop();
            a.scanCfg.tune = a.tune;
            std::string err;
            if (!a.scanner.start(a.devices[hw], a.scanCfg, err)) a.engine.log("scan: " + err);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s  (uses the gains from the toolbar; stops the receiver while scanning)", pr.phase.c_str());
    } else {
        if (ImGui::Button("  Stop scan  ")) a.scanner.stop();
        ImGui::SameLine();
        ImGui::ProgressBar(pr.total ? (float)pr.index / pr.total : 0, ImVec2(260, 0));
        ImGui::SameLine();
        ImGui::Text("%.1f MHz - %s", pr.currentMHz, pr.phase.c_str());
    }
    ImGui::Separator();
    if (ImGui::BeginTable("scan", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("MHz", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("level", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("result", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("mode", ImGuiTableColumnFlags_WidthFixed, 240);
        ImGui::TableSetupColumn("SNR", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("PLP", ImGuiTableColumnFlags_WidthFixed, 180);
        ImGui::TableSetupColumn("services");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < res.size(); i++) {
            auto& r = res[i];
            if (!r.occupied && !r.t2) continue; // hide empty channels
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char lbl[32]; snprintf(lbl, sizeof lbl, "%.1f##%zu", r.freqMHz, i);
            if (ImGui::Selectable(lbl, false, ImGuiSelectableFlags_SpanAllColumns) && !pr.running) {
                a.freqMhz = r.freqMHz; a.bwIdx = r.bwMhz >= 7.5 ? 0 : 1;
                for (int k = 0; k < (int)(sizeof(kBw) / sizeof(kBw[0])); k++) if (kBw[k].mhz == r.bwMhz) a.bwIdx = k;
                a.tune.centerHz = a.freqMhz * 1e6; applyBandwidth(a);
                a.engine.setComputeMode(a.computeMode);
                a.engine.start(a.devices[hw], a.tune, a.file);
                a.devIdx = hw; a.smooth.clear(); a.peak.clear(); a.lastSeq = 0; savePrefs(a);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to tune the receiver to this channel");
            ImGui::TableNextColumn(); ImGui::Text("%.1f dBFS", r.levelDbfs);
            ImGui::TableNextColumn();
            if (r.t2) ImGui::TextColored(ImVec4(0.4f, 1, 0.5f, 1), "DVB-T2"); else ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "other");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.mode.c_str());
            ImGui::TableNextColumn(); if (r.t2) ImGui::Text("%.1f dB", r.snrDb);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.plpInfo.c_str());
            ImGui::TableNextColumn();
            std::string sv;
            for (auto& x : r.services) { if (!sv.empty()) sv += ",  "; sv += x; }
            if (sv.empty() && !r.t2) sv = r.note;
            if (!r.unsupported.empty()) sv = (sv.empty() ? "" : sv + "  ") + "! " + r.unsupported[0];
            ImGui::TextWrapped("%s", sv.c_str());
        }
        ImGui::EndTable();
    }
}

static void outputsTab(App& a) {
    bool ch = false;
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), "What to send");
    {
        std::string cur = "Whole multiplex";
        for (auto& sv : a.ts.services) if (sv.id == a.selService) cur = sv.name.empty() ? "service " + std::to_string(sv.id) : sv.name;
        ImGui::SetNextItemWidth(320);
        if (ImGui::BeginCombo("service", cur.c_str())) {
            if (ImGui::Selectable("Whole multiplex", a.selService < 0)) { a.selService = -1; ch = true; }
            for (auto& sv : a.ts.services) {
                std::string n = (sv.name.empty() ? "service " + std::to_string(sv.id) : sv.name) + "  [" + sv.typeName() + "]";
                if (ImGui::Selectable(n.c_str(), sv.id == a.selService)) { a.selService = sv.id; ch = true; }
            }
            ImGui::EndCombo();
        }
    }
    if (ImGui::Checkbox("remove null packets", &a.out.dropNull)) ch = true;
    ImGui::SameLine(); ImGui::TextDisabled("(a single service is always rewritten with its own PAT)");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), "File (.ts)");
    ImGui::SetNextItemWidth(520);
    ImGui::InputText("##fp", a.filePath, sizeof a.filePath);
    ImGui::SameLine();
    if (ImGui::Button("Choose...")) { auto p = saveFileDialog("recording.ts"); if (!p.empty()) snprintf(a.filePath, sizeof a.filePath, "%s", p.c_str()); }
    ImGui::SameLine();
    if (ImGui::Checkbox("record", &a.out.file)) ch = true;
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1, 1), "UDP");
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("address", a.udpHost, sizeof a.udpHost)) {}
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    ImGui::InputInt("port", &a.out.port, 0, 0);
    ImGui::SameLine(); ImGui::SetNextItemWidth(70);
    ImGui::InputInt("TTL", &a.out.ttl, 0, 0);
    ImGui::SameLine();
    ImGui::Checkbox("RTP", &a.out.rtp);
    ImGui::SameLine();
    if (ImGui::Checkbox("stream", &a.out.udp)) ch = true;
    ImGui::TextDisabled("Unicast or multicast (e.g. 239.1.1.1). Datagrams carry 7 packets and are paced evenly. Play with: ffplay udp://@:%d", a.out.port);
    if (ch || ImGui::IsItemDeactivatedAfterEdit()) applyOutputs(a);
    if (ImGui::Button("Apply address / port / TTL")) applyOutputs(a);
    ImGui::Spacing();
    OutputStats os = a.engine.outputStats();
    ImGui::PushFont(a.mono, 0);
    if (os.fileOpen) ImGui::Text("recording: %llu packets, %.1f MB", (unsigned long long)os.filePackets, os.fileBytes / 1e6);
    else ImGui::TextDisabled("recording: off");
    if (os.udpOpen) ImGui::Text("streaming: %llu datagrams, queue %.0f ms, dropped %llu", (unsigned long long)os.udpDatagrams, os.udpQueueMs, (unsigned long long)os.udpDropped);
    else ImGui::TextDisabled("streaming: off");
    ImGui::PopFont();
    if (!os.error.empty()) ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.3f, 1), "%s", os.error.c_str());
}

static std::string channelLabel(const SavedChannel& c) {
    char b[200];
    snprintf(b, sizeof b, "%.3f MHz  %s%s", c.freqMhz, c.name.empty() ? "DVB-T2 mux" : c.name.c_str(), c.nServices > 1 ? (" +" + std::to_string(c.nServices - 1)).c_str() : "");
    return b;
}

// Merge DVB-T2 muxes found by the scanner into the remembered channel list.
static void harvestScan(App& a) {
    ScanProgress pr = a.scanner.progress();
    const double now = glfwGetTime();
    const bool due = pr.running ? now - a.scanHarvestT > 0.5 : a.scanWas;
    if (!due) return;
    a.scanWas = pr.running;
    a.scanHarvestT = now;
    bool changed = false;
    for (auto& r : a.scanner.results()) {
        if (!r.t2) continue;
        SavedChannel sc;
        sc.freqMhz = r.freqMHz; sc.bwMhz = r.bwMhz; sc.mode = r.mode; sc.snrDb = r.snrDb; sc.nServices = (int)r.services.size();
        if (!r.services.empty()) { sc.name = r.services[0]; size_t br = sc.name.rfind(" ["); if (br != std::string::npos) sc.name.resize(br); }
        if (sc.name.empty()) sc.name = r.networkName;
        bool found = false;
        for (auto& c : a.channels)
            if (std::fabs(c.freqMhz - sc.freqMhz) < 0.01) {
                if (c.favourite) { sc.name = c.name; sc.favourite = true; }
                if (c.name != sc.name || c.nServices != sc.nServices || c.mode != sc.mode) { c = sc; changed = true; }
                found = true; break;
            }
        if (!found) { a.channels.push_back(sc); changed = true; }
    }
    if (changed) {
        std::sort(a.channels.begin(), a.channels.end(), [](const SavedChannel& x, const SavedChannel& y) { return x.freqMhz < y.freqMhz; });
        savePrefs(a);
    }
}

// Select a remembered channel: stop what is playing and tune the HackRF to it.
static void tuneToChannel(App& a, const SavedChannel& c) {
    int hw = -1;
    for (int i = 0; i < (int)a.devices.size(); i++) if (a.devices[i].kind == DeviceInfo::HackRF) hw = i;
    if (hw < 0) { a.engine.log("channel selector needs a HackRF"); return; }
    if (a.scanner.progress().running) a.scanner.stop();
    const double oldBw = a.tune.bandwidthMhz;
    a.freqMhz = c.freqMhz;
    for (int k = 0; k < (int)(sizeof(kBw) / sizeof(kBw[0])); k++) if (kBw[k].mhz == c.bwMhz) a.bwIdx = k;
    a.tune.centerHz = a.freqMhz * 1e6;
    applyBandwidth(a);
    a.engine.player().select(-1);
    a.selService = -1;
    if (a.engine.running() && a.devIdx == hw && oldBw == a.tune.bandwidthMhz) {
        a.engine.log("channel: " + channelLabel(c));
        a.engine.retuneReset(a.tune);
    } else {
        if (a.engine.running()) a.engine.stop();
        a.devIdx = hw;
        a.engine.setComputeMode(a.computeMode);
        a.engine.start(a.devices[hw], a.tune, a.file);
    }
    a.smooth.clear(); a.peak.clear(); a.lastSeq = 0;
    a.mpd.reset(); a.quality.reset();
    savePrefs(a);
}

static void qualityBar(App& a, float width) {
    const QualityReport& q = a.quality.report();
    const bool run = a.engine.running();
    const float pct = run && q.valid ? (float)q.percent : 0.f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), IM_COL32(120, 28, 28, 255), 3.f); // red = nothing yet
    const float t = pct / 100.f;
    const ImVec4 lo(0.95f, 0.55f, 0.15f, 1), hi(0.25f, 0.85f, 0.35f, 1);
    const ImVec4 col(lo.x + (hi.x - lo.x) * t, lo.y + (hi.y - lo.y) * t, lo.z + (hi.z - lo.z) * t, 1);
    if (pct > 0) dl->AddRectFilled(p, ImVec2(p.x + width * t, p.y + h), ImGui::ColorConvertFloat4ToU32(col), 3.f);
    dl->AddRect(p, ImVec2(p.x + width, p.y + h), IM_COL32(70, 76, 84, 255), 3.f);
    char txt[96];
    if (!run || !q.valid) snprintf(txt, sizeof txt, "signal quality: %s", run ? "no lock" : "-");
    else snprintf(txt, sizeof txt, "signal quality %.0f%%  %s", q.percent, q.label.c_str());
    ImVec2 ts = ImGui::CalcTextSize(txt);
    dl->AddText(ImVec2(p.x + (width - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), IM_COL32(235, 238, 242, 255), txt);
    ImGui::Dummy(ImVec2(width, h));
    if (ImGui::IsItemHovered() && run && q.valid) {
        ImGui::BeginTooltip();
        ImGui::Text("data SNR %.1f dB, needed for this modulation/code rate about %.1f dB", q.snrDb, q.requiredDb);
        ImGui::Text("margin %+.1f dB, FEC blocks decoded %.1f%% (last frames)", q.marginDb, q.fecOk * 100);
        ImGui::TextDisabled("0 dB margin is the edge of reception (25%%); +6 dB or more is comfortable.");
        ImGui::EndTooltip();
    }
}

static void playerTab(App& a) {
    Player& pl = a.engine.player();
    PlayerStats ps = pl.stats();
    {   // channel selector (muxes found by the scanner) and the signal-quality bar
        const SavedChannel* cur = nullptr;
        for (auto& c : a.channels) if (std::fabs(c.freqMhz - a.freqMhz) < 0.01) cur = &c;
        char fb[48]; snprintf(fb, sizeof fb, "%.3f MHz", a.freqMhz);
        const std::string curLab = cur ? channelLabel(*cur) : std::string(fb);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("channel");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(360);
        if (ImGui::BeginCombo("##chan", ((cur && cur->favourite) ? "* " + curLab : curLab).c_str())) {
            if (a.channels.empty()) ImGui::TextDisabled("no channels yet - run a scan (Scan tab)");
            for (size_t i = 0; i < a.channels.size(); i++) {
                const SavedChannel& c = a.channels[i];
                std::string l = (c.favourite ? "* " : "") + channelLabel(c);
                char extra[64]; snprintf(extra, sizeof extra, "   SNR %.0f dB", c.snrDb);
                if (ImGui::Selectable((l + extra).c_str(), cur == &c)) tuneToChannel(a, c);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.mode.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+##fav")) { ImGui::OpenPopup("savefav"); if (!a.favName[0] && cur) snprintf(a.favName, sizeof a.favName, "%s", cur->name.c_str()); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save the current frequency as a favourite channel");
        if (ImGui::BeginPopup("savefav")) {
            ImGui::TextDisabled("name for %.3f MHz", a.freqMhz);
            ImGui::SetNextItemWidth(220);
            ImGui::InputText("##favn", a.favName, sizeof a.favName);
            if (ImGui::Button("Save")) {
                SavedChannel sc; sc.freqMhz = a.freqMhz; sc.bwMhz = kBw[a.bwIdx].mhz; sc.name = a.favName; sc.favourite = true;
                if (cur) { sc.mode = cur->mode; sc.snrDb = cur->snrDb; sc.nServices = cur->nServices; }
                bool f = false;
                for (auto& c : a.channels) if (std::fabs(c.freqMhz - sc.freqMhz) < 0.01) { c = sc; f = true; }
                if (!f) a.channels.push_back(sc);
                std::sort(a.channels.begin(), a.channels.end(), [](const SavedChannel& x, const SavedChannel& y) { return x.freqMhz < y.freqMhz; });
                a.favName[0] = 0; savePrefs(a); ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (cur) {
            ImGui::SameLine();
            if (ImGui::SmallButton("x##del")) { const double f = cur->freqMhz; a.channels.erase(std::remove_if(a.channels.begin(), a.channels.end(), [&](const SavedChannel& c) { return std::fabs(c.freqMhz - f) < 0.01; }), a.channels.end()); savePrefs(a); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forget this channel");
        }
        ImGui::SameLine(0, 18);
        qualityBar(a, std::max(200.f, ImGui::GetContentRegionAvail().x - 8));
    }
    {   // PLP selector for multi-PLP multiplexes, and a notice for anything the receiver cannot decode
        const RxTelemetry& rx = a.rx;
        if (rx.plpList.size() > 1) {
            static const char* mods[] = {"QPSK", "16-QAM", "64-QAM", "256-QAM"};
            static const char* rates[] = {"1/2", "3/5", "2/3", "3/4", "4/5", "5/6", "1/3", "2/5"};
            auto lab = [&](const RxTelemetry::PlpInfo& p) {
                char b[120];
                snprintf(b, sizeof b, "PLP %d  type %d  %s %s%s%s", p.id, p.type, p.mod >= 0 && p.mod < 4 ? mods[p.mod] : "?", p.cod >= 0 && p.cod < 8 ? rates[p.cod] : "?", p.type == 0 ? "  (common)" : "", p.supported ? "" : "  (not supported)");
                return std::string(b);
            };
            std::string cur = a.plpSel < 0 ? "automatic" : "PLP " + std::to_string(a.plpSel);
            if (a.plpSel < 0 && rx.plpSelectedId >= 0) cur += " (PLP " + std::to_string(rx.plpSelectedId) + ")";
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("PLP");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(260);
            if (ImGui::BeginCombo("##plp", cur.c_str())) {
                if (ImGui::Selectable("automatic (first data PLP)", a.plpSel < 0)) { a.plpSel = -1; a.engine.selectPlp(-1); }
                for (auto& p : rx.plpList) if (ImGui::Selectable(lab(p).c_str(), a.plpSel == p.id)) { a.plpSel = p.id; a.engine.selectPlp(p.id); }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("This multiplex carries several physical layer pipes. The receiver decodes one at a time.");
        }
        for (auto& u : rx.unsupported) { ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1), "! %s", u.c_str()); }
    }
    const bool haveNN = pl.selected() >= 0;
    float h = ImGui::GetContentRegionAvail().y - 92 - (haveNN ? 46.f : 0.f);
    ImGui::BeginChild("vbox", ImVec2(-1, h), ImGuiChildFlags_Borders);
    if (pl.selected() < 0) ImGui::TextDisabled("Pick a service in the list on the right to start playback.");
    else if (!a.video.tex) ImGui::TextDisabled("%s", ps.hasAudio && !ps.hasVideo ? "audio only" : ps.status.c_str());
    else a.video.draw(ImGui::GetContentRegionAvail());
    ImGui::EndChild();
    if (ImGui::Button(pl.selected() >= 0 ? "Stop" : "Play")) { if (pl.selected() >= 0) pl.select(-1); else if (a.playReq < 0 && !a.ts.services.empty()) { int sid = a.ts.services[0].id; pl.select(sid); pl.setVolume(a.volume); } }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderFloat("volume", &a.volume, 0, 1, "%.2f")) pl.setVolume(a.volume);
    ImGui::SameLine();
    if (ImGui::Checkbox("mute", &a.muted)) pl.setMuted(a.muted);
    ImGui::SameLine();
    ImGui::Checkbox("pop out", &a.popOut);
    ImGui::SameLine();
    if (ImGui::Button("Fullscreen")) { a.videoOnly = true; if (!glfwGetWindowMonitor(gWindow)) toggleFullscreen(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Video fills the screen (press F or Esc to leave)");
    ImGui::SameLine();
    ImGui::Checkbox("deinterlace", &a.video.deint);
    ImGui::SameLine();
    auto tracks = pl.audioTracks();
    if (!tracks.empty()) {
        static int cur = 0;
        cur = std::min(cur, (int)tracks.size() - 1);
        std::string lab = tracks[cur].codec + " " + tracks[cur].lang;
        ImGui::SetNextItemWidth(170);
        if (ImGui::BeginCombo("audio", lab.c_str())) {
            for (int i = 0; i < (int)tracks.size(); i++) {
                std::string l = tracks[i].codec + " " + tracks[i].lang + " (" + std::to_string(tracks[i].channels) + " ch)";
                if (ImGui::Selectable(l.c_str(), i == cur)) { cur = i; pl.setAudioTrack(i); }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    if (pl.subtitlesAvailable()) { if (ImGui::Checkbox("subtitles", &a.subsOn)) pl.setSubtitles(a.subsOn); }
    ImGui::PushFont(a.mono, 0);
    ImGui::Text("%s  %s%s %dx%d %.1f fps   audio %s %d ch   buffer %.0f ms   A/V %+.0f ms",
                ps.status.c_str(), ps.videoCodec.c_str(), ps.hardware ? " (VideoToolbox)" : " (software)", ps.width, ps.height, ps.fps, ps.audioCodec.c_str(), ps.audioChannels, ps.audioBufferMs, ps.avOffsetMs);
    ImGui::Text("pictures: decoded %llu  shown %llu  late %llu  errors %llu  queue %d    audio underruns %d",
                (unsigned long long)ps.decoded, (unsigned long long)ps.shown, (unsigned long long)ps.late, (unsigned long long)ps.errors, ps.videoQueue, ps.underruns);
    ImGui::PopFont();
    if (pl.selected() >= 0) {   // what is on now, and what comes next
        const EpgEvent* nx = nullptr;
        const EpgEvent* cur = epgCurrent(a, pl.selected(), &nx);
        if (cur) {
            const int64_t now = utcNowOf(a);
            const float f = std::min(1.f, std::max(0.f, (float)(now - cur->start) / (float)cur->duration));
            ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.4f, 1), "NOW");
            ImGui::SameLine();
            ImGui::Text("%s - %s  %s", fmtLocal(cur->start, "%H:%M").c_str(), fmtLocal(cur->end(), "%H:%M").c_str(), cur->title.c_str());
            ImGui::SameLine();
            char left[32]; snprintf(left, sizeof left, "%d min left", (int)std::max<int64_t>(0, (cur->end() - now + 30) / 60));
            ImGui::ProgressBar(f, ImVec2(160, 12), left);
            if (nx) { ImGui::TextDisabled("NEXT"); ImGui::SameLine(); ImGui::Text("%s  %s", fmtLocal(nx->start, "%H:%M").c_str(), nx->title.c_str()); }
        } else if (nx) {
            ImGui::TextDisabled("NEXT"); ImGui::SameLine(); ImGui::Text("%s  %s", fmtLocal(nx->start, "%H:%M").c_str(), nx->title.c_str());
        } else ImGui::TextDisabled("no programme information is broadcast for this service");
    }
}

static void rightPanel(App& a) {
    ImGui::TextDisabled("Services");
    ImGui::BeginChild("svcs", ImVec2(0, 250), ImGuiChildFlags_Borders);
    if (a.ts.services.empty()) ImGui::TextDisabled("waiting for PAT / SDT...");
    for (auto& sv : a.ts.services) {
        std::string vc, ac;
        double kb = 0;
        for (auto& st : sv.streams) { kb += st.kbps; if (st.kind == "video" && vc.empty()) vc = st.codec; if (st.kind == "audio" && ac.empty()) ac = st.codec; }
        std::string label = (sv.name.empty() ? "service " + std::to_string(sv.id) : sv.name);
        ImGui::PushID(sv.id);
        bool sel = sv.id == a.engine.player().selected();
        if (ImGui::Selectable(label.c_str(), sel)) { if (sel) a.engine.player().select(-1); else { a.engine.player().select(sv.id); a.engine.player().setVolume(a.volume); } }
        char r[32];
        ImGui::TextDisabled("  %s  %s%s%s  %s%s", sv.typeName(), vc.c_str(), (!vc.empty() && !ac.empty()) ? " + " : "", ac.c_str(), fmtKbps(r, sizeof r, kb), sv.caFlag ? "  [scrambled]" : "");
        {
            const EpgEvent* cur = epgCurrent(a, sv.id);
            if (cur) ImGui::TextDisabled("  %s-%s  %s", fmtLocal(cur->start, "%H:%M").c_str(), fmtLocal(cur->end(), "%H:%M").c_str(), cur->title.c_str());
            else if (!sv.now.empty()) ImGui::TextDisabled("  now: %s", sv.now.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::TextDisabled("Video");
    ImGui::BeginChild("video", ImVec2(0, 190), ImGuiChildFlags_Borders);
    {
        PlayerStats ps = a.engine.player().stats();
        if (a.engine.player().selected() < 0) ImGui::TextDisabled("click a service to play it");
        else if (!ps.hasVideo && ps.hasAudio) ImGui::TextDisabled("audio only: %s", ps.audioCodec.c_str());
        else if (!a.video.tex) ImGui::TextDisabled("%s", ps.status.c_str());
        else a.video.draw(ImGui::GetContentRegionAvail());
    }
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::TextDisabled("Outputs");
    {
        OutputStats os = a.engine.outputStats();
        ImGui::Text("File %s   UDP %s   Live %s", os.fileOpen ? "●" : "○", os.udpOpen ? "●" : "○", "○");
        if (os.fileOpen) ImGui::TextDisabled("recording %.1f MB", os.fileBytes / 1e6);
        if (os.udpOpen) ImGui::TextDisabled("udp %s:%d%s", a.udpHost, a.out.port, a.out.rtp ? " (RTP)" : "");
    }
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

static void gainControl(App& a) {
    if (!a.engine.running() || a.devices[a.devIdx].kind == DeviceInfo::File) { a.sweep = GainSweep(); return; }
    GainSetting g{a.tune.lnaDb, a.tune.vgaDb, a.tune.ampOn};
    bool changed = false;
    const double now = ImGui::GetTime();
    if (a.sweep.active()) {
        GainSweep::Sample sm;
        sm.locked = a.rx.dataValid; sm.snrDb = a.rx.dataSnrDb;
        sm.clip = a.spec.stats.clipFraction; sm.rms = a.spec.stats.rmsDbfs; sm.peak = a.spec.stats.peak;
        changed = a.sweep.update(now, sm, g);
        if (!a.sweep.active()) a.engine.log("gain helper: " + a.sweep.summary());
    } else if (a.agcOn) {
        changed = a.agc.update(now, a.spec.stats, g);
    }
    if (changed) {
        a.tune.lnaDb = g.lna; a.tune.vgaDb = g.vga; a.tune.ampOn = g.amp;
        a.tune.centerHz = a.freqMhz * 1e6;
        a.engine.retune(a.tune);
        if (!a.sweep.active() || true) savePrefs(a);
        if (a.agcOn && !a.sweep.active()) a.engine.log("AGC: LNA " + std::to_string(g.lna) + " dB, VGA " + std::to_string(g.vga) + " dB, amp " + (g.amp ? "on" : "off"));
    }
}

static void drawUI(App& a, ImVec2 disp) {
    ingestSpectrum(a);
    ingestRx(a);
    harvestScan(a);
    if (a.engine.running() && glfwGetTime() - a.epgT > 1.0) {
        a.epg = a.engine.epg();
        if (a.fakeEpg) {
            const int64_t base = (int64_t)time(nullptr) / 1800 * 1800 - 1800;
            static const char* titles[] = {"Morning News", "The Garden Show", "Football: Derby", "Documentary: Oceans", "Quiz Night", "Late Film"};
            for (auto& sv : a.ts.services) for (int k = 0; k < 8; k++) {
                EpgEvent e; e.eventId = k + 1; e.start = base + k * 3600; e.duration = 3600; e.running = k == 1 ? 4 : 0; e.genre = 1 + k % 5;
                e.title = std::string(titles[(k + sv.id) % 6]); e.text = "Short synopsis of " + e.title; e.extended = "A longer description of the programme, with details about the presenters, guests and what to expect during the hour.";
                a.epg[sv.id].push_back(e);
            }
        }
        a.epgT = glfwGetTime();
    }
    if (!a.engine.running()) { a.epg.clear(); }
    gainControl(a);
    {
        int pid = -1;
        const int sid = a.engine.player().selected();
        if (sid >= 0) for (auto& sv : a.ts.services) if (sv.id == sid) for (auto& st : sv.streams) if (st.kind == "teletext") { pid = st.pid; break; }
        a.engine.teletext().setPid(pid);
    }
    a.video.update(a.engine.player(), gQueue);
    if (a.playReq >= 0) for (auto& sv : a.ts.services) if (sv.id == a.playReq && sv.havePmt) { a.engine.player().select(sv.id); a.engine.player().setVolume(a.volume); a.playReq = -1; break; }
    {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            if (a.videoOnly) { a.videoOnly = false; if (glfwGetWindowMonitor(gWindow)) toggleFullscreen(); }
            else if (a.video.tex) { a.videoOnly = true; if (!glfwGetWindowMonitor(gWindow)) toggleFullscreen(); }
        }
        if (a.videoOnly && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { a.videoOnly = false; if (glfwGetWindowMonitor(gWindow)) toggleFullscreen(); }
    }
    if (a.videoOnly) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(disp);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
        ImGui::Begin("##vonly", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        if (a.video.tex) a.video.draw(disp); else ImGui::TextDisabled("no picture - press Esc");
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(0)) { a.videoOnly = false; if (glfwGetWindowMonitor(gWindow)) toggleFullscreen(); }
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(disp);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

    toolbar(a);
    ImGui::Separator();
    statusBar(a);
    ImGui::Separator();

    float logH = 250, rightW = 330;
    float mainH = ImGui::GetContentRegionAvail().y - logH - 6;
    ImGui::BeginChild("main", ImVec2(disp.x - rightW - 16, mainH));
    if (ImGui::BeginTabBar("tabs")) {
        if (tabItem("Player")) { playerTab(a); ImGui::EndTabItem(); }
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
        if (tabItem("Channel")) { channelTab(a); ImGui::EndTabItem(); }
        if (tabItem("Impulse response")) { impulseTab(a); ImGui::EndTabItem(); }
        if (tabItem("SNR per carrier")) { snrTab(a); ImGui::EndTabItem(); }
        if (tabItem("FEC")) { fecTab(a); ImGui::EndTabItem(); }
        if (tabItem("TS")) { tsTab(a); ImGui::EndTabItem(); }
        if (tabItem("Outputs")) { outputsTab(a); ImGui::EndTabItem(); }
        if (tabItem("Scan")) { scanTab(a); ImGui::EndTabItem(); }
        if (tabItem("Guide")) { guideTab(a); ImGui::EndTabItem(); }
        if (tabItem("Teletext")) { teletextTab(a); ImGui::EndTabItem(); }
        if (tabItem("Log")) { logPanel(a); ImGui::EndTabItem(); }
        if (tabItem("Frame map")) { frameMapTab(a); ImGui::EndTabItem(); }
        if (tabItem("History")) { historyTab(a); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("right", ImVec2(0, mainH));
    rightPanel(a);
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::BeginChild("constellations", ImVec2(0, 0));
    constellationsTab(a);
    ImGui::EndChild();
    ImGui::End();
    if (a.popOut) {
        ImGui::SetNextWindowSize(ImVec2(640, 380), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(200, 160), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
        if (ImGui::Begin("Video", &a.popOut)) {
            if (a.video.tex) a.video.draw(ImGui::GetContentRegionAvail()); else ImGui::TextDisabled("no picture");
            if (ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(0)) { a.videoOnly = true; if (!glfwGetWindowMonitor(gWindow)) toggleFullscreen(); }
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }
}

// ------------------------------------------------------------------ main

int main(int argc, char** argv) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1500, 900, "DecT2", nullptr, nullptr);
    if (!window) return 1;
    gWindow = window;

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    gDevice = device;
    id<MTLCommandQueue> queue = [device newCommandQueue];
    gQueue = queue;

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

    bool autostart = false, hackrfStart = false, fileStart = false;
    const char* shotPath = nullptr;
    int shotFrame = 0;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--autostart") autostart = true;
        if (std::string(argv[i]) == "--shot" && i + 1 < argc) shotPath = argv[++i];
        if (std::string(argv[i]) == "--tab" && i + 1 < argc) gForceTab = argv[++i];
        if (std::string(argv[i]) == "--synthgain") app.tune.synth.gainModel = true;
        if (std::string(argv[i]) == "--nodeint") app.video.deint = false;
        if (std::string(argv[i]) == "--fakeepg") app.fakeEpg = true;
        if (std::string(argv[i]) == "--agc") app.agcOn = true;
        if (std::string(argv[i]) == "--fft" && i + 1 < argc) app.tune.synth.tx.s2field1 = atoi(argv[++i]);
        if (std::string(argv[i]) == "--gi" && i + 1 < argc) app.tune.synth.tx.giIdx = atoi(argv[++i]);
        if (std::string(argv[i]) == "--cfo" && i + 1 < argc) app.tune.synth.cfoHz = atof(argv[++i]);
        if (std::string(argv[i]) == "--snr" && i + 1 < argc) app.tune.synth.snrDb = atof(argv[++i]);
        if (std::string(argv[i]) == "--echo" && i + 1 < argc) app.tune.synth.echoDb = atof(argv[++i]);
        if (std::string(argv[i]) == "--echodelay" && i + 1 < argc) app.tune.synth.echoDelay = atoi(argv[++i]);
        if (std::string(argv[i]) == "--pp" && i + 1 < argc) app.tune.synth.tx.pp = atoi(argv[++i]);
        if (std::string(argv[i]) == "--ext") app.tune.synth.tx.ext = true;
        if (std::string(argv[i]) == "--hackrf") hackrfStart = true;
        if (std::string(argv[i]) == "--file" && i + 1 < argc) { app.file.path = argv[++i]; app.file.sampleRate = 10e6; app.file.format = FileFormat::CS8; fileStart = true; hackrfStart = true; autostart = true; }
        if (std::string(argv[i]) == "--play" && i + 1 < argc) app.playReq = atoi(argv[++i]);
        if (std::string(argv[i]) == "--lna" && i + 1 < argc) app.tune.lnaDb = atoi(argv[++i]);
        if (std::string(argv[i]) == "--vga" && i + 1 < argc) app.tune.vgaDb = atoi(argv[++i]);
        if (std::string(argv[i]) == "--amp") app.tune.ampOn = true;
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
                app.devIdx = 0;
                if (hackrfStart) for (int i = 0; i < (int)app.devices.size(); i++) if (app.devices[i].kind == (fileStart ? DeviceInfo::File : DeviceInfo::HackRF)) app.devIdx = i;
                app.tune.centerHz = app.freqMhz * 1e6; applyBandwidth(app);
                app.engine.setComputeMode(app.computeMode);
                app.engine.start(app.devices[app.devIdx], app.tune, app.file);
            }
            double now = glfwGetTime();
            last = now;
            drawUI(app, io.DisplaySize);
            ImGui::Render();
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cb, enc);
            [enc endEncoding];
            [cb presentDrawable:drawable];
            [cb commit];
            if (shotPath && ++shotFrame == (hackrfStart ? (app.playReq >= 0 || app.engine.player().selected() >= 0 ? 2400 : 900) : 400)) {
                [cb waitUntilCompleted];
                id<MTLTexture> t = drawable.texture;
                NSUInteger tw = t.width, th = t.height;
                std::vector<uint8_t> px(tw * th * 4);
                [t getBytes:px.data() bytesPerRow:tw * 4 fromRegion:MTLRegionMake2D(0, 0, tw, th) mipmapLevel:0];
                for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]); // BGRA -> RGBA
                unsigned char* planes[1] = {px.data()};
                NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes pixelsWide:tw pixelsHigh:th bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:tw * 4 bitsPerPixel:32];
                [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:[NSString stringWithUTF8String:shotPath] atomically:YES];
                { PlayerStats ps = app.engine.player().stats(); { BbStats bb = app.engine.bbStats(); fprintf(stderr, "stream: BB frames %llu lost %llu, PLP frames dropped by busy decoder %llu, FEC blocks ok %llu bad %llu, samples dropped %llu, CPU decode %.0f ms\n", (unsigned long long)bb.frames, (unsigned long long)bb.framesLost, (unsigned long long)app.rx.plpFramesDropped, (unsigned long long)app.rx.blocksOk, (unsigned long long)app.rx.blocksBad, (unsigned long long)app.engine.droppedSamples(), app.rx.plpDecodeMs); }
                fprintf(stderr, "player: %s hw=%d %s %dx%d decoded %llu shown %llu late %llu errors %llu audio %s ch %d buf %.0f ms underruns %d A/V %+.0f ms\n", ps.status.c_str(), ps.hardware, ps.videoCodec.c_str(), ps.width, ps.height, (unsigned long long)ps.decoded, (unsigned long long)ps.shown, (unsigned long long)ps.late, (unsigned long long)ps.errors, ps.audioCodec.c_str(), ps.audioChannels, ps.audioBufferMs, ps.underruns, ps.avOffsetMs); }
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
