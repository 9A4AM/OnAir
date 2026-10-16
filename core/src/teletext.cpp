#include "dect2/tv/teletext.h"
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <vector>

namespace dect2 {

// ------------------------------------------------------------------ hamming
namespace {
uint8_t hammEncode(int n) {
    const int d1 = n & 1, d2 = (n >> 1) & 1, d3 = (n >> 2) & 1, d4 = (n >> 3) & 1;
    const int p1 = 1 ^ d1 ^ d3 ^ d4, p2 = 1 ^ d1 ^ d2 ^ d4, p3 = 1 ^ d1 ^ d2 ^ d3;
    const int x7 = p1 ^ d1 ^ p2 ^ d2 ^ p3 ^ d3 ^ d4;
    const int p4 = 1 ^ x7;
    return (uint8_t)(p1 | d1 << 1 | p2 << 2 | d2 << 3 | p3 << 4 | d3 << 5 | p4 << 6 | d4 << 7);
}
struct HammTab {
    int8_t dec[256];
    HammTab() {
        for (int b = 0; b < 256; b++) {
            int best = -1, bd = 9;
            for (int n = 0; n < 16; n++) {
                int d = __builtin_popcount((unsigned)(b ^ hammEncode(n)));
                if (d < bd) { bd = d; best = n; }
            }
            dec[b] = bd <= 1 ? (int8_t)best : (int8_t)-1;
        }
    }
};
const HammTab& hamm() { static HammTab t; return t; }
} // namespace

int ttxHamming84(uint8_t b) { return hamm().dec[b]; }
int ttxHamming2418(const uint8_t* p) {
    uint32_t w = p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;   // bit n-1 of w is bit n of the word
    unsigned syn = 0;
    for (int n = 1; n <= 23; n++) if (w >> (n - 1) & 1) syn ^= (unsigned)n;
    const bool odd = __builtin_popcount(w & 0xFFFFFF) & 1;   // a valid word has odd overall parity (P6)
    if (syn == 0) {
        // fine, or only P6 is wrong
    } else if (odd || syn > 23) {
        return -1;                      // two errors (parity unchanged) or a syndrome outside the word
    } else w ^= 1u << (syn - 1);        // one error: flip it
    // data bits D1..D18 are the non-parity positions 3,5,6,7,9..15,17..23
    int d = 0, k = 0;
    for (int n = 1; n <= 23; n++) {
        if ((n & (n - 1)) == 0) continue;   // parity position (1,2,4,8,16)
        if (w >> (n - 1) & 1) d |= 1 << k;
        k++;
    }
    return d;
}
uint8_t ttxReverse8(uint8_t b) {
    b = (uint8_t)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = (uint8_t)((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}

// ------------------------------------------------------------------ decoder
void TeletextDecoder::setPid(int pid) {
    std::lock_guard<std::mutex> lk(mu_);
    if (pid == pid_) return;
    pid_ = pid;
    pes_.clear(); inPes_ = false; cc_ = -1;
    pages_.clear();
    curInit_ = false;
}
int TeletextDecoder::pid() const { std::lock_guard<std::mutex> lk(mu_); return pid_; }
void TeletextDecoder::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    pes_.clear(); inPes_ = false; cc_ = -1; pages_.clear(); curInit_ = false; packets_ = 0;
}
uint64_t TeletextDecoder::packets() const { std::lock_guard<std::mutex> lk(mu_); return packets_; }

void TeletextDecoder::feedTs(const uint8_t* p) {
    if (p[0] != 0x47) return;
    const int pid = ((p[1] & 0x1F) << 8) | p[2];
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (pid != pid_) return;
    }
    const bool pusi = (p[1] & 0x40) != 0;
    const int afc = (p[3] >> 4) & 3;
    int off = 4;
    if (afc & 2) off += 1 + p[4];
    if (!(afc & 1) || off >= 188) return;
    std::vector<uint8_t> done;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const int cc = p[3] & 15;
        if (cc_ >= 0 && ((cc_ + 1) & 15) != cc && !pusi) { pes_.clear(); inPes_ = false; } // lost packets: drop the partial PES
        cc_ = cc;
        if (pusi) {
            if (inPes_ && !pes_.empty()) done.swap(pes_);
            pes_.clear();
            inPes_ = true;
            pesLen_ = 0;
        }
        if (inPes_) {
            pes_.insert(pes_.end(), p + off, p + 188);
            if (pes_.size() >= 6 && pesLen_ == 0 && pes_[0] == 0 && pes_[1] == 0 && pes_[2] == 1) {
                const size_t l = ((size_t)pes_[4] << 8) | pes_[5];
                if (l) pesLen_ = l + 6;
            }
            if (pesLen_ && pes_.size() >= pesLen_ && done.empty()) { done.swap(pes_); inPes_ = false; pesLen_ = 0; }
        }
    }
    if (!done.empty()) feedPes(done.data(), done.size());
}

void TeletextDecoder::feedPes(const uint8_t* pes, size_t n) {
    if (n < 10 || pes[0] != 0 || pes[1] != 0 || pes[2] != 1) return;
    if (pes[3] != 0xBD) return; // private stream 1
    const size_t hdr = 9 + pes[8];
    if (hdr + 1 > n) return;
    const uint8_t* d = pes + hdr;
    size_t left = n - hdr;
    if (d[0] < 0x10 || d[0] > 0x1F) return; // data_identifier
    d++; left--;
    while (left >= 2) {
        const uint8_t id = d[0], len = d[1];
        if (2u + len > left) break;
        if ((id == 0x02 || id == 0x03) && len == 44) dataUnit(d + 2);
        d += 2 + len; left -= 2 + len;
    }
}

void TeletextDecoder::closePage(int64_t key) {
    if (key < 0) return;
    auto it = pages_.find((uint32_t)key);
    if (it == pages_.end()) return;
    it->second.live.complete = true;
    it->second.done = it->second.live;
    it->second.haveDone = true;
}

void TeletextDecoder::dataUnit(const uint8_t* u) {
    // u[0] field/line, u[1] framing code, u[2..43] = 2 address bytes + 40 data bytes (bit-reversed in the PES)
    if (u[1] != 0xE4 && u[1] != 0x27) return;
    uint8_t b[42];
    for (int i = 0; i < 42; i++) b[i] = ttxReverse8(u[2 + i]);
    const int a0 = ttxHamming84(b[0]), a1 = ttxHamming84(b[1]);
    if (a0 < 0 || a1 < 0) return;
    const int addr = a0 | a1 << 4;
    int mag = addr & 7;
    if (mag == 0) mag = 8;
    const int row = addr >> 3;
    std::lock_guard<std::mutex> lk(mu_);
    if (!curInit_) { for (int i = 0; i < 9; i++) cur_[i] = -1; curInit_ = true; }
    packets_++;
    if (row == 0) {
        const int un = ttxHamming84(b[2]), tn = ttxHamming84(b[3]);
        int nib[6];
        for (int i = 0; i < 6; i++) nib[i] = ttxHamming84(b[4 + i]);
        const bool serial = nib[5] >= 0 && (nib[5] & 1);   // C11
        // this header ends the page being sent in its magazine (every magazine's, with serial transmission)
        if (serial) { for (int m = 1; m <= 8; m++) { closePage(cur_[m]); cur_[m] = -1; } }
        else { closePage(cur_[mag]); cur_[mag] = -1; }
        if (un < 0 || tn < 0) return;
        if (un > 9 || tn > 9) return;   // time filler (0xFF) and non-decimal pages
        const int number = mag * 100 + tn * 10 + un;
        int subcode = 0;
        if (nib[0] >= 0 && nib[1] >= 0 && nib[2] >= 0 && nib[3] >= 0) subcode = nib[0] | (nib[1] & 7) << 4 | nib[2] << 8 | (nib[3] & 3) << 12;
        const uint32_t key = (uint32_t)number << 16 | (uint32_t)subcode;
        if (pages_.size() > 4096 && !pages_.count(key)) {   // a stream with ever-changing subcodes: drop the oldest copies
            std::vector<std::pair<uint64_t, uint32_t>> age;
            for (auto& kv : pages_) age.push_back({kv.second.live.version, kv.first});
            std::sort(age.begin(), age.end());
            for (size_t i = 0; i < 512; i++) pages_.erase(age[i].second);
        }
        TtxPage& p = pages_[key].live;
        const bool erase = nib[1] >= 0 && (nib[1] & 8);
        if (erase || p.number == 0) { p.rows = {}; p.rowMask = 0; for (int i = 0; i < 6; i++) p.fastext[i] = p.fastextSub[i] = 0; }
        p.number = number;
        p.subcode = subcode;
        p.complete = false;
        p.serial = serial;
        p.newsflash = nib[3] >= 0 && (nib[3] & 4);
        p.subtitle = nib[3] >= 0 && (nib[3] & 8);
        p.inhibit = nib[4] >= 0 && (nib[4] & 8);
        p.national = nib[5] >= 0 ? ((nib[5] >> 1) & 7) : 0;
        for (int i = 0; i < 8; i++) p.rows[0][i] = ' ';
        for (int i = 0; i < 32; i++) p.rows[0][8 + i] = b[10 + i] & 0x7F;
        p.rowMask |= 1;
        p.version = ++version_;
        cur_[mag] = key;
    } else if (row >= 1 && row <= 24 && cur_[mag] >= 0) {
        auto it = pages_.find((uint32_t)cur_[mag]);
        if (it == pages_.end()) return;
        TtxPage& p = it->second.live;
        for (int i = 0; i < 40; i++) p.rows[row][i] = b[2 + i] & 0x7F;
        p.rowMask |= 1u << row;
        p.version = ++version_;
    } else if (row == 27 && cur_[mag] >= 0) {
        // packet X/27/0: Fastext links (red, green, yellow, cyan, index, spare), six Hamming 8/4 nibbles each
        if (ttxHamming84(b[2]) != 0) return;   // only designation code 0 carries page links
        auto it = pages_.find((uint32_t)cur_[mag]);
        if (it == pages_.end()) return;
        TtxPage& p = it->second.live;
        for (int l = 0; l < 6; l++) {
            int n[6];
            bool ok = true;
            for (int i = 0; i < 6; i++) { n[i] = ttxHamming84(b[3 + l * 6 + i]); if (n[i] < 0) ok = false; }
            if (!ok) continue;
            const int units = n[0], tens = n[1];
            if (units > 9 || tens > 9) { p.fastext[l] = 0; continue; }   // 0xFF = no link (and anything not decimal)
            const int m = ((n[3] >> 3) & 1) | ((n[5] >> 2) & 3) << 1;   // M1, M2, M3: the magazine relative to this one
            int lm = (mag & 7) ^ m;
            if (lm == 0) lm = 8;
            p.fastext[l] = lm * 100 + tens * 10 + units;
            p.fastextSub[l] = n[2] | (n[3] & 7) << 4 | n[4] << 8 | (n[5] & 3) << 12;
        }
        p.version = ++version_;
    }
}

bool TeletextDecoder::page(int number, TtxPage& out, int subcode) const {
    std::lock_guard<std::mutex> lk(mu_);
    const Entry* best = nullptr;
    uint64_t bestV = 0;
    for (auto it = pages_.lower_bound((uint32_t)number << 16); it != pages_.end() && (it->first >> 16) == (uint32_t)number; ++it) {
        if (subcode >= 0 && (int)(it->first & 0xFFFF) != subcode) continue;
        const TtxPage& v = it->second.haveDone ? it->second.done : it->second.live;
        if (!best || v.version >= bestV) { best = &it->second; bestV = v.version; }
    }
    if (!best) return false;
    out = best->haveDone ? best->done : best->live;
    // the Fastext links arrive after the rows: take them from the live copy
    for (int i = 0; i < 6; i++) { out.fastext[i] = best->live.fastext[i]; out.fastextSub[i] = best->live.fastextSub[i]; }
    return true;
}

std::vector<int> TeletextDecoder::pages() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<int> v;
    for (auto& kv : pages_) { const int n = (int)(kv.first >> 16); if (v.empty() || v.back() != n) v.push_back(n); }
    return v;
}

std::vector<int> TeletextDecoder::subpages(int number) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<int> v;
    for (auto it = pages_.lower_bound((uint32_t)number << 16); it != pages_.end() && (it->first >> 16) == (uint32_t)number; ++it) v.push_back((int)(it->first & 0xFFFF));
    return v;
}

// ------------------------------------------------------------------ renderer
// Latin G0 national option subsets (EN 300 706 table 36): the 13 code positions 0x23 0x24 0x40 0x5B-0x5F 0x60 0x7B-0x7E
uint32_t ttxG0(uint8_t c, int national) {
    static const int pos[13] = {0x23, 0x24, 0x40, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x7B, 0x7C, 0x7D, 0x7E};
    static const uint32_t sub[7][13] = {
        {0x00A3, '$', '@', 0x2190, 0x00BD, 0x2192, 0x2191, '#', 0x2015, 0x00BC, 0x2016, 0x00BE, 0x00F7},        // English
        {'#', '$', 0x00A7, 0x00C4, 0x00D6, 0x00DC, '^', '_', 0x00B0, 0x00E4, 0x00F6, 0x00FC, 0x00DF},          // German
        {'#', 0x00A4, 0x00C9, 0x00C4, 0x00D6, 0x00C5, 0x00DC, '_', 0x00E9, 0x00E4, 0x00F6, 0x00E5, 0x00FC},    // Swedish, Finnish, Hungarian
        {0x00A3, '$', 0x00E9, 0x00B0, 0x00E7, 0x2192, 0x2191, '#', 0x00F9, 0x00E0, 0x00F2, 0x00E8, 0x00EC},    // Italian
        {0x00E9, 0x00EF, 0x00E0, 0x00EB, 0x00EA, 0x00F9, 0x00EE, '#', 0x00E8, 0x00E2, 0x00F4, 0x00FB, 0x00E7}, // French
        {0x00E7, '$', 0x00A1, 0x00E1, 0x00E9, 0x00ED, 0x00F3, 0x00FA, 0x00BF, 0x00FC, 0x00F1, 0x00E8, 0x00E0}, // Portuguese, Spanish
        {'#', 0x016F, 0x010D, 0x0165, 0x017E, 0x00FD, 0x00ED, 0x0159, 0x00E9, 0x00E1, 0x011B, 0x00FA, 0x0161}, // Czech, Slovak
    };
    const int set = (national >= 0 && national < 7) ? national : 0;
    for (int i = 0; i < 13; i++) if (c == pos[i]) return sub[set][i];
    if (c == 0x7F) return 0x25A0;
    return c;
}

void ttxRender(const TtxPage& p, TtxGrid& g) {
    for (auto& r : g) for (auto& c : r) c = TtxCell();
    bool skipNext = false, anyBox = false;
    for (int r = 0; r < 25; r++) {
        if (skipNext) { skipNext = false; continue; }   // lower half of a double-height row was filled in below
        if (!(p.rowMask & (1u << r))) continue;
        uint8_t fg = 7, bg = 0;
        bool mosaic = false, sep = false, dbl = false, flash = false, hold = false, conceal = false, inBox = false;
        uint8_t held = 0;
        bool rowDouble = false;
        for (int c = 0; c < 40; c++) {
            uint8_t code = p.rows[r][c] & 0x7F;
            TtxCell cell;
            if (r == 0 && c < 8) { cell.ch = ' '; g[r][c] = cell; continue; }
            if (code < 0x20) {
                // spacing attribute: takes effect from the next cell; this cell shows a space (or the held mosaic)
                bool boxCode = false;
                switch (code) {
                case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: fg = code; mosaic = false; conceal = false; break;
                case 0x08: flash = true; break;
                case 0x09: flash = false; break;
                case 0x0A: inBox = false; boxCode = true; break;
                case 0x0B: inBox = true; anyBox = true; boxCode = true; break;
                case 0x0C: dbl = false; break;
                case 0x0D: case 0x0F: dbl = true; rowDouble = true; break;
                case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: fg = code & 7; mosaic = true; conceal = false; break;
                case 0x18: conceal = true; break;
                case 0x19: sep = false; break;
                case 0x1A: sep = true; break;
                case 0x1C: bg = 0; break;
                case 0x1D: bg = fg; break;
                case 0x1E: hold = true; break;
                case 0x1F: hold = false; break;
                default: break;
                }
                cell.fg = fg; cell.bg = bg; cell.flash = flash; cell.dh = dbl ? 1 : 0; cell.conceal = conceal;
                cell.boxed = !boxCode && inBox;
                if (hold && mosaic && held) { cell.mosaic = true; cell.sextants = held; cell.separated = sep; }
                else cell.ch = ' ';
                g[r][c] = cell;
                continue;
            }
            cell.fg = fg; cell.bg = bg; cell.flash = flash; cell.dh = dbl ? 1 : 0; cell.conceal = conceal; cell.boxed = inBox;
            if (mosaic && (code < 0x40 || code >= 0x60)) {
                cell.mosaic = true;
                cell.separated = sep;
                cell.sextants = (uint8_t)((code & 0x1F) | ((code & 0x40) ? 0x20 : 0));
                held = cell.sextants;
            } else cell.ch = ttxG0(code, p.national);
            g[r][c] = cell;
        }
        if (rowDouble && r < 24) {
            for (int c = 0; c < 40; c++) {
                TtxCell lo = g[r][c];
                if (lo.dh == 1) lo.dh = 2; else { const bool bx = lo.boxed; lo = TtxCell(); lo.bg = g[r][c].bg; lo.fg = g[r][c].fg; lo.ch = ' '; lo.boxed = bx; }
                g[r + 1][c] = lo;
            }
            skipNext = true;
        }
    }
    if (!anyBox) for (auto& r : g) for (auto& c : r) c.boxed = true;
}

std::string ttxRowText(const TtxGrid& g, int row) {
    std::string s;
    if (row < 0 || row >= 25) return s;
    for (const TtxCell& c : g[row]) {
        uint32_t u = (c.mosaic || c.conceal) ? ' ' : c.ch;
        if (u < 0x80) s += (char)u;
        else if (u < 0x800) { s += (char)(0xC0 | (u >> 6)); s += (char)(0x80 | (u & 0x3F)); }
        else { s += (char)(0xE0 | (u >> 12)); s += (char)(0x80 | ((u >> 6) & 0x3F)); s += (char)(0x80 | (u & 0x3F)); }
    }
    return s;
}

} // namespace dect2
