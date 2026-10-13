// DVB subtitles (ETSI EN 300 743): the segments of one subtitling PID (PES private stream 1, data_identifier 0x20) decoded into bitmaps -
// page composition, region composition, CLUT definition, object data (2/4/8-bit run-length pixel strings, map tables) and display
// definition - with the presentation time of every display set, so the player's clock decides when it is shown.
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <deque>
#include <map>
#include <vector>

namespace dect2 {

struct DvbSubRegionImage {
    int x = 0, y = 0, w = 0, h = 0;      // position in the display (see DvbSubDisplay::dispW / dispH)
    std::vector<uint8_t> rgba;           // w * h pixels, straight (not premultiplied) alpha
};

struct DvbSubDisplay {
    double pts = 0;                      // seconds (PES PTS / 90000); -1 = the PES carried none (show at once)
    double timeout = 0;                  // page_time_out: seconds after pts at which the page goes away
    int dispW = 720, dispH = 576;        // display definition (default 720 x 576)
    uint64_t seq = 0;
    std::vector<DvbSubRegionImage> regions;   // none = an empty page (clears the screen)
};

// Distance a - b in seconds on the 33-bit PTS circle (26.5 hours), so a clock that wrapped still compares correctly
double ptsDelta(double a, double b);

// Alpha-blend the regions of a display into one dispW x dispH RGBA picture (transparent where nothing is drawn)
void dvbSubComposite(const DvbSubDisplay& d, std::vector<uint8_t>& rgba);

class DvbSubDecoder {
public:
    // Follow the subtitle service with this composition page id (and its ancillary page id) on this PID; pid -1 switches decoding off.
    void setStream(int pid, int compositionPage, int ancillaryPage);
    int pid() const;
    int compositionPage() const;
    void reset();
    void feedTs(const uint8_t* pkt188);             // cheap PID check; reassembles PES packets
    void feedPes(const uint8_t* pes, size_t n);     // a whole PES packet (exposed for tests)
    // The segments of a PES data field (starting at the 0x0F sync byte of the first segment), presented at `pts` seconds (-1 = none)
    void feedSegments(const uint8_t* data, size_t n, double pts);
    // What should be on screen when the video being shown has presentation time clockPts; null = nothing
    std::shared_ptr<const DvbSubDisplay> displayAt(double clockPts) const;
    uint64_t displays() const;                      // display sets decoded so far

private:
    struct Obj { int id = 0; int x = 0, y = 0; int type = 0; int fg = 0, bg = 0; };
    struct Region {
        int id = 0, w = 0, h = 0, depth = 4, clut = 0, version = -1;
        int pix8 = 0, pix4 = 0, pix2 = 0;
        bool fill = false;
        std::vector<uint8_t> pix;                   // palette indices, w * h
        std::vector<Obj> objs;
    };
    struct Clut { uint32_t e2[4], e4[16], e8[256]; };   // RGBA as 0xAABBGGRR bytes: r | g << 8 | b << 16 | a << 24
    struct PageReg { int id, x, y; };
    struct ObjData { int id; std::vector<uint8_t> top, bottom; };

    void segment(int type, int page, const uint8_t* d, int n);
    void pageComposition(const uint8_t* d, int n);
    void regionComposition(const uint8_t* d, int n);
    void clutDefinition(const uint8_t* d, int n);
    void objectData(const uint8_t* d, int n);
    void displayDefinition(const uint8_t* d, int n);
    void drawObject(Region& r, const Obj& o, const ObjData& od);
    void endOfDisplaySet(double pts);
    Clut defaultClut() const;

    mutable std::mutex mu_;
    int pid_ = -1, comp_ = 0, anc_ = 0;
    std::vector<uint8_t> pes_;
    bool inPes_ = false;
    size_t pesLen_ = 0;
    int cc_ = -1;
    // decoder state (guarded by mu_)
    int timeout_ = 0, pageVersion_ = -1, pageState_ = 0;
    bool havePage_ = false;
    std::vector<PageReg> pageRegs_;
    std::map<int, Region> regions_;
    std::map<int, Clut> cluts_;
    std::map<int, ObjData> objects_;
    int dispW_ = 720, dispH_ = 576;
    std::deque<std::shared_ptr<DvbSubDisplay>> queue_;
    uint64_t seq_ = 0;
};

} // namespace dect2
