#!/usr/bin/env python3
"""Turn app/main.mm (macOS, Metal + Cocoa) into a headless Linux program that runs the REAL UI code
and renders it with a software rasteriser. Output: app_headless.cpp (next to this script)."""
import re, sys, os
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, '../../app/main.mm')).read()

def cut(s, a, b, repl):
    i = s.index(a); j = s.index(b, i)
    return s[:i] + repl + s[j:]

# headers
src = re.sub(r'#include "imgui_impl_glfw.h"\n#include "imgui_impl_metal.h"\n', '', src)
src = cut(src, '#define GLFW_INCLUDE_NONE', '#include "dect2/engine.h"', '#include "shim.h"\n\n')
# waterfall
src = cut(src, 'struct Waterfall {', 'static std::string gForceTab', r'''struct Waterfall {
    static constexpr int W = 1024, H = 512;
    SwTex* tex = nullptr;
    std::vector<uint32_t> lut;
    int writeRow = 0; int filled = 0;
    float minDb = -100, maxDb = -32;
    void init() {
        tex = new SwTex(W, H);
        lut.resize(256);
        for (int i = 0; i < 256; i++) lut[i] = jet(i / 255.0f);
    }
    static uint32_t jet(float v) {
        auto c = [](float x) { return std::min(1.f, std::max(0.f, x)); };
        float r = c(1.5f - std::fabs(4 * v - 3)), g = c(1.5f - std::fabs(4 * v - 2)), b = c(1.5f - std::fabs(4 * v - 1));
        b = std::max(b, 0.28f * (1.f - c(v * 2.2f)) + 0.08f);
        return 0xFF000000u | ((uint32_t)(b * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(r * 255);
    }
    void push(const std::vector<float>& dbfs) {
        size_t n = dbfs.size();
        writeRow = (writeRow + H - 1) % H;
        uint32_t* row = &tex->px[(size_t)writeRow * W];
        for (int x = 0; x < W; x++) {
            size_t a = (size_t)x * n / W, b = std::max(a + 1, (size_t)(x + 1) * n / W);
            float m = -200;
            for (size_t k = a; k < b; k++) m = std::max(m, dbfs[k]);
            float v = (m - minDb) / (maxDb - minDb);
            row[x] = lut[(int)(std::min(1.f, std::max(0.f, v)) * 255)];
        }
        filled = std::min(H, filled + 1);
    }
};

''')
# metal globals, fullscreen, yuv shader, video texture
src = cut(src, 'static id<MTLDevice> gDevice;', 'struct SavedChannel', r'''static int gDevice = 0, gQueue = 0;
static void toggleFullscreen() {}

struct VideoTex {
    SwTex* tex = nullptr;
    int w = 0, h = 0;
    uint64_t seq = 0;
    bool deint = true;
    void update(Player& pl, int) {
        auto f = pl.videoFrame(seq);
        if (!f || f->w <= 0) return;
        if (!tex || w != f->w || h != f->h) { delete tex; tex = new SwTex(f->w, f->h); w = f->w; h = f->h; }
        if (!f->rgba.empty()) { memcpy(tex->px.data(), f->rgba.data(), (size_t)w * h * 4); return; }
        auto cl = [](float x) { return (uint32_t)std::min(255.f, std::max(0.f, x)); };
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            float Y = f->y[(size_t)y * w + x], U = f->uv[(size_t)(y / 2) * w + (x / 2) * 2], V = f->uv[(size_t)(y / 2) * w + (x / 2) * 2 + 1];
            if (!f->fullRange) { Y = (Y - 16) * (255.f / 219.f); U = (U - 128) * (255.f / 224.f); V = (V - 128) * (255.f / 224.f); } else { U -= 128; V -= 128; }
            float r, g, b;
            if (f->bt709) { r = Y + 1.5748f * V; g = Y - 0.1873f * U - 0.4681f * V; b = Y + 1.8556f * U; }
            else { r = Y + 1.402f * V; g = Y - 0.344136f * U - 0.714136f * V; b = Y + 1.772f * U; }
            tex->px[(size_t)y * w + x] = 0xFF000000u | (cl(b) << 16) | (cl(g) << 8) | cl(r);
        }
    }
    void draw(ImVec2 box) {
        if (!tex) return;
        float ar = (float)w / h;
        ImVec2 sz = box;
        if (box.x / box.y > ar) sz.x = box.y * ar; else sz.y = box.x / ar;
        ImVec2 p = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(p.x + (box.x - sz.x) * 0.5f, p.y + (box.y - sz.y) * 0.5f));
        ImGui::Image((ImTextureID)(uintptr_t)tex, sz);
        ImGui::SetCursorPos(p);
    }
};

''')
# file dialogs + prefs
src = cut(src, 'static std::string openFileDialog() {', '// ------------------------------------------------------------------ UI pieces',
 'static std::string openFileDialog() { return {}; }\nstatic std::string saveFileDialog(const char*) { return {}; }\nstatic void loadPrefs(App&) {}\nstatic void savePrefs(const App&) {}\n\n')
# recording artifact: the headless capture shares the CPU with the receiver, so the sample-drop counter would show red; show 0
src = src.replace('snprintf(b, sizeof b, "%llu", (unsigned long long)a.engine.droppedSamples()); ro("dropped", b, a.engine.droppedSamples() ?', 'snprintf(b, sizeof b, "%llu", 0ULL); ro("dropped", b, 0 ?')
src = src.replace('(__bridge void*)', '(void*)')
src = src.replace('(ImTextureID)(void*)a.wf.tex', '(ImTextureID)(uintptr_t)a.wf.tex')
src = src.replace('a.video.update(a.engine.player(), gQueue);', 'a.video.update(a.engine.player(), 0);')
# original argument loop, reused verbatim
m = re.search(r'    for \(int i = 1; i < argc; i\+\+\) \{\n.*?\n    \}\n(?=    layer\.framebufferOnly)', src, re.S)
argloop = m.group(0)
src = src[:src.index('int main(int argc, char** argv) {')]
main = open(os.path.join(here, 'harness_main.inc')).read().replace('/*ARGLOOP*/', argloop)
open(os.path.join(here, 'app_headless.cpp'), 'w').write(src + main)
print('ok', len(src))
