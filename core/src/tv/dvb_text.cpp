// DVB SI text (see dvb_text.h). convertCharset() is the iconv / Windows code page wrapper and stays C++; dvbText() is the prefix dispatch, which the Rust crate
// rust/onair_dvbsi does with DECT2_RUST_DVBSI (dvb_text_rs.cpp: it calls convertCharset back for the tables the system provides).
#include "dect2/tv/dvb_text.h"
#include "dect2/common/text_charset.h"
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <iconv.h>
#endif

namespace dect2::dvbsi {
namespace textns = dect2::text;

#ifdef _WIN32
// Windows has the ISO 8859 tables built in (code pages 28591..28599, 28603, 28605): no iconv needed.
std::string convertCharset(const char* charset, const uint8_t* p, int n) {
    std::wstring w;
    if (!strcmp(charset, "UCS-2BE")) {
        for (int i = 0; i + 1 < n; i += 2) w += (wchar_t)((p[i] << 8) | p[i + 1]);
    } else {
        UINT cp = 28591;
        int v = 0;
        if (sscanf(charset, "ISO-8859-%d", &v) == 1) {
            if ((v >= 1 && v <= 9) || v == 13 || v == 15) cp = 28590 + v;
            else if (v == 11) cp = 874;   // Thai
        } else if (!strcmp(charset, "BIG5")) cp = 950;
        else if (!strcmp(charset, "GB2312")) cp = 936;
        else if (!strcmp(charset, "EUC-KR")) cp = 949;
        const int wn = MultiByteToWideChar(cp, 0, (const char*)p, n, nullptr, 0);
        if (wn <= 0) return std::string((const char*)p, n);
        w.resize((size_t)wn);
        MultiByteToWideChar(cp, 0, (const char*)p, n, w.data(), wn);
    }
    if (w.empty()) return {};
    const int un = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)(un > 0 ? un : 0), '\0');
    if (un > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), un, nullptr, nullptr);
    return out;
}
#else
std::string convertCharset(const char* charset, const uint8_t* p, int n) {
    iconv_t cd = iconv_open("UTF-8", charset);
    if (cd == (iconv_t)-1) return std::string((const char*)p, n);
    std::string out(n * 4 + 8, '\0');
    char* in = (char*)p;
    size_t inLeft = n, outLeft = out.size();
    char* o = &out[0];
    iconv(cd, &in, &inLeft, &o, &outLeft);
    iconv_close(cd);
    out.resize(out.size() - outLeft);
    return out;
}
#endif

#ifndef DECT2_RUST_DVBSI
// Unprefixed text that is a run of valid GB 2312 (EUC-CN) pairs, as some Chinese DTMB muxes send.
static bool looksGb2312(const uint8_t* p, int n) {
    if (n < 2 || p[0] < 0xA1) return false;
    int pairs = 0;
    for (int i = 0; i < n;) {
        if (p[i] < 0x80) { i++; continue; }
        if (i + 1 >= n || p[i] < 0xA1 || p[i] > 0xF7 || p[i + 1] < 0xA1 || p[i + 1] > 0xFE) return false;
        i += 2; pairs++;
    }
    return pairs > 0;
}

std::string dvbText(const uint8_t* p, int n, bool gbDefault) {
    if (n <= 0) return {};
    std::string s;
    auto clean = [](std::string t) {
        std::string r;
        for (size_t i = 0; i < t.size(); i++) {
            unsigned char c = t[i];
            if (c == 0xC2 && i + 1 < t.size() && (unsigned char)t[i + 1] >= 0x80 && (unsigned char)t[i + 1] < 0xA0) { i++; continue; } // C1 controls
            if (c == 0x0A || c == 0x0D) { r += ' '; continue; }
            if (c >= 0x20 || c >= 0x80) r += (char)c;
        }
        return r;
    };
    unsigned first = p[0];
    if (gbDefault && looksGb2312(p, n)) s = convertCharset("GB2312", p, n);
    else if (first >= 0x20) s = textns::iso6937(p, (size_t)n);                         // default table: ISO/IEC 6937 (EN 300 468 Annex A)
    else if (first >= 0x01 && first <= 0x0B && first != 0x0 && n > 1) {
        static const char* cs[] = {"", "ISO-8859-5", "ISO-8859-6", "ISO-8859-7", "ISO-8859-8", "ISO-8859-9", "ISO-8859-10", "ISO-8859-11", "", "ISO-8859-13", "ISO-8859-14", "ISO-8859-15"};
        s = convertCharset(cs[first], p + 1, n - 1);
    } else if (first == 0x10 && n > 3) {
        char name[24]; snprintf(name, sizeof name, "ISO-8859-%d", (p[1] << 8) | p[2]);
        s = convertCharset(name, p + 3, n - 3);
    } else if (first == 0x11 && n > 1) s = convertCharset("UCS-2BE", p + 1, n - 1);
    else if (first == 0x15 && n > 1) s = std::string((const char*)p + 1, n - 1);
    else if (first == 0x14 && n > 1) s = convertCharset("BIG5", p + 1, n - 1);
    else if (first == 0x13 && n > 1) s = convertCharset("GB2312", p + 1, n - 1);   // GB-2312-1980 (EUC-CN form)
    else if (first == 0x12 && n > 1) s = convertCharset("EUC-KR", p + 1, n - 1);   // KS X 1001-2004
    else s = textns::iso6937(p + 1, (size_t)(n - 1));   // reserved selector: the default table
    return textns::clean(clean(s));
}
#endif

} // namespace dect2::dvbsi
