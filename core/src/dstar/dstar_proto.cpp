// D-STAR protocol pieces (see dstar_proto.h).
#include "dect2/dstar/dstar_proto.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "dect2/common/bits.h"
#include "dect2/common/str.h"

namespace dect2 {
namespace dstar {

const uint8_t kFrameSync[15] = {1, 1, 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0};
const uint8_t kDataSync[24] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0};
const uint8_t kEndPattern[48] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
                                 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 0};
const uint8_t kSlowScramble[3] = {0x70, 0x4F, 0x93};

uint16_t crc(const uint8_t* p, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0x8408) : (uint16_t)(c >> 1);
    }
    return (uint16_t)~c;
}

static void putCall(uint8_t* o, const std::string& s, size_t n) {
    for (size_t i = 0; i < n; i++) o[i] = i < s.size() ? (uint8_t)s[i] : ' ';
}
static std::string getCall(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) s += (p[i] >= 32 && p[i] < 127) ? (char)p[i] : '?';
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

void headerPack(const Header& h, uint8_t o[kHeaderBytes]) {
    o[0] = h.flag[0]; o[1] = h.flag[1]; o[2] = h.flag[2];
    putCall(o + 3, h.rpt2, 8); putCall(o + 11, h.rpt1, 8); putCall(o + 19, h.ur, 8); putCall(o + 27, h.my, 8); putCall(o + 35, h.sfx, 4);
    const uint16_t c = crc(o, 39);
    o[39] = (uint8_t)c; o[40] = (uint8_t)(c >> 8);
}

bool headerUnpack(const uint8_t in[kHeaderBytes], Header& h) {
    h.flag[0] = in[0]; h.flag[1] = in[1]; h.flag[2] = in[2];
    h.rpt2 = getCall(in + 3, 8); h.rpt1 = getCall(in + 11, 8); h.ur = getCall(in + 19, 8); h.my = getCall(in + 27, 8); h.sfx = getCall(in + 35, 4);
    const uint16_t c = crc(in, 39);
    return in[39] == (uint8_t)c && in[40] == (uint8_t)(c >> 8);
}

std::string flagText(const uint8_t f[3]) {
    std::string s;
    auto add = [&](const char* t) { if (!s.empty()) s += ", "; s += t; };
    if (f[0] & 0x80) add("data");
    add((f[0] & 0x40) ? "via repeater" : "simplex");
    if (f[0] & 0x20) add("interrupted");
    if (f[0] & 0x10) add("control");
    if (f[0] & 0x08) add("EMERGENCY");
    static const char* kSub[8] = {nullptr, "relay unavailable", "no response", "ACK", "resend", "unused", "auto reply", "repeater control"};
    if (kSub[f[0] & 7]) add(kSub[f[0] & 7]);
    return s;
}

void bytesToBits(const uint8_t* b, int nBytes, uint8_t* bits) { dect2::bits::bytesToBitsLsb(b, (size_t)nBytes, bits); }
void bitsToBytes(const uint8_t* bits, int nBytes, uint8_t* b) { dect2::bits::bitsToBytesLsb(bits, (size_t)nBytes, b); }

void convEncode(const uint8_t* in, int nIn, uint8_t* out) {
    int d1 = 0, d2 = 0;
    for (int i = 0; i < nIn; i++) {
        const int b = in[i] & 1;
        out[2 * i] = (uint8_t)(b ^ d1 ^ d2);
        out[2 * i + 1] = (uint8_t)(b ^ d2);
        d2 = d1; d1 = b;
    }
}

int viterbi(const float* soft, int nOut, uint8_t* out) {
    // state = (d1, d2) as d1 | d2 << 1; metric: correlation, larger is better
    std::vector<uint8_t> from((size_t)nOut * 4);
    double m[4] = {0, -1e30, -1e30, -1e30};
    for (int i = 0; i < nOut; i++) {
        double nm[4] = {-1e30, -1e30, -1e30, -1e30};
        uint8_t nf[4] = {};
        for (int s = 0; s < 4; s++) {
            if (m[s] < -1e29) continue;
            const int d1 = s & 1, d2 = s >> 1;
            for (int b = 0; b < 2; b++) {
                const int g1 = b ^ d1 ^ d2, g2 = b ^ d2;
                const double v = m[s] + (g1 ? soft[2 * i] : -soft[2 * i]) + (g2 ? soft[2 * i + 1] : -soft[2 * i + 1]);
                const int ns = b | d1 << 1;
                if (v > nm[ns]) { nm[ns] = v; nf[ns] = (uint8_t)s; }
            }
        }
        for (int s = 0; s < 4; s++) { m[s] = nm[s]; from[(size_t)i * 4 + s] = nf[s]; }
    }
    int s = 0;   // the tail bits bring the coder back to state 0
    for (int i = nOut - 1; i >= 0; i--) { out[i] = (uint8_t)(s & 1); s = from[(size_t)i * 4 + s]; }
    std::vector<uint8_t> re((size_t)nOut * 2);
    convEncode(out, nOut, re.data());
    int err = 0;
    for (int i = 0; i < 2 * nOut; i++) if ((soft[i] > 0) != (re[(size_t)i] != 0)) err++;
    return err;
}

static void ilvOrder(int* k660) {
    int k = 0;
    for (int i = 0; i < kHeaderBits; i++) {
        k660[i] = k;
        k += 24;
        if (k >= 672) k -= 671;
        else if (k >= 660) k -= 647;
    }
}
static const int* ilv() {
    static int t[kHeaderBits];
    static const bool done = (ilvOrder(t), true);
    (void)done;
    return t;
}

void interleave(const uint8_t in[kHeaderBits], uint8_t out[kHeaderBits]) {
    const int* t = ilv();
    for (int i = 0; i < kHeaderBits; i++) out[t[i]] = in[i];
}
void deinterleave(const float in[kHeaderBits], float out[kHeaderBits]) {
    const int* t = ilv();
    for (int i = 0; i < kHeaderBits; i++) out[i] = in[t[i]];
}

void scrambleBits(uint8_t* bits, int n) {
    int s = 0x7F;
    for (int i = 0; i < n; i++) {
        const int o = ((s >> 3) ^ (s >> 6)) & 1;
        s = ((s << 1) | o) & 0x7F;
        bits[i] ^= (uint8_t)o;
    }
}

void headerEncode(const uint8_t hdr[kHeaderBytes], uint8_t bits[kHeaderBits]) {
    uint8_t in[330] = {};
    bytesToBits(hdr, kHeaderBytes, in);
    uint8_t c[kHeaderBits];
    convEncode(in, 330, c);
    interleave(c, bits);
    scrambleBits(bits, kHeaderBits);
}

int headerDecode(const float soft[kHeaderBits], uint8_t hdr[kHeaderBytes]) {
    uint8_t sc[kHeaderBits] = {};
    scrambleBits(sc, kHeaderBits);
    float d[kHeaderBits], c[kHeaderBits];
    for (int i = 0; i < kHeaderBits; i++) d[i] = sc[i] ? -soft[i] : soft[i];
    deinterleave(d, c);
    uint8_t out[330];
    const int err = viterbi(c, 330, out);
    bitsToBytes(out, kHeaderBytes, hdr);
    return err;
}

void ambeFrame(const uint8_t bits[kVoiceBits], char fr[4][24]) {
    static const int dW[72] = {0, 0, 3, 2, 1, 1, 0, 0, 1, 1, 0, 0, 3, 2, 1, 1, 3, 2, 1, 1, 0, 0, 3, 2, 0, 0, 3, 2, 1, 1, 0, 0, 1, 1, 0, 0,
                               3, 2, 1, 1, 3, 2, 1, 1, 0, 0, 3, 2, 0, 0, 3, 2, 1, 1, 0, 0, 1, 1, 0, 0, 3, 2, 1, 1, 3, 3, 2, 1, 0, 0, 3, 3};
    static const int dX[72] = {10, 22, 11, 9, 10, 22, 11, 23, 8, 20, 9, 21, 10, 8, 9, 21, 8, 6, 7, 19, 8, 20, 9, 7, 6, 18, 7, 5, 6, 18, 7, 19, 4, 16, 5, 17,
                               6, 4, 5, 17, 4, 2, 3, 15, 4, 16, 5, 3, 2, 14, 3, 1, 2, 14, 3, 15, 0, 12, 1, 13, 2, 0, 1, 13, 0, 12, 10, 11, 0, 12, 1, 13};
    memset(fr, 0, 4 * 24);
    for (int i = 0; i < 72; i++) fr[dW[i]][dX[i]] = (char)(bits[i] & 1);
}

// ---------------------------------------------------------------- GPS text

std::string nmeaChecksum(const std::string& body) {
    uint8_t x = 0;
    for (char c : body) x ^= (uint8_t)c;
    char b[4];
    snprintf(b, sizeof b, "%02X", x);
    return b;
}

static std::vector<std::string> split(const std::string& s, char sep) { return dect2::str::split(s, sep); }

// ddmm.mmmm (or dddmm.mmmm) and a hemisphere letter
static bool coord(const std::string& v, const std::string& h, int degDigits, double& out) {
    if (v.size() < (size_t)degDigits + 2 || h.empty()) return false;
    for (char c : v) if (!(c == '.' || (c >= '0' && c <= '9'))) return false;
    const double deg = atof(v.substr(0, (size_t)degDigits).c_str()), min = atof(v.substr((size_t)degDigits).c_str());
    if (min >= 60) return false;
    out = deg + min / 60.0;
    if (h[0] == 'S' || h[0] == 'W') out = -out;
    else if (h[0] != 'N' && h[0] != 'E') return false;
    return true;
}

bool parseNmea(const std::string& line, Fix& f) {
    if (line.size() < 10 || line[0] != '$') return false;
    const size_t star = line.find('*');
    if (star == std::string::npos || star + 3 > line.size()) return false;
    const std::string body = line.substr(1, star - 1);
    if (nmeaChecksum(body) != line.substr(star + 1, 2)) return false;
    const std::vector<std::string> p = split(body, ',');
    if (p[0].size() != 5) return false;
    const std::string t = p[0].substr(2);
    f = Fix();
    if (t == "RMC" && p.size() >= 9) {
        if (p[2] != "A") return false;
        if (!coord(p[3], p[4], 2, f.lat) || !coord(p[5], p[6], 3, f.lon)) return false;
        if (!p[7].empty()) { f.speedKmh = atof(p[7].c_str()) * 1.852; f.course = (int)std::lround(atof(p[8].c_str())); f.velValid = true; }
        return true;
    }
    if (t == "GGA" && p.size() >= 10) {
        if (p[6].empty() || p[6] == "0") return false;
        if (!coord(p[2], p[3], 2, f.lat) || !coord(p[4], p[5], 3, f.lon)) return false;
        if (!p[9].empty()) { f.altM = atof(p[9].c_str()); f.altValid = true; }
        return true;
    }
    return false;
}

uint16_t dprsCrc(const std::string& s) { return crc((const uint8_t*)s.data(), s.size()); }

bool parseDprs(const std::string& line, Fix& f) {
    if (line.compare(0, 5, "$$CRC") != 0 || line.size() < 11 || line[9] != ',') return false;
    const std::string rest = line.substr(10);
    const unsigned want = (unsigned)strtoul(line.substr(5, 4).c_str(), nullptr, 16);
    if (dprsCrc(rest) != want && dprsCrc(rest + "\r") != want) return false;   // the CRC covers the closing carriage return, which the line may have lost
    const size_t gt = rest.find('>'), colon = rest.find(':');
    if (gt == std::string::npos || colon == std::string::npos || colon + 20 > rest.size()) return false;
    f = Fix();
    f.aprs = true;
    f.call = rest.substr(0, gt);
    std::string pos = rest.substr(colon + 1);
    if (pos[0] != '!' && pos[0] != '=' && pos[0] != '/' && pos[0] != '@') return false;
    pos = pos.substr(pos[0] == '/' || pos[0] == '@' ? 8 : 1);   // a time stamp first with / and @
    if (pos.size() < 19) return false;
    // DDMM.mmN / DDDMM.mmE, the symbol table between, the symbol after
    if (!coord(pos.substr(0, 7), pos.substr(7, 1), 2, f.lat) || !coord(pos.substr(9, 8), pos.substr(17, 1), 3, f.lon)) return false;
    std::string c = pos.size() > 19 ? pos.substr(19) : "";
    if (c.size() >= 7 && c[3] == '/' && isdigit((unsigned char)c[0]) && isdigit((unsigned char)c[4])) {   // course/speed in knots
        f.course = atoi(c.substr(0, 3).c_str()); f.speedKmh = atoi(c.substr(4, 3).c_str()) * 1.852; f.velValid = true;
        c = c.substr(7);
    }
    const size_t a = c.find("/A=");
    if (a != std::string::npos && a + 9 <= c.size()) { f.altM = atof(c.substr(a + 3, 6).c_str()) * 0.3048; f.altValid = true; c.erase(a, 9); }
    while (!c.empty() && (c.back() == '\r' || c.back() == '\n' || c.back() == ' ')) c.pop_back();
    f.comment = c;
    return true;
}

} // namespace dstar
} // namespace dect2
