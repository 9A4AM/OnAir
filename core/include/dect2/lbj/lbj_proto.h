// LBJ link-layer constants and decode entry points (protocol reference only; implementation is original).
#pragma once
#include "lbj_tel.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace dect2::lbj {

constexpr double kBaud = 1200.0;
constexpr double kAudioRate = 48000.0;   // target rate after channel filtering (before the bit slicer)
constexpr double kDefaultMhz = 821.2375;

constexpr uint32_t kAddrBasic = 1234000;
constexpr uint32_t kAddrBasicAlt = 1233999;
constexpr uint32_t kAddrDetail = 1234002;
constexpr uint32_t kAddrDetailAlt = 1234001;
constexpr uint32_t kAddrTime = 1234008;

bool isLbjPocsagAddress(uint32_t ric);

// POCSAG numeric body (pager::numericFromBits) for an LBJ RIC; may update session state and emit one display row.
struct LbjSessionState {
    struct Row {
        std::string baseTrain, prefix, direction, speedKmh, kmMarker, loco, locoCode, route;
        bool detailed = false;
        bool routeValid = false;
        double updatedSec = 0;
    };
    std::string lastTrain;
    std::map<std::string, Row> sessions;
};

bool parsePocsagPage(uint32_t ric, int function, const std::string& bcd, bool damaged, double timeSec, LbjSessionState& st, LbjMessage& out);

} // namespace dect2::lbj
