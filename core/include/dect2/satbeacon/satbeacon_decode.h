// What the frames of a beacon carry, as far as it can be told from the frames alone (satbeacon_decode.cpp).
// The meaning of the words of a spacecraft's telemetry is its maker's; nothing here guesses. The Analyzer keeps statistics of every word of every
// frame by the position of the frame in the major frame (the counter), and from them reports:
//   - words that never change, idle fill, the counter and its copies, a checksum that holds for the frames, a clock (a word that counts at a steady rate),
//   - words that depend only on the position in the major frame (a commutated word: the table of values per position is shown), and
//   - words that change from frame to frame (samples: range and mean, as raw counts),
//   - text (printable runs that stay the same).
// What is inferred is labelled "estimated" with the reason; things that are not in the frames are listed as not found.
#pragma once
#include "dect2/ccsds/ccsds_tm.h"
#include "dect2/satbeacon/satbeacon_tel.h"
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace dect2 {

struct SbKnownBeacon { double posE; const char* satellite; double mhz; const char* pol; bool confirmed; const char* source; };
const SbKnownBeacon* sbKnownBeacons(size_t& n);   // the table of published Ku-band beacons (satbeacon_ku.cpp)

namespace sb {

class Analyzer {
public:
    void reset();
    // one frame; counterByte < 0: no counter known (the frames are then treated as one position)
    void add(const std::vector<uint8_t>& frame, int counterByte, int counterMod, double frameSec);
    uint64_t frames() const { return n_; }
    // fields and summary lines; rfHz: the beacon's frequency (0 unknown), cn0, level for the summary
    void fill(SbTelemetry& t, double rfHz) const;
private:
    struct Pos { uint32_t n = 0; uint8_t first = 0, last = 0, mn = 255, mx = 0; };
    int len_ = 0, ctrByte_ = -1, mod_ = 1;
    double frameSec_ = 0;
    uint64_t n_ = 0, ckOk_ = 0, ckBad_ = 0;
    std::vector<uint8_t> prev_;
    int prevCtr_ = -1;
    struct Col {
        std::vector<Pos> pos;         // by position
        uint8_t mn = 255, mx = 0, first = 0;
        double sum = 0;
        uint64_t steps01 = 0, stepsOther = 0, stepSum = 0;   // changes from the previous frame
        bool copyOk = true; int copyD = -1;                  // value minus counter is constant
        bool seen[256] = {};
        int distinct = 0;
    };
    std::vector<Col> col_;
    // frames that start with the CCSDS attached sync marker: the codeblock layout (randomiser, Reed-Solomon) is found from the first blocks, the transfer frames are decoded and
    // analysed as frames of their own
    struct Cc {
        std::vector<std::vector<uint8_t>> blocks;
        bool haveFormat = false;
        ccsds::Format format;
        uint64_t blocksSeen = 0, decoded = 0, failed = 0, corrected = 0, crcOk = 0, crcBad = 0, lost = 0;
        struct Vc { uint64_t frames = 0; int lastCount = -1; };
        std::map<std::pair<int, int>, Vc> channels;
        ccsds::Header last;
        std::vector<uint8_t> lastFrame;
        std::shared_ptr<Analyzer> inner;
    } cc_;
    bool ccsds_ = false;
    void addCcsds(const std::vector<uint8_t>& frame, double frameSec);
    int sumKind_ = 0;                  // 0 none, 1 last byte = sum of the others
};

} // namespace sb
} // namespace dect2
