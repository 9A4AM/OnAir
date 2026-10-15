// Tables of the keyboard modes (hfdig_kbd_tables.cpp, generated from fldigi's sources).
#pragma once
namespace dect2 {
namespace kbd {
extern const char* const kMfskVaricode[256];            // IZ8BLY MFSK varicode: the bit string of each character
extern const unsigned char kDominoVaricode[256][3];     // DominoEX primary alphabet: nibbles, a following nibble has bit 3 set (0 = none)
extern const unsigned short kHellFont[95][14];          // ' ' .. '~'
} // namespace kbd
} // namespace dect2
