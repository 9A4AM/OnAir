// HamDRM internals shared by the decoder (hfdig_hamdrm.cpp) and the transmitter (hfdig_hamdrm_gen.cpp): the OFDM geometry on top of the
// DRM mode timing, the cell layout of a frame, the codes, the 8 <-> 12 kHz resampler and the data groups.
#pragma once
#include "dect2/drm/drm_defs.h"
#include "dect2/drm/drm_fec.h"
#include "dect2/common/ring.h"
#include "hfdig_usb.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dect2 { namespace hamdrm {

constexpr double kRate = 12000.0;        // the OFDM runs at 12 kHz, where the DRM mode timing is in whole samples
constexpr double kCentreHz = 1600.0;     // the middle of the signal in the audio
constexpr int kModes = 3, kOccs = 2, kQams = 3;
constexpr int kFacCells = 38;            // 32 bits (24 + CRC-8), rate 1/2 with the tail, 4-QAM
constexpr int kGroupHeader = 9, kGroupCrc = 2;

inline int drmMode(int m) { return m == 0 ? drm::kModeA : m == 1 ? drm::kModeB : drm::kModeC; }

struct Geometry {
    int mode = 1, occ = 1;
    int tu = 0, tg = 0, ns = 0;
    int kmin = 0, kmax = 0;              // carriers of this occupancy
    int fmin = 0, fmax = 0;              // carriers of the 2.3 kHz set: the frame information is in them
    double spacing = 0;
    int len() const { return tu + tg; }
};

inline Geometry geometry(int mode, int occ) {
    Geometry g;
    g.mode = mode; g.occ = occ;
    const drm::ModeParams& mp = drm::modeParams(drmMode(mode));
    g.tu = mp.tu12; g.tg = mp.tg12; g.ns = mp.ns;
    g.spacing = kRate / g.tu;
    const int h = (int)std::floor((occ ? 2500.0 : 2300.0) * 0.5 / g.spacing);
    const int h23 = (int)std::floor(2300.0 * 0.5 / g.spacing);
    g.kmin = -h; g.kmax = h; g.fmin = -h23; g.fmax = h23;
    return g;
}

// the reference symbol (symbol 0 of each frame): 4-QAM of a fixed pseudo-random sequence on every carrier, amplitude 1
inline cf32 refCell(int k) {
    static const std::vector<cf32> t = [] {
        std::vector<uint8_t> b(512);
        drm::prbs(b.data(), b.size());
        std::vector<cf32> v(128);
        const float a = 0.70710678f;
        for (int i = 0; i < 128; i++) v[(size_t)i] = cf32(b[(size_t)(2 * i + 7)] ? -a : a, b[(size_t)(2 * i + 100)] ? -a : a);
        return v;
    }();
    return t[(size_t)((k + 64) & 127)];
}

// scattered pilots in the data symbols: every 6th carrier, moved by 3 every other symbol; amplitude sqrt 2
inline bool isPilot(int s, int k) { return s > 0 && ((k + 120 - 3 * (s & 1)) % 6) == 0; }
inline cf32 pilotValue(int s, int k) { return refCell(k + 17 * s) * 1.41421356f; }

struct Layout {
    Geometry g;
    std::vector<std::pair<int16_t, int16_t>> fac, msc;   // (symbol, carrier)
    int qam = 1, prot = 1;
    std::shared_ptr<drm::MlcCode> code;                  // the data cells
    int payload = 0;                                     // bytes per frame (one data group)
};

inline int qamLevels(int qam) { return qam + 1; }        // 1 4-QAM, 2 16-QAM, 3 64-QAM

inline drm::MlcParams mscParams(int qam, int prot, int cells) {
    drm::MlcParams p;
    p.levels = qamLevels(qam);
    for (int l = 0; l < p.levels; l++) {
        drm::Rate r;
        if (qam == 0) r = drm::mscRate(drm::kModeE, 2, prot ? 3 : 1, 0);            // 1/2, high 1/3
        else if (qam == 1) r = drm::mscRate(drm::kModeA, 4, prot ? 1 : 0, l);       // 1/2 + 3/4, high 1/3 + 2/3
        else r = drm::mscRate(drm::kModeA, 6, prot ? 1 : 0, l);                     // 1/3 + 2/3 + 4/5, high 1/4 + 1/2 + 3/4
        p.rxB[l] = r.rx; p.ryB[l] = r.ry;
    }
    p.n1 = 0; p.n2 = cells;
    return p;
}

inline std::shared_ptr<const Layout> layout(int mode, int occ, int qam, int prot) {
    auto L = std::make_shared<Layout>();
    L->g = geometry(mode, occ);
    L->qam = qam; L->prot = prot;
    const Geometry& g = L->g;
    for (int s = 1; s < g.ns && (int)L->fac.size() < kFacCells; s++)
        for (int k = g.fmin; k <= g.fmax && (int)L->fac.size() < kFacCells; k++) if (!isPilot(s, k)) L->fac.push_back({(int16_t)s, (int16_t)k});
    for (int s = 1; s < g.ns; s++)
        for (int k = g.kmin; k <= g.kmax; k++) {
            if (isPilot(s, k)) continue;
            if (std::find(L->fac.begin(), L->fac.end(), std::make_pair((int16_t)s, (int16_t)k)) != L->fac.end()) continue;
            L->msc.push_back({(int16_t)s, (int16_t)k});
        }
    L->code = std::make_shared<drm::MlcCode>(mscParams(qam, prot, (int)L->msc.size()));
    L->payload = L->code->infoBits() / 8;
    return L;
}

inline const drm::MlcCode& facCode() {
    static const drm::MlcCode c = [] { drm::MlcParams p; p.levels = 1; p.rxB[0] = 1; p.ryB[0] = 2; p.n2 = kFacCells; p.fac = true; return drm::MlcCode(p); }();
    return c;
}

// frame information: 24 bits + CRC-8
struct Fac { int mode = 0, occ = 0, qam = 0, prot = 0, count = 0; };
inline void facBits(const Fac& f, uint8_t* b /* 32 */) {
    uint32_t v = (uint32_t)(f.mode & 3) << 22 | (uint32_t)(f.occ & 1) << 21 | (uint32_t)(f.qam & 3) << 19 | (uint32_t)(f.prot & 1) << 18 | (uint32_t)(f.count & 15) << 14 | 0x2A5u;
    for (int i = 0; i < 24; i++) b[i] = (uint8_t)((v >> (23 - i)) & 1);
    const uint32_t c = drm::crc8(b, 24);
    for (int i = 0; i < 8; i++) b[24 + i] = (uint8_t)((c >> (7 - i)) & 1);
}
inline bool facParse(const uint8_t* b, Fac& f) {
    uint32_t c = 0;
    for (int i = 0; i < 8; i++) c = (c << 1) | b[24 + i];
    if (drm::crc8(b, 24) != c) return false;
    uint32_t v = 0;
    for (int i = 0; i < 24; i++) v = (v << 1) | b[i];
    if ((v & 0x3FFF) != 0x2A5u) return false;
    f.mode = (int)(v >> 22) & 3; f.occ = (int)(v >> 21) & 1; f.qam = (int)(v >> 19) & 3; f.prot = (int)(v >> 18) & 1; f.count = (int)(v >> 14) & 15;
    return f.mode < kModes && f.qam < kQams;
}

// the data cells of a frame are spread over it (cell interleaver of DRM, t = 5)
inline const std::vector<int>& cellPerm(int n) { return drm::interleavePerm(n, 5); }

// energy dispersal of the data group bits
inline void scramble(uint8_t* bits, size_t n) {
    static std::vector<uint8_t> p;
    if (p.size() < n) { p.resize(n); drm::prbs(p.data(), n); }
    for (size_t i = 0; i < n; i++) bits[i] ^= p[i];
}

// data group: type (0 idle, 3 header, 4 body), transport id, segment, last segment, length, the segment, zeros, CRC-16 over the rest
enum { kGroupIdle = 0, kGroupHeaderSeg = 3, kGroupBody = 4 };
struct Group { int type = 0; uint16_t tid = 0; int seg = 0, last = 0; std::vector<uint8_t> data; };

inline std::vector<uint8_t> packGroup(const Group& g, int payload) {
    std::vector<uint8_t> b((size_t)payload, 0);
    b[0] = (uint8_t)g.type;
    b[1] = (uint8_t)(g.tid >> 8); b[2] = (uint8_t)g.tid;
    b[3] = (uint8_t)(g.seg >> 8); b[4] = (uint8_t)g.seg;
    b[5] = (uint8_t)(g.last >> 8); b[6] = (uint8_t)g.last;
    const size_t n = std::min(g.data.size(), (size_t)std::max(0, payload - kGroupHeader - kGroupCrc));
    b[7] = (uint8_t)(n >> 8); b[8] = (uint8_t)n;
    std::copy(g.data.begin(), g.data.begin() + (long)n, b.begin() + kGroupHeader);
    const uint32_t c = drm::crc16Bytes(b.data(), (size_t)payload - 2);
    b[(size_t)payload - 2] = (uint8_t)(c >> 8); b[(size_t)payload - 1] = (uint8_t)c;
    return b;
}
inline bool parseGroup(const std::vector<uint8_t>& b, Group& g) {
    const size_t P = b.size();
    if (P < (size_t)(kGroupHeader + kGroupCrc)) return false;
    const uint32_t c = drm::crc16Bytes(b.data(), P - 2);
    if (c != ((uint32_t)b[P - 2] << 8 | b[P - 1])) return false;
    g.type = b[0];
    g.tid = (uint16_t)(b[1] << 8 | b[2]);
    g.seg = b[3] << 8 | b[4];
    g.last = b[5] << 8 | b[6];
    const size_t n = (size_t)(b[7] << 8 | b[8]);
    if (n > P - kGroupHeader - kGroupCrc || g.seg > g.last) return false;
    g.data.assign(b.begin() + kGroupHeader, b.begin() + kGroupHeader + (long)n);
    return true;
}

// the header segment: body size (4), content type (1, 2 = image), subtype (1, MOT: 1 JPEG, 3 PNG), then parameters id, length, bytes:
// 12 the file name (MOT ContentName), 0x40 the callsign
inline int mimeSubtype(const std::string& m) { return m == "image/png" ? 3 : m == "image/gif" ? 0 : 1; }
inline const char* subtypeMime(int t) { return t == 3 ? "image/png" : t == 0 ? "image/gif" : "image/jpeg"; }
inline std::vector<uint8_t> headerData(size_t bodySize, const std::string& mime, const std::string& name, const std::string& call) {
    std::vector<uint8_t> h = {(uint8_t)(bodySize >> 24), (uint8_t)(bodySize >> 16), (uint8_t)(bodySize >> 8), (uint8_t)bodySize, 2, (uint8_t)mimeSubtype(mime)};
    auto param = [&](int id, const std::string& s) { const size_t n = std::min<size_t>(s.size(), 64); h.push_back((uint8_t)id); h.push_back((uint8_t)n); h.insert(h.end(), s.begin(), s.begin() + (long)n); };
    param(12, name);
    param(0x40, call);
    return h;
}
struct Header { size_t bodySize = 0; std::string mime, name, callsign; };
inline bool parseHeader(const std::vector<uint8_t>& h, Header& o) {
    if (h.size() < 6) return false;
    o.bodySize = (size_t)h[0] << 24 | (size_t)h[1] << 16 | (size_t)h[2] << 8 | h[3];
    o.mime = subtypeMime(h[5]);
    for (size_t i = 6; i + 2 <= h.size();) {
        const int id = h[i], n = h[i + 1];
        if (i + 2 + (size_t)n > h.size()) return false;
        std::string s(h.begin() + (long)i + 2, h.begin() + (long)i + 2 + n);
        for (char& c : s) if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '_';
        if (id == 12) o.name = s; else if (id == 0x40) o.callsign = s;
        i += 2 + (size_t)n;
    }
    return true;
}

// a rational resampler of real audio (up by L, a Kaiser low-pass at `cutHz`, down by M)
class Resampler {
public:
    Resampler(int up, int down, double inRate, double cutHz) : L_(up), M_(down) {
        const int n = 48 * up;
        const auto lp = hfdig::lowpass(n, cutHz / (inRate * up), 7.0);
        taps_.resize((size_t)n);
        for (int i = 0; i < n; i++) taps_[(size_t)i] = (float)(lp[(size_t)i] * up);
        perPhase_ = n / up;
    }
    void reset() { hist_.assign((size_t)perPhase_, 0.f); pos_ = 0; cnt_ = 0; }
    void process(const float* x, size_t n, std::vector<float>& out) {
        if (hist_.empty()) reset();
        for (size_t i = 0; i < n; i++) {
            hist_.push_back(x[i]);
            // input i sits at i * L of the upsampled stream; the outputs are at multiples of M
            const int64_t inIdx = cnt_++, newest = inIdx * L_;
            while (pos_ <= newest) {
                const int64_t i0 = pos_ / L_;
                float s = 0;
                int64_t idx = (int64_t)hist_.size() - 1 - (inIdx - i0);
                for (int64_t j = pos_ - i0 * L_; j < (int64_t)taps_.size() && idx >= 0; j += L_, idx--) s += taps_[(size_t)j] * hist_[(size_t)idx];
                out.push_back(s);
                pos_ += M_;
            }
            if (hist_.size() > (size_t)(4 * perPhase_ + 64)) hist_.erase(hist_.begin(), hist_.end() - perPhase_ - 2);
        }
    }
private:
    int L_, M_, perPhase_ = 0;
    std::vector<float> taps_, hist_;
    int64_t cnt_ = 0, pos_ = 0;
};

}} // namespace dect2::hamdrm
