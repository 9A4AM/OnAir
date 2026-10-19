// EDACS control channel (outbound) frame, clean-room from the public description in US 7,546,135 B2 (extended addressing for a digital trunked radio system):
//  - 9600 baud GFSK; a 30 ms "bucket" of 288 bits = 48 sync bits (16 dotting, a 16-bit field with an 11-bit Barker word and five dotting bits, 16 dotting)
//    + two 28-bit messages, each with 12 BCH bits (40 bits), each sent three times with the middle copy inverted (majority voting per bit).
// NOT established from non-code public sources, so left out: the BCH polynomial (the 12 check bits are never checked or generated), the Barker word,
// the V.52 mask seed of the check bits, the polarity of the data (a setting), the bit positions of the message type / GID / LID / LCN fields and the
// message type table (the data words are shown as raw 28-bit values). The test signal writes placeholder bits where the Barker word and the check bits go.
#pragma once
#include "dect2/common/ring.h"
#include <cstdint>
#include <vector>

namespace dect2::edacs {

constexpr double kBaud = 9600.0;
constexpr int kSyncBits = 48, kMsgBits = 40, kFrameBits = 288;

// One frame, as bits (1 = higher frequency): 48 sync bits (dotting; the Barker field is a placeholder), then a (28 data + 12 placeholder check) bits
// message A three times (middle inverted) and the same for message B. checkA / checkB: the 12 check bits to write (the test signal has no real BCH).
std::vector<uint8_t> encodeFrame(uint32_t msgA, uint32_t msgB, uint32_t checkA = 0, uint32_t checkB = 0);

// Majority vote of a received message: 120 bits (copy, inverted copy, copy). Returns the 40 voted bits (packed MSB first in a uint64) and the number of
// bit positions where the copies disagreed.
uint64_t vote(const uint8_t* bits120, int& disagreements);

// FM test signal at fs (bit 1 = +devHz), carrier offsetHz from 0 Hz. invert flips the polarity.
std::vector<cf32> modulate(const std::vector<uint8_t>& bits, double fs, double offsetHz, double devHz, float amp, bool invert = false);

} // namespace dect2::edacs
