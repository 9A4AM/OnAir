// AX.25 link layer: see packet_ax25.h. Written from the AX.25 2.2 specification.
#include "dect2/packet/packet_ax25.h"

namespace dect2 {
namespace ax25 {

namespace {
void putAddr(std::vector<uint8_t>& o, const Address& a, bool last) {
    for (size_t i = 0; i < 6; i++) o.push_back((uint8_t)((i < a.call.size() ? a.call[i] : ' ') << 1));
    o.push_back((uint8_t)(0x60 | (a.h ? 0x80 : 0) | ((a.ssid & 15) << 1) | (last ? 1 : 0)));
}
} // namespace

// Everything from here to the matching #endif is the C++ implementation of the FCS, frame parser, text forms and HDLC receiver step; with
// DECT2_RUST_AX25 (rust/onair_ax25) packet_ax25_rs.cpp provides the same functions. It stays until the Rust build is mandatory (rust/README.md);
// tests/packet/test_packet_proto_rs.cpp compares the two. Frame building and the transmit-side bit helpers below are C++ in both builds.
#ifndef DECT2_RUST_AX25
uint16_t fcs(const uint8_t* d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int k = 0; k < 8; k++) crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
    }
    return (uint16_t)~crc;
}
bool fcsOk(const uint8_t* d, size_t n) {
    if (n < 3) return false;
    const uint16_t c = fcs(d, n - 2);
    return d[n - 2] == (c & 0xFF) && d[n - 1] == (c >> 8);
}

std::string Address::str() const { return ssid ? call + "-" + std::to_string(ssid) : call; }

std::string Frame::pathStr() const {
    int lastH = -1;
    for (size_t i = 0; i < path.size(); i++) if (path[i].h) lastH = (int)i;
    std::string s;
    for (size_t i = 0; i < path.size(); i++) {
        if (i) s += ',';
        s += path[i].str();
        if ((int)i == lastH) s += '*';
    }
    return s;
}
std::string Frame::tnc2() const {
    std::string s = from.str() + ">" + to.str();
    const std::string p = pathStr();
    if (!p.empty()) s += "," + p;
    return s + ":" + info;
}
std::string Frame::typeName() const {
    if (!(control & 1)) return "I";
    if ((control & 3) == 1) {
        static const char* s[4] = {"RR", "RNR", "REJ", "SREJ"};
        return s[(control >> 2) & 3];
    }
    switch (control & 0xEF) {
    case 0x03: return "UI";
    case 0x2F: return "SABM";
    case 0x6F: return "SABME";
    case 0x43: return "DISC";
    case 0x0F: return "DM";
    case 0x63: return "UA";
    case 0x87: return "FRMR";
    case 0xAF: return "XID";
    case 0xE3: return "TEST";
    }
    return "U";
}

namespace {
bool parseAddr(const uint8_t* p, Address& a, bool& last) {
    std::string c;
    for (int i = 0; i < 6; i++) {
        const uint8_t ch = p[i] >> 1;
        if (p[i] & 1) return false;                      // only the last byte of the last address ends the field
        if (ch == ' ') continue;
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z'))) return false;
        c += (char)ch;
    }
    if (c.empty()) return false;
    a.call = c;
    a.ssid = (p[6] >> 1) & 0x0F;
    a.h = (p[6] & 0x80) != 0;
    last = (p[6] & 1) != 0;
    return true;
}
} // namespace
bool parseFrame(const uint8_t* d, size_t n, Frame& f) {
    f = Frame();
    if (n < 15) return false;
    bool last = false;
    if (!parseAddr(d, f.to, last) || last) return false;
    if (!parseAddr(d + 7, f.from, last)) return false;
    size_t pos = 14;
    while (!last) {
        if (f.path.size() >= 8 || pos + 7 > n) return false;
        Address a;
        if (!parseAddr(d + pos, a, last)) return false;
        f.path.push_back(a);
        pos += 7;
    }
    if (pos >= n) return false;
    f.control = d[pos++];
    f.pid = -1;
    const bool hasPid = !(f.control & 1) || (f.control & 0xEF) == 0x03;     // I frames and UI frames
    if (hasPid) {
        if (pos >= n) return false;
        f.pid = d[pos++];
    }
    f.info.assign((const char*)d + pos, n - pos);
    return true;
}

int hdlcStep(HdlcState& s, int level, bool nrzi, size_t& len) {
    len = 0;
    int d = level & 1;
    if (nrzi) { d = (d == s.prev) ? 1 : 0; s.prev = level & 1; }
    if (s.flagRecent > 0) s.flagRecent--;
    s.pat = (uint8_t)((s.pat >> 1) | (d << 7));
    if (s.pat == 0x7E) {
        // a flag: seven of its bits were already stored, so a frame that ends here leaves seven bits in the accumulator
        int flagHold = 16, ev = 0;
        if (!s.hunting && s.inFrame && s.cnt == 7 && s.len >= 17 && s.len <= 400) {
            if (fcsOk(s.buf, s.len)) { ev = 1; len = s.len - 2; s.resyncClock = true; flagHold = 48; }
            else { ev = 2; len = s.len; }
            s.hunting = true;
            s.inFrame = false;
        } else {
            s.hunting = false;
            s.inFrame = true;
        }
        s.len = 0; s.acc = 0; s.cnt = 0;
        s.flagRecent = flagHold;
        return ev;
    }
    if (s.pat == 0xFE) { s.inFrame = false; s.hunting = true; s.len = 0; s.cnt = 0; return 0; }       // seven ones: abort
    if ((s.pat & 0xFC) == 0x7C) return 0;                                              // a stuffed zero
    if (s.hunting || !s.inFrame) return 0;
    s.acc = (uint8_t)((s.acc >> 1) | (d << 7));
    if (++s.cnt == 8) {
        if (s.len > 400) { s.inFrame = false; s.len = 0; s.cnt = 0; return 0; }
        s.buf[s.len++] = s.acc;
        s.cnt = 0;
    }
    return 0;
}

void hdlcReset(HdlcState& s) { s = HdlcState(); }
#endif // DECT2_RUST_AX25

void appendFcs(std::vector<uint8_t>& f) {
    const uint16_t c = fcs(f.data(), f.size());
    f.push_back((uint8_t)(c & 0xFF));
    f.push_back((uint8_t)(c >> 8));
}
std::vector<uint8_t> buildFrame(const Frame& f) {
    std::vector<uint8_t> o;
    putAddr(o, f.to, false);
    putAddr(o, f.from, f.path.empty());
    for (size_t i = 0; i < f.path.size(); i++) putAddr(o, f.path[i], i + 1 == f.path.size());
    o.push_back(f.control);
    if (f.pid >= 0) o.push_back((uint8_t)f.pid);
    o.insert(o.end(), f.info.begin(), f.info.end());
    appendFcs(o);
    return o;
}

std::vector<uint8_t> hdlcBits(const std::vector<uint8_t>& fr, int before, int after) {
    std::vector<uint8_t> b;
    auto flag = [&] { static const uint8_t f[8] = {0, 1, 1, 1, 1, 1, 1, 0}; b.insert(b.end(), f, f + 8); };
    for (int i = 0; i < before; i++) flag();
    int ones = 0;
    for (uint8_t by : fr)
        for (int k = 0; k < 8; k++) {
            const int v = (by >> k) & 1;
            b.push_back((uint8_t)v);
            if (v) { if (++ones == 5) { b.push_back(0); ones = 0; } } else ones = 0;
        }
    for (int i = 0; i < after; i++) flag();
    return b;
}

std::vector<uint8_t> nrziEncode(const std::vector<uint8_t>& bits, uint8_t level) {
    std::vector<uint8_t> o;
    o.reserve(bits.size());
    for (uint8_t b : bits) { if (!b) level ^= 1; o.push_back(level); }
    return o;
}

std::vector<uint8_t> scramble(const std::vector<uint8_t>& bits, uint32_t& st) {
    std::vector<uint8_t> o;
    o.reserve(bits.size());
    for (uint8_t b : bits) {
        const uint8_t s = (uint8_t)((b ^ (st >> 11) ^ (st >> 16)) & 1);     // taps 12 and 17 bits back
        st = ((st << 1) | s) & 0x1FFFF;
        o.push_back(s);
    }
    return o;
}

void HdlcRx::bit(int level) {
    size_t n = 0;
    const int ev = hdlcStep(st_, level, nrzi_, n);
    if (ev && cb_) cb_(std::vector<uint8_t>(st_.buf, st_.buf + n), ev == 1);
}

} // namespace ax25
} // namespace dect2
