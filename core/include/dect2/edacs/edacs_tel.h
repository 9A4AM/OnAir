// EDACS control channel telemetry. RxTelemetry::edacs, engine standard 132, active standard 131 (family 130).
// Control data only (no voice, nothing encrypted). Receive only.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace dect2 {

struct EdacsFrame {
    int64_t wallTime = 0;
    double timeSec = 0;
    uint32_t msgA = 0, msgB = 0;      // the 28 data bits of the two messages of the frame, after majority voting over the three copies
    int errA = 0, errB = 0;           // bits (of 40) on which the three copies of each message disagreed
    bool good = false;
};

struct EdacsWordCount {
    uint32_t word = 0;                // 28 data bits
    uint64_t count = 0;
};

struct EdacsTelemetry {
    uint64_t seq = 0;
    int state = 0;                    // 0 no input, 1 listening, 2 a good frame in the last 5 s
    bool locked = false;
    double cfoHz = 0;              // not measured (0)
    float snrDb = 0, levelDb = -200, deviationHz = 0;
    bool dataValid = false;
    uint64_t blocksOk = 0, blocksBad = 0;   // frames whose copies agree / frames framed by the dotting whose copies disagree
    uint64_t syncs = 0;                     // dotting patterns found
    bool inverted = false;                  // the data polarity setting
    double timeSec = 0, inputRate = 0, centerHz = 0;
    std::vector<EdacsFrame> frames;         // newest first
    std::vector<EdacsWordCount> words;      // the most frequent data words, descending
};

constexpr size_t kEdacsLogMax = 400;

inline std::string edacsSummary(const EdacsTelemetry& t) {
    char b[160];
    if (t.state == 0) return "EDACS: waiting for samples";
    if (!t.dataValid) { snprintf(b, sizeof b, "EDACS: listening (%.0f dBFS)", t.levelDb); return b; }
    snprintf(b, sizeof b, "EDACS: %s, %llu good frames", t.locked ? "locked" : "no lock", (unsigned long long)t.blocksOk);
    return b;
}

} // namespace dect2
