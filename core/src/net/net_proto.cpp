// The C++ implementation of net_proto.h (compiled out when DECT2_RUST_NET is set: core/src/net/net_proto_rs.cpp then calls the Rust crate onair_net).
#include "dect2/net/net_proto.h"
#ifndef DECT2_RUST_NET
#include "dect2/common/str.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace dect2::netp {

// ------------------------------------------------------------------ SHA-1, base64 (RFC 3174, RFC 4648): for the WebSocket handshake
std::string sha1(const std::string& data) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::string m = data;
    const uint64_t bits = (uint64_t)data.size() * 8;
    m.push_back((char)0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; i--) m.push_back((char)((bits >> (8 * i)) & 0xFF));
    auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for (size_t o = 0; o < m.size(); o += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)(uint8_t)m[o + 4 * i] << 24 | (uint32_t)(uint8_t)m[o + 4 * i + 1] << 16 | (uint32_t)(uint8_t)m[o + 4 * i + 2] << 8 | (uint32_t)(uint8_t)m[o + 4 * i + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::string out;
    for (int i = 0; i < 5; i++) for (int j = 3; j >= 0; j--) out.push_back((char)((h[i] >> (8 * j)) & 0xFF));
    return out;
}

std::string base64(const std::string& d) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    size_t i = 0;
    for (; i + 2 < d.size(); i += 3) {
        const uint32_t v = (uint32_t)(uint8_t)d[i] << 16 | (uint32_t)(uint8_t)d[i + 1] << 8 | (uint8_t)d[i + 2];
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
    }
    if (i + 1 == d.size()) {
        const uint32_t v = (uint32_t)(uint8_t)d[i] << 16;
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "==";
    } else if (i + 2 == d.size()) {
        const uint32_t v = (uint32_t)(uint8_t)d[i] << 16 | (uint32_t)(uint8_t)d[i + 1] << 8;
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '=';
    }
    return o;
}

std::string wsAcceptKey(const std::string& key) { return base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")); }

std::string wsFrame(int opcode, const std::string& p) {
    std::string f;
    f.push_back((char)(0x80 | (opcode & 0x0F)));
    if (p.size() < 126) f.push_back((char)p.size());
    else if (p.size() < 65536) { f.push_back(126); f.push_back((char)(p.size() >> 8)); f.push_back((char)(p.size() & 0xFF)); }
    else { f.push_back(127); for (int i = 7; i >= 0; i--) f.push_back((char)(((uint64_t)p.size() >> (8 * i)) & 0xFF)); }
    return f + p;
}

WsClient wsParseClient(const std::string& in) {
    constexpr size_t kMaxWsFrame = 4096;
    WsClient r;
    if (in.size() < 2) return r;
    const uint8_t b0 = (uint8_t)in[0], b1 = (uint8_t)in[1];
    const int op = b0 & 0x0F;
    const bool fin = (b0 & 0x80) != 0, masked = (b1 & 0x80) != 0;
    uint64_t len = b1 & 0x7F;
    size_t hl = 2;
    if (len == 126) { if (in.size() < 4) return r; len = (uint64_t)(uint8_t)in[2] << 8 | (uint8_t)in[3]; hl = 4; }
    else if (len == 127) { r.kind = 1; r.code = 1009; return r; }   // 64-bit lengths are never this small
    if (!masked) { r.kind = 1; r.code = 1002; return r; }           // RFC 6455 5.1: clients mask
    if (len > kMaxWsFrame) { r.kind = 1; r.code = 1009; return r; }
    if (in.size() < hl + 4 + len) return r;
    const uint8_t* mk = (const uint8_t*)in.data() + hl;
    std::string pl((size_t)len, 0);
    for (size_t i = 0; i < len; i++) pl[i] = (char)((uint8_t)in[hl + 4 + i] ^ mk[i & 3]);
    if (!fin || op == 0 || op == 2 || (op > 2 && op < 8) || op > 10) { r.kind = 1; r.code = 1003; return r; }   // no fragmentation, no binary
    r.kind = 2; r.op = op; r.fin = fin; r.used = hl + 4 + (size_t)len; r.payload = std::move(pl);
    return r;
}

// ------------------------------------------------------------------ URL and header text
std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) { o.push_back((char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
        else if (s[i] == '+') o.push_back(' ');
        else o.push_back(s[i]);
    }
    return o;
}
std::string queryParam(const std::string& q, const std::string& key) {
    size_t i = 0;
    while (i <= q.size()) {
        size_t e = q.find('&', i);
        if (e == std::string::npos) e = q.size();
        const std::string kv = q.substr(i, e - i);
        const size_t eq = kv.find('=');
        if (urlDecode(kv.substr(0, eq)) == key) return eq == std::string::npos ? "" : urlDecode(kv.substr(eq + 1));
        i = e + 1;
    }
    return "";
}
std::string hostOnly(const std::string& h) {
    if (!h.empty() && h[0] == '[') { const size_t e = h.find(']'); return e == std::string::npos ? h : h.substr(0, e + 1); }
    const size_t c = h.rfind(':');
    return c == std::string::npos ? h : h.substr(0, c);
}
bool loopbackName(const std::string& n) {
    if (n == "localhost" || n == "[::1]") return true;
    if (n.compare(0, 4, "127.") != 0) return false;
    for (char c : n) if (!isdigit((unsigned char)c) && c != '.') return false;
    return true;
}
bool ctEqual(const std::string& a, const std::string& b) {
    unsigned char d = a.size() == b.size() ? 0 : 1;
    for (size_t i = 0; i < std::min(a.size(), b.size()); i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}
std::string statusText(int c) {
    switch (c) {
    case 200: return "OK"; case 101: return "Switching Protocols"; case 400: return "Bad Request"; case 401: return "Unauthorized";
    case 403: return "Forbidden"; case 404: return "Not Found"; case 405: return "Method Not Allowed"; case 408: return "Request Timeout";
    case 411: return "Length Required"; case 413: return "Payload Too Large"; case 415: return "Unsupported Media Type";
    case 426: return "Upgrade Required"; case 431: return "Request Header Fields Too Large"; case 501: return "Not Implemented";
    case 503: return "Service Unavailable"; default: return "Error";
    }
}

bool parseFlatJson(const std::string& t, std::map<std::string, JsonVal>& out) {
    out.clear();
    size_t i = 0;
    auto ws = [&] { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) i++; };
    auto str = [&](std::string& s) {
        if (i >= t.size() || t[i] != '"') return false;
        i++;
        s.clear();
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if ((unsigned char)c < 0x20) return false;
            if (c == '\\') {
                if (i >= t.size()) return false;
                const char e = t[i++];
                switch (e) {
                case '"': case '\\': case '/': s.push_back(e); break;
                case 'b': s.push_back('\b'); break;
                case 'f': s.push_back('\f'); break;
                case 'n': s.push_back('\n'); break;
                case 'r': s.push_back('\r'); break;
                case 't': s.push_back('\t'); break;
                case 'u': {
                    if (i + 4 > t.size()) return false;
                    unsigned v = 0;
                    for (int k = 0; k < 4; k++) {
                        const char h = t[i++];
                        if (!isxdigit((unsigned char)h)) return false;
                        v = v * 16 + (unsigned)(isdigit((unsigned char)h) ? h - '0' : (tolower(h) - 'a' + 10));
                    }
                    if (v < 0x80) s.push_back((char)v);
                    else if (v < 0x800) { s.push_back((char)(0xC0 | (v >> 6))); s.push_back((char)(0x80 | (v & 63))); }
                    else { s.push_back((char)(0xE0 | (v >> 12))); s.push_back((char)(0x80 | ((v >> 6) & 63))); s.push_back((char)(0x80 | (v & 63))); }
                    break;
                }
                default: return false;
                }
            } else s.push_back(c);
        }
        if (i >= t.size()) return false;
        i++;
        return true;
    };
    ws();
    if (i >= t.size() || t[i] != '{') return false;
    i++;
    ws();
    if (i < t.size() && t[i] == '}') { i++; ws(); return i == t.size(); }
    while (true) {
        ws();
        std::string k;
        if (!str(k)) return false;
        ws();
        if (i >= t.size() || t[i] != ':') return false;
        i++;
        ws();
        JsonVal v;
        if (i < t.size() && t[i] == '"') { v.kind = JsonVal::Str; if (!str(v.str)) return false; }
        else if (t.compare(i, 4, "true") == 0) { v.kind = JsonVal::Bool; v.b = true; i += 4; }
        else if (t.compare(i, 5, "false") == 0) { v.kind = JsonVal::Bool; v.b = false; i += 5; }
        else if (t.compare(i, 4, "null") == 0) { v.kind = JsonVal::Null; i += 4; }
        else {
            size_t j = i;
            while (j < t.size() && (isdigit((unsigned char)t[j]) || t[j] == '-' || t[j] == '+' || t[j] == '.' || t[j] == 'e' || t[j] == 'E')) j++;
            if (j == i || j - i > 32) return false;
            const std::string num = t.substr(i, j - i);
            char* end = nullptr;
            v.num = strtod(num.c_str(), &end);
            if (!end || *end || !std::isfinite(v.num)) return false;
            v.kind = JsonVal::Num;
            i = j;
        }
        if (out.size() >= 32 && !out.count(k)) return false;
        out[k] = v;
        ws();
        if (i < t.size() && t[i] == ',') { i++; continue; }
        if (i < t.size() && t[i] == '}') { i++; break; }
        return false;
    }
    ws();
    return i == t.size();
}

// ------------------------------------------------------------------ HTTP request head
namespace {
constexpr size_t kMaxHeader = 8192, kMaxBody = 4096;
std::string lower(std::string s) { return dect2::str::lower(std::move(s)); }
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}
HttpError err(int status, const char* msg) { HttpError e; e.status = status; e.msg = msg; return e; }
} // namespace

int httpScan(const std::string& buf, size_t& end) {
    const size_t hdrEnd = buf.find("\r\n\r\n");
    end = hdrEnd == std::string::npos ? 0 : hdrEnd;
    if (hdrEnd == std::string::npos) return buf.size() > kMaxHeader ? 2 : 0;
    return hdrEnd > kMaxHeader ? 2 : 1;
}

HttpError httpHead(const std::string& head, HttpHead& rq) {
    size_t pos = head.find("\r\n");
    const std::string line = head.substr(0, pos);
    const size_t s1 = line.find(' '), s2 = line.rfind(' ');
    if (s1 == std::string::npos || s2 == s1 || line.compare(s2 + 1, 5, "HTTP/") != 0) return err(400, "bad request line");
    rq.method = line.substr(0, s1);
    std::string target = line.substr(s1 + 1, s2 - s1 - 1);
    if (target.empty() || target[0] != '/' || target.size() > 512) return err(400, "bad target");
    for (unsigned char c : target) if (c < 0x21 || c == 0x7f) return err(400, "bad target");
    const size_t q = target.find('?');
    rq.path = target.substr(0, q);
    rq.query = q == std::string::npos ? "" : target.substr(q + 1);
    int nh = 0;
    while (pos != std::string::npos && pos < head.size()) {
        const size_t nx = head.find("\r\n", pos + 2);
        const std::string l = head.substr(pos + 2, nx == std::string::npos ? std::string::npos : nx - pos - 2);
        pos = nx;
        if (l.empty()) continue;
        const size_t c = l.find(':');
        if (c == std::string::npos || c == 0 || ++nh > 48) return err(400, "bad header");
        const std::string name = lower(trim(l.substr(0, c)));
        if (rq.h.count(name) && (name == "content-length" || name == "host")) return err(400, "duplicate header");
        rq.h[name] = trim(l.substr(c + 1));
    }
    if (rq.h.count("transfer-encoding")) return err(501, "chunked bodies are not supported");
    if (rq.h.count("content-length")) {
        const std::string& cl = rq.h["content-length"];
        if (cl.empty() || cl.size() > 9 || !std::all_of(cl.begin(), cl.end(), [](char c) { return isdigit((unsigned char)c) != 0; })) return err(400, "bad Content-Length");
        rq.contentLength = (size_t)atol(cl.c_str());
        if (rq.contentLength > kMaxBody) return err(413, "body too large (limit 4096 bytes)");
    } else if (rq.method == "POST") return err(411, "Content-Length required");
    return HttpError();
}

// ------------------------------------------------------------------ rtl_tcp
bool rtlGreeting(const unsigned char* hdr, size_t n, RtlGreeting& g) {
    if (n < 12) return false;
    auto be32 = [](const unsigned char* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; };
    for (int i = 0; i < 4; i++) if (hdr[i] < 0x20 || hdr[i] > 0x7e) return false;
    const uint32_t tuner = be32(hdr + 4), nGains = be32(hdr + 8);
    if (tuner > 0xffff || nGains > 0xffff) return false;   // not a greeting
    g.magic.assign((const char*)hdr, 4);
    g.tuner = tuner;
    g.nGains = nGains;
    g.other = g.magic != "RTL0" || tuner < 1 || tuner > 6;
    return true;
}
std::vector<int> rtlTunerGains(int tuner) {
    switch (tuner) {
    case 1: return {-10, 15, 40, 65, 90, 115, 140, 165, 190, 215, 240, 290, 340, 420};
    case 2: return {-99, -40, 71, 179, 192};
    case 3: return {-99, -73, -65, -63, -60, -58, -54, 58, 61, 63, 65, 67, 68, 70, 71, 179, 181, 182, 184, 186, 188, 191, 197};
    case 4: return {0};
    default: return {0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254, 280, 297, 328, 338, 364, 372, 386, 402, 421, 434, 439, 445, 480, 496};
    }
}
std::string rtlTunerName(int t) {
    switch (t) {
    case 1: return "E4000"; case 2: return "FC0012"; case 3: return "FC0013"; case 4: return "FC2580";
    case 5: return "R820T"; case 6: return "R828D"; default: return "unknown tuner";
    }
}
std::string rtlCommand(uint8_t cmd, uint32_t v) {
    const char c[5] = {(char)cmd, (char)(v >> 24), (char)(v >> 16), (char)(v >> 8), (char)v};
    return std::string(c, 5);
}
bool splitHostPort(const std::string& a, std::string& host, int& port) {
    const size_t c = a.rfind(':');
    if (c == std::string::npos || c == 0) return false;
    host = a.substr(0, c);
    if (host.size() > 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);   // [::1]:1234
    port = atoi(a.c_str() + c + 1);
    return port > 0 && port < 65536;
}

// ------------------------------------------------------------------ SpyServer
namespace {
uint32_t le32(const unsigned char* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
void put32(std::string& s, uint32_t v) { for (int k = 0; k < 4; k++) s.push_back((char)(v >> (8 * k))); }
} // namespace

void spyDeviceInfo(const unsigned char* b, uint32_t n, uint32_t out[12]) {
    for (uint32_t k = 0; k < 12; k++) out[k] = 0;
    for (uint32_t k = 0; k < 12 && 4 * k + 4 <= n; k++) out[k] = le32(b + 4 * k);
}
void spyClientSync(const unsigned char* b, uint32_t n, uint32_t out[9]) {
    for (uint32_t k = 0; k < 9; k++) out[k] = 0;
    for (uint32_t k = 0; k < 9 && 4 * k + 4 <= n; k++) out[k] = le32(b + 4 * k);
}
std::string spyCommand(uint32_t type, const unsigned char* body, uint32_t n) {
    std::string b;
    put32(b, type);
    put32(b, n);
    if (n) b.append((const char*)body, n);
    return b;
}
std::string spyHello() {
    static const char kName[] = "OnAir";
    std::string b;
    put32(b, (2u << 24) | (0u << 16) | 1700u);
    b.append(kName, sizeof kName - 1);
    return spyCommand(0, (const unsigned char*)b.data(), (uint32_t)b.size());
}
std::string spySetSetting(uint32_t id, uint32_t value) {
    std::string b;
    put32(b, id);
    put32(b, value);
    return spyCommand(2, (const unsigned char*)b.data(), (uint32_t)b.size());
}
bool spyHeader(const unsigned char* h, uint32_t& type, uint32_t& flags, uint32_t& size) {
    const uint32_t mt = le32(h + 4), sz = le32(h + 16);
    if (sz > (1u << 20)) return false;
    type = mt & 0xffff;
    flags = mt >> 16;
    size = sz;
    return true;
}
std::vector<double> spyRates(uint32_t maxRate, uint32_t decStages, uint32_t minDec) {
    std::vector<double> r;
    const uint32_t stages = std::max<uint32_t>(decStages, minDec + 1);
    for (uint32_t d = minDec; d < stages && d < 20; d++) r.push_back((double)maxRate / (double)(1u << d));
    return r;
}
std::string spyDevName(uint32_t t) {
    switch (t) { case 1: return "Airspy"; case 2: return "Airspy HF+"; case 3: return "RTL-SDR"; default: return "radio"; }
}

// ------------------------------------------------------------------ SDR++ server
namespace {
enum : uint8_t { kElStep = 0, kElBool = 1, kElInt = 2, kElFloat = 3, kElString = 4 };
enum : uint8_t {
    kStFillWidth = 0x00, kStSameLine, kStBeginDisabled, kStEndDisabled,
    kStCombo = 0x80, kStButton, kStColumns, kStNextColumn, kStRadio, kStBeginGroup, kStEndGroup, kStLeftLabel, kStSliderInt,
    kStSliderFloatSteps, kStInputInt, kStCheckbox, kStSliderFloat, kStInputText, kStText, kStTextColored,
};
constexpr uint32_t kMaxPacket = 32u << 20;
constexpr const char* kServerSourceId = "##sdrpp_server_src_sel";
uint16_t le16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }
float lef32(const unsigned char* p) { float f; uint32_t u = le32(p); memcpy(&f, &u, 4); return f; }

std::string hiddenPart(const std::string& id) { const size_t p = id.find("##"); return p == std::string::npos ? std::string() : id.substr(p + 2); }
std::string visiblePart(const std::string& id) { return id.substr(0, id.find("##")); }

// the words of a widget's hidden id, without the module's prefix and the source's instance name: "_airspy_lna_gain_Airspy Source" -> lna, gain
std::vector<std::string> idWords(const std::string& id) {
    std::vector<std::string> w;
    std::string cur;
    for (char c : hiddenPart(id) + "_") {
        if (c == '_') { if (!cur.empty()) w.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!w.empty() && (w.back().find(' ') != std::string::npos || std::any_of(w.back().begin(), w.back().end(), [](char c) { return isupper((unsigned char)c); })))
        w.pop_back();   // the instance name ("Airspy Source")
    if (w.size() > 1) w.erase(w.begin());   // the module ("airspy", "rtlsdr", ...)
    for (auto& s : w) s = lower(s);
    return w;
}
bool hasWord(const std::vector<std::string>& w, const char* x) { return std::find(w.begin(), w.end(), x) != w.end(); }

// a readable name for a control that has no label of its own
std::string labelFromId(const std::string& id) {
    if (id == kServerSourceId) return "Server source";
    static const std::map<std::string, std::string> kWords = {
        {"dev", "Device"}, {"sr", "Sample rate"}, {"bw", "Bandwidth"}, {"ant", "Antenna"}, {"gm", "Gain mode"}, {"clk", "Clock"},
        {"ch", "Channel"}, {"chan", "Channel"}, {"ds", "Direct sampling"}, {"agc", "AGC"}, {"port", "Port"}, {"lna", "LNA"}, {"vga", "VGA"},
        {"mix", "Mixer"}, {"att", "Attenuator"}, {"atten", "Attenuator"}, {"attn", "Attenuator"}, {"ppm", "PPM"}, {"biast", "Bias tee"},
        {"ofs", "Offset tuning"}, {"rtl", "RTL"}, {"lin", "Linear"}, {"sens", "Sensitive"}, {"refr", "Refresh"}, {"ref", "Reference"},
    };
    std::string s;
    for (const auto& w : idWords(id)) {
        if (w == "sel" || w == "select") continue;
        const auto it = kWords.find(w);
        s += (s.empty() ? "" : " ") + (it != kWords.end() ? it->second : w);
    }
    if (s.empty()) s = hiddenPart(id);
    if (!s.empty()) s[0] = (char)toupper((unsigned char)s[0]);
    return s;
}

struct Elem { uint8_t type = 0, step = 0; bool b = false; int32_t i = 0; float f = 0; std::string s; };

// the draw list's elements as SmGui::DrawList::loadItem reads them; false when it is cut short or holds an unknown element type
bool parseElems(const unsigned char* p, size_t n, std::vector<Elem>& out) {
    out.clear();
    size_t k = 0;
    while (k < n) {
        Elem e;
        e.type = p[k++];
        switch (e.type) {
        case kElStep: if (k + 2 > n) return false; e.step = p[k]; k += 2; break;   // the step, and its "force sync" flag
        case kElBool: if (k + 1 > n) return false; e.b = p[k] != 0; k += 1; break;
        case kElInt: if (k + 4 > n) return false; e.i = (int32_t)le32(p + k); k += 4; break;
        case kElFloat: if (k + 4 > n) return false; e.f = lef32(p + k); k += 4; break;
        case kElString: {
            if (k + 2 > n) return false;
            const size_t len = le16(p + k);
            k += 2;
            if (k + len > n) return false;
            e.s.assign((const char*)p + k, len);
            k += len;
            break;
        }
        default: return false;
        }
        out.push_back(std::move(e));
    }
    return true;
}
} // namespace

std::string sdrppLabelFromId(const std::string& id) { return labelFromId(id); }

double sdrppRateText(const std::string& t) {
    const char* s = t.c_str();
    while (*s == ' ') s++;
    char* end = nullptr;
    const double v = strtod(s, &end);
    if (end == s || !(v > 0)) return 0;
    std::string unit = lower(end);
    unit.erase(std::remove(unit.begin(), unit.end(), ' '), unit.end());
    double mul = 1;
    if (!unit.empty() && (unit[0] == 'k' || unit[0] == 'm' || unit[0] == 'g')) { mul = unit[0] == 'k' ? 1e3 : unit[0] == 'm' ? 1e6 : 1e9; unit.erase(0, 1); }
    if (!(unit.empty() || unit == "hz" || unit == "sps" || unit == "s/s")) return 0;
    return v * mul;
}

unsigned sdrppIdFlags(const std::string& id) {
    const auto w = idWords(id);
    unsigned f = 0;
    if (hasWord(w, "sr")) f |= kWordSr;
    if (hasWord(w, "bw")) f |= kWordBw;
    if (hasWord(w, "lna")) f |= kWordLna;
    if (hasWord(w, "vga")) f |= kWordVga;
    if (hasWord(w, "mix")) f |= kWordMix;
    if (hasWord(w, "mixer")) f |= kWordMixer;
    if (hasWord(w, "if")) f |= kWordIf;
    if (hasWord(w, "lin")) f |= kWordLin;
    if (hasWord(w, "sens")) f |= kWordSens;
    return f;
}

bool sdrppParseUi(const unsigned char* p, size_t n, std::vector<UiWidget>& out) {
    out.clear();
    std::vector<Elem> el;
    if (!parseElems(p, n, el)) return false;
    int disabled = 0;
    std::string leftLabel;
    for (size_t k = 0; k < el.size();) {
        if (el[k].type != kElStep) return false;   // every step is followed by its arguments, none of which is a step
        const uint8_t st = el[k].step;
        size_t a = ++k;
        while (k < el.size() && el[k].type != kElStep) k++;
        const size_t na = k - a;
        auto is = [&](size_t j, uint8_t t) { return j < na && el[a + j].type == t; };
        UiWidget w;
        bool widget = false;
        switch (st) {
        case kStBeginDisabled: disabled++; break;
        case kStEndDisabled: disabled = std::max(0, disabled - 1); break;
        case kStLeftLabel: if (is(0, kElString)) leftLabel = el[a].s; break;
        case kStCombo:
            if (!(is(0, kElString) && is(1, kElInt) && is(2, kElString))) break;
            w.kind = UiWidget::Combo; w.id = el[a].s; w.i = el[a + 1].i;
            for (size_t q = 0; q < el[a + 2].s.size();) {   // ImGui's "a\0b\0c\0" list
                const size_t z = el[a + 2].s.find('\0', q);
                const std::string item = el[a + 2].s.substr(q, z == std::string::npos ? std::string::npos : z - q);
                if (item.empty() && z == std::string::npos) break;
                w.items.push_back(item);
                if (z == std::string::npos) break;
                q = z + 1;
            }
            widget = true;
            break;
        case kStCheckbox:
            if (!(is(0, kElString) && is(1, kElBool))) break;
            w.kind = UiWidget::Checkbox; w.id = el[a].s; w.b = el[a + 1].b; widget = true;
            break;
        case kStRadio:
            if (!(is(0, kElString) && is(1, kElBool))) break;
            w.kind = UiWidget::Radio; w.id = el[a].s; w.b = el[a + 1].b; widget = true;
            break;
        case kStSliderInt:
            if (!(is(0, kElString) && is(1, kElInt) && is(2, kElInt) && is(3, kElInt))) break;
            w.kind = UiWidget::SliderInt; w.id = el[a].s; w.value = el[a + 1].i; w.minV = el[a + 2].i; w.maxV = el[a + 3].i; w.step = 1;
            widget = true;
            break;
        case kStSliderFloatSteps:
            if (!(is(0, kElString) && is(1, kElFloat) && is(2, kElFloat) && is(3, kElFloat) && is(4, kElFloat))) break;
            w.kind = UiWidget::SliderFloat; w.id = el[a].s; w.value = el[a + 1].f; w.minV = el[a + 2].f; w.maxV = el[a + 3].f; w.step = el[a + 4].f;
            widget = true;
            break;
        case kStSliderFloat:
            if (!(is(0, kElString) && is(1, kElFloat) && is(2, kElFloat) && is(3, kElFloat))) break;
            w.kind = UiWidget::SliderFloat; w.id = el[a].s; w.value = el[a + 1].f; w.minV = el[a + 2].f; w.maxV = el[a + 3].f; w.step = 0;
            widget = true;
            break;
        case kStInputInt:
            if (!(is(0, kElString) && is(1, kElInt) && is(2, kElInt))) break;
            w.kind = UiWidget::InputInt; w.id = el[a].s; w.value = el[a + 1].i; w.step = std::max(1, el[a + 2].i); widget = true;
            break;
        case kStText: case kStTextColored:
            for (size_t j = 0; j < na; j++) if (el[a + j].type == kElString && !el[a + j].s.empty()) { w.kind = UiWidget::Text; w.label = el[a + j].s; widget = true; }
            break;
        default: break;   // layout steps (columns, groups, tables, popups), buttons, text inputs: nothing to drive
        }
        if (widget) {
            w.disabled = disabled > 0;
            if (w.kind != UiWidget::Text) {
                const std::string vis = visiblePart(w.id);
                w.label = !leftLabel.empty() ? leftLabel : !vis.empty() && w.kind != UiWidget::Radio ? vis : labelFromId(w.id);
            }
            out.push_back(std::move(w));
        }
        if (st != kStLeftLabel && st != kStFillWidth && st != kStSameLine && st != kStBeginDisabled && st != kStEndDisabled) leftLabel.clear();
    }
    return true;
}

std::string sdrppCommand(uint32_t cmd, const unsigned char* arg, size_t n) {
    std::string b;
    put32(b, 0);
    put32(b, (uint32_t)(12 + n));
    put32(b, cmd);
    if (n) b.append((const char*)arg, n);
    return b;
}
std::string sdrppUiAction(const std::string& id, uint8_t type, int32_t i, float f, bool b, bool sync) {
    std::string a;
    a.push_back(sync ? 1 : 0);
    a.push_back((char)kElString);
    const size_t len = std::min<size_t>(id.size(), 0xffff);
    a.push_back((char)(unsigned char)len); a.push_back((char)(unsigned char)(len >> 8));
    a.append(id, 0, len);
    a.push_back((char)type);
    if (type == kElBool) a.push_back(b ? 1 : 0);
    else if (type == kElInt) put32(a, (uint32_t)i);
    else { uint32_t u; memcpy(&u, &f, 4); put32(a, u); }
    return a;
}
bool sdrppPacketHeader(const unsigned char* h, uint32_t& type, uint32_t& bodySize) {
    const uint32_t t = le32(h), size = le32(h + 4);
    if (size < 8 || size > kMaxPacket || t > 6) return false;
    type = t;
    bodySize = size - 8;
    return true;
}
bool sdrppErrorText(uint8_t c, std::string& text) {
    static const char* what[] = {"none", "invalid packet", "invalid command", "invalid argument"};
    if (c >= 4) return false;
    text = what[c];
    return true;
}

// ------------------------------------------------------------------ network radios
namespace {
const char* typeKey(uint32_t t) {
    return t == 0 ? "pluto" : t == 2 ? "soapyremote" : t == 3 ? "spyserver" : t == 4 ? "hl2" : t == 5 ? "redpitaya" : t == 6 ? "sdrpp" : "rtltcp";
}
} // namespace

std::string cleanHost(const std::string& h) {
    std::string r;
    for (char c : h) if (c != ' ' && c != '\t' && c != ',' && c != ';') r += c;
    for (const char* pre : {"ip:", "tcp://"}) if (r.rfind(pre, 0) == 0) r.erase(0, strlen(pre));
    return r;
}
std::string radioTypeName(uint32_t t) {
    return t == 0 ? "PlutoSDR" : t == 2 ? "SoapyRemote" : t == 3 ? "SpyServer" : t == 4 ? "Hermes Lite 2" : t == 5 ? "Red Pitaya" : t == 6 ? "SDR++ server" : "rtl_tcp";
}
int radioDefaultPort(uint32_t t) { return t == 0 ? 0 : t == 2 ? 55132 : t == 3 ? 5555 : (t == 4 || t == 5) ? 1024 : t == 6 ? 5259 : 1234; }
std::string radioDefaultHost(uint32_t t) { return t == 0 ? "192.168.2.1" : ""; }
int radioPort(uint32_t t, int port) {
    if (port > 0) return port;
    return t == 0 ? 30431 : radioDefaultPort(t);
}

std::string radiosToString(const std::vector<Radio>& v) {
    std::string s;
    for (const auto& r : v) {
        const std::string h = cleanHost(r.host);
        if (h.empty()) continue;
        std::string rem;
        if (r.kind == 2) for (char c : r.remote) if (c != ';') rem += c;
        s += (s.empty() ? "" : ";") + std::string(typeKey(r.kind)) + "," + h + "," + std::to_string(r.port > 0 ? r.port : 0) + (rem.empty() ? "" : "," + rem);
    }
    return s;
}

std::vector<Radio> radiosFromString(const std::string& s) {
    std::vector<Radio> v;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t e = s.find(';', pos);
        if (e == std::string::npos) e = s.size();
        const std::string item = s.substr(pos, e - pos);
        pos = e + 1;
        const size_t c1 = item.find(','), c2 = c1 == std::string::npos ? c1 : item.find(',', c1 + 1);
        const size_t c3 = c2 == std::string::npos ? c2 : item.find(',', c2 + 1);   // a SoapyRemote's "remote driver" follows (it may hold commas)
        if (c2 == std::string::npos) continue;
        const std::string t = item.substr(0, c1);
        Radio r;
        if (t == "pluto") r.kind = 0;
        else if (t == "rtltcp") r.kind = 1;
        else if (t == "soapyremote") r.kind = 2;
        else if (t == "spyserver") r.kind = 3;
        else if (t == "hl2") r.kind = 4;
        else if (t == "redpitaya") r.kind = 5;
        else if (t == "sdrpp") r.kind = 6;
        else continue;
        r.host = cleanHost(item.substr(c1 + 1, c2 - c1 - 1));
        r.port = atoi(item.c_str() + c2 + 1);
        if (c3 != std::string::npos && r.kind == 2) r.remote = item.substr(c3 + 1);
        if (r.port < 0 || r.port > 65535) r.port = 0;
        if (r.host.empty()) continue;
        bool dup = false;
        for (const auto& o : v) if (o.kind == r.kind && o.host == r.host && o.port == r.port && o.remote == r.remote) dup = true;
        if (!dup) v.push_back(r);
    }
    return v;
}

} // namespace dect2::netp
#endif // DECT2_RUST_NET
