// LBJ link-layer constants and decode entry points (protocol reference only; implementation is original).
#pragma once
#include "lbj_tel.h"
#include <cstddef>
#include <cstdint>

namespace dect2::lbj {

constexpr double kBaud = 1200.0;
constexpr double kAudioRate = 48000.0;   // target rate after channel filtering (before the bit slicer)
constexpr double kDefaultMhz = 821.2375;

// Hunt sync and BCH(31,21) on NRZ bits; fills out when a complete frame is recognised. Stub: always false until DSP is wired.
bool tryDecodeFrame(const uint8_t* bits, size_t nBits, LbjMessage& out, double timeSec, double centreHz);

} // namespace dect2::lbj
