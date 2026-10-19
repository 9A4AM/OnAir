// DGPS beacon mode (work in progress): the engine tuning is registered under its number, the mode row and the family
// count agree with it, and the panel's strings are in the screen source.
#include "dect2/dgps/dgps_engine.h"
#include "dect2/engine/mode_tuning.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int fails = 0;
#define CHECK(c, fmt, ...) do { if (!(c)) { std::printf("FAIL line %d: " fmt "\n", __LINE__ __VA_OPT__(,) __VA_ARGS__); fails++; } } while (0)   // Flawfinder: ignore (the format is a literal)

static std::string readFile(const std::string& rel) {
    std::ifstream in(std::string(DECT2_SOURCE_DIR) + "/" + rel);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main() {
    // engine: the tuning is registered as standard 121 and the receiver's telemetry is 120
    const dect2::ModeTuning t = dect2::dgpsTuning();
    CHECK(t.stdMode == 121, "stdMode %d, want 121", t.stdMode);
    const dect2::ModeTuning* reg = dect2::modeTuning(121);
    CHECK(reg != nullptr, "modeTuning(121) not registered");
    if (reg) CHECK(std::string(reg->name) == "DGPS beacon (WIP)", "registered name '%s'", reg->name);

    // the mode row, the family count and the screen mapping in the app source
    const std::string table = readFile("app/ui/mode_table.cpp");
    CHECK(table.find("{119, \"DGPS beacon (WIP)\", 4,") != std::string::npos, "mode row for family 119 missing");
    const std::string appH = readFile("app/app.h");
    CHECK(appH.find("constexpr int kNumFamilies = 120;") != std::string::npos, "kNumFamilies is not 120");
    const std::string modeUi = readFile("app/panels/modeui.cpp");
    CHECK(modeUi.find("case 119: return &kDgpsUi;") != std::string::npos, "family 119 not mapped to kDgpsUi");

    // the panel strings
    const std::string ui = readFile("app/dgps/dgps_ui.cpp");
    const char* strings[] = {"DGPS beacon (WIP)", "Station ID: %d", "Carrier offset: %+.1f Hz", "Level: %.0f dBFS",
                             "Frames: %llu", "Message types seen: %s", "tune to about 300 kHz (283.5 - 325 kHz) and start the receiver"};
    for (const char* s : strings)
        CHECK(ui.find(s) != std::string::npos, "panel string missing: %s", s);

    std::printf("%s: %d failure(s)\n", fails ? "test_dgps_mode FAILED" : "test_dgps_mode OK", fails);
    return fails ? 1 : 0;
}
