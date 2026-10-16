// DAB FIG 1 and FIG 2 decoders: 16-character labels and the long labels of TS 103 176. Ported from etisnoop (GPL-3.0-or-later).
#include "dab_fig_internal.h"

namespace dect2 {

namespace dab_fig_detail {

// ------------------------------------------------------------------ FIG 1 and 2
std::string labelUtf8(int charset, const uint8_t* p, size_t n) { return text::clean(text::dabCharset(charset, p, n)); }

const char* xpadApp(int t) { return t == 2 ? "DLS" : t == 12 ? "MOT" : "?"; }

void fig1(Fig& g, int cs, int ext) {   // 8.1.13, 8.1.14 (16 character labels with a short label mask)
    const uint8_t* f = g.f;
    g.add(0, pf("Charset=%d", cs));
    if (g.len < 19) { g.err("FIG 1 too short for a label"); return; }
    const uint8_t* lab = f + g.len - 18;
    const unsigned mask = g.u16(g.len - 2);
    switch (ext) {
        case 0: g.add(0, pf("Ensemble ID=0x%04X", g.u16(1))); break;
        case 1: g.add(0, pf("Service ID=0x%04X", g.u16(1))); break;
        case 4: {
            const int pd = f[1] >> 7;
            g.add(0, pf(pd ? "Service ID=0x%08X" : "Service ID=0x%04X", pd ? g.u32(2) : g.u16(2)));
            g.add(0, pf("Service Component ID=0x%04X", f[1] & 15));
            break;
        }
        case 5: g.add(0, pf("Service ID=0x%08X", g.u32(1))); break;
        case 6: {
            const int pd = f[1] >> 7;
            g.add(0, pf(pd ? "Service ID=0x%08X" : "Service ID=0x%04X", pd ? g.u32(2) : g.u16(2)));
            g.add(0, pf("Service Component ID=0x%04X", f[1] & 15));
            const int app = f[pd ? 6 : 4] & 0x1F;
            g.add(0, pf("X-PAD App=%02X (%s)", app, xpadApp(app)));
            break;
        }
        default: g.add(0, "data: " + hexOf(f + 1, (size_t)(g.len - 1))); return;
    }
    g.add(0, "Label=\"" + labelUtf8(cs, lab, 16) + "\"");
    g.add(0, pf("Short label mask=0x%04X", mask));
    std::string sel;
    for (int i = 0; i < 16; i++) if (mask & (0x8000u >> i)) sel += (char)lab[i];
    g.add(0, "Short label=\"" + labelUtf8(cs, (const uint8_t*)sel.data(), sel.size()) + "\"");
}

int fig2IdLen(const uint8_t* f, int ext) {
    switch (ext) {
        case 0: case 1: return 2;
        case 4: return (f[1] >> 7) ? 5 : 3;
        case 5: return 4;
        case 6: return (f[1] >> 7) ? 6 : 4;
        default: return 0;
    }
}

std::string ucsOrUtf8(bool ucs2, const std::vector<uint8_t>& b) {
    return text::clean(ucs2 ? text::ucs2be(b.data(), b.size()) : std::string(b.begin(), b.end()));
}

void fig2(Fig& g, int toggle, int seg, int rfu, int ext) {   // 8.1.13, 8.1.14 and TS 103 176: UTF-8 / UCS-2 labels in segments
    const uint8_t* f = g.f;
    g.add(0, pf("toggle flag=%d", toggle));
    g.add(0, pf("segment index=%d", seg));
    g.add(0, pf("rfu=%d", rfu));
    const int idl = fig2IdLen(f, ext);
    if (g.len <= 1 + idl || ext == 2 || ext == 3 || ext > 6) {
        if (ext > 6 || ext == 2 || ext == 3) g.add(0, "data: " + hexOf(f + 1, (size_t)(g.len - 1)));
        else g.err("FIG2 length error");
        return;
    }
    switch (ext) {
        case 0: g.add(0, pf("Ensemble ID=0x%04X", g.u16(1))); break;
        case 1: g.add(0, pf("Service ID=0x%04X", g.u16(1))); break;
        case 4: {
            const int pd = f[1] >> 7;
            g.add(0, pf(pd ? "Service ID=0x%08X" : "Service ID=0x%04X", pd ? g.u32(2) : g.u16(2)));
            g.add(0, pf("Service Component ID=0x%04X", f[1] & 15));
            break;
        }
        case 5: g.add(0, pf("Service ID=0x%08X", g.u32(1))); break;
        case 6: {
            const int pd = f[1] >> 7;
            g.add(0, pf(pd ? "Service ID=0x%08X" : "Service ID=0x%04X", pd ? g.u32(2) : g.u16(2)));
            g.add(0, pf("Service Component ID=0x%04X", f[1] & 15));
            const int app = f[pd ? 6 : 4] & 0x1F;
            g.add(0, pf("X-PAD App=%02X (%s)", app, xpadApp(app)));
            break;
        }
    }
    const uint8_t* d = f + 1 + idl;
    int n = g.len - 1 - idl;
    bool ucs2 = false;
    int total = 0;
    if (seg == 0) {
        ucs2 = d[0] >> 7;
        total = ((d[0] >> 4) & 7) + 1;
        g.add(0, pf("encoding=%s", ucs2 ? "UCS-2" : "UTF-8"));
        g.add(0, pf("Total number of segments=%d", total));
        if (!rfu) {
            g.add(0, pf("rfa=%d", d[0] & 15));
            g.add(0, pf("character flag=%04x", g.u16((int)(d - f) + 1)));
            if (n <= 3) { g.err("FIG2 label length too short"); return; }
            d += 3; n -= 3;
        } else {
            g.add(0, pf("text control=0x%02x", d[0] & 15));
            if (n <= 1) { g.err("FIG2 label length too short"); return; }
            d += 1; n -= 1;
        }
    }
    g.add(0, "Segment text=\"" + ucsOrUtf8(ucs2, std::vector<uint8_t>(d, d + n)) + "\"");
    // put the label together from the segments of the same label and toggle value
    if (!g.ctx->all) return;
    std::map<int, std::vector<uint8_t>> segs;
    std::map<int, uint64_t> seen;
    bool u = false;
    int cnt = 0;
    for (const DabFigRecord& r : *g.ctx->all) {
        if (r.type != 2 || r.data.size() < 2 + (size_t)idl || (r.data[0] & 7) != ext || (r.data[0] >> 7) != toggle) continue;
        if (!std::equal(f + 1, f + 1 + idl, r.data.begin() + 1)) continue;
        const int s = (r.data[0] >> 4) & 7, rf = (r.data[0] >> 3) & 1;
        const uint8_t* rd = r.data.data() + 1 + idl;
        int rn = (int)r.data.size() - 1 - idl;
        if (s == 0) {
            if (rn < (rf ? 2 : 4)) continue;
            u = rd[0] >> 7;
            cnt = ((rd[0] >> 4) & 7) + 1;
            rd += rf ? 1 : 3; rn -= rf ? 1 : 3;
        }
        if (rn < 0 || (seen.count(s) && seen[s] > r.lastFrame)) continue;
        seen[s] = r.lastFrame;
        segs[s].assign(rd, rd + rn);
    }
    std::string state = "(";
    for (const auto& s : segs) state += pf("%d,", s.first);
    state += pf("count=%d,charset=%s)", cnt, cnt ? (u ? "UCS-2" : "UTF-8") : "undefined");
    g.add(0, "Label segments=\"" + state + "\"");
    if (!cnt) return;
    std::vector<uint8_t> all;
    for (int s = 0; s < cnt; s++) {
        const auto it = segs.find(s);
        if (it == segs.end()) return;
        all.insert(all.end(), it->second.begin(), it->second.end());
    }
    g.add(0, "Label=\"" + ucsOrUtf8(u, all) + "\"");
}


} // namespace dab_fig_detail

} // namespace dect2
