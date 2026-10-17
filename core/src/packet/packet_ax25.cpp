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
