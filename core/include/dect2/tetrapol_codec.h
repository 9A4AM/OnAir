// TETRAPOL PAS 0001-2 data frames (UHF), written from the published air-interface coding.
// 160 bits / 20 ms, GMSK 8 kbit/s. Receive-only: no voice, no identities, no decryption.
#pragma once
#include <cstdint>

namespace dect2 {
namespace tetrapol {

constexpr int kFrameBits = 160;
constexpr int kDataBits = 152;
constexpr int kPayloadBits = 66;   // the 66 information bits of a data frame (PAS 0001-2 6.2)
constexpr int kScrMax = 127;
constexpr uint8_t kFrameHdr = 0x46;   // PAS 0001-2 6.2.5.2

struct DataFrame {
    uint8_t payload[kPayloadBits] = {};   // 66 bits, 0 or 1
    uint8_t asb[2] = {};
    int scramble = 0;                     // 0 .. 127 (0: no inner scramble)
    int metric = 0;                       // Viterbi agreement (0 .. 152); used to pick among CRC-5 collisions
    bool ok = false;
};

// Inner coding of a data frame (no GMSK, no outer differential). bits160[0..7] = header 0x46.
void encodeDataFrame(const DataFrame& in, uint8_t bits160[kFrameBits]);
bool decodeDataFrame(const uint8_t bits152[kDataBits], int scr, DataFrame& out);   // 152 payload bits after the header
bool decodeFrame(const uint8_t bits160[kFrameBits], int scr, DataFrame& out);

// Undo / apply the modulator's bit differential (PAS 0001-2 7.2). prev is the bit before this frame (0 at start).
uint8_t undiffBits(uint8_t* bits, int n, uint8_t prev);
uint8_t diffBits(uint8_t* bits, int n, uint8_t prev);

// Hunt a data frame in hard bits (already undiff'd). Tries scramble 0 .. 127 unless scrambleHint >= 0.
bool huntFrame(const uint8_t* bits, int n, int scrambleHint, int& off, DataFrame& out);

constexpr uint8_t kCodopSysinfo = 0x90;   // D_SYSTEM_INFO (PAS 0001-3-2)
void packSysinfo(uint8_t payload[kPayloadBits], int country, int systemId, int bn, int cell, int la, int channel, int band);
bool parseSysinfo(const uint8_t payload[kPayloadBits], int& country, int& systemId, int& bn, int& cell, int& la, int& channel, int& band);

} // namespace tetrapol
} // namespace dect2
