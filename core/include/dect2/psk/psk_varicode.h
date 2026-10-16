// PSK31 / PSK63 protocol pieces: the varicode (G3PLX), the QPSK convolutional code (K = 5, polynomials 0x17 and 0x19) and its Viterbi
// decoder. Sources (read, re-implemented, credited): P. Martinez G3PLX, "PSK31: A New Radio-Teletype Mode", RadCom 1998; fldigi
// (W1HKJ et al., GPL-3) src/psk/pskvaricode.cxx (the table), src/psk/psk.cxx (the QPSK phase mapping), src/filters/viterbi.cxx.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dect2 {
namespace psk {

// The varicode of an ASCII character (0 .. 127): no two zeros in a row, starts and ends with a one; characters are separated by "00".
const char* varicode(int c);
// Text -> the bits sent (each character's code and two zeros); characters above 127 are skipped
std::vector<uint8_t> varicodeBits(const std::string& text);

// Bit by bit decoder: a character comes out after the two zeros that end it (-1 none yet, -2 a code not in the table)
class VaricodeRx {
public:
    int bit(int b);
    void reset() { sh_ = 0; n_ = 0; }
private:
    uint32_t sh_ = 0;
    int n_ = 0;
};

// QPSK: one bit in, a 2-bit symbol out: bit 0 = parity(register & 0x17), bit 1 = parity(register & 0x19); the newest bit is the register's bit 0
class ConvEncoder {
public:
    int encode(int bit);
    void reset() { sh_ = 0; }
private:
    uint32_t sh_ = 0;
};

// Soft decision Viterbi decoder for that code: each symbol's two soft values (> 0 means 1), a decided bit out after kDepth symbols
class Viterbi {
public:
    static constexpr int kDepth = 40;
    Viterbi() { reset(); }
    void reset();
    int decode(float s0, float s1);   // -1 until the traceback is filled
private:
    float metric_[16];
    std::vector<uint16_t> hist_;      // the survivors' decisions, kDepth x 16 states, as bit masks
    int pos_ = 0, filled_ = 0;
};

// QPSK phase change (0 .. 3 quarter turns, counter-clockwise) for a symbol, and back (fldigi's mapping, normal sideband)
int qpskTurns(int sym);
int qpskSymbol(int turns);

} // namespace psk
} // namespace dect2
