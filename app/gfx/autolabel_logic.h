// Auto-labelled spectrum, the pure part: the labels (name, frequency, source), a store sorted by frequency, and the layout that places the names
// of the frequencies in view on the plot without overlap: highest priority first, stacked in rows, thinned out when the view is wide, and spelled
// out when there is room. No ImGui and no App in here, so tests/app/test_autolabel.cpp drives it directly. The App side is autolabel.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace autolabel {

enum Src { Band = 0, FreqDb, Fm, Dab, Dvb, Memories, NumSrc };   // where a name comes from: the band plan, the frequency database, RDS names, DAB ensembles, DVB networks and services, the user's memories
constexpr unsigned kAllSources = (1u << NumSrc) - 1;

// decoded names beat the memories, the memories beat the database, the database beats the plain channel raster
inline float srcPriority(int s) {
    static const float p[NumSrc] = {1.f, 2.f, 4.f, 4.f, 4.f, 3.f};
    return s >= 0 && s < NumSrc ? p[s] : 0.f;
}

struct Label {
    double mhz = 0;        // where the name sits on the frequency axis
    std::string text;      // the name
    std::string detail;    // shown when there is room (and in the tooltip): services of a mux, the radio text of a station ...
    std::string mode;      // the mode a click opens ("" = only tune); a Memory / Scanner mode key
    int src = Band;
    float prio = 0;        // within a source: higher wins; the source's priority is added
};

// the labels, sorted by frequency
class Store {
public:
    void clear() { v_.clear(); }
    void add(Label l) { l.prio += srcPriority(l.src); v_.push_back(std::move(l)); }
    void finish() { std::stable_sort(v_.begin(), v_.end(), [](const Label& a, const Label& b) { return a.mhz < b.mhz; }); }
    size_t size() const { return v_.size(); }
    const Label& at(size_t i) const { return v_[i]; }
    // indexes of the labels with loMhz <= mhz <= hiMhz whose source is on in mask
    std::vector<size_t> inRange(double loMhz, double hiMhz, unsigned mask) const {
        std::vector<size_t> o;
        auto it = std::lower_bound(v_.begin(), v_.end(), loMhz, [](const Label& l, double x) { return l.mhz < x; });
        for (; it != v_.end() && it->mhz <= hiMhz; ++it) if (mask & (1u << it->src)) o.push_back((size_t)(it - v_.begin()));
        return o;
    }
private:
    std::vector<Label> v_;
};

struct Placed { size_t idx = 0; float x0 = 0, x1 = 0, cx = 0; int row = 0; };

struct View {
    double loMhz = 0, hiMhz = 0;   // the frequency range of the plot
    float px0 = 0, px1 = 0;        // its left and right edge in pixels
    int maxRows = 3;
    float gap = 4.f;               // the smallest space between two names in a row
    float padX = 4.f;              // space inside a name's box, each side
    unsigned mask = kAllSources;
};

struct Result {
    std::vector<Placed> placed;    // sorted by x0
    size_t candidates = 0, dropped = 0;
    bool detailed = false;         // every name is shown with its detail (there was room for all)
};

// width of the box of a label (text and padding) in pixels; detailed: with its detail
using WidthFn = std::function<float(const Label&, bool detailed)>;

// two labels of different sources for the same station (within tolMhz) are one: the higher priority stays. The plain band-plan channels never merge.
inline std::vector<size_t> mergeSame(const Store& s, std::vector<size_t> idx, double tolMhz) {
    std::vector<char> gone(idx.size(), 0);
    for (size_t i = 0; i < idx.size(); i++) {
        if (gone[i] || s.at(idx[i]).src == Band) continue;
        for (size_t j = i + 1; j < idx.size() && s.at(idx[j]).mhz - s.at(idx[i]).mhz <= tolMhz; j++) {
            if (gone[j] || s.at(idx[j]).src == Band) continue;
            if (s.at(idx[j]).prio > s.at(idx[i]).prio) { gone[i] = 1; break; }
            gone[j] = 1;
        }
    }
    std::vector<size_t> o;
    for (size_t i = 0; i < idx.size(); i++) if (!gone[i]) o.push_back(idx[i]);
    return o;
}

namespace detail {
inline Result place(const Store& s, const std::vector<size_t>& order, const View& v, const WidthFn& width, bool detailed) {
    Result r;
    r.candidates = order.size();
    r.detailed = detailed;
    const double perPx = (v.hiMhz - v.loMhz) / std::max(1.0, (double)(v.px1 - v.px0));
    std::vector<std::vector<std::pair<float, float>>> rows((size_t)std::max(1, v.maxRows));
    for (size_t k : order) {
        const Label& l = s.at(k);
        const float w = width(l, detailed);
        const float cx = (float)(v.px0 + (l.mhz - v.loMhz) / perPx);
        float x0 = cx - w * 0.5f;
        x0 = std::max(v.px0, std::min(x0, v.px1 - w));   // a name at the edge of the plot stays inside it
        const float x1 = x0 + w;
        int row = -1;
        for (size_t ri = 0; ri < rows.size() && row < 0; ri++) {
            bool free = true;
            for (const auto& q : rows[ri]) if (x0 < q.second + v.gap && x1 + v.gap > q.first) { free = false; break; }
            if (free) row = (int)ri;
        }
        if (row < 0) { r.dropped++; continue; }
        rows[(size_t)row].push_back({x0, x1});
        r.placed.push_back({k, x0, x1, cx, row});
    }
    std::sort(r.placed.begin(), r.placed.end(), [](const Placed& a, const Placed& b) { return a.x0 < b.x0; });
    return r;
}
} // namespace detail

// Places the labels in view. The strongest are placed first; a name that finds no free spot in any row is left out (dropped counts them), so
// zooming in brings the rest. When everything fits in less than a third of the rows' length, the names are laid out again with their detail.
inline Result layout(const Store& s, const View& v, const WidthFn& width) {
    Result empty;
    if (!(v.hiMhz > v.loMhz) || !(v.px1 > v.px0) || v.maxRows < 1) return empty;
    const double perPx = (v.hiMhz - v.loMhz) / (double)(v.px1 - v.px0);
    std::vector<size_t> idx = mergeSame(s, s.inRange(v.loMhz, v.hiMhz, v.mask), std::max(0.002, perPx));
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return s.at(a).prio > s.at(b).prio; });
    constexpr size_t kCap = 800;   // beyond what could ever fit: the strongest only (a raster of thousands of channels in a wide view)
    if (idx.size() > kCap) idx.resize(kCap);
    Result r = detail::place(s, idx, v, width, false);
    if (r.dropped == 0 && !r.placed.empty()) {
        float used = 0;
        for (const Placed& p : r.placed) used += p.x1 - p.x0;
        bool anyDetail = false;
        for (const Placed& p : r.placed) anyDetail = anyDetail || !s.at(p.idx).detail.empty();
        if (anyDetail && used < (v.px1 - v.px0) * (float)v.maxRows / 3.f) {
            Result d = detail::place(s, idx, v, width, true);
            if (d.dropped == 0) return d;
        }
    }
    return r;
}

// the label under a point (the rows are rowH high and start at y0, row 0 on top); -1: none
inline int hit(const Result& r, float x, float y, float y0, float rowH, float rowGap) {
    if (y < y0) return -1;
    const int row = (int)((y - y0) / (rowH + rowGap));
    if (y - y0 - row * (rowH + rowGap) > rowH) return -1;
    for (size_t i = 0; i < r.placed.size(); i++) if (r.placed[i].row == row && x >= r.placed[i].x0 && x <= r.placed[i].x1) return (int)i;
    return -1;
}

// the trimmed name for a radio (RDS names come padded to 8 characters)
inline std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    size_t b = 0;
    while (b < s.size() && s[b] == ' ') b++;
    return s.substr(b);
}

// a cheap fingerprint of the sources, to know when the store must be rebuilt
inline uint64_t mix(uint64_t h, uint64_t x) { h ^= x + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); return h; }
inline uint64_t mix(uint64_t h, const std::string& s) { for (unsigned char c : s) h = (h ^ c) * 1099511628211ull; return h; }

} // namespace autolabel
