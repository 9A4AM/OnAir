// Private to the HD Radio Layer 2 decoder: the SIS, program and fixed-data state that hdr_l2.cpp and hdr_l2_aas.cpp both use. Not part of the API.
#pragma once
#include "dect2/hdr/hdr_l2.h"
#include <cmath>
#include <string>
#include <vector>

namespace dect2 { namespace hdr {

// the 5-bit character set of the short station name, and the size of a LOC field by codec mode (shared by the encoder and the decoder)
inline constexpr char kChars5[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ ?-*$ ";
inline int locBits(int codec, int stream) {
    switch (codec) {
    case 0: return 16;
    case 1: case 2: case 3: return stream == 0 ? 12 : 16;
    case 10: case 13: return 12;
    default: return 16;
    }
}

struct L2Decoder::Sis {
    std::string country;
    int facility = -1;
    std::string shortName;
    char longName[8 * 7 + 1] = {};
    bool longHave[8] = {};
    int longSeq = -1;
    bool longDone = false;
    double lat = NAN, lon = NAN;
    int alt = 0;
    char msg[256 + 8] = {};
    bool msgHave[32] = {};
    int msgSeq = -1, msgLen = 0, msgEnc = 0, msgCk = 0;
    bool msgDone = false;
    char uname[16] = {};
    bool unameHave[16] = {};
    int unameLen = -1, unameEnc = 0, unameAppend = 0;
    bool unameDone = false;
    char slogan[128] = {};
    bool sloganHave[16] = {};
    int sloganLen = -1, sloganEnc = 0;
    bool sloganDone = false;
    char alert[512] = {};
    bool alertHave[64] = {};
    int alertSeq = -1, alertLen = -1, alertEnc = 0, alertCrc = 0, alertCnt = 0;
    bool alertDone = false;
    int alertTimeout = 0;
    struct Asd { int access = -1, type = -1, sound = -1; } asd[64];
    struct Dsd { int access, type, mime; };
    std::vector<Dsd> dsd;
};

struct L2Decoder::Prog {
    HdrProgram p;
    std::vector<uint8_t> psd;
    bool psdOpen = false;
    double rate[3] = {0, 0, 0};       // audio bytes per second on P1, P3 and P4 (moving average)
    bool rateInit[3] = {false, false, false};
    uint64_t bytesNow[3] = {0, 0, 0}; // in the transfer frame being parsed
};

struct L2Decoder::Fixed {
    int syncWidth = 0, syncCount = 0;
    std::vector<uint8_t> ccc;
    bool cccOpen = false;
    bool ready = false;
    struct Sub { int length = 0; std::vector<uint8_t> blocks, acc; bool open = false; } sub[4];
};

}} // namespace dect2::hdr
