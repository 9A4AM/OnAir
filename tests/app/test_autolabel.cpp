// The layout of the names on the spectrum (app/gfx/autolabel_logic.h): the store, merging of the same station, stacking in rows without overlap,
// thinning when the view is wide, the detail when there is room, the edges of the plot, and the hit test.
#include "../../app/gfx/autolabel_logic.h"
#include <cstdio>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using namespace autolabel;

static Label mk(double mhz, const char* text, int src, float prio = 0, const char* detail = "") {
    Label l; l.mhz = mhz; l.text = text; l.src = src; l.prio = prio; l.detail = detail; return l;
}
// 6 px a character, 8 px of padding; detail adds its length
static float width(const Label& l, bool d) { return 6.f * (float)(l.text.size() + (d && !l.detail.empty() ? 2 + l.detail.size() : 0)) + 8.f; }

static View view(double lo, double hi, int rows = 3) {
    View v; v.loMhz = lo; v.hiMhz = hi; v.px0 = 0; v.px1 = 1000; v.maxRows = rows; v.gap = 4; return v;
}
static bool overlap(const Result& r) {
    for (size_t i = 0; i < r.placed.size(); i++)
        for (size_t j = i + 1; j < r.placed.size(); j++)
            if (r.placed[i].row == r.placed[j].row && r.placed[i].x0 < r.placed[j].x1 && r.placed[j].x0 < r.placed[i].x1) return true;
    return false;
}

int main() {
    // the store: sorted, range query with the source mask
    {
        Store s;
        s.add(mk(101.0, "B", Fm)); s.add(mk(99.0, "A", Dab)); s.add(mk(100.0, "C", FreqDb)); s.add(mk(120.0, "D", Fm));
        s.finish();
        CHECK(s.size() == 4 && s.at(0).text == "A" && s.at(3).text == "D", "sorted by frequency");
        CHECK(s.inRange(99.5, 110, kAllSources).size() == 2, "range");
        CHECK(s.inRange(0, 200, 1u << Fm).size() == 2, "mask keeps only FM");
        CHECK(s.inRange(0, 200, 0).empty(), "no source on, no label");
        CHECK(s.inRange(150, 160, kAllSources).empty(), "nothing in an empty range");
        CHECK(s.at(0).prio == srcPriority(Dab), "the source's priority is added");
    }
    // plain stacking: three names on the same spot go to three rows, the fourth is dropped
    {
        Store s;
        for (int i = 0; i < 4; i++) s.add(mk(100.0, "STATION", Fm, (float)i));
        s.finish();
        View v = view(90, 110);
        v.mask = kAllSources;
        Result r = layout(s, v, width);
        CHECK(r.candidates == 1, "four labels of one station are one (same frequency): %zu", r.candidates);
        CHECK(r.placed.size() == 1 && r.placed[0].row == 0, "one placed on row 0");
        Store t;
        t.add(mk(100.000, "ALPHA", Band)); t.add(mk(100.002, "BRAVO", Band)); t.add(mk(100.004, "CHARLIE", Band)); t.add(mk(100.006, "DELTA", Band));
        t.finish();
        r = layout(t, v, width);   // band-plan names never merge
        CHECK(r.placed.size() == 3 && r.dropped == 1, "three rows, one dropped: %zu placed %zu dropped", r.placed.size(), r.dropped);
        CHECK(!overlap(r), "no overlap");
        int rows[3] = {0, 0, 0};
        for (const Placed& p : r.placed) rows[p.row]++;
        CHECK(rows[0] == 1 && rows[1] == 1 && rows[2] == 1, "one name in each row");
    }
    // the strongest source wins the first row and the last place
    {
        Store s;
        s.add(mk(100.0, "plain", Band)); s.add(mk(100.0, "decoded", Fm)); s.add(mk(100.0, "memory", Memories));
        s.finish();
        Result r = layout(s, view(90, 110, 1), width);
        CHECK(r.placed.size() == 1 && s.at(r.placed[0].idx).text == "decoded", "the decoded name stays when only one fits");
    }
    // the same station from two sources: the higher priority remains; with the stronger source off the other one shows
    {
        Store s;
        s.add(mk(93.900, "Noor FM", FreqDb)); s.add(mk(93.9004, "NOOR", Fm));
        s.finish();
        Result r = layout(s, view(90, 100), width);
        CHECK(r.placed.size() == 1 && s.at(r.placed[0].idx).text == "NOOR", "merged to the RDS name");
        View v = view(90, 100); v.mask = 1u << FreqDb;
        r = layout(s, v, width);
        CHECK(r.placed.size() == 1 && s.at(r.placed[0].idx).text == "Noor FM", "the database name when FM is off");
        Store t;
        t.add(mk(93.9, "Noor", Fm)); t.add(mk(94.1, "Other", Fm));
        t.finish();
        CHECK(layout(t, view(90, 100), width).placed.size() == 2, "stations 200 kHz apart stay two");
    }
    // zoom-aware: a wide view thins the names out, a narrow one shows them all
    {
        Store s;
        for (int i = 0; i < 100; i++) { char n[16]; std::snprintf(n, sizeof n, "ST%03d", i); s.add(mk(88.0 + i * 0.2, n, Fm)); }
        s.finish();
        Result wide = layout(s, view(88, 108), width);
        CHECK(!overlap(wide), "wide view: no overlap");
        CHECK(wide.dropped > 0 && wide.placed.size() < 100, "wide view: some names held back (%zu placed)", wide.placed.size());
        Result narrow = layout(s, view(93, 95), width);
        CHECK(narrow.dropped == 0 && narrow.placed.size() >= 9, "narrow view: all %zu names in view are shown", narrow.placed.size());
        CHECK(!overlap(narrow), "narrow view: no overlap");
        // zooming in never loses a name that was shown: the 3 rows of the wide view are a subset of what the narrow view shows when it covers them
        Result mid = layout(s, view(96, 100), width);
        bool all = true;
        for (const Placed& p : wide.placed) {
            const double f = s.at(p.idx).mhz;
            if (f < 96 || f > 100) continue;
            bool found = false;
            for (const Placed& q : mid.placed) found = found || q.idx == p.idx;
            all = all && found;
        }
        CHECK(all, "a name shown wide is still shown when zoomed in around it");
    }
    // the detail is added when there is room for all, and not otherwise
    {
        Store s;
        s.add(mk(100.0, "Mux A", Dvb, 0, "One, Two, Three")); s.add(mk(106.0, "Mux B", Dvb, 0, "Four"));
        s.finish();
        Result r = layout(s, view(90, 110), width);
        CHECK(r.detailed && r.dropped == 0, "room for the detail");
        Store t;
        for (int i = 0; i < 40; i++) { char n[16]; std::snprintf(n, sizeof n, "M%02d", i); t.add(mk(90.0 + i * 0.5, n, Dvb, 0, "a long list of services")); }
        t.finish();
        r = layout(t, view(90, 110), width);
        CHECK(!r.detailed, "no detail when crowded");
        CHECK(!overlap(r), "no overlap");
    }
    // the edges: a name at the edge of the plot is moved inside, never past it
    {
        Store s;
        s.add(mk(90.0, "LEFTEDGE", Fm)); s.add(mk(110.0, "RIGHTEDGE", Fm)); s.add(mk(111.0, "OUTSIDE", Fm));
        s.finish();
        Result r = layout(s, view(90, 110), width);
        CHECK(r.placed.size() == 2, "the one outside the view is not placed");
        for (const Placed& p : r.placed) CHECK(p.x0 >= 0 && p.x1 <= 1000, "inside the plot: %.0f..%.0f", p.x0, p.x1);
    }
    // many candidates: capped, still fast and without overlap
    {
        Store s;
        for (int i = 0; i < 20000; i++) s.add(mk(30.0 + i * 0.001, "CH", Band));
        s.finish();
        Result r = layout(s, view(30, 50), width);
        CHECK(r.candidates <= 800 && !overlap(r) && !r.placed.empty(), "capped: %zu candidates", r.candidates);
    }
    // degenerate views
    {
        Store s; s.add(mk(100, "X", Fm)); s.finish();
        CHECK(layout(s, view(100, 100), width).placed.empty(), "empty frequency span");
        View v = view(90, 110); v.px1 = v.px0;
        CHECK(layout(s, v, width).placed.empty(), "empty pixel span");
        CHECK(layout(s, view(90, 110, 0), width).placed.empty(), "no rows");
    }
    // the hit test
    {
        Store s;
        s.add(mk(95, "LEFT", Fm)); s.add(mk(105, "RIGHT", Fm));
        s.finish();
        Result r = layout(s, view(90, 110), width);
        CHECK(r.placed.size() == 2, "two placed");
        const Placed& p = r.placed[0];
        const float rowH = 14, gap = 2, y0 = 10;
        CHECK(hit(r, (p.x0 + p.x1) / 2, y0 + 5, y0, rowH, gap) == 0, "inside the first name");
        CHECK(hit(r, (p.x0 + p.x1) / 2, y0 + 15, y0, rowH, gap) == -1, "in the gap under it");
        CHECK(hit(r, (p.x0 + p.x1) / 2, y0 - 1, y0, rowH, gap) == -1, "above the rows");
        CHECK(hit(r, p.x1 + 40, y0 + 5, y0, rowH, gap) == -1, "beside it");
        CHECK(hit(r, (r.placed[1].x0 + r.placed[1].x1) / 2, y0 + 1, y0, rowH, gap) == 1, "the second name");
    }
    // helpers
    CHECK(trimmed("  NOOR FM  ") == "NOOR FM" && trimmed("   ").empty() && trimmed("").empty(), "trimmed");
    CHECK(mix(1, std::string("a")) != mix(1, std::string("b")) && mix(1, std::string("a")) == mix(1, std::string("a")), "fingerprint");
    if (failures) { std::printf("%d failed\n", failures); return 1; }
    std::printf("autolabel: all passed\n");
    return 0;
}
