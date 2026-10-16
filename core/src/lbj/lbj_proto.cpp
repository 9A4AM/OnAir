// LBJ: POCSAG 1200 numeric pages on 821 MHz (TB/T 3504-2018 style train proximity warnings).
#include "dect2/lbj/lbj_proto.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#ifndef _WIN32
#include <iconv.h>
#endif

namespace dect2::lbj {

bool isLbjPocsagAddress(uint32_t ric) {
    return ric == kAddrBasic || ric == kAddrBasicAlt || ric == kAddrDetail || ric == kAddrDetailAlt || ric == kAddrTime;
}

namespace {

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) if (!std::isdigit(c)) return false;
    return true;
}

std::string compactBcd(const std::string& bcd) {
    std::string c;
    for (unsigned char ch : bcd) if (ch != ' ') c.push_back((char)ch);
    return c;
}

bool trainTokenOk(const std::string& raw, bool strict) {
    if (raw.empty()) return false;
    if (!strict) return raw.find('*') == std::string::npos && raw.find('-') == std::string::npos;
    for (unsigned char c : raw) {
        if (std::isalnum(c)) continue;
        return false;
    }
    return true;
}

std::string directionFromFn(int fn) {
    if (fn == 1) return "down";
    if (fn == 3) return "up";
    char b[24];
    snprintf(b, sizeof b, "unknown(%d)", fn);
    return b;
}

static char hexNibbleChar(char c) {
    switch (c) {
    case '*': return 'A';
    case 'U': return 'B';
    case ' ': return 'C';
    case '-': return 'D';
    case ')': return 'E';
    case '(' : return 'F';
    default: return c;
    }
}

std::string gbkFromHex(const std::string& ih) {
    if (ih.size() < 4 || (ih.size() & 1)) return {};
    std::string bytes;
    bytes.reserve(ih.size() / 2);
    for (size_t i = 0; i + 1 < ih.size(); i += 2) {
        const char h[3] = {ih[i], ih[i + 1], 0};
        char* end = nullptr;
        const long v = std::strtol(h, &end, 16);
        if (end != h + 2 || v < 0 || v > 255) return {};
        bytes.push_back((char)v);
    }
#ifndef _WIN32
    iconv_t cd = iconv_open("UTF-8", "GBK");
    if (cd != (iconv_t)-1) {
        size_t inLeft = bytes.size(), outLeft = bytes.size() * 3 + 8;
        std::string out(outLeft, '\0');
        char* in = bytes.data();
        char* o = out.data();
        iconv(cd, &in, &inLeft, &o, &outLeft);
        iconv_close(cd);
        out.resize(out.size() - outLeft);
        while (!out.empty() && (unsigned char)out.back() == 0) out.pop_back();
        return out;
    }
#endif
    std::string asc;
    for (unsigned char c : bytes) if (c >= 32 && c < 127) asc.push_back((char)c);
    return asc;
}

bool routeLooksOk(const std::string& r) {
    if (r.empty()) return false;
    for (unsigned char c : r) {
        if (c >= 0x80) continue;
        if (std::isalnum(c) || c == '-' || c == ' ') continue;
        return false;
    }
    return true;
}

void fillOut(const LbjSessionState::Row& s, double timeSec, LbjMessage& out) {
    out = LbjMessage();
    out.timeSec = timeSec;
    out.trainBase = s.baseTrain;
    out.prefix = s.prefix;
    out.train = s.prefix + s.baseTrain;
    out.direction = s.direction;
    out.speedKmh = s.speedKmh;
    out.kmMarker = s.kmMarker;
    out.loco = s.loco;
    out.locoCode = s.locoCode;
    out.route = s.route;
    out.detailed = s.detailed;
    out.crcOk = true;
}

} // namespace

bool parsePocsagPage(uint32_t ric, int function, const std::string& bcd, bool damaged, double timeSec, LbjSessionState& st, LbjMessage& out) {
    out = LbjMessage();
    if (!isLbjPocsagAddress(ric)) return false;
    if (ric == kAddrTime) return false;
    if (damaged) return false;
    if (ric == kAddrBasic && function == 0 && bcd.size() == 5 && (bcd[0] == '-' || bcd[0] == '*') && bcd[4] != '-')
        return false;

    const std::string compact = compactBcd(bcd);
    const bool isShort = (ric == kAddrBasic || ric == kAddrBasicAlt) && (compact.size() >= 10 || bcd.size() >= 10);
    const bool isMerged = (ric == kAddrBasic || ric == kAddrBasicAlt || ric == kAddrDetail) && compact.size() >= 50;
    const bool isStandalone = (ric == kAddrDetailAlt || ric == kAddrDetail) && compact.size() < 50;

    std::string baseTrain;
    if (isShort) {
        std::string raw = compact.size() >= 6 ? compact.substr(0, 6) : compact;
        while (!raw.empty() && raw.back() == ' ') raw.pop_back();
        if (!trainTokenOk(raw, true)) {
            std::string trimmed;
            for (unsigned char c : raw) if (std::isalnum(c)) trimmed.push_back((char)c);
            raw = trainTokenOk(trimmed, false) ? trimmed : std::string();
        }
        baseTrain = raw.empty() ? "----" : raw;
        st.lastTrain = baseTrain;

        const std::string dir = directionFromFn(function);
        std::string speed = "---", position = "---.-";
        int tr = 0, sp = 0, km = 0;
        if (std::sscanf(bcd.c_str(), "%d %d %d", &tr, &sp, &km) == 3) {
            if (tr > 0) baseTrain = std::to_string(tr);
            if (sp >= 0 && sp <= 400) speed = std::to_string(sp);
            if (km >= 0) {
                char pb[16];
                snprintf(pb, sizeof pb, "%d.%d", km / 10, km % 10);
                position = pb;
            }
        } else if (compact.size() >= 10) {
            std::string rs = compact.size() >= 9 ? compact.substr(6, 3) : std::string();
            for (char& c : rs) {
                if (c == ' ' || c == 'U' || c == '*') c = '0';
            }
            if (allDigits(rs)) {
                const int v = std::atoi(rs.c_str());
                if (v >= 0 && v <= 400) speed = std::to_string(v);
            }
            if (compact.size() >= 15) {
                const std::string pr = compact.substr(10, 5);
                bool bad = false;
                for (char c : pr) if (c == '*' || c == '-' || c == 'X' || c == 'U') bad = true;
                if (!bad) {
                    std::string ps = pr;
                    for (char& c : ps) if (c == ' ') c = '0';
                    if (allDigits(ps) && ps.size() == 5) position = ps.substr(0, 4) + "." + ps.substr(4, 1);
                }
            }
        }

        auto& row = st.sessions[baseTrain];
        row.baseTrain = baseTrain;
        if (dir.rfind("unknown", 0) != 0) row.direction = dir;
        if (speed != "---") row.speedKmh = speed;
        if (position != "---.-") row.kmMarker = position;
        row.updatedSec = timeSec;
    }

    if (isMerged || isStandalone) {
        const std::string tid = isMerged ? baseTrain : st.lastTrain;
        if (!tid.empty() && st.sessions.count(tid)) {
            if (!isStandalone || timeSec - st.sessions[tid].updatedSec <= 2.0) {
                const std::string buf = bcd.size() >= 50 ? bcd.substr(bcd.size() - 50) : bcd;
                std::string ih;
                ih.reserve(buf.size());
                for (char c : buf) ih.push_back(hexNibbleChar(c));

                std::string prefix;
                if (ih.size() >= 4) {
                    for (int i = 0; i < 2; i++) {
                        const int v = (int)std::strtol(ih.substr(i * 2, 2).c_str(), nullptr, 16);
                        if (v > 31 && v < 127 && v != '"' && v != ',') prefix.push_back((char)v);
                    }
                }
                prefix.erase(std::remove(prefix.begin(), prefix.end(), ' '), prefix.end());

                std::string loco = "----", locoCode = "---";
                if (buf.size() >= 12) {
                    const std::string lr = buf.substr(4, 8);
                    const std::string cp = lr.substr(0, 3);
                    std::string np = lr.substr(3);
                    while (!np.empty() && np.back() == ' ') np.pop_back();
                    if (allDigits(cp)) {
                        locoCode = cp;
                        if (!allDigits(np) || np.find('*') != std::string::npos) np = "----";
                        if (np.empty()) np = "----";
                        loco = np == "----" ? "----" : ("type" + cp + "-" + np);
                    }
                }

                std::string route = "----";
                if (ih.size() >= 30) {
                    const std::string dec = gbkFromHex(ih.substr(14, 16));
                    if (routeLooksOk(dec)) route = dec;
                }

                auto& row = st.sessions[tid];
                if (!prefix.empty()) row.prefix = prefix;
                if (loco != "----") {
                    row.loco = loco;
                    row.locoCode = locoCode;
                }
                if (route != "----") {
                    row.route = route;
                    row.routeValid = true;
                }
                row.detailed = true;
                row.updatedSec = timeSec;
            }
        }
    }

    const std::string aid = isShort ? baseTrain : st.lastTrain;
    if (aid.empty() || !st.sessions.count(aid)) return false;

    for (auto it = st.sessions.begin(); it != st.sessions.end();) {
        if (timeSec - it->second.updatedSec > 10.0) {
            if (st.lastTrain == it->first) st.lastTrain.clear();
            it = st.sessions.erase(it);
        } else
            ++it;
    }

    fillOut(st.sessions[aid], timeSec, out);
    return !out.train.empty() && out.train != "----";
}

} // namespace dect2::lbj
