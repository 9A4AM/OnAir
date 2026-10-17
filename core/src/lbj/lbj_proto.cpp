// LBJ: numeric POCSAG pages on 821.2375 MHz. Field layout of the short message was read off real recordings
// (see docs/lbj.md); the long (locomotive / route) message follows public descriptions and is not yet verified on air.
#include "dect2/lbj/lbj_proto.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifndef _WIN32
#include <iconv.h>
#endif

namespace dect2::lbj {

bool isLbjAddress(uint32_t ric) {
    return ric == kAddrBasic || ric == kAddrBasicAlt || ric == kAddrDetail || ric == kAddrDetailAlt || ric == kAddrTime;
}

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && s[a] == ' ') a++;
    while (b > a && s[b - 1] == ' ') b--;
    return s.substr(a, b - a);
}

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) if (!std::isdigit(c)) return false;
    return true;
}

bool blankField(const std::string& s) {          // '-' and '*' fill unused fields, '?' marks digits lost to errors
    for (unsigned char c : s) if (c != '-' && c != '*' && c != ' ' && c != '?') return false;
    return true;
}

char hexNibble(char c) {                          // numeric codes 10..15 carry the hex digits A..F in the long message
    switch (c) {
    case '*': return 'A';
    case 'U': return 'B';
    case ' ': return 'C';
    case '-': return 'D';
    case ')': return 'E';
    case '(': return 'F';
    default: return c;
    }
}

std::string gbkToUtf8(const std::string& hex) {
    if (hex.size() < 4 || (hex.size() & 1)) return {};
    std::string bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const char h[3] = {hex[i], hex[i + 1], 0};
        char* end = nullptr;
        const long v = std::strtol(h, &end, 16);
        if (end != h + 2) return {};
        bytes.push_back((char)v);
    }
    while (!bytes.empty() && bytes.back() == 0) bytes.pop_back();
    if (bytes.empty()) return {};
#ifndef _WIN32
    iconv_t cd = iconv_open("UTF-8", "GBK");
    if (cd != (iconv_t)-1) {
        size_t inLeft = bytes.size(), outLeft = bytes.size() * 3 + 8;
        std::string out(outLeft, '\0');
        char* in = bytes.data();
        char* o = out.data();
        const size_t r = iconv(cd, &in, &inLeft, &o, &outLeft);
        iconv_close(cd);
        if (r != (size_t)-1 && inLeft == 0) {
            out.resize(out.size() - outLeft);
            return out;
        }
        return {};
    }
#endif
    std::string asc;
    for (unsigned char c : bytes) if (c >= 32 && c < 127) asc.push_back((char)c);
    return asc;
}

std::string directionOf(int fn) {
    if (fn == 1) return "down";
    if (fn == 3) return "up";
    return {};
}

void fill(const LbjSessionState::Row& r, double t, LbjMessage& out) {
    out = LbjMessage();
    out.timeSec = t;
    out.trainBase = r.baseTrain;
    out.prefix = r.prefix;
    out.train = r.prefix + r.baseTrain;
    out.direction = r.direction;
    out.speedKmh = r.speedKmh;
    out.kmMarker = r.kmMarker;
    out.loco = r.loco;
    out.locoCode = r.locoCode;
    out.route = r.route;
    out.detailed = r.detailed;
    out.crcOk = true;
}

void expire(LbjSessionState& st, double now) {
    for (auto it = st.sessions.begin(); it != st.sessions.end();) {
        if (now - it->second.updatedSec > 10.0) {
            if (st.lastTrain == it->first) st.lastTrain.clear();
            it = st.sessions.erase(it);
        } else
            ++it;
    }
}

bool parseBasic(const Page& p, double t, LbjSessionState& st, LbjMessage& out) {
    if (p.digits.size() < 15) return false;
    const std::string trainRaw = p.digits.substr(0, 5), speedRaw = p.digits.substr(5, 5), kmRaw = p.digits.substr(10, 5);
    if (blankField(trainRaw) && blankField(speedRaw) && blankField(kmRaw)) { st.emptyFrames++; return false; }
    const std::string train = trim(trainRaw), speed = trim(speedRaw), km = trim(kmRaw);
    if (!allDigits(train)) { st.rejected++; return false; }   // a lost train number is not shown
    LbjSessionState::Row row;
    std::string base = train;
    base.erase(0, std::min(base.find_first_not_of('0'), base.size() - 1));   // leading zeros are padding
    if (allDigits(speed)) {
        const int v = std::atoi(speed.c_str());
        if (v > 400) { st.rejected++; return false; }   // no train runs that fast
        row.speedKmh = std::to_string(v);
    } else if (!blankField(speed)) { st.rejected++; return false; }
    if (allDigits(km)) {
        const int v = std::atoi(km.c_str());
        char b[24];
        snprintf(b, sizeof b, "%d.%d", v / 10, v % 10);
        row.kmMarker = b;
    } else if (!blankField(km)) { st.rejected++; return false; }
    auto& s = st.sessions[base];
    s.baseTrain = base;
    s.direction = directionOf(p.function);
    if (!row.speedKmh.empty()) s.speedKmh = row.speedKmh; else s.speedKmh.clear();
    if (!row.kmMarker.empty()) s.kmMarker = row.kmMarker; else s.kmMarker.clear();
    s.updatedSec = t;
    st.lastTrain = base;
    expire(st, t);
    fill(s, t, out);
    out.crcOk = p.digits.find('?') == std::string::npos && p.fixed <= 2;
    return true;
}

// Long message: the last 50 characters are 25 bytes in hex. Offsets (characters): 0-3 two ASCII letters (train class prefix, e.g. "K"),
// 4-6 locomotive type code, 7-11 locomotive number, 14-29 eight bytes of GBK text (the route). Unverified on air.
bool parseDetail(const Page& p, double t, LbjSessionState& st, LbjMessage& out) {
    if (p.missing || p.digits.size() < 30) return false;
    const std::string buf = p.digits.size() > 50 ? p.digits.substr(p.digits.size() - 50) : p.digits;
    std::string hex;
    for (char c : buf) hex.push_back(hexNibble(c));
    std::string prefix;
    for (int i = 0; i < 2; i++) {
        const int v = (int)std::strtol(hex.substr(i * 2, 2).c_str(), nullptr, 16);
        if (v >= 'A' && v <= 'Z') prefix.push_back((char)v);
    }
    std::string locoCode = buf.substr(4, 3), locoNum = trim(buf.substr(7, 5)), loco;
    if (allDigits(locoCode) && allDigits(locoNum)) loco = locoCode + "-" + locoNum;
    else locoCode.clear();
    std::string route = trim(gbkToUtf8(hex.substr(14, 16)));
    for (unsigned char c : route) if (c < 32) { route.clear(); break; }
    if (loco.empty() && route.empty()) { st.rejected++; return false; }
    // attach to the train named by the short message of the last two seconds
    const std::string id = st.lastTrain;
    if (id.empty() || !st.sessions.count(id) || t - st.sessions[id].updatedSec > 2.0) return false;
    auto& s = st.sessions[id];
    if (!prefix.empty()) s.prefix = prefix;
    if (!loco.empty()) { s.loco = loco; s.locoCode = locoCode; }
    if (!route.empty()) s.route = route;
    s.detailed = true;
    s.updatedSec = t;
    fill(s, t, out);
    return true;
}

} // namespace

bool parsePage(const Page& p, double t, LbjSessionState& st, LbjMessage& out) {
    out = LbjMessage();
    if (p.ric == kAddrBasic || p.ric == kAddrBasicAlt) {
        const bool ok = parseBasic(p, t, st, out);
        if (ok && p.digits.size() >= 50 + 15) {          // basic and long message in one page
            LbjMessage o2;
            Page q = p;
            if (parseDetail(q, t, st, o2)) out = o2;
        }
        return ok;
    }
    if (p.ric == kAddrDetail || p.ric == kAddrDetailAlt) return parseDetail(p, t, st, out);
    return false;
}

std::string formatBasic(const std::string& train, int speedKmh, int kmTenths) {
    char b[48];
    snprintf(b, sizeof b, "%-5s %3d %5d", train.c_str(), speedKmh, kmTenths);
    return b;
}

} // namespace dect2::lbj
