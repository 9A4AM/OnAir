// The C++ implementation of dect2/files/files_proto.h; the Rust crate onair_files replaces it when DECT2_RUST_FILES is defined (files_proto_rs.cpp). The code is what
// used to sit in iq_file.cpp, iq_record.cpp, channel_db.cpp, passes.cpp, mbtiles.h and results_io.h, moved here; the changes made on the way (identically in Rust):
//  * numbers read from text (SigMF meta, TLE fields) are decimal tokens only, [+-]? (digits [. digits*] | . digits) ([eE] [+-]? digits)?, converted by strtod: hex floats,
//    "inf" and "nan" are no numbers (strtod used to accept them);
//  * the SigMF meta writers do not cut long descriptions at 1023 bytes, a rate or frequency that is not finite is written as 0 (it was "nan" / "inf", not JSON) and
//    the recorder leaves out a centre frequency of 1e18 or more (the %.1f text of 1e300 was cut at 95 digits);
//  * a double that does not fit a 64 bit unsigned (SigMF sample_start, header_bytes) saturates, it was undefined;
//  * the b-tree code refuses a usable page size under 36 bytes and checks the page is long enough for its header.
#include "dect2/files/files_proto.h"
#ifndef DECT2_RUST_FILES
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include "dect2/common/endian.h"

namespace dect2::filesp {

namespace {
std::string trimWith(const std::string& s, const char* chars) {
    const size_t a = s.find_first_not_of(chars);
    if (a == std::string::npos) return std::string();
    return s.substr(a, s.find_last_not_of(chars) - a + 1);
}

// length of the decimal number token at p (0 when there is none)
size_t scanDecimal(const char* p, size_t n) {
    size_t j = 0;
    if (j < n && (p[j] == '+' || p[j] == '-')) j++;
    const size_t d0 = j;
    while (j < n && p[j] >= '0' && p[j] <= '9') j++;
    const size_t intDigits = j - d0;
    size_t frac = 0;
    if (j < n && p[j] == '.') {
        size_t k = j + 1;
        while (k < n && p[k] >= '0' && p[k] <= '9') k++;
        frac = k - j - 1;
        if (intDigits > 0 || frac > 0) j = k;
    }
    if (intDigits == 0 && frac == 0) return 0;
    if (j < n && (p[j] == 'e' || p[j] == 'E')) {
        size_t k = j + 1;
        if (k < n && (p[k] == '+' || p[k] == '-')) k++;
        const size_t e0 = k;
        while (k < n && p[k] >= '0' && p[k] <= '9') k++;
        if (k > e0) j = k;
    }
    return j;
}
double toDouble(const char* p, size_t n) { return strtod(std::string(p, n).c_str(), nullptr); }

uint64_t toU64(double v) { return v > 0 ? (v >= 18446744073709551616.0 ? UINT64_MAX : (uint64_t)v) : 0; }
double finite(double v) { return std::isfinite(v) ? v : 0.0; }
} // namespace

// ================================================================ CSV
std::vector<std::vector<std::string>> csvParse(const std::string& t) {
    std::vector<std::vector<std::string>> out;
    std::vector<std::string> rec;
    std::string f;
    bool q = false, any = false;
    auto endRec = [&]() {
        rec.push_back(f); f.clear();
        if (!(rec.size() == 1 && rec[0].empty())) out.push_back(rec);
        rec.clear(); any = false;
    };
    for (size_t i = 0; i < t.size(); i++) {
        const char c = t[i];
        if (q) {
            if (c == '"') { if (i + 1 < t.size() && t[i + 1] == '"') { f += '"'; i++; } else q = false; }
            else f += c;
        } else if (c == '"') { q = true; any = true; }
        else if (c == ',') { rec.push_back(f); f.clear(); any = true; }
        else if (c == '\n') endRec();
        else if (c == '\r') {}
        else { f += c; any = true; }
    }
    if (any || !f.empty() || !rec.empty()) endRec();
    return out;
}

std::string csvQuote(const std::string& f) {
    if (f.find_first_of(",\"\r\n") == std::string::npos) return f;
    std::string o = "\"";
    for (char c : f) { if (c == '"') o += '"'; o += c; }
    return o + "\"";
}

std::vector<std::string> splitList(const std::string& s, char sep) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : s + std::string(1, sep)) {
        if (c == sep) { cur = trimWith(cur, " \t\r"); if (!cur.empty()) v.push_back(cur); cur.clear(); }
        else cur += c;
    }
    return v;
}

// ================================================================ SigMF
namespace {
// a small JSON reader: enough for SigMF meta files
struct J {
    enum T { Null, Num, Str, Bool, Arr, Obj } t = Null;
    double n = 0; std::string s; bool b = false;
    std::vector<J> a; std::map<std::string, J> o;
    const J* get(const std::string& k) const { auto i = o.find(k); return i == o.end() ? nullptr : &i->second; }
};

struct P {
    const std::string& s; size_t i = 0; bool bad = false; int depth = 0;
    void ws() { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
    bool lit(const char* w) { size_t n = strlen(w); if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }
    std::string str() {
        std::string r; i++;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                i++;
                switch (s[i]) {
                case 'n': r += '\n'; break;
                case 't': r += '\t'; break;
                case 'r': r += '\r'; break;
                case 'u': if (i + 4 < s.size()) { unsigned c = (unsigned)strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16); r += c < 128 ? (char)c : '?'; i += 4; } break;
                default: r += s[i];
                }
            } else r += s[i];
            i++;
        }
        if (i >= s.size()) bad = true; else i++;
        return r;
    }
    J val() {
        J v; ws();
        if (i >= s.size() || ++depth > 64) { bad = true; return v; }
        char c = s[i];
        if (c == '{') {
            v.t = J::Obj; i++; ws();
            if (i < s.size() && s[i] == '}') i++;
            else for (;;) {
                ws(); if (i >= s.size() || s[i] != '"') { bad = true; break; }
                std::string k = str(); ws();
                if (i >= s.size() || s[i] != ':') { bad = true; break; }
                i++; v.o[k] = val(); if (bad) break; ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; break; }
                bad = true; break;
            }
        } else if (c == '[') {
            v.t = J::Arr; i++; ws();
            if (i < s.size() && s[i] == ']') i++;
            else for (;;) {
                v.a.push_back(val()); if (bad) break; ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; break; }
                bad = true; break;
            }
        } else if (c == '"') { v.t = J::Str; v.s = str(); }
        else if (lit("true")) { v.t = J::Bool; v.b = true; }
        else if (lit("false")) { v.t = J::Bool; }
        else if (lit("null")) {}
        else {
            const size_t k = scanDecimal(s.data() + i, s.size() - i);
            if (!k) bad = true; else { v.t = J::Num; v.n = toDouble(s.data() + i, k); i += k; }
        }
        depth--;
        return v;
    }
};

double num(const J* j, double def = 0) { return j && j->t == J::Num ? j->n : def; }

// a SigMF data type -> (format, big endian); false for one OnAir cannot play
bool parseDatatype(const std::string& dt, int& fmt, bool& be) {
    std::string d = dt;
    for (auto& c : d) c = (char)std::tolower((unsigned char)c);
    be = false;
    if (d.size() > 3 && d.compare(d.size() - 3, 3, "_be") == 0) { be = true; d.resize(d.size() - 3); }
    else if (d.size() > 3 && d.compare(d.size() - 3, 3, "_le") == 0) d.resize(d.size() - 3);
    if (d == "cf32") fmt = 2;
    else if (d == "ci16" || d == "ri16") fmt = 3;
    else if (d == "cu16") fmt = 4;
    else if (d == "ci8" || d == "ri8") fmt = 0;
    else if (d == "cu8" || d == "ru8") fmt = 1;
    else return false;
    if (fmt == 0 || fmt == 1) be = false;
    return true;
}
} // namespace

SigmfMeta sigmfParse(const std::string& txt) {
    SigmfMeta m;
    P p{txt}; J root = p.val();
    if (p.bad || root.t != J::Obj) { m.status = 1; return m; }
    const J* g = root.get("global");
    if (!g || g->t != J::Obj) { m.status = 2; return m; }
    const J* dt = g->get("core:datatype");
    if (!dt || dt->t != J::Str) { m.status = 3; return m; }
    m.datatype = dt->s;
    int fmt = 0; bool be = false;
    if (!parseDatatype(dt->s, fmt, be)) { m.status = 4; return m; }
    m.format = fmt; m.bigEndian = be;
    m.sampleRate = num(g->get("core:sample_rate"));
    if (const J* d = g->get("core:description")) if (d->t == J::Str) m.description = d->s;
    if (const J* d = g->get("core:dataset")) if (d->t == J::Str) m.dataset = d->s;
    m.headerBytes = toU64(num(g->get("core:header_bytes")));
    m.trailingBytes = toU64(num(g->get("core:trailing_bytes")));
    const J* caps = root.get("captures");
    if (caps && caps->t == J::Arr && !caps->a.empty() && caps->a[0].t == J::Obj) {
        const J& c = caps->a[0];
        m.centerHz = num(c.get("core:frequency"));
        m.sampleStart = toU64(num(c.get("core:sample_start")));
        if (m.sampleRate <= 0) m.sampleRate = num(c.get("core:sample_rate"));
    }
    if (const J* an = root.get("annotations")) if (an->t == J::Arr) {
        m.annotations = (int)an->a.size();
        if (m.centerHz <= 0) for (const J& a : an->a) {   // no capture frequency: the middle of the first annotated band
            const double lo = num(a.get("core:freq_lower_edge")), hi = num(a.get("core:freq_upper_edge"));
            if (lo > 0 && hi > lo) { m.centerHz = (lo + hi) / 2; break; }
        }
    }
    return m;
}

std::string sigmfToolMeta(const std::string& datatype, double rate, double center, uint64_t sampleStart, const std::string& description) {
    std::string d;
    for (char c : description) { if (c == '"' || c == '\\') d += '\\'; if ((unsigned char)c >= 32) d += c; }
    char r[64], f[64];
    snprintf(r, sizeof r, "%.9g", finite(rate));
    snprintf(f, sizeof f, "%.9g", finite(center));
    return "{\n \"global\": {\"core:datatype\": \"" + datatype + "\", \"core:sample_rate\": " + r + ", \"core:version\": \"1.0.0\", \"core:description\": \"" + d +
           "\", \"core:recorder\": \"OnAir\"},\n \"captures\": [{\"core:sample_start\": " + std::to_string((unsigned long long)sampleStart) + ", \"core:frequency\": " + f +
           "}],\n \"annotations\": []\n}\n";
}

namespace {
std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}
} // namespace

std::string sigmfRecorderMeta(const std::string& datatype, double rateHz, double centerHz, const std::string& datetime, const std::string& dataset, uint64_t sampleCount,
                              const std::string& description) {
    char b[96];
    std::string j = "{\n  \"global\": {\n";
    j += "    \"core:datatype\": \"" + datatype + "\",\n";
    snprintf(b, sizeof b, "%.10g", finite(rateHz));
    j += std::string("    \"core:sample_rate\": ") + b + ",\n";
    j += "    \"core:version\": \"1.0.0\",\n";
    if (sampleCount) j += "    \"core:sample_count\": " + std::to_string((unsigned long long)sampleCount) + ",\n";
    if (!dataset.empty()) j += "    \"core:dataset\": " + jsonStr(dataset) + ",\n";
    if (!description.empty()) j += "    \"core:description\": " + jsonStr(description) + ",\n";
    j += "    \"core:recorder\": \"OnAir\"\n  },\n  \"captures\": [\n    {\n      \"core:sample_start\": 0";
    if (finite(centerHz) > 0 && centerHz < 1e18) { snprintf(b, sizeof b, "%.1f", finite(centerHz)); j += std::string(",\n      \"core:frequency\": ") + b; }
    if (!datetime.empty()) j += ",\n      \"core:datetime\": " + jsonStr(datetime);
    j += "\n    }\n  ],\n  \"annotations\": []\n}\n";
    return j;
}

// ================================================================ WAV
namespace {
uint32_t rd16w(const uint8_t* b, size_t n, size_t i) { return (i < n ? b[i] : 0u) | ((i + 1 < n ? b[i + 1] : 0u) << 8); }
uint32_t rd32w(const uint8_t* b, size_t n, size_t i) { return rd16w(b, n, i) | (rd16w(b, n, i + 2) << 16); }
} // namespace

WavFmt wavFmt(const uint8_t* b, size_t n, uint32_t len) {
    WavFmt f;
    f.tag = rd16w(b, n, 0); f.channels = rd16w(b, n, 2); f.rate = rd32w(b, n, 4); f.bits = rd16w(b, n, 14);
    if (f.tag == 0xFFFE && len >= 26) f.tag = rd16w(b, n, 24);
    return f;
}

int wavClassify(const WavFmt& f, int& format) {
    if (f.channels != 2) return 1;
    if (f.tag == 1 && f.bits == 16) format = 3;
    else if (f.tag == 1 && f.bits == 8) format = 1;
    else if (f.tag == 3 && f.bits == 32) format = 2;
    else return 2;
    return 0;
}

uint64_t wavDataEnd(uint64_t offset, uint32_t len, uint64_t size) { return (len == 0 || len == 0xFFFFFFFFu || offset + len > size) ? 0 : offset + len; }

double wavCenterFromName(const std::string& name) {
    std::string n = name;
    for (auto& c : n) c = (char)std::tolower((unsigned char)c);
    const size_t m = n.find("mhz");
    if (m == std::string::npos) return 0;
    size_t b = m;
    while (b > 0 && (isdigit((unsigned char)n[b - 1]) || n[b - 1] == '.')) b--;
    if (b >= m) return 0;
    return atof(n.substr(b, m - b).c_str()) * 1e6;
}

// ================================================================ two-line elements
namespace {
std::string trimTle(const std::string& s) { return trimWith(s, " \t\r\n"); }
bool tleNum(const std::string& s, size_t pos, size_t len, double& v) {
    if (s.size() < pos + len) return false;
    std::string f = trimTle(s.substr(pos, len));
    if (f.empty()) { v = 0; return true; }
    if (scanDecimal(f.data(), f.size()) != f.size()) return false;
    v = strtod(f.c_str(), nullptr);
    return true;
}
// "-12345-6" (an implied leading decimal point and a power of ten) as in the B* and second derivative fields
bool tleExpField(const std::string& s, size_t pos, size_t len, double& v) {
    if (s.size() < pos + len) return false;
    std::string f = trimTle(s.substr(pos, len));
    if (f.empty()) { v = 0; return true; }
    double sign = 1;
    if (f[0] == '-' || f[0] == '+') { if (f[0] == '-') sign = -1; f = f.substr(1); }
    size_t ep = f.find_first_of("+-", 1);
    std::string mant = ep == std::string::npos ? f : f.substr(0, ep), ex = ep == std::string::npos ? "0" : f.substr(ep);
    const std::string mt = "0." + trimTle(mant);
    if (scanDecimal(mt.data(), mt.size()) != mt.size()) return false;
    const double m = strtod(mt.c_str(), nullptr);
    // the exponent: a sign and digits, nothing else (strtol would stop at a NUL or accept blanks); huge values saturate
    size_t k = 0;
    bool neg = false;
    if (k < ex.size() && (ex[k] == '+' || ex[k] == '-')) { neg = ex[k] == '-'; k++; }
    if (k >= ex.size()) return false;
    int64_t x = 0;
    for (; k < ex.size(); k++) {
        if (ex[k] < '0' || ex[k] > '9') return false;
        x = x > (INT64_MAX - (ex[k] - '0')) / 10 ? INT64_MAX : x * 10 + (ex[k] - '0');
    }
    if (neg) x = -x;
    v = sign * m * std::pow(10.0, (double)x);
    return true;
}
bool tleChecksumOk(const std::string& l) {
    if (l.size() < 69) return l.size() >= 68;   // some sources drop the checksum
    int sum = 0;
    for (size_t i = 0; i < 68; i++) { char c = l[i]; if (c >= '0' && c <= '9') sum += c - '0'; else if (c == '-') sum++; }
    return l[68] < '0' || l[68] > '9' || sum % 10 == l[68] - '0';
}
double jdOfYearStart(int y) {   // Julian date of 1 January 0h UTC
    return 367.0 * y - std::floor(7.0 * y / 4.0) + std::floor(275.0 / 9.0) + 1 + 1721013.5;
}
} // namespace

TleData tleParse(const std::string& a, const std::string& b, const std::string& name) {
    TleData t;
    const std::string l1 = trimTle(a), l2 = trimTle(b);
    if (l1.size() < 64 || l2.size() < 63 || l1[0] != '1' || l2[0] != '2' || !tleChecksumOk(l1) || !tleChecksumOk(l2)) return t;
    double norad = 0, yr = 0, day = 0, bstar = 0;
    if (!tleNum(l1, 2, 5, norad) || !tleNum(l1, 18, 2, yr) || !tleNum(l1, 20, 12, day) || !tleExpField(l1, 53, 8, bstar)) return t;
    double inc, raan, ecc, argp, ma, mm;
    if (!tleNum(l2, 8, 8, inc) || !tleNum(l2, 17, 8, raan) || !tleNum(l2, 34, 8, argp) || !tleNum(l2, 43, 8, ma) || !tleNum(l2, 52, 11, mm)) return t;
    if (!tleNum(l2, 26, 7, ecc)) return t;
    double n2chk = 0; if (!tleNum(l2, 2, 5, n2chk) || n2chk != norad) return t;
    const int year = (int)yr < 57 ? 2000 + (int)yr : 1900 + (int)yr;
    t.name = trimTle(name);
    if (!t.name.empty() && t.name.size() > 2 && t.name[0] == '0' && t.name[1] == ' ') t.name = trimTle(t.name.substr(2));   // the "0 NAME" 3LE form
    t.norad = (int)norad;
    t.epochJd = jdOfYearStart(year) + day - 1;
    t.bstar = bstar; t.inclDeg = inc; t.raanDeg = raan; t.ecc = ecc * 1e-7; t.argpDeg = argp; t.meanAnomDeg = ma; t.meanMotion = mm;
    if (t.name.empty()) t.name = std::to_string(t.norad);
    t.status = mm > 0 && t.ecc < 1 ? 0 : 2;
    return t;
}

std::vector<TleData> tleScan(const std::string& text) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < text.size()) {   // getline: split at \n, a final line without one counts, an empty tail does not
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string l = trimTle(text.substr(pos, e - pos));
        if (!l.empty()) lines.push_back(l);
        pos = e + 1;
    }
    std::vector<TleData> out;
    for (size_t i = 0; i + 1 < lines.size(); i++) {
        if (lines[i][0] == '1' && lines[i + 1][0] == '2') {
            const std::string name = i > 0 && lines[i - 1][0] != '1' && lines[i - 1][0] != '2' ? lines[i - 1] : "";
            TleData t = tleParse(lines[i], lines[i + 1], name);
            if (t.status == 0) { out.push_back(t); i++; }
        }
    }
    return out;
}

// ================================================================ SQLite
DbHeader dbHeader(const uint8_t* h, size_t n) {
    DbHeader r;
    if (n < 100 || memcmp(h, "SQLite format 3", 16) != 0) { r.status = 1; return r; }
    unsigned ps = (h[16] << 8) | h[17];
    if (ps == 1) ps = 65536;
    if (ps < 512 || ps > 65536 || (ps & (ps - 1))) { r.status = 2; return r; }
    const unsigned reserved = h[20], usable = ps - reserved;
    if (usable < 480) { r.status = 3; return r; }
    if (h[18] == 2 || h[19] == 2) { r.status = 4; return r; }
    const uint32_t enc = (uint32_t)dect2::endian::loadBe32(h + 56);
    if (enc != 0 && enc != 1) { r.status = 5; return r; }
    r.pageSize = ps; r.reserved = reserved; r.usable = usable;
    return r;
}

int varint(const uint8_t* p, const uint8_t* end, uint64_t& v) {
    v = 0;
    for (int i = 0; i < 8; i++) {
        if (p + i >= end) return 0;
        v = (v << 7) | (p[i] & 0x7F);
        if (!(p[i] & 0x80)) return i + 1;
    }
    if (p + 8 >= end) return 0;
    v = (v << 8) | p[8];
    return 9;
}

bool record(const std::vector<uint8_t>& r, std::vector<Col>& cols, bool wantBlob) {
    cols.clear();
    const uint8_t* p = r.data(); const uint8_t* end = p + r.size();
    uint64_t hs;
    int n = varint(p, end, hs);
    if (!n || hs > r.size() || hs < (uint64_t)n) return false;
    const uint8_t* h = p + n; const uint8_t* he = p + hs;
    const uint8_t* d = he;
    while (h < he) {
        uint64_t t;
        const int m = varint(h, he, t);
        if (!m) return false;
        h += m;
        Col c;
        size_t len = 0;
        if (t == 0) { c.isNull = true; }
        else if (t >= 1 && t <= 6) {
            static const int sz[] = {0, 1, 2, 3, 4, 6, 8};
            len = sz[t];
            if (d + len > end) return false;
            int64_t v = (int8_t)d[0];
            for (size_t k = 1; k < len; k++) v = (int64_t)(((uint64_t)v << 8) | d[k]);
            c.isNull = false; c.isInt = true; c.i = v;
        } else if (t == 7) { len = 8; if (d + len > end) return false; c.isNull = false; }
        else if (t == 8) { c.isNull = false; c.isInt = true; c.i = 0; }
        else if (t == 9) { c.isNull = false; c.isInt = true; c.i = 1; }
        else if (t >= 12) {
            len = (size_t)((t - 12) / 2);
            if (d + len > end) return false;
            c.isNull = false;
            if (t & 1) c.s.assign((const char*)d, len);
            else { c.isBlob = true; if (wantBlob) c.b.assign(d, d + len); }
        } else return false;
        d += len;
        cols.push_back(std::move(c));
    }
    return true;
}

bool payloadLocal(uint64_t total, uint64_t usable, bool indexTree, uint64_t& local, bool& overflow) {
    if (total > (1u << 28) || usable < 36) return false;
    const uint64_t U = usable;
    const uint64_t X = indexTree ? ((U - 12) * 64 / 255) - 23 : U - 35;
    local = total;
    overflow = total > X;
    if (overflow) {
        const uint64_t M = ((U - 12) * 32 / 255) - 23;
        const uint64_t K = M + ((total - M) % (U - 4));
        local = K <= X ? K : M;
    }
    return true;
}

bool pageHeader(const std::vector<uint8_t>& pg, size_t base, uint8_t& type, unsigned& cells, uint32_t& rightMost) {
    if (pg.size() < base + 12) return false;
    type = pg[base];
    cells = (unsigned)dect2::endian::loadBe16(&pg[base + 3]);
    rightMost = (uint32_t)dect2::endian::loadBe32(&pg[base + 8]);
    return true;
}

bool cell(const std::vector<uint8_t>& pg, int kind, size_t base, size_t i, size_t cells, CellInfo& c) {
    const size_t hdr = base + ((kind == kTableLeaf || kind == kIndexLeaf) ? 8 : 12);
    c = CellInfo();
    const uint8_t* end = pg.data() + pg.size();
    if ((kind == kTableInterior || kind == kTableInteriorKey) && i == cells) {
        if (pg.size() < base + 12) return false;
        c.child = (uint32_t)dect2::endian::loadBe32(&pg[base + 8]);
        return true;
    }
    const size_t ptrAt = hdr + 2 * i;
    if (ptrAt + 2 > pg.size()) return false;
    size_t o = (size_t)dect2::endian::loadBe16(&pg[ptrAt]);
    uint64_t total = 0, key = 0;
    switch (kind) {
    case kTableLeaf: {
        if (o >= pg.size()) return false;
        const int a = varint(pg.data() + o, end, total); if (!a) return false;
        const int b = varint(pg.data() + o + a, end, key); if (!b) return false;
        c.total = total; c.key = key; c.payloadOff = (uint32_t)(o + a + b);
        return true;
    }
    case kTableInterior:
        if (o + 4 > pg.size()) return false;
        c.child = (uint32_t)dect2::endian::loadBe32(&pg[o]);
        return true;
    case kTableInteriorKey:
        if (o + 5 > pg.size()) return false;
        c.child = (uint32_t)dect2::endian::loadBe32(&pg[o]);
        if (!varint(pg.data() + o + 4, end, key)) return false;
        c.key = key;
        return true;
    case kIndexLeaf: {
        if (o >= pg.size()) return false;
        const int a = varint(pg.data() + o, end, total); if (!a) return false;
        c.total = total; c.payloadOff = (uint32_t)(o + a);
        return true;
    }
    case kIndexInterior: {
        if (o + 4 > pg.size()) return false;
        c.child = (uint32_t)dect2::endian::loadBe32(&pg[o]);
        o += 4;
        if (o >= pg.size()) return false;
        const int a = varint(pg.data() + o, end, total); if (!a) return false;
        c.total = total; c.payloadOff = (uint32_t)(o + a);
        return true;
    }
    default: return false;
    }
}

bool tileRow(int z, int x, int y, int64_t& row) {
    if (z < 0 || z > 30 || x < 0 || y < 0) return false;
    const int64_t n = (int64_t)1 << z;
    if (x >= n || y >= n) return false;
    row = n - 1 - y;
    return true;
}

bool isPng(const std::vector<uint8_t>& d) { return d.size() >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G'; }
bool isRaster(const std::vector<uint8_t>& d) {
    if (isPng(d)) return true;
    return d.size() >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF;
}

namespace {
std::string squash(const std::string& s) {
    std::string o;
    for (char ch : s) if (ch != ' ' && ch != '\n' && ch != '\t' && ch != '"' && ch != '`' && ch != '[' && ch != ']') o += (char)tolower((unsigned char)ch);
    return o;
}
} // namespace

bool tilesSchemaOk(const std::string& sql, std::string& squashed) {
    squashed = squash(sql);
    const std::string& ts = squashed;
    const size_t lp = ts.find('(');
    if (lp == std::string::npos || ts.compare(lp + 1, 38, "zoom_level,tile_column,tile_row,tile_d") != 0) {
        // allow "zoom_level integer, tile_column integer, ..." forms: only the names in order
        size_t a = ts.find("zoom_level"), b = ts.find("tile_column"), c2 = ts.find("tile_row"), d = ts.find("tile_data");
        if (a == std::string::npos || !(a < b && b < c2 && c2 < d)) return false;
    }
    return true;
}

bool indexCovers(const std::string& indexSql, const std::string& ts) {
    const char* pat = "(zoom_level,tile_column,tile_row)";
    if (squash(indexSql).find(pat) != std::string::npos) return true;
    return indexSql.empty() && ts.find(pat) != std::string::npos;   // autoindex of a PRIMARY KEY/UNIQUE
}

// ================================================================ results container
uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}
uint64_t signatureHash(const std::string& sig) { return fnv1a(sig) ^ 1u; }

size_t containerCheck(const std::string& blob, uint64_t hash) {
    if (blob.size() < 16 || memcmp(blob.data(), "OARS", 4) != 0) return SIZE_MAX;
    uint32_t fmt; uint64_t h;
    memcpy(&fmt, blob.data() + 4, 4); memcpy(&h, blob.data() + 8, 8);
    if (fmt != 1 || h != hash) return SIZE_MAX;
    return 16;
}
std::string containerHeader(uint64_t hash) {
    std::string s("OARS", 4);
    const uint32_t fmt = 1;
    s.append((const char*)&fmt, 4);
    s.append((const char*)&hash, 8);
    return s;
}

} // namespace dect2::filesp
#endif // !DECT2_RUST_FILES
