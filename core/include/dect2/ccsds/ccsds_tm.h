// CCSDS telemetry codeblocks (CCSDS 131.0-B): the Reed-Solomon code (255,223) / (255,239) over GF(256) with F(x) = x^8 + x^7 + x^2 + x + 1, in the dual or the conventional
// basis, byte-interleaved to depth 1 to 8, the pseudo-randomiser, the codeblock layout found from a few blocks, and the TM (version 0) and AOS (version 1) transfer frame headers.
//
// Adapted from DecSAT by Arclamp (CasualArclamp), https://github.com/CasualArclamp/DecSAT, GPL-3.0-or-later: decsat-modem/src/ccsds.rs, rewritten in C++. The original file
// begins "made by CasualArclamp".
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dect2 {
namespace ccsds {

constexpr uint32_t kAsm = 0x1ACFFC1D;                 // the attached sync marker
std::vector<uint8_t> randomiser(size_t n);            // 131.0-B clause 10.4: FF 48 0E C0 9A 0D 70 BC ...
uint16_t crcCcitt(const uint8_t* p, size_t n);        // the frame error control field (initial value FFFF)

class Rs {
public:
    Rs(int e, int n);                                 // e = 8 or 16 (t = e), n <= 255 (shortened below 255)
    int k() const { return n_ - 2 * e_; }
    void encode(uint8_t* cw, bool dual) const;        // the first k bytes are data, the parity is written after them
    int decode(uint8_t* cw, bool dual) const;         // errors corrected, or -1 when uncorrectable
private:
    int e_, n_;
    std::vector<uint8_t> g_;
};

struct RsLayout { int e = 16, interleave = 1, n = 255; bool dual = true; };
struct Format {
    size_t blockBytes = 0;
    bool rs = false;
    RsLayout layout;
    bool randomised = false;
    size_t frameBytes() const { return rs ? (size_t)layout.interleave * (size_t)(layout.n - 2 * layout.e) : blockBytes; }
    std::string describe() const;
    // the frame of a block; corrected: symbols fixed; returns false when the Reed-Solomon decoding failed (the frame holds what it could)
    bool decode(const uint8_t* block, size_t n, std::vector<uint8_t>& frame, int& corrected) const;
};
std::vector<uint8_t> encodeBlock(const Format& f, const std::vector<uint8_t>& frame);
// the layout that makes all of these equal-length blocks decode (a good frame CRC without Reed-Solomon, or every Reed-Solomon word correct); false: none
bool findFormat(const std::vector<std::vector<uint8_t>>& blocks, Format& out);

struct Header {
    int version = -1;            // 0 TM, 1 AOS
    int scid = 0, vcid = 0;
    bool ocf = false;
    int mcCount = -1;            // TM only
    uint32_t vcCount = 0;
    int firstHeader = -1;        // TM only: the first header pointer (0x7FE: idle data)
    bool idle() const { return version == 0 ? firstHeader == 0x7FE : vcid == 63; }
    const char* kind() const { return version == 0 ? "TM" : "AOS"; }
};
bool parseHeader(const uint8_t* f, size_t n, Header& h);
std::vector<uint8_t> buildTmFrame(size_t len, int scid, int vcid, int mc, int vc, const std::vector<uint8_t>& data);

} // namespace ccsds
} // namespace dect2
