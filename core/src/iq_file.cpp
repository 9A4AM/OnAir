#include "dect2/source/iq_file.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace dect2 {
namespace {

// ---- a small JSON reader: enough for SigMF meta files
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
        else { char* e = nullptr; v.n = strtod(s.c_str() + i, &e); if (e == s.c_str() + i) bad = true; else { v.t = J::Num; i = (size_t)(e - s.c_str()); } }
        depth--;
        return v;
    }
};

std::string lower(std::string x) { for (auto& c : x) c = (char)tolower((unsigned char)c); return x; }
bool endsWith(const std::string& s, const char* e) { size_t n = strlen(e); return s.size() >= n && lower(s.substr(s.size() - n)) == e; }
std::filesystem::path fsPath(const std::string& p) { return std::filesystem::u8path(p); }

bool readAll(const std::string& path, std::string& out) {
    std::ifstream f(fsPath(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}

double num(const J* j, double def = 0) { return j && j->t == J::Num ? j->n : def; }

bool parseDatatype(const std::string& dt, FileFormat& fmt, bool& be, std::string& err) {
    std::string d = lower(dt);
    be = false;
    if (d.size() > 3 && d.compare(d.size() - 3, 3, "_be") == 0) { be = true; d.resize(d.size() - 3); }
    else if (d.size() > 3 && d.compare(d.size() - 3, 3, "_le") == 0) d.resize(d.size() - 3);
    if (d == "cf32") fmt = FileFormat::CF32;
    else if (d == "ci16" || d == "ri16") fmt = FileFormat::CI16;
    else if (d == "cu16") fmt = FileFormat::CU16;
    else if (d == "ci8" || d == "ri8") fmt = FileFormat::CS8;   // ri8: legacy OnAir recordings
    else if (d == "cu8" || d == "ru8") fmt = FileFormat::CU8;
    else { err = "SigMF datatype \"" + dt + "\" is not supported (complex cf32, ci16, cu16, ci8, cu8 are)"; return false; }
    if (fmt == FileFormat::CS8 || fmt == FileFormat::CU8) be = false;
    return true;
}

size_t bytesPerSample(FileFormat f) { return f == FileFormat::CF32 ? 8 : (f == FileFormat::CI16 || f == FileFormat::CU16) ? 4 : 2; }

bool probeSigmf(const std::string& metaPath, const std::string& dataPathIn, IqFileInfo& out, std::string& err) {
    std::string txt;
    if (!readAll(metaPath, txt)) { err = "cannot read " + metaPath; return false; }
    P p{txt}; J root = p.val();
    if (p.bad || root.t != J::Obj) { err = "not a valid SigMF meta file: " + metaPath; return false; }
    const J* g = root.get("global");
    if (!g || g->t != J::Obj) { err = "SigMF meta has no \"global\" section"; return false; }
    const J* dt = g->get("core:datatype");
    if (!dt || dt->t != J::Str) { err = "SigMF meta has no core:datatype"; return false; }
    IqFileInfo r;
    if (!parseDatatype(dt->s, r.format, r.bigEndian, err)) return false;
    r.sampleRate = num(g->get("core:sample_rate"));
    if (const J* d = g->get("core:description")) if (d->t == J::Str) r.description = d->s;
    uint64_t sampleStart = 0;
    const J* caps = root.get("captures");
    if (caps && caps->t == J::Arr && !caps->a.empty() && caps->a[0].t == J::Obj) {
        const J& c = caps->a[0];
        r.centerHz = num(c.get("core:frequency"));
        const double ss = num(c.get("core:sample_start"));
        if (ss > 0) sampleStart = (uint64_t)ss;
        if (r.sampleRate <= 0) r.sampleRate = num(c.get("core:sample_rate"));
    }
    if (const J* an = root.get("annotations")) if (an->t == J::Arr) {
        r.annotations = (int)an->a.size();
        if (r.centerHz <= 0) for (const J& a : an->a) {   // no capture frequency: the middle of the first annotated band
            const double lo = num(a.get("core:freq_lower_edge")), hi = num(a.get("core:freq_upper_edge"));
            if (lo > 0 && hi > lo) { r.centerHz = (lo + hi) / 2; break; }
        }
    }
    std::string data = dataPathIn;
    if (data.empty()) {
        const std::string base = metaPath.substr(0, metaPath.size() - strlen(".sigmf-meta"));
        const std::filesystem::path metaDir = fsPath(metaPath).parent_path();
        if (const J* ds = g->get("core:dataset"); ds && ds->t == J::Str && !ds->s.empty()) {
            std::filesystem::path dp = fsPath(ds->s);
            if (!dp.is_absolute()) dp = metaDir / dp;
            if (std::filesystem::exists(dp)) data = dp.string();
        }
        if (data.empty())
            for (const char* ext : {".sigmf-data", ".SIGMF-DATA"}) if (std::filesystem::exists(fsPath(base + ext))) { data = base + ext; break; }
        if (data.empty()) { err = "no IQ data file for " + metaPath + " (expected .sigmf-data or global core:dataset)"; return false; }
    }
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(fsPath(data), ec);
    if (ec) { err = "cannot read " + data; return false; }
    const uint64_t bps = bytesPerSample(r.format);
    const uint64_t hdr = (uint64_t)std::max(0.0, num(g->get("core:header_bytes")));
    const uint64_t trail = (uint64_t)std::max(0.0, num(g->get("core:trailing_bytes")));
    r.offsetBytes = hdr + sampleStart * bps;
    if (r.offsetBytes > size) { err = "SigMF sample_start is past the end of the data file"; return false; }
    if (trail && trail < size) r.endBytes = std::max(size - trail, r.offsetBytes);
    r.dataPath = data;
    out = r;
    return true;
}

uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool probeWav(const std::string& path, IqFileInfo& out, std::string& err) {
    std::ifstream f(fsPath(path), std::ios::binary);
    if (!f) { err = "cannot read " + path; return false; }
    uint8_t h[12];
    if (!f.read((char*)h, 12) || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { err = "not a WAV file: " + path; return false; }
    uint16_t tag = 0, ch = 0, bits = 0; uint32_t rate = 0;
    bool haveFmt = false;
    IqFileInfo r;
    uint8_t ck[8];
    while (f.read((char*)ck, 8)) {
        const uint32_t len = rd32(ck + 4);
        if (!memcmp(ck, "fmt ", 4)) {
            std::vector<uint8_t> b(std::max<uint32_t>(len, 16));
            if (!f.read((char*)b.data(), len)) break;
            tag = rd16(b.data()); ch = rd16(b.data() + 2); rate = rd32(b.data() + 4); bits = rd16(b.data() + 14);
            if (tag == 0xFFFE && len >= 26) tag = rd16(b.data() + 24);
            haveFmt = true;
            if (len & 1) f.seekg(1, std::ios::cur);
        } else if (!memcmp(ck, "data", 4)) {
            if (!haveFmt) { err = "WAV data before its format"; return false; }
            if (ch != 2) { err = "a WAV needs two channels (I and Q) to play as IQ, this one has " + std::to_string(ch); return false; }
            if (tag == 1 && bits == 16) r.format = FileFormat::CI16;
            else if (tag == 1 && bits == 8) r.format = FileFormat::CU8;
            else if (tag == 3 && bits == 32) r.format = FileFormat::CF32;
            else { err = "WAV sample format not supported (16-bit, 8-bit or 32-bit float are)"; return false; }
            r.sampleRate = rate;
            r.offsetBytes = (uint64_t)f.tellg();
            std::error_code ec;
            const uint64_t size = std::filesystem::file_size(fsPath(path), ec);
            r.endBytes = (len == 0 || len == 0xFFFFFFFFu || r.offsetBytes + len > size) ? 0 : r.offsetBytes + len;
            r.dataPath = path;
            // SDR programs put the centre frequency in the name: "..._100.000MHz_..."
            const std::string n = lower(fsPath(path).filename().string());
            const size_t m = n.find("mhz");
            if (m != std::string::npos) {
                size_t b = m;
                while (b > 0 && (isdigit((unsigned char)n[b - 1]) || n[b - 1] == '.')) b--;
                if (b < m) r.centerHz = atof(n.substr(b, m - b).c_str()) * 1e6;
            }
            out = r;
            return true;
        } else f.seekg((std::streamoff)len + (len & 1), std::ios::cur);
        if (!f) break;
    }
    err = "no audio data in " + path;
    return false;
}

} // namespace

bool probeIqFile(const std::string& path, IqFileInfo& out, std::string& err) {
    err.clear();
    if (endsWith(path, ".sigmf-meta")) return probeSigmf(path, "", out, err);
    if (endsWith(path, ".sigmf-data")) {
        const std::string meta = path.substr(0, path.size() - strlen(".sigmf-data")) + ".sigmf-meta";
        if (!std::filesystem::exists(fsPath(meta))) { err = "no .sigmf-meta file beside " + path; return false; }
        return probeSigmf(meta, path, out, err);
    }
    if (endsWith(path, ".wav")) return probeWav(path, out, err);
    return false;
}

const char* sigmfDatatype(FileFormat fmt, bool be) {
    switch (fmt) {
    case FileFormat::CF32: return be ? "cf32_be" : "cf32_le";
    case FileFormat::CI16: return be ? "ci16_be" : "ci16_le";
    case FileFormat::CU16: return be ? "cu16_be" : "cu16_le";
    case FileFormat::CU8: return "cu8";
    default: return "ci8";
    }
}

bool writeSigmfMeta(const std::string& metaPath, FileFormat fmt, bool be, double rate, double center, uint64_t sampleStart,
                    const std::string& description, std::string& err) {
    std::ofstream f(fsPath(metaPath), std::ios::binary);
    if (!f) { err = "cannot create " + metaPath; return false; }
    std::string d;
    for (char c : description) { if (c == '"' || c == '\\') d += '\\'; if ((unsigned char)c >= 32) d += c; }
    char b[1024];
    snprintf(b, sizeof b,
        "{\n \"global\": {\"core:datatype\": \"%s\", \"core:sample_rate\": %.9g, \"core:version\": \"1.0.0\", \"core:description\": \"%s\", \"core:recorder\": \"OnAir\"},\n"
        " \"captures\": [{\"core:sample_start\": %llu, \"core:frequency\": %.9g}],\n \"annotations\": []\n}\n",
        sigmfDatatype(fmt, be), rate, d.c_str(), (unsigned long long)sampleStart, center);
    f << b;
    if (!f) { err = "writing failed: " + metaPath; return false; }
    return true;
}

} // namespace dect2
