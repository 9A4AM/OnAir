// JS8 protocol: frame, CRC-12, (174,87) LDPC, tones and the message layer (see js8_proto.h).
#include "dect2/js8/js8_proto.h"
#include "../hfdig/hfdig_ftx_int.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include "dect2/common/bits.h"

namespace dect2 {
namespace js8 {

// ---------------------------------------------------------------- code

uint16_t crc12(const uint8_t bytes[11]) {
    uint32_t reg = 0;
    for (int i = 0; i < 11; i++)
        for (int b = 7; b >= 0; b--) {
            const uint32_t top = reg & 0x800;
            reg = ((reg << 1) | ((bytes[i] >> b) & 1u)) & 0xFFF;
            if (top) reg ^= 0xC06;
        }
    return (uint16_t)(reg ^ 42);
}

static void toBytes(const uint8_t* msg72, int flags, uint8_t* bytes) {
    std::memset(bytes, 0, 11);
    for (int i = 0; i < 72; i++) if (msg72[i]) bytes[i / 8] |= (uint8_t)(0x80 >> (i % 8));
    bytes[9] = (uint8_t)((flags & 7) << 5);
}

void frameBits(const uint8_t* msg72, int flags, uint8_t* bits87) {
    uint8_t bytes[11];
    toBytes(msg72, flags, bytes);
    const uint16_t crc = crc12(bytes);
    for (int i = 0; i < 72; i++) bits87[i] = msg72[i] & 1;
    for (int i = 0; i < 3; i++) bits87[72 + i] = (uint8_t)((flags >> (2 - i)) & 1);
    for (int i = 0; i < 12; i++) bits87[75 + i] = (uint8_t)((crc >> (11 - i)) & 1);
}

bool checkCrc(const uint8_t* bits87) {
    int flags = 0;
    for (int i = 0; i < 3; i++) flags = (flags << 1) | (bits87[72 + i] & 1);
    uint8_t bytes[11];
    toBytes(bits87, flags, bytes);
    uint16_t crc = 0;
    for (int i = 0; i < 12; i++) crc = (uint16_t)((crc << 1) | (bits87[75 + i] & 1));
    return crc == crc12(bytes);
}

static const std::vector<std::vector<uint8_t>>& generator() {
    static const std::vector<std::vector<uint8_t>> g = [] {
        std::vector<std::vector<uint8_t>> m(kM, std::vector<uint8_t>(kK));
        for (int i = 0; i < kM; i++)
            for (int k = 0; k < kK; k++) {
                const char c = kParityHex[i][k / 4];
                const int v = c <= '9' ? c - '0' : c - 'a' + 10;
                m[(size_t)i][(size_t)k] = (uint8_t)((v >> (3 - k % 4)) & 1);
            }
        return m;
    }();
    return g;
}

void encode174(const uint8_t* bits87, uint8_t* cw174) {
    const auto& g = generator();
    for (int i = 0; i < kM; i++) {
        int s = 0;
        for (int k = 0; k < kK; k++) s += g[(size_t)i][(size_t)k] & bits87[k];
        cw174[i] = (uint8_t)(s & 1);
    }
    for (int k = 0; k < kK; k++) cw174[kM + k] = bits87[k] & 1;
}

int ldpcDecode(const float* llr, int iters, uint8_t* cw) {
    float toc[kM][7], tov[kM][7];
    for (int i = 0; i < kM; i++) for (int j = 0; j < kNmCount[i]; j++) { toc[i][j] = llr[kNm[i][j]]; tov[i][j] = 0; }
    float zn[kN];
    int best = kM;
    for (int it = 0; it <= iters; it++) {
        for (int n = 0; n < kN; n++) zn[n] = llr[n];
        for (int i = 0; i < kM; i++) for (int j = 0; j < kNmCount[i]; j++) zn[kNm[i][j]] += tov[i][j];
        for (int n = 0; n < kN; n++) cw[n] = zn[n] > 0;
        int bad = 0;
        for (int i = 0; i < kM; i++) {
            int s = 0;
            for (int j = 0; j < kNmCount[i]; j++) s += cw[kNm[i][j]];
            bad += s & 1;
        }
        best = bad;
        if (bad == 0 || it == iters) break;
        for (int i = 0; i < kM; i++) {
            const int nb = kNmCount[i];
            float t[7];
            for (int j = 0; j < nb; j++) { toc[i][j] = zn[kNm[i][j]] - tov[i][j]; t[j] = std::tanh(-toc[i][j] / 2); }
            for (int j = 0; j < nb; j++) {
                float p = 1;
                for (int k = 0; k < nb; k++) if (k != j) p *= t[k];
                p = std::max(-0.999999f, std::min(0.999999f, p));
                tov[i][j] = -2 * std::atanh(p);
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------- waveform

const Spec& spec(int m) {
    static const Spec s[kSubmodes] = {{"Normal", 1920, 15, 0.5}, {"Fast", 1200, 10, 0.2}, {"Turbo", 600, 6, 0.1}};
    return s[std::max(0, std::min(kSubmodes - 1, m))];
}

const uint8_t* costas(int submode, int which) {
    static const uint8_t orig[7] = {4, 2, 5, 6, 1, 3, 0};
    static const uint8_t mod[3][7] = {{0, 6, 2, 3, 5, 4, 1}, {1, 5, 0, 2, 3, 6, 4}, {2, 5, 0, 6, 4, 1, 3}};
    return submode == kNormal ? orig : mod[std::max(0, std::min(2, which))];
}

std::vector<int> tones(int submode, const uint8_t* bits87) {
    uint8_t cw[kN];
    encode174(bits87, cw);
    std::vector<int> t(kSymbols, 0);
    for (int c = 0; c < 3; c++) for (int k = 0; k < 7; k++) t[(size_t)(36 * c + k)] = costas(submode, c)[k];
    for (int j = 0; j < 58; j++) t[(size_t)dataSymbol(j)] = cw[3 * j] * 4 + cw[3 * j + 1] * 2 + cw[3 * j + 2];
    return t;
}

// ---------------------------------------------------------------- message layer

namespace {
const char* const kAlnum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /@";   // callsign and grid alphabet
const char* const kAlpha72 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+";
constexpr uint32_t kNBaseCall = 37u * 36 * 10 * 27 * 27 * 27;
constexpr uint16_t kNBaseGrid = 180 * 180, kNUserGrid = kNBaseGrid + 10, kNMaxGrid = (1 << 15) - 1;

int alIdx(char c) { const char* p = std::strchr(kAlnum, c); return p && c ? (int)(p - kAlnum) : -1; }

const std::vector<std::pair<const char*, uint32_t>>& baseCalls() {
    static const char* const g[] = {"<....>", "@ALLCALL", "@JS8NET", "@DX/NA", "@DX/SA", "@DX/EU", "@DX/AS", "@DX/AF", "@DX/OC", "@DX/AN",
        "@REGION/1", "@REGION/2", "@REGION/3", "@GROUP/0", "@GROUP/1", "@GROUP/2", "@GROUP/3", "@GROUP/4", "@GROUP/5", "@GROUP/6", "@GROUP/7",
        "@GROUP/8", "@GROUP/9", "@COMMAND", "@CONTROL", "@NET", "@NTS", "@RESERVE/0", "@RESERVE/1", "@RESERVE/2", "@RESERVE/3", "@RESERVE/4",
        "@APRSIS", "@RAGCHEW", "@JS8", "@EMCOMM", "@ARES", "@MARS", "@AMRRON", "@RACES", "@RAYNET", "@RADAR", "@SKYWARN", "@CQ", "@HB", "@QSO",
        "@QSOPARTY", "@CONTEST", "@FIELDDAY", "@SOTA", "@IOTA", "@POTA", "@QRP", "@QRO"};
    static const std::vector<std::pair<const char*, uint32_t>> v = [] {
        std::vector<std::pair<const char*, uint32_t>> r;
        for (size_t i = 0; i < sizeof g / sizeof *g; i++) r.push_back({g[i], kNBaseCall + 1 + (uint32_t)i});
        return r;
    }();
    return v;
}

const char* const kCqs[8] = {"CQ CQ CQ", "CQ DX", "CQ QRP", "CQ CONTEST", "CQ FIELD", "CQ FD", "CQ CQ", "CQ"};

const char* const kCmds[32] = {" SNR?", " DIT DIT", " NACK", " HEARING?", " GRID?", ">", " STATUS?", " STATUS", " HEARING", " MSG", " MSG TO:",
    " QUERY", " QUERY MSGS", " QUERY CALL", " ACK", " GRID", " INFO?", " INFO", " FB", " HW CPY?", " SK", " RR", " QSL?", " QSL", " CMD",
    " SNR", " NO", " YES", " 73", " HEARTBEAT SNR", " AGN?", " "};

const std::vector<std::pair<char, const char*>>& huff() {
    static const std::vector<std::pair<char, const char*>> h = {
        {' ', "01"}, {'E', "100"}, {'T', "1101"}, {'A', "0011"}, {'O', "11111"}, {'I', "11100"}, {'N', "10111"}, {'S', "10100"}, {'H', "00011"},
        {'R', "00000"}, {'D', "111011"}, {'L', "110011"}, {'C', "110001"}, {'U', "101101"}, {'M', "101011"}, {'W', "001011"}, {'F', "001001"},
        {'G', "000101"}, {'Y', "000011"}, {'P', "1111011"}, {'B', "1111001"}, {'.', "1110100"}, {'V', "1100101"}, {'K', "1100100"},
        {'-', "1100001"}, {'+', "1100000"}, {'?', "1011001"}, {'!', "1011000"}, {'X', "1010100"}, {'0', "0010101"}, {'J', "0010100"},
        {'1', "0010001"}, {'Q', "0010000"}, {'2', "0001001"}, {'Z', "0001000"}, {'3', "0000101"}, {'5', "0000100"}, {'4', "11110101"},
        {'9', "11110100"}, {'8', "11110001"}, {'6', "11110000"}, {'7', "11101011"}, {'/', "11101010"}};
    return h;
}

uint64_t getBits(const uint8_t* b, int from, int n) { return bits::getMsb(b, (size_t)from, n); }
void putBits(uint8_t* b, int from, int n, uint64_t v) { bits::putMsb(b, (size_t)from, v, n); }

std::string formatSnr(int snr) {
    char b[16];
    if (snr >= 0) snprintf(b, sizeof b, "+%02d", snr); else snprintf(b, sizeof b, "-%02d", -snr);
    return b;
}

// JS8Call's grid2deg: longitude positive to the WEST, as WSJT
void grid2deg(const std::string& grid, double& dlong, double& dlat) {
    std::string g = grid.substr(0, 4);
    for (char& c : g) c = (char)std::toupper((unsigned char)c);
    g += grid.size() >= 6 ? std::string{(char)std::tolower((unsigned char)grid[4]), (char)std::tolower((unsigned char)grid[5])} : std::string("mm");
    const int nlong = 180 - 20 * (g[0] - 'A');
    const int n20d = 2 * (g[2] - '0');
    const double xminlong = 5 * (g[4] - 'a' + 0.5);
    dlong = nlong - n20d - xminlong / 60.0;
    const int nlat = -90 + 10 * (g[1] - 'A') + g[3] - '0';
    const double xminlat = 2.5 * (g[5] - 'a' + 0.5);
    dlat = nlat + xminlat / 60.0;
}

std::string deg2grid4(double dlong, double dlat) {
    if (dlong < -180) dlong += 360;
    if (dlong > 180) dlong -= 360;
    const int nlong = (int)(60.0 * (180.0 - dlong) / 5);
    const int nlat = (int)(60.0 * (dlat + 90) / 2.5);
    std::string g(4, ' ');
    g[0] = (char)('A' + nlong / 240); g[2] = (char)('0' + (nlong - 240 * (nlong / 240)) / 24);
    g[1] = (char)('A' + nlat / 240); g[3] = (char)('0' + (nlat - 240 * (nlat / 240)) / 24);
    return g;
}

bool validGrid4(const std::string& g) {
    return g.size() >= 4 && g[0] >= 'A' && g[0] <= 'R' && g[1] >= 'A' && g[1] <= 'R' && std::isdigit((unsigned char)g[2]) && std::isdigit((unsigned char)g[3]);
}
} // namespace

std::string bitsToChars(const uint8_t* m) {
    std::string s;
    for (int i = 0; i < 12; i++) s += kAlpha72[getBits(m, 6 * i, 6)];
    return s;
}

bool charsToBits(const std::string& t, uint8_t* m) {
    if (t.size() < 12) return false;
    for (int i = 0; i < 12; i++) {
        const char* p = std::strchr(kAlpha72, t[(size_t)i]);
        if (!p || !t[(size_t)i]) return false;
        putBits(m, 6 * i, 6, (uint64_t)(p - kAlpha72));
    }
    return true;
}

int commandCode(const std::string& cmd) {
    for (int i = 0; i < 32; i++) if (cmd == kCmds[i]) return i;
    if (cmd == "?") return 0;
    if (cmd == " QUERY MSGS?") return 12;
    return -1;
}
const char* commandText(int code) { return code >= 0 && code < 32 ? kCmds[code] : ""; }

uint32_t packCallsign(const std::string& value, bool* portable) {
    std::string cs;
    for (char c : value) if (c != ' ' && c != '\t') cs += (char)std::toupper((unsigned char)c);
    for (const auto& b : baseCalls()) if (cs == b.first) return b.second;
    if (portable) *portable = false;
    if (cs.size() > 2 && cs.compare(cs.size() - 2, 2, "/P") == 0) { cs.resize(cs.size() - 2); if (portable) *portable = true; }
    if (cs.rfind("3DA0", 0) == 0) cs = "3D0" + cs.substr(4);
    if (cs.rfind("3X", 0) == 0 && cs.size() > 2 && cs[2] >= 'A' && cs[2] <= 'Z') cs = "Q" + cs.substr(2);
    const size_t n = cs.size();
    if (n < 2 || n > 6) return 0;
    std::vector<std::string> perm = {cs};
    if (n == 2) perm.push_back(" " + cs + "   ");
    if (n == 3) { perm.push_back(" " + cs + "  "); perm.push_back(cs + "   "); }
    if (n == 4) { perm.push_back(" " + cs + " "); perm.push_back(cs + "  "); }
    if (n == 5) { perm.push_back(" " + cs); perm.push_back(cs + " "); }
    auto fits = [](const std::string& p) {
        if (p.size() != 6) return false;
        auto an = [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'); };
        auto al = [](char c) { return (c >= 'A' && c <= 'Z') || c == ' '; };
        return (an(p[0]) || p[0] == ' ') && an(p[1]) && p[2] >= '0' && p[2] <= '9' && al(p[3]) && al(p[4]) && al(p[5]);
    };
    std::string m;
    for (const auto& p : perm) if (fits(p)) m = p;
    if (m.empty()) return 0;
    uint32_t v = (uint32_t)alIdx(m[0]);
    v = 36 * v + (uint32_t)alIdx(m[1]);
    v = 10 * v + (uint32_t)alIdx(m[2]);
    v = 27 * v + (uint32_t)(alIdx(m[3]) - 10);
    v = 27 * v + (uint32_t)(alIdx(m[4]) - 10);
    v = 27 * v + (uint32_t)(alIdx(m[5]) - 10);
    return v;
}

std::string unpackCallsign(uint32_t v, bool portable) {
    for (const auto& b : baseCalls()) if (b.second == v) return b.first;
    if (v >= kNBaseCall) return "";
    char w[7] = {};
    w[5] = kAlnum[v % 27 + 10]; v /= 27;
    w[4] = kAlnum[v % 27 + 10]; v /= 27;
    w[3] = kAlnum[v % 27 + 10]; v /= 27;
    w[2] = kAlnum[v % 10]; v /= 10;
    w[1] = kAlnum[v % 36]; v /= 36;
    w[0] = kAlnum[std::min<uint32_t>(v, 38)];
    std::string cs(w, 6);
    if (cs.rfind("3D0", 0) == 0) cs = "3DA0" + cs.substr(3);
    if (cs[0] == 'Q' && cs[1] >= 'A' && cs[1] <= 'Z') cs = "3X" + cs.substr(1);
    while (!cs.empty() && cs.front() == ' ') cs.erase(cs.begin());
    while (!cs.empty() && cs.back() == ' ') cs.pop_back();
    if (portable) cs += "/P";
    return cs;
}

uint64_t packAlphaNumeric50(const std::string& value) {
    std::string w;
    for (char c : value) { const char u = (char)std::toupper((unsigned char)c); if (alIdx(u) >= 0) w += u; }
    if (w.size() > 3 && w[3] != '/') w.insert(3, " ");
    if (w.size() > 7 && w[7] != '/') w.insert(7, " ");
    if (w.size() < 11) w += std::string(11 - w.size(), ' ');
    if (w.size() > 11) return 0;
    const uint64_t a = 38, b = 2;
    uint64_t v = (uint64_t)alIdx(w[0]);
    v = v * a + (uint64_t)alIdx(w[1]);
    v = v * a + (uint64_t)alIdx(w[2]);
    v = v * b + (w[3] == '/');
    v = v * a + (uint64_t)alIdx(w[4]);
    v = v * a + (uint64_t)alIdx(w[5]);
    v = v * a + (uint64_t)alIdx(w[6]);
    v = v * b + (w[7] == '/');
    v = v * a + (uint64_t)alIdx(w[8]);
    v = v * a + (uint64_t)alIdx(w[9]);
    v = v * a + (uint64_t)alIdx(w[10]);
    return v;
}

std::string unpackAlphaNumeric50(uint64_t v) {
    char w[11];
    w[10] = kAlnum[v % 38]; v /= 38;
    w[9] = kAlnum[v % 38]; v /= 38;
    w[8] = kAlnum[v % 38]; v /= 38;
    w[7] = v % 2 ? '/' : ' '; v /= 2;
    w[6] = kAlnum[v % 38]; v /= 38;
    w[5] = kAlnum[v % 38]; v /= 38;
    w[4] = kAlnum[v % 38]; v /= 38;
    w[3] = v % 2 ? '/' : ' '; v /= 2;
    w[2] = kAlnum[v % 38]; v /= 38;
    w[1] = kAlnum[v % 38]; v /= 38;
    w[0] = kAlnum[v % 39];
    std::string s;
    for (char c : w) if (c != ' ') s += c;
    return s;
}

uint16_t packGrid(const std::string& grid) {
    std::string g = grid;
    while (!g.empty() && g.back() == ' ') g.pop_back();
    while (!g.empty() && g.front() == ' ') g.erase(g.begin());
    if (g.size() < 4) return kNMaxGrid;
    for (int i = 0; i < 2; i++) g[(size_t)i] = (char)std::toupper((unsigned char)g[(size_t)i]);
    if (!validGrid4(g)) return kNMaxGrid;
    double lo, la;
    grid2deg(g.substr(0, 4), lo, la);
    const int ilong = (int)lo, ilat = (int)(la + 90);
    return (uint16_t)(((ilong + 180) / 2) * 180 + ilat);
}

std::string unpackGrid(uint16_t v) {
    if (v > kNBaseGrid) return "";
    const double dlat = v % 180 - 90;
    const double dlong = v / 180 * 2 - 180 + 2;
    return deg2grid4(dlong, dlat);
}

bool gridToLatLon(const std::string& grid, double& lat, double& lon) { return ftx::gridToLatLon(grid, lat, lon); }

static bool packCompound(int type, const std::string& call, uint16_t num, int bits3, uint8_t* m) {
    const uint64_t c = packAlphaNumeric50(call);
    if (c == 0) return false;
    putBits(m, 0, 3, (uint64_t)type);
    putBits(m, 3, 50, c);
    putBits(m, 53, 11, (uint64_t)(num >> 5));
    putBits(m, 64, 5, (uint64_t)(num & 31));
    putBits(m, 69, 3, (uint64_t)(bits3 & 7));
    return true;
}

bool packHeartbeat(const std::string& call, const std::string& grid, uint8_t* m) { return packCompound(kHeartbeat, call, packGrid(grid), 0, m); }
bool packCq(const std::string& call, const std::string& grid, int cqIndex, uint8_t* m) {
    return packCompound(kHeartbeat, call, (uint16_t)(packGrid(grid) | (1u << 15)), cqIndex & 7, m);
}

bool packDirected(const std::string& from, const std::string& to, int cmdCode, int num, uint8_t* m) {
    bool pf = false, pt = false;
    const uint32_t f = packCallsign(from, &pf), t = packCallsign(to, &pt);
    if (!f || !t || cmdCode < 0 || cmdCode > 31) return false;
    const int inum = (cmdCode == 25 || cmdCode == 29) ? std::max(-30, std::min(31, num)) + 31 : 0;
    putBits(m, 0, 3, kDirected);
    putBits(m, 3, 28, f);
    putBits(m, 31, 28, t);
    putBits(m, 59, 5, (uint64_t)cmdCode);
    putBits(m, 64, 8, (uint64_t)(((int)pf << 7) | ((int)pt << 6) | inum));
    return true;
}

int packData(const std::string& text, uint8_t* m) {
    std::vector<uint8_t> b = {1, 0};
    int taken = 0;
    for (char ch : text) {
        const char u = (char)std::toupper((unsigned char)ch);
        const char* code = nullptr;
        for (const auto& h : huff()) if (h.first == u) code = h.second;
        if (!code) return 0;
        const size_t len = std::strlen(code);
        if (b.size() + len >= 72) break;
        for (size_t i = 0; i < len; i++) b.push_back(code[i] == '1');
        taken++;
    }
    for (size_t i = 0; b.size() < 72; i++) b.push_back(i == 0 ? 0 : 1);
    std::memcpy(m, b.data(), 72);
    return taken;
}

std::vector<TxFrame> buildMessage(const std::string& from, const std::string& to, int cmdCode, int num, const std::string& text) {
    std::vector<TxFrame> v;
    TxFrame f{};
    if (!packDirected(from, to, cmdCode < 0 ? 31 : cmdCode, num, f.bits)) return v;
    f.flags = kFirst;
    v.push_back(f);
    std::string rest = text;
    while (!rest.empty()) {
        TxFrame d{};
        const int n = packData(rest, d.bits);
        if (n <= 0) break;
        d.flags = 0;
        v.push_back(d);
        rest = rest.substr((size_t)n);
    }
    v.back().flags |= kLast;
    return v;
}

Frame unpackFrame(const uint8_t* m, int flags) {
    Frame f;
    f.flags = flags;
    if (flags & kFastData) { f.type = kData; f.compressed = true; return f; }
    if (m[0]) {   // data: 1, compressed flag, the code, a 0 and ones to the end
        f.type = kData;
        f.compressed = m[1] != 0;
        int last0 = -1;
        for (int i = 1; i < 72; i++) if (!m[i]) last0 = i;
        if (f.compressed) return f;
        std::string bits;
        for (int i = 2; i < last0; i++) bits += m[i] ? '1' : '0';
        size_t p = 0;
        while (p < bits.size()) {
            bool found = false;
            for (const auto& h : huff()) {
                const size_t len = std::strlen(h.second);
                if (bits.compare(p, len, h.second) == 0) { f.text += h.first; p += len; found = true; break; }
            }
            if (!found) break;
        }
        return f;
    }
    const int type = (int)getBits(m, 0, 3);
    if (type == kDirected) {
        const uint32_t from = (uint32_t)getBits(m, 3, 28), to = (uint32_t)getBits(m, 31, 28);
        const int cmd = (int)getBits(m, 59, 5);
        int extra = (int)getBits(m, 64, 8);
        const bool pf = (extra >> 7) & 1, pt = (extra >> 6) & 1;
        extra %= 64;
        f.type = kDirected;
        f.from = unpackCallsign(from, pf);
        f.to = unpackCallsign(to, pt);
        f.cmdCode = cmd;
        f.cmd = kCmds[cmd];
        if (extra != 0) f.num = (cmd == 25 || cmd == 29) ? formatSnr(extra - 31) : std::to_string(extra - 31);
        if (f.from.empty() || f.to.empty()) f.type = -1;
        return f;
    }
    // heartbeat / compound: a 50-bit callsign and a 16-bit number (grid or command)
    const uint64_t c = getBits(m, 3, 50);
    const uint16_t num = (uint16_t)((getBits(m, 53, 11) << 5) | getBits(m, 64, 5));
    const int bits3 = (int)getBits(m, 69, 3);
    f.from = unpackAlphaNumeric50(c);
    if (f.from.empty()) return f;
    f.type = type;
    if (type == kHeartbeat) {
        f.grid = unpackGrid(num & 0x7FFF);
        f.cq = (num >> 15) & 1;
        if (f.cq) f.cqText = kCqs[bits3];
    } else {
        if (num <= kNBaseGrid) f.grid = unpackGrid(num);
        else if (num >= kNUserGrid && num < kNMaxGrid) {
            const int v = num - kNUserGrid;
            if (v & 0x80) { f.cmdCode = (v & 0x40) ? 29 : 25; f.num = formatSnr((v & 0x3F) - 31); }
            else f.cmdCode = v & 0x7F;
            f.cmd = commandText(f.cmdCode);
        }
    }
    return f;
}

std::string Frame::display() const {
    switch (type) {
    case kHeartbeat:
        return from + ": " + (cq ? "@ALLCALL " + cqText : std::string("@HB HEARTBEAT")) + (grid.empty() ? "" : " " + grid);
    case kCompound:
    case kCompoundDirected:
        return from + ":" + (grid.empty() ? "" : " " + grid) + cmd + (num.empty() ? "" : " " + num);
    case kDirected:
        return from + ": " + to + cmd + (num.empty() ? "" : " " + num);
    case kData:
        return compressed ? "[compressed]" : text;
    default:
        return "";
    }
}

} // namespace js8
} // namespace dect2
