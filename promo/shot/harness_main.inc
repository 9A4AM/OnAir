// ------------------------------------------------------------------ headless main (appended by make_headless.py)
#include <unistd.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <thread>
static GLFWwindow* gWindow = nullptr;
int main(int argc, char** argv) {
    int W = 1500, H = 900; float scale = 2; int frames = 300; double fps = 30, warm = 1.0;
    std::string outPath = "out.mkv";
    std::vector<std::pair<int, std::string>> tabAt; double fileRate = 0; struct Click { int f; float x, y; }; std::vector<Click> clicks;
    std::vector<std::string> rest; rest.push_back(argv[0]);
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        if (s == "--size" && i + 1 < argc) sscanf(argv[++i], "%dx%d", &W, &H);
        else if (s == "--scale" && i + 1 < argc) scale = atof(argv[++i]);
        else if (s == "--frames" && i + 1 < argc) frames = atoi(argv[++i]);
        else if (s == "--fps" && i + 1 < argc) fps = atof(argv[++i]);
        else if (s == "--warm" && i + 1 < argc) warm = atof(argv[++i]);
        else if (s == "--filerate" && i + 1 < argc) fileRate = atof(argv[++i]);
        else if (s == "--click" && i + 1 < argc) { Click c; sscanf(argv[++i], "%d:%f,%f", &c.f, &c.x, &c.y); clicks.push_back(c); }
        else if (s == "--out" && i + 1 < argc) outPath = argv[++i];
        else if (s == "--at" && i + 1 < argc) { std::string v = argv[++i]; auto c = v.find(':'); tabAt.push_back({atoi(v.substr(0, c).c_str()), v.substr(c + 1)}); }
        else rest.push_back(s);
    }
    std::vector<char*> av; for (auto& s : rest) av.push_back((char*)s.c_str());
    argc = (int)av.size(); argv = av.data();

    setpriority(PRIO_PROCESS, 0, -10);   // the receiver threads inherit this; the rasteriser thread below runs niced so it cannot starve them
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    applyTheme();
    App app;
    ImFont* ui = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 14.5f);
    app.mono = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 12.5f);
    if (!ui) ui = io.Fonts->AddFontDefault();
    app.ui = ui; if (!app.mono) app.mono = ui;
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DisplayFramebufferScale = ImVec2(scale, scale);
    app.wf.init();
    app.engine.log("OnAir started");
    refreshDevices(app);
    bool autostart = true, hackrfStart = false, fileStart = false;
    const char* shotPath = nullptr; (void)shotPath;
/*ARGLOOP*/
    if (fileRate > 0) app.file.sampleRate = fileRate;
    SwFb fb; fb.init((int)(W * scale), (int)(H * scale));
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "nice -n 10 ffmpeg -v error -y -f rawvideo -pix_fmt rgba -s %dx%d -r %g -i - -threads 1 -c:v libx264 -preset ultrafast -crf 12 -pix_fmt yuv420p '%s'", fb.w, fb.h, fps, outPath.c_str());
    FILE* pipe = popen(cmd, "w");
    double tStart = glfwGetTime(), last = tStart, t0 = -1; int written = 0, tabIdx = 0;
    std::sort(tabAt.begin(), tabAt.end());
    while (written < frames) {
        double now = glfwGetTime();
        io.DeltaTime = (float)std::min(0.1, std::max(1.0 / 500, now - last)); last = now;
        ImGui::NewFrame();
        if (autostart) {
            autostart = false;
            app.devIdx = 0;
            if (hackrfStart) for (int i = 0; i < (int)app.devices.size(); i++) if (app.devices[i].kind == (fileStart ? DeviceInfo::File : DeviceInfo::HackRF)) app.devIdx = i;
            app.tune.centerHz = app.freqMhz * 1e6; applyBandwidth(app);
            app.engine.setComputeMode(app.computeMode); app.engine.setStandard(app.atscMode ? 3 : app.stdMode);
            app.engine.start(app.devices[app.devIdx], app.tune, app.file);
        }
        if (t0 >= 0) while (tabIdx < (int)tabAt.size() && tabAt[tabIdx].first <= written) routeTab(app, tabAt[tabIdx++].second);
        for (auto& c : clicks) {
            if (t0 < 0) break;
            if (written == c.f - 2) io.AddMousePosEvent(c.x, c.y);
            if (written == c.f) io.AddMouseButtonEvent(0, true);
            if (written == c.f + 3) io.AddMouseButtonEvent(0, false);
        }
        double td0 = glfwGetTime(); drawUI(app, io.DisplaySize);
        ImGui::Render(); static double accD = 0; accD += glfwGetTime() - td0;
        for (auto& p : fb.px) p = 0xFF0B0C0D;
        double tr0 = glfwGetTime(); { ImDrawData* dd = ImGui::GetDrawData(); std::thread th([&] { setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 15); swRender(dd, fb, scale); }); th.join(); } static double acc = 0; static int cnt = 0; acc += glfwGetTime() - tr0; if (++cnt % 60 == 0) { fprintf(stderr, "render %.0f ms, ui %.0f ms\n", acc / 60 * 1000, accD / 60 * 1000); acc = 0; accD = 0; }
        now = glfwGetTime();
        if (t0 < 0) { if (now - tStart >= warm) t0 = now; else { usleep(5000); continue; } }
        while (written < frames && now >= t0 + written / fps) { fwrite(fb.px.data(), 4, fb.px.size(), pipe); written++; }
        double next = t0 + written / fps - glfwGetTime(); if (next > 0.002) usleep((useconds_t)(next * 0.9e6));
    }
    pclose(pipe);
    app.engine.stop();
    fprintf(stderr, "wrote %d frames (%.1fs wall)\n", written, glfwGetTime() - tStart);
    return 0;
}
