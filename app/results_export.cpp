#include "results_export.h"
#include <cstdio>
#include <ctime>

namespace resexp {

std::string num(double v, int dec) {
    if (dec < 0) dec = 0;
    if (dec > 9) dec = 9;
    char b[64];
    snprintf(b, sizeof b, "%.*f", dec, v);
    for (char* p = b; *p; p++) if (*p == ',') *p = '.';   // belt and braces if a C locale was changed
    std::string s = b;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0" || s.empty()) s = "0";
    return s;
}

static std::string csvCell(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos && (s.empty() || (s.front() != ' ' && s.back() != ' '))) return s;
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += '"'; o += c; }
    return o + "\"";
}

std::string toCsv(const Table& t) {
    std::string o;
    auto line = [&](const std::vector<std::string>& r) {
        for (size_t i = 0; i < r.size(); i++) { if (i) o += ','; o += csvCell(r[i]); }
        o += "\r\n";
    };
    line(t.cols);
    for (const auto& r : t.rows) line(r);
    return o;
}

static bool isNumber(const std::string& s) {
    if (s.empty() || s.size() > 24) return false;
    size_t i = 0;
    if (s[i] == '-') i++;
    if (i >= s.size() || !(s[i] >= '0' && s[i] <= '9')) return false;
    if (s[i] == '0' && i + 1 < s.size() && s[i + 1] != '.') return false;   // 007 stays text
    bool dot = false;
    for (; i < s.size(); i++) {
        if (s[i] == '.') { if (dot || i + 1 >= s.size()) return false; dot = true; }
        else if (!(s[i] >= '0' && s[i] <= '9')) return false;
    }
    return true;
}

static std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += (char)c;   // UTF-8 passes through
        }
    }
    return o + "\"";
}

std::string toJson(const Table& t) {
    std::string o = "[\n";
    for (size_t r = 0; r < t.rows.size(); r++) {
        o += "  {";
        for (size_t c = 0; c < t.cols.size(); c++) {
            if (c) o += ", ";
            const std::string v = c < t.rows[r].size() ? t.rows[r][c] : std::string();
            o += jsonStr(t.cols[c]) + ": " + (isNumber(v) ? v : jsonStr(v));
        }
        o += r + 1 < t.rows.size() ? "},\n" : "}\n";
    }
    return o + "]\n";
}

std::string stamp() {
    char b[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(b, sizeof b, "%Y%m%d-%H%M", std::localtime(&now));
    return b;
}

}  // namespace resexp
