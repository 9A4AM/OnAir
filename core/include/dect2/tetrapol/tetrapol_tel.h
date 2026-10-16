// TETRAPOL receiver telemetry (PAS 0001-2 / 0001-3-2), shared by the receiver and the UI.
// Only the control-channel data frames a base station sends to every terminal: scramble, cell identity when D_SYSTEM_INFO
// is recognised, frame counts. No voice, no short data content, no subscriber identities.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace dect2 {

struct TetrapolCell {
    double freqHz = 0;
    double cfoHz = 0;
    int scramble = -1;
    int country = -1;          // D_SYSTEM_INFO country_code when parsed
    int systemId = -1;
    int bnId = -1;
    int cellId = -1;
    int locArea = -1;
    int channelId = -1;
    int band = 0;              // 1 VHF, 2 UHF (PAS)
    bool hasSysinfo = false;
    float levelDb = -200, snrDb = -99;
    int64_t firstWall = 0, lastWall = 0;
    double lastSec = 0;
    uint64_t framesOk = 0, framesBad = 0;
};

struct TetrapolEvent { int64_t wallTime = 0; double sec = 0; std::string text; };
struct TetrapolCarrierSeen { double freqHz = 0; float levelDb = 0; bool synced = false; };

struct TetrapolTelemetry {
    uint64_t seq = 0;
    int state = 0;             // 0 no input, 1 searching, 2 a cell in the last 30 s
    double cfoHz = 0;
    float snrDb = 0;
    float levelDb = -200;
    bool dataValid = false;
    uint64_t blocksOk = 0, blocksBad = 0;
    double timeSec = 0;
    double inputRate = 0;
    double centerHz = 0;
    uint64_t blocks = 0;
    int spacingHz = 12500;     // 12500 or 10000
    std::vector<TetrapolCarrierSeen> carriers;
    std::vector<TetrapolCell> cells;
    std::vector<TetrapolEvent> events;
};

constexpr size_t kTetrapolLogMax = 300;
constexpr size_t kTetrapolMax = 200;

inline std::string tetrapolSummary(const TetrapolTelemetry& t) {
    if (t.state == 0) return "waiting for samples";
    size_t named = 0;
    for (const auto& c : t.cells) if (c.hasSysinfo) named++;
    char b[160];
    snprintf(b, sizeof b, "%zu TETRAPOL carrier%s, %zu with system info", t.cells.size(), t.cells.size() == 1 ? "" : "s", named);
    return b;
}

} // namespace dect2
