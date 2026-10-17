// Remote-control API server: see dect2/net/remote_api.h. One thread accepts; every connection has a thread of its own (at most 16), so a slow or
// silent client never holds up another one. Blocking sockets with select() for the waits, on top of the helpers of tcp_client.h.
#include "dect2/net/remote_api.h"
#include "dect2/common/str.h"
#include "dect2/source/rotator.h"
#include "dect2/engine/mode_tuning.h"
#include "netcompat.h"
#include "tcp_client.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <random>
#include <thread>
#ifndef _WIN32
#include <sys/select.h>
#include <sys/time.h>
#include "dect2/common/str.h"
#endif

namespace dect2 {

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

// ------------------------------------------------------------------ JSON
std::string jsonEscape(const std::string& s) { return dect2::str::jsonEscape(s); }

std::string jsonNum(double v) {
    if (!std::isfinite(v)) return "null";
    char b[40];
    snprintf(b, sizeof b, "%.10g", v);
    return b;
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

std::string remoteApiNewToken() {
    std::random_device rd;
    std::seed_seq sq{rd(), rd(), rd(), rd(), (unsigned)std::chrono::steady_clock::now().time_since_epoch().count()};
    std::mt19937_64 g(sq);
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 24; i++) s.push_back(hex[g() & 15]);
    return s;
}

// ------------------------------------------------------------------ the server
namespace {
constexpr size_t kMaxHeader = 8192, kMaxBody = 4096, kMaxConns = 16, kMaxWs = 8, kMaxWsFrame = 4096;
constexpr int kRequestMs = 5000;

std::string lower(std::string s) { return dect2::str::lower(std::move(s)); }
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}
bool waitReadable(sock_t s, int ms) {
    fd_set rd;
    FD_ZERO(&rd); FD_SET(s, &rd);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select((int)s + 1, &rd, nullptr, nullptr, &tv) > 0;
}
void sockSendTimeout(sock_t s, int ms) {
#ifdef _WIN32
    DWORD t = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof t);
#else
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
}
bool ctEqual(const std::string& a, const std::string& b) {   // time does not depend on where they differ
    unsigned char d = a.size() == b.size() ? 0 : 1;
    for (size_t i = 0; i < std::min(a.size(), b.size()); i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}
std::string hostOnly(const std::string& h) {   // "127.0.0.1:8765" -> "127.0.0.1", "[::1]:80" -> "[::1]"
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
const char* statusText(int c) {
    switch (c) {
    case 200: return "OK"; case 101: return "Switching Protocols"; case 400: return "Bad Request"; case 401: return "Unauthorized";
    case 403: return "Forbidden"; case 404: return "Not Found"; case 405: return "Method Not Allowed"; case 408: return "Request Timeout";
    case 411: return "Length Required"; case 413: return "Payload Too Large"; case 415: return "Unsupported Media Type";
    case 426: return "Upgrade Required"; case 431: return "Request Header Fields Too Large"; case 501: return "Not Implemented";
    case 503: return "Service Unavailable"; default: return "Error";
    }
}
struct Request {
    std::string method, path, query, body;
    std::map<std::string, std::string> h;   // lower-case names
    bool loopback = false;
    std::string hdr(const char* k) const { auto it = h.find(k); return it == h.end() ? std::string() : it->second; }
};
} // namespace

struct RemoteApiServer::Impl {
    struct Conn { sock_t s = kBadSock; std::thread th; std::atomic<bool> done{false}; std::atomic<bool> ws{false}; bool loopback = false; };
    explicit Impl(RemoteApiHost& h) : host(h) {}
    RemoteApiHost& host;
    mutable std::mutex mu;
    RemoteApiConfig cfg, live;
    RemoteApiStatus st;
    std::vector<std::unique_ptr<Conn>> conns;
    std::mutex hostMu;   // one call into the host at a time
    std::thread th;
    std::atomic<bool> quit{false};
    std::atomic<uint64_t> requests{0}, rejected{0};

    void stopAll() {
        quit = true;
        if (th.joinable()) th.join();   // the accept thread closes and joins its connections on the way out
        quit = false;
        std::lock_guard<std::mutex> lk(mu);
        st.listening = false; st.httpClients = st.wsClients = 0;
    }
    void reap() {   // under no lock: joins the connections that finished
        for (size_t i = 0; i < conns.size();) {
            if (conns[i]->done) { if (conns[i]->th.joinable()) conns[i]->th.join(); sockClose(conns[i]->s); conns.erase(conns.begin() + (long)i); }
            else i++;
        }
        int http = 0, ws = 0;
        for (auto& c : conns) (c->ws ? ws : http)++;
        std::lock_guard<std::mutex> lk(mu);
        st.httpClients = http; st.wsClients = ws;
    }
    std::atomic<int> wsActive{0};

    // ---- responses
    bool send(sock_t s, int code, const std::string& body, const char* type = "application/json", const std::string& extra = "") {
        std::string r = "HTTP/1.1 " + std::to_string(code) + " " + statusText(code) + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
                        "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n" + extra + "\r\n" + body;
        return sockSendAll(s, r.data(), r.size());
    }
    bool sendErr(sock_t s, int code, const std::string& msg, const std::string& extra = "") {
        rejected++;
        return send(s, code, "{\"error\":\"" + jsonEscape(msg) + "\"}", "application/json", extra);
    }

    // ---- reading a request; false: the connection is answered (error) or dead
    bool readRequest(sock_t s, Request& rq) {
        std::string buf;
        const auto t0 = std::chrono::steady_clock::now();
        auto late = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() > kRequestMs; };
        size_t hdrEnd = std::string::npos;
        while (hdrEnd == std::string::npos) {
            if (quit) return false;
            if (late()) { sendErr(s, 408, "request timeout"); return false; }
            if (!waitReadable(s, 100)) continue;
            char b[2048];
            const long long k = sockRecv(s, b, sizeof b);
            if (k <= 0) return false;
            buf.append(b, (size_t)k);
            hdrEnd = buf.find("\r\n\r\n");
            if (hdrEnd == std::string::npos && buf.size() > kMaxHeader) { sendErr(s, 431, "headers too large"); return false; }
            if (hdrEnd != std::string::npos && hdrEnd > kMaxHeader) { sendErr(s, 431, "headers too large"); return false; }
        }
        const std::string head = buf.substr(0, hdrEnd);
        std::string rest = buf.substr(hdrEnd + 4);
        size_t pos = head.find("\r\n");
        const std::string line = head.substr(0, pos);
        const size_t s1 = line.find(' '), s2 = line.rfind(' ');
        if (s1 == std::string::npos || s2 == s1 || line.compare(s2 + 1, 5, "HTTP/") != 0) { sendErr(s, 400, "bad request line"); return false; }
        rq.method = line.substr(0, s1);
        std::string target = line.substr(s1 + 1, s2 - s1 - 1);
        if (target.empty() || target[0] != '/' || target.size() > 512) { sendErr(s, 400, "bad target"); return false; }
        for (unsigned char c : target) if (c < 0x21 || c == 0x7f) { sendErr(s, 400, "bad target"); return false; }
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
            if (c == std::string::npos || c == 0 || ++nh > 48) { sendErr(s, 400, "bad header"); return false; }
            const std::string name = lower(trim(l.substr(0, c)));
            if (rq.h.count(name) && (name == "content-length" || name == "host")) { sendErr(s, 400, "duplicate header"); return false; }
            rq.h[name] = trim(l.substr(c + 1));
        }
        if (rq.h.count("transfer-encoding")) { sendErr(s, 501, "chunked bodies are not supported"); return false; }
        size_t want = 0;
        if (rq.h.count("content-length")) {
            const std::string& cl = rq.h["content-length"];
            if (cl.empty() || cl.size() > 9 || !std::all_of(cl.begin(), cl.end(), [](char c) { return isdigit((unsigned char)c) != 0; })) { sendErr(s, 400, "bad Content-Length"); return false; }
            want = (size_t)atol(cl.c_str());
            if (want > kMaxBody) { sendErr(s, 413, "body too large (limit 4096 bytes)"); return false; }
        } else if (rq.method == "POST") { sendErr(s, 411, "Content-Length required"); return false; }
        while (rest.size() < want) {
            if (quit) return false;
            if (late()) { sendErr(s, 408, "request timeout"); return false; }
            if (!waitReadable(s, 100)) continue;
            char b[2048];
            const long long k = sockRecv(s, b, sizeof b);
            if (k <= 0) return false;
            rest.append(b, (size_t)k);
        }
        rq.body = rest.substr(0, want);
        return true;
    }

    // Host / Origin / token. False: answered.
    bool authorise(sock_t s, const Request& rq, const RemoteApiConfig& c) {
        const std::string host = rq.hdr("host");
        if (host.empty()) { sendErr(s, 400, "Host header required"); return false; }
        const std::string org = rq.hdr("origin");
        if (!org.empty()) {   // a web page: only this server's own pages (there are none), never another site's script
            const size_t sch = org.find("://");
            if (sch == std::string::npos || lower(org.substr(sch + 3)) != lower(host)) { sendErr(s, 403, "cross-origin requests are refused"); return false; }
        }
        if (rq.loopback) {
            if (!loopbackName(lower(hostOnly(host)))) { sendErr(s, 403, "Host must be a loopback name"); return false; }   // DNS rebinding
            return true;
        }
        std::string tok;
        const std::string a = rq.hdr("authorization");
        if (a.size() > 7 && lower(a.substr(0, 7)) == "bearer ") tok = trim(a.substr(7));
        else tok = queryParam(rq.query, "token");
        if (c.token.size() >= 16 && ctEqual(tok, c.token)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));   // guessing is slow
        sendErr(s, 401, "token required", "WWW-Authenticate: Bearer\r\n");
        return false;
    }

    static bool needJson(const Request& rq) { return lower(rq.hdr("content-type")).compare(0, 16, "application/json") == 0; }

    // ---- the API
    void route(sock_t s, const Request& rq) {
        const std::string& p = rq.path;
        const bool isGet = rq.method == "GET", isPost = rq.method == "POST";
        if (!isGet && !isPost) { sendErr(s, 405, "method not allowed", "Allow: GET, POST\r\n"); return; }
        std::string err;
        uint64_t seq = 0;
        auto okJson = [&] { send(s, 200, "{\"ok\":true}"); };
        auto fail = [&](const std::string& e) { sendErr(s, 400, e.empty() ? "failed" : e); };
        auto getOnly = [&](const std::function<std::string()>& f) {
            if (!isGet) { sendErr(s, 405, "use GET", "Allow: GET\r\n"); return; }
            std::string j;
            { std::lock_guard<std::mutex> lk(hostMu); j = f(); }
            send(s, 200, j);
        };
        if (p == "/api/v1" || p == "/api/v1/") {
            getOnly([] { return std::string("{\"name\":\"OnAir remote API\",\"version\":1,\"endpoints\":[\"GET /api/v1/status\",\"GET /api/v1/devices\",\"GET /api/v1/modes\",\"GET /api/v1/telemetry\","
                                            "\"GET /api/v1/spectrum?bins=N\",\"GET /api/v1/rotator\",\"GET /api/v1/ws\",\"POST /api/v1/start\",\"POST /api/v1/stop\",\"POST /api/v1/mode\",\"POST /api/v1/frequency\","
                                            "\"POST /api/v1/samplerate\",\"POST /api/v1/gain\",\"POST /api/v1/rotator/goto\",\"POST /api/v1/rotator/stop\",\"POST /api/v1/rotator/park\"]}"); });
            return;
        }
        if (p == "/api/v1/status") { getOnly([&] { return host.statusJson(); }); return; }
        if (p == "/api/v1/devices") { getOnly([&] { return host.devicesJson(); }); return; }
        if (p == "/api/v1/modes") { getOnly([&] { return host.modesJson(); }); return; }
        if (p == "/api/v1/telemetry") { getOnly([&] { return host.telemetryJson(seq); }); return; }
        if (p == "/api/v1/spectrum") {
            int bins = 1024;
            const std::string b = queryParam(rq.query, "bins");
            if (!b.empty()) bins = std::max(16, std::min(4096, atoi(b.c_str())));
            getOnly([&] { return host.spectrumJson(bins, seq); });
            return;
        }
        if (p == "/api/v1/rotator") {
            getOnly([] { return Rotator::get().apiStatusJson(); });
            return;
        }
        if (p == "/api/v1/ws") { sendErr(s, 426, "WebSocket upgrade required", "Upgrade: websocket\r\n"); return; }   // a proper upgrade is handled before route()
        const bool known = p == "/api/v1/start" || p == "/api/v1/stop" || p == "/api/v1/mode" || p == "/api/v1/frequency" || p == "/api/v1/samplerate" || p == "/api/v1/gain"
                         || p == "/api/v1/rotator/goto" || p == "/api/v1/rotator/stop" || p == "/api/v1/rotator/park";
        if (!known) { sendErr(s, 404, "no such endpoint"); return; }
        if (!isPost) { sendErr(s, 405, "use POST", "Allow: POST\r\n"); return; }
        if (!needJson(rq)) { sendErr(s, 415, "Content-Type: application/json required"); return; }
        std::map<std::string, JsonVal> j;
        if (!rq.body.empty() && !parseFlatJson(rq.body, j)) { sendErr(s, 400, "body must be a flat JSON object"); return; }
        auto num = [&](const char* k, double& v) { auto it = j.find(k); if (it == j.end() || it->second.kind != JsonVal::Num) return false; v = it->second.num; return true; };
        std::lock_guard<std::mutex> lk(hostMu);
        double v = 0;
        if (p == "/api/v1/start") {
            int dev = -1;
            if (j.count("device")) { if (!num("device", v) || v < 0 || v > 1000 || v != std::floor(v)) { fail("device must be a whole number >= 0"); return; } dev = (int)v; }
            if (host.start(dev, err)) okJson(); else fail(err);
        } else if (p == "/api/v1/stop") {
            if (host.stop(err)) okJson(); else fail(err);
        } else if (p == "/api/v1/mode") {
            if (!num("family", v) || v < 0 || v > 1000 || v != std::floor(v)) { fail("family must be a whole number (see /api/v1/modes)"); return; }
            std::string sub;
            if (j.count("sub")) { if (j["sub"].kind != JsonVal::Str || j["sub"].str.size() > 40) { fail("sub must be a short string"); return; } sub = j["sub"].str; }
            if (host.setMode((int)v, sub, err)) okJson(); else fail(err);
        } else if (p == "/api/v1/frequency") {
            double hz = 0;
            if (num("hz", v)) hz = v; else if (num("mhz", v)) hz = v * 1e6; else { fail("give hz or mhz"); return; }
            if (!(hz > 0) || hz > kMaxTuneMhz * 1e6) { fail("frequency out of range"); return; }
            if (host.setFrequencyHz(hz, err)) okJson(); else fail(err);
        } else if (p == "/api/v1/samplerate") {
            double hz = 0;
            if (num("hz", v)) hz = v; else if (num("msps", v)) hz = v * 1e6; else { fail("give hz or msps (0 = automatic)"); return; }
            if (hz < 0 || hz > 1e9) { fail("sample rate out of range"); return; }
            if (host.setSampleRateHz(hz, err)) okJson(); else fail(err);
        } else if (p == "/api/v1/rotator/stop") {
            Rotator::get().stop();
            okJson();
        } else if (p == "/api/v1/rotator/park") {
            Rotator::get().park();
            okJson();
        } else if (p == "/api/v1/rotator/goto") {
            double az = 0, el = 0;
            if (!num("az", az) && !num("azimuth", az)) { fail("give az or azimuth (corrected degrees)"); return; }
            if (!num("el", el) && !num("elevation", el)) el = 0;
            Rotator::get().gotoAzEl(az, el);
            okJson();
        } else {   // gain
            if (!num("db", v) || v < -100 || v > 200) { fail("db must be a number"); return; }
            if (host.setGainDb(v, err)) okJson(); else fail(err);
        }
    }

    // ---- WebSocket: telemetry (and the spectrum when asked for) pushed as text frames; the client may send {"telemetry":bool,"spectrum":bool,"bins":N}
    void serveWs(sock_t s, const Request& rq, Conn& cn) {
        const std::string key = rq.hdr("sec-websocket-key");
        if (lower(rq.hdr("upgrade")) != "websocket" || lower(rq.hdr("connection")).find("upgrade") == std::string::npos || rq.hdr("sec-websocket-version") != "13" || key.size() != 24) {
            sendErr(s, 400, "not a WebSocket handshake (needs Upgrade, Connection, Sec-WebSocket-Version: 13 and a key)");
            return;
        }
        if (wsActive.fetch_add(1) >= (int)kMaxWs) { wsActive--; rejected++; send(s, 503, "{\"error\":\"too many WebSocket clients\"}"); return; }
        struct Dec { std::atomic<int>& n; ~Dec() { n--; } } dec{wsActive};
        cn.ws = true;
        const std::string r = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + wsAcceptKey(key) + "\r\n\r\n";
        if (!sockSendAll(s, r.data(), r.size())) return;
        bool wantTel = true, wantSpec = false;
        int bins = 512;
        uint64_t telSeq = ~0ull, specSeq = ~0ull;
        auto lastTel = std::chrono::steady_clock::now() - std::chrono::seconds(1), lastSpec = lastTel;
        std::string in;
        auto closeWith = [&](int code) {
            std::string pl;
            pl.push_back((char)(code >> 8)); pl.push_back((char)(code & 0xFF));
            const std::string f = wsFrame(8, pl);
            sockSendAll(s, f.data(), f.size());
        };
        auto sendText = [&](const std::string& t) { const std::string f = wsFrame(1, t); return sockSendAll(s, f.data(), f.size()); };
        while (!quit) {
            if (waitReadable(s, 50)) {
                char b[1024];
                const long long k = sockRecv(s, b, sizeof b);
                if (k <= 0) return;
                in.append(b, (size_t)k);
                if (in.size() > 2 * (kMaxWsFrame + 16)) { closeWith(1009); return; }
            }
            while (in.size() >= 2) {   // whole frames
                const uint8_t b0 = (uint8_t)in[0], b1 = (uint8_t)in[1];
                const int op = b0 & 0x0F;
                const bool fin = (b0 & 0x80) != 0, masked = (b1 & 0x80) != 0;
                uint64_t len = b1 & 0x7F;
                size_t hl = 2;
                if (len == 126) { if (in.size() < 4) break; len = (uint64_t)(uint8_t)in[2] << 8 | (uint8_t)in[3]; hl = 4; }
                else if (len == 127) { closeWith(1009); return; }   // 64-bit lengths are never this small
                if (!masked) { closeWith(1002); return; }           // RFC 6455 5.1: clients mask
                if (len > kMaxWsFrame) { closeWith(1009); return; }
                if (in.size() < hl + 4 + len) break;
                const uint8_t* mk = (const uint8_t*)in.data() + hl;
                std::string pl((size_t)len, 0);
                for (size_t i = 0; i < len; i++) pl[i] = (char)((uint8_t)in[hl + 4 + i] ^ mk[i & 3]);
                in.erase(0, hl + 4 + (size_t)len);
                if (!fin || op == 0 || op == 2 || (op > 2 && op < 8) || op > 10) { closeWith(1003); return; }   // no fragmentation, no binary
                if (op == 8) { closeWith(1000); return; }
                if (op == 9) { const std::string f = wsFrame(10, pl.substr(0, 125)); if (!sockSendAll(s, f.data(), f.size())) return; continue; }
                if (op == 10) continue;
                std::map<std::string, JsonVal> j;
                if (!parseFlatJson(pl, j)) { if (!sendText("{\"type\":\"error\",\"error\":\"send a flat JSON object like {\\\"spectrum\\\":true}\"}")) return; continue; }
                if (j.count("telemetry") && j["telemetry"].kind == JsonVal::Bool) wantTel = j["telemetry"].b;
                if (j.count("spectrum") && j["spectrum"].kind == JsonVal::Bool) wantSpec = j["spectrum"].b;
                if (j.count("bins") && j["bins"].kind == JsonVal::Num) bins = std::max(16, std::min(2048, (int)j["bins"].num));
                telSeq = specSeq = ~0ull;   // the new choice is answered with the current values
                lastTel = lastSpec = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                if (!sendText(std::string("{\"type\":\"subscribed\",\"telemetry\":") + (wantTel ? "true" : "false") + ",\"spectrum\":" + (wantSpec ? "true" : "false") + ",\"bins\":" + std::to_string(bins) + "}")) return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (wantTel && now - lastTel >= std::chrono::milliseconds(100)) {
                uint64_t sq = telSeq;
                std::string j;
                { std::lock_guard<std::mutex> lk(hostMu); j = host.telemetryJson(sq); }
                if (sq != telSeq) {
                    telSeq = sq; lastTel = now;
                    if (!sendText("{\"type\":\"telemetry\",\"data\":" + j + "}")) return;
                }
            }
            if (wantSpec && now - lastSpec >= std::chrono::milliseconds(200)) {
                uint64_t sq = specSeq;
                std::string j;
                { std::lock_guard<std::mutex> lk(hostMu); j = host.spectrumJson(bins, sq); }
                if (sq != specSeq) {
                    specSeq = sq; lastSpec = now;
                    if (!sendText("{\"type\":\"spectrum\",\"data\":" + j + "}")) return;
                }
            }
        }
    }

    void serve(Conn* cn) {
        sock_t s = cn->s;
        sockSendTimeout(s, 3000);
        Request rq;
        rq.loopback = cn->loopback;
        RemoteApiConfig c;
        { std::lock_guard<std::mutex> lk(mu); c = live; }
        if (readRequest(s, rq) && authorise(s, rq, c)) {
            requests++;
            if (rq.method == "GET" && rq.path == "/api/v1/ws" && !rq.hdr("upgrade").empty()) serveWs(s, rq, *cn);
            else route(s, rq);
        }
        sockShutdown(s);
        cn->done = true;
    }

    void run(RemoteApiConfig c) {
        netInit();
        sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
        if (ls == kBadSock) { std::lock_guard<std::mutex> lk(mu); st.lastError = "cannot open a socket"; return; }
#ifndef _WIN32
        { int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); }
#endif
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(c.lan ? INADDR_ANY : INADDR_LOOPBACK);
        a.sin_port = htons((uint16_t)c.port);
        if (c.port < 0 || c.port > 65535 || bind(ls, (sockaddr*)&a, sizeof a) != 0 || listen(ls, 16) != 0) {
            std::lock_guard<std::mutex> lk(mu);
            st.lastError = "cannot listen on TCP port " + std::to_string(c.port) + " (in use by another program?)";
            sockClose(ls);
            return;
        }
        {
            sockaddr_in b{}; socklen_t bl = sizeof b;
            std::lock_guard<std::mutex> lk(mu);
            if (getsockname(ls, (sockaddr*)&b, &bl) == 0) live.port = ntohs(b.sin_port);
        }
        sockSetNonBlocking(ls, true);
        { std::lock_guard<std::mutex> lk(mu); st.listening = true; st.lastError.clear(); }
        while (!quit) {
            if (waitReadable(ls, 50)) {
                sockaddr_in pa{}; socklen_t pl = sizeof pa;
                const sock_t k = accept(ls, (sockaddr*)&pa, &pl);
                if (k != kBadSock) {
                    sockSetNonBlocking(k, false);
#ifdef SO_NOSIGPIPE
                    { int one = 1; setsockopt(k, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one); }
#endif
                    reap();
                    if (conns.size() >= kMaxConns) {
                        rejected++;
                        const std::string r = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                        sockSendAll(k, r.data(), r.size());
                        sockClose(k);
                    } else {
                        auto cn = std::make_unique<Conn>();
                        cn->s = k;
                        cn->loopback = (ntohl(pa.sin_addr.s_addr) >> 24) == 127;
                        Conn* raw = cn.get();
                        cn->th = std::thread([this, raw] { serve(raw); });
                        conns.push_back(std::move(cn));
                    }
                }
            } else reap();
        }
        sockClose(ls);
        for (auto& c2 : conns) sockShutdown(c2->s);
        for (auto& c2 : conns) { if (c2->th.joinable()) c2->th.join(); sockClose(c2->s); }
        conns.clear();
    }
};

RemoteApiServer::RemoteApiServer(RemoteApiHost& host) : p_(new Impl(host)) {}
RemoteApiServer::~RemoteApiServer() { p_->stopAll(); }

bool RemoteApiServer::configure(const RemoteApiConfig& cIn) {
    RemoteApiConfig c = cIn;
    Impl& p = *p_;
    {
        std::lock_guard<std::mutex> lk(p.mu);
        const int cur = p.cfg.on && p.cfg.port == 0 ? p.live.port : p.cfg.port;
        const bool same = c.on == p.cfg.on && c.port == cur && c.lan == p.cfg.lan && p.st.listening == c.on;
        if (same) {   // only the token changed (or nothing): it takes effect at once
            if (c.lan && c.token.size() < 16) { p.st.lastError = "LAN access needs a token of at least 16 characters"; }
            else { p.cfg.token = c.token; p.live.token = c.token; p.st.lastError.clear(); }
            return !c.on || p.st.listening;
        }
        p.cfg = c;
    }
    p.stopAll();
    if (!c.on) { std::lock_guard<std::mutex> lk(p.mu); p.st.lastError.clear(); return true; }
    if (c.lan && c.token.size() < 16) {
        std::lock_guard<std::mutex> lk(p.mu);
        p.st.lastError = "LAN access needs a token of at least 16 characters";
        return false;
    }
    { std::lock_guard<std::mutex> lk(p.mu); p.live = c; p.st.lastError.clear(); p.st.listening = false; }
    p.th = std::thread([&p, c] { p.run(c); });
    for (int i = 0; i < 400; i++) {   // until it listens (or failed), so that port 0 can be read back
        { std::lock_guard<std::mutex> lk(p.mu); if (p.st.listening || !p.st.lastError.empty()) break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::lock_guard<std::mutex> lk(p.mu);
    return p.st.listening;
}

RemoteApiConfig RemoteApiServer::config() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    RemoteApiConfig c = p_->cfg;
    if (c.on && c.port == 0) c.port = p_->live.port;
    return c;
}

RemoteApiStatus RemoteApiServer::status() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    RemoteApiStatus s = p_->st;
    s.requests = p_->requests; s.rejected = p_->rejected;
    return s;
}

} // namespace dect2
