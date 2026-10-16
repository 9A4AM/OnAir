// Private to the DAB FIG decoder: the formatting helpers, name tables and the Fig reader shared by dab_fig*.cpp, and the prototypes of the
// per-FIG decoders. Not part of the API. Ported from etisnoop (GPL-3.0-or-later), see dab_fig.cpp.
#pragma once
#include "dect2/dab/dab_fig.h"
#include "dect2/common/text_charset.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace dect2 {

namespace dab_fig_detail {

inline std::string pf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline std::string pf(const char* fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    return b;
}

inline const char* const kLanguage[] = {
    "Unknown/not applicable", "Albanian", "Breton", "Catalan", "Croatian", "Welsh", "Czech", "Danish", "German", "English", "Spanish",
    "Esperanto", "Estonian", "Basque", "Faroese", "French", "Frisian", "Irish", "Gaelic", "Galician", "Icelandic", "Italian", "Lappish",
    "Latin", "Latvian", "Luxembourgian", "Lithuanian", "Hungarian", "Maltese", "Dutch", "Norwegian", "Occitan", "Polish", "Portuguese",
    "Romanian", "Romansh", "Serbian", "Slovak", "Slovene", "Finnish", "Swedish", "Turkish", "Flemish", "Walloon", "rfu", "rfu", "rfu",
    "rfu", "Reserved for national assignment", "Reserved for national assignment", "Reserved for national assignment",
    "Reserved for national assignment", "Reserved for national assignment", "Reserved for national assignment",
    "Reserved for national assignment", "Reserved for national assignment", "Reserved for national assignment",
    "Reserved for national assignment", "Reserved for national assignment", "Reserved for national assignment",
    "Reserved for national assignment", "Reserved for national assignment", "Reserved for national assignment",
    "Reserved for national assignment", "Background sound/clean feed", "rfu", "rfu", "rfu", "rfu", "Zulu", "Vietnamese", "Uzbek", "Urdu",
    "Ukranian", "Thai", "Telugu", "Tatar", "Tamil", "Tadzhik", "Swahili", "Sranan Tongo", "Somali", "Sinhalese", "Shona", "Serbo-Croat",
    "Rusyn", "Russian", "Quechua", "Pushtu", "Punjabi", "Persian", "Papiamento", "Oriya", "Nepali", "Ndebele", "Marathi", "Moldavian",
    "Malaysian", "Malagasay", "Macedonian", "Laotian", "Korean", "Khmer", "Kazakh", "Kannada", "Japanese", "Indonesian", "Hindi", "Hebrew",
    "Hausa", "Gurani", "Gujurati", "Greek", "Georgian", "Fulani", "Dari", "Chuvash", "Chinese", "Burmese", "Bulgarian", "Bengali",
    "Belorussian", "Bambora", "Azerbaijani", "Assamese", "Armenian", "Arabic", "Amharic"};
inline const char* language(unsigned c) { return c < sizeof kLanguage / sizeof *kLanguage ? kLanguage[c] : "invalid language code"; }

// TS 101 756 tables 14 and 15
inline const char* const kAnnounce[16] = {"Alarm", "Road Traffic flash", "Transport flash", "Warning/Service", "News flash", "Area weather flash",
    "Event announcement", "Special event", "Programme Information", "Sport report", "Financial report", "Reserved for future definition",
    "Reserved for future definition", "Reserved for future definition", "Reserved for future definition", "Reserved for future definition"};

// TS 101 756 tables 12 and 13
inline const char* const kPty[2][32] = {
    {"No programme type", "News", "Current Affairs", "Information", "Sport", "Education", "Drama", "Culture", "Science", "Varied",
     "Pop Music", "Rock Music", "Easy Listening Music", "Light Classical", "Serious Classical", "Other Music", "Weather/meteorology",
     "Finance/Business", "Children's programmes", "Social Affairs", "Religion", "Phone In", "Travel", "Leisure", "Jazz Music",
     "Country Music", "National Music", "Oldies Music", "Folk Music", "Documentary", "Not used", "Not used"},
    {"No program type", "News", "Information", "Sports", "Talk", "Rock", "Classic Rock", "Adult Hits", "Soft Rock", "Top 40", "Country",
     "Oldies", "Soft", "Nostalgia", "Jazz", "Classical", "Rhythm and Blues", "Soft Rhythm and Blues", "Foreign Language",
     "Religious Music", "Religious Talk", "Personality", "Public", "College", "rfu", "rfu", "rfu", "rfu", "rfu", "Weather", "Not used",
     "Not used"}};
inline const char* ptyName(int table, unsigned pty) {
    if (table < 1 || table > 2) return "unknown international table Id";
    return pty < 32 ? kPty[table - 1][pty] : "invalid programme type";
}

// TS 101 756 table 2
inline const char* dscty(int t) {
    switch (t) {
        case 0: return "Unspecified data";
        case 1: return "Traffic Message Channel (TMC)";
        case 2: return "Emergency Warning System (EWS)";
        case 3: return "Interactive Text Transmission System (ITTS)";
        case 4: return "Paging";
        case 5: return "Transparent Data Channel (TDC)";
        case 24: return "MPEG-2 Transport Stream, see ETSI TS 102 427";
        case 59: return "Embedded IP packets";
        case 60: return "Multimedia Object Transfer (MOT)";
        case 61: return "Proprietary service: no DSCTy signalled";
        case 62: case 63: return "Not used";
        default: return "Rfu";
    }
}

inline const char* const kCaMode[8] = {"Sub-channel CA", "Data Group CA", "MOT CA", "proprietary CA", "reserved", "reserved", "reserved", "reserved"};

inline const char* userApp(int t) {   // TS 101 756 table 16
    switch (t) {
        case 0x000: return "Reserved for future definition";
        case 0x001: return "Not used";
        case 0x002: return "MOT Slideshow";
        case 0x003: return "MOT Broadcast Web Site";
        case 0x004: return "TPEG";
        case 0x005: return "DGPS";
        case 0x006: return "TMC";
        case 0x007: return "EPG";
        case 0x008: return "DAB Java";
        case 0x009: return "DMB";
        case 0x44a: return "Journaline";
        default: return "Reserved for future applications";
    }
}

inline std::string pnum(unsigned p) {
    const unsigned minute = p & 0x3F, hour = (p >> 6) & 0x1F, day = (p >> 11) & 0x1F;
    if (day) return pf("day of month=%u time=%02u:%02u", day, hour, minute);
    if (hour == 0 && minute == 0) return "Status code: no meaningful PNum is currently provided";
    if (hour == 0 && minute == 1) return "Blank code: the current programme is not worth recording";
    if (hour == 0 && minute == 2) return "Interrupt code: the interrupt is unplanned (for example a traffic announcement)";
    return "invalid value";
}

inline std::string mjdDate(int mjd) {   // EN 62106 annex G, valid 1900-03-01 .. 2100-02-28
    static const char* wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* mo[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const int y = (int)(((double)mjd - 15078.2) / 365.25);
    const int m = (int)(((double)mjd - 14956.1 - (int)((double)y * 365.25)) / 30.6001);
    const int d = mjd - 14956 - (int)((double)y * 365.25) - (int)((double)m * 30.6001);
    const int k = (m == 14 || m == 15) ? 1 : 0;
    const int year = y + k + 1900, mon = m - 1 - k * 12 - 1;
    if (d < 0 || mon < 0 || mon > 11) return "invalid MJD";
    return pf("%s %s %02d %d", wd[((mjd + 2) % 7 + 1) % 7], mo[mon], d, year);
}

inline std::string hexOf(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) s += pf(i ? " %02X" : "%02X", p[i]);
    return s;
}

inline std::string ltoText(int lto) {   // 6 bit two's complement, half hours
    if (lto & 0x20) lto |= ~0x3F;
    return pf("%s%d:%02d", lto < 0 ? "-" : "+", std::abs(lto) >> 1, (std::abs(lto) & 1) * 30);
}

struct Fig {
    const uint8_t* f;      // the data field, zero padded after len
    int len;
    int cn, oe, pd;
    const DabFigContext* ctx;
    DabFigDecoded* out;
    void add(int lvl, const std::string& s) { out->lines.push_back({lvl, s}); }
    void err(const std::string& s) { out->errors.push_back(s); }
    uint32_t u16(int i) const { return (uint32_t)(f[i] << 8) | f[i + 1]; }
    uint32_t u32(int i) const { return ((uint32_t)f[i] << 24) | ((uint32_t)f[i + 1] << 16) | ((uint32_t)f[i + 2] << 8) | f[i + 3]; }
    void announce(int lvl, unsigned flags) { for (int j = 0; j < 16; j++) if (flags & (1u << j)) add(lvl, pf("- %s", kAnnounce[j])); }
};


// the per-FIG decoders (dab_fig_0a.cpp, dab_fig_0b.cpp, dab_fig_label.cpp)
void fig0_0(Fig& g);
void fig0_1(Fig& g);
void fig0_2(Fig& g);
void fig0_3(Fig& g);
void fig0_5(Fig& g);
void fig0_6(Fig& g);
void fig0_7(Fig& g);
void fig0_8(Fig& g);
void fig0_9(Fig& g);
void fig0_10(Fig& g);
void mainIdCheck(Fig& g, int mainId);
void fig0_11(Fig& g);
void fig0_13(Fig& g);
void fig0_14(Fig& g);
void fig0_16(Fig& g);
void fig0_17(Fig& g);
void fig0_18(Fig& g);
void fig0_19(Fig& g);
void fig0_21(Fig& g);
void fig0_22(Fig& g);
void fig0_24(Fig& g);
void fig0_25(Fig& g);
void fig0_26(Fig& g);
void fig0_27(Fig& g);
void fig0_28(Fig& g);
void fig0_31(Fig& g);
std::string labelUtf8(int charset, const uint8_t* p, size_t n);
void fig1(Fig& g, int cs, int ext);
int fig2IdLen(const uint8_t* f, int ext);
std::string ucsOrUtf8(bool ucs2, const std::vector<uint8_t>& b);
void fig2(Fig& g, int toggle, int seg, int rfu, int ext);

} // namespace dab_fig_detail
using namespace dab_fig_detail;

} // namespace dect2
