// SAME / EAS message parsing and the tables: see same_proto.h.
#include "dect2/same_proto.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace dect2 {
namespace same {

namespace {

struct Named { const char* code; const char* name; const char* kind; };

// 47 CFR 11.31 (e): the national codes and the state and local codes
const Named kOrg[] = {
    {"PEP", "Primary Entry Point System", ""}, {"CIV", "Civil authorities", ""}, {"WXR", "National Weather Service", ""}, {"EAS", "EAS participant", ""},
};

const Named kEvents[] = {
    {"EAN", "Emergency Action Notification (national)", "emergency"}, {"NIC", "National Information Center", "message"},
    {"NPT", "National Periodic Test", "test"}, {"RMT", "Required Monthly Test", "test"}, {"RWT", "Required Weekly Test", "test"},
    {"ADR", "Administrative Message", "message"}, {"AVA", "Avalanche Watch", "watch"}, {"AVW", "Avalanche Warning", "warning"},
    {"BLU", "Blue Alert", "warning"}, {"BZW", "Blizzard Warning", "warning"}, {"CAE", "Child Abduction Emergency", "emergency"},
    {"CDW", "Civil Danger Warning", "warning"}, {"CEM", "Civil Emergency Message", "emergency"}, {"CFA", "Coastal Flood Watch", "watch"},
    {"CFW", "Coastal Flood Warning", "warning"}, {"DMO", "Practice / Demo Warning", "test"}, {"DSW", "Dust Storm Warning", "warning"},
    {"EQW", "Earthquake Warning", "warning"}, {"EVI", "Evacuation Immediate", "emergency"}, {"EWW", "Extreme Wind Warning", "warning"},
    {"FFA", "Flash Flood Watch", "watch"}, {"FFS", "Flash Flood Statement", "statement"}, {"FFW", "Flash Flood Warning", "warning"},
    {"FLA", "Flood Watch", "watch"}, {"FLS", "Flood Statement", "statement"}, {"FLW", "Flood Warning", "warning"},
    {"FRW", "Fire Warning", "warning"}, {"FSW", "Flash Freeze Warning", "warning"}, {"FZW", "Freeze Warning", "warning"},
    {"HLS", "Hurricane Statement", "statement"}, {"HMW", "Hazardous Materials Warning", "warning"}, {"HUA", "Hurricane Watch", "watch"},
    {"HUW", "Hurricane Warning", "warning"}, {"HWA", "High Wind Watch", "watch"}, {"HWW", "High Wind Warning", "warning"},
    {"LAE", "Local Area Emergency", "emergency"}, {"LEW", "Law Enforcement Warning", "warning"}, {"LSW", "Land Slide Warning", "warning"},
    {"NMN", "Network Message Notification", "message"}, {"NUW", "Nuclear Power Plant Warning", "warning"}, {"POS", "Power Outage Statement", "statement"},
    {"RHW", "Radiological Hazard Warning", "warning"}, {"SMW", "Special Marine Warning", "warning"}, {"SPS", "Special Weather Statement", "statement"},
    {"SPW", "Shelter in Place Warning", "warning"}, {"SSA", "Storm Surge Watch", "watch"}, {"SSW", "Storm Surge Warning", "warning"},
    {"SVA", "Severe Thunderstorm Watch", "watch"}, {"SVR", "Severe Thunderstorm Warning", "warning"}, {"SVS", "Severe Weather Statement", "statement"},
    {"TOA", "Tornado Watch", "watch"}, {"TOE", "911 Telephone Outage Emergency", "emergency"}, {"TOR", "Tornado Warning", "warning"},
    {"TRA", "Tropical Storm Watch", "watch"}, {"TRW", "Tropical Storm Warning", "warning"}, {"TSA", "Tsunami Watch", "watch"},
    {"TSW", "Tsunami Warning", "warning"}, {"TXB", "Transmitter backup on", "message"}, {"TXF", "Transmitter carrier off", "message"},
    {"TXO", "Transmitter carrier on", "message"}, {"TXP", "Transmitter primary on", "message"}, {"VOW", "Volcano Warning", "warning"},
    {"WSA", "Winter Storm Watch", "watch"}, {"WSW", "Winter Storm Warning", "warning"},
};

const char* const kPca[10] = {"", "northwest", "north", "northeast", "west", "central", "east", "southwest", "south", "southeast"};

struct Region { int fips; const char* abbr; const char* name; double lat, lon; };

// US states, DC and territories (FIPS PUB 5-2, the codes SAME uses); the centre is rounded: it only places a marker for a whole state
const Region kUs[] = {
    {1, "AL", "Alabama", 32.8, -86.8}, {2, "AK", "Alaska", 64.0, -152.0}, {4, "AZ", "Arizona", 34.3, -111.7}, {5, "AR", "Arkansas", 34.9, -92.4},
    {6, "CA", "California", 37.2, -119.5}, {8, "CO", "Colorado", 39.0, -105.5}, {9, "CT", "Connecticut", 41.6, -72.7}, {10, "DE", "Delaware", 39.0, -75.5},
    {11, "DC", "District of Columbia", 38.9, -77.0}, {12, "FL", "Florida", 28.6, -82.4}, {13, "GA", "Georgia", 32.7, -83.4}, {15, "HI", "Hawaii", 20.8, -156.3},
    {16, "ID", "Idaho", 44.4, -114.6}, {17, "IL", "Illinois", 40.0, -89.2}, {18, "IN", "Indiana", 39.9, -86.3}, {19, "IA", "Iowa", 42.1, -93.5},
    {20, "KS", "Kansas", 38.5, -98.4}, {21, "KY", "Kentucky", 37.5, -85.3}, {22, "LA", "Louisiana", 31.1, -92.0}, {23, "ME", "Maine", 45.3, -69.2},
    {24, "MD", "Maryland", 39.0, -76.8}, {25, "MA", "Massachusetts", 42.3, -71.8}, {26, "MI", "Michigan", 44.3, -85.4}, {27, "MN", "Minnesota", 46.3, -94.3},
    {28, "MS", "Mississippi", 32.7, -89.7}, {29, "MO", "Missouri", 38.4, -92.5}, {30, "MT", "Montana", 47.0, -109.6}, {31, "NE", "Nebraska", 41.5, -99.8},
    {32, "NV", "Nevada", 39.3, -116.6}, {33, "NH", "New Hampshire", 43.7, -71.6}, {34, "NJ", "New Jersey", 40.2, -74.7}, {35, "NM", "New Mexico", 34.4, -106.1},
    {36, "NY", "New York", 42.9, -75.5}, {37, "NC", "North Carolina", 35.6, -79.4}, {38, "ND", "North Dakota", 47.5, -100.5}, {39, "OH", "Ohio", 40.3, -82.8},
    {40, "OK", "Oklahoma", 35.6, -97.5}, {41, "OR", "Oregon", 43.9, -120.6}, {42, "PA", "Pennsylvania", 40.9, -77.8}, {44, "RI", "Rhode Island", 41.7, -71.5},
    {45, "SC", "South Carolina", 33.9, -80.9}, {46, "SD", "South Dakota", 44.4, -100.2}, {47, "TN", "Tennessee", 35.9, -86.4}, {48, "TX", "Texas", 31.5, -99.3},
    {49, "UT", "Utah", 39.3, -111.7}, {50, "VT", "Vermont", 44.1, -72.7}, {51, "VA", "Virginia", 37.5, -78.8}, {53, "WA", "Washington", 47.4, -120.6},
    {54, "WV", "West Virginia", 38.6, -80.6}, {55, "WI", "Wisconsin", 44.6, -89.8}, {56, "WY", "Wyoming", 43.0, -107.5},
    {60, "AS", "American Samoa", -14.3, -170.7}, {66, "GU", "Guam", 13.4, 144.8}, {69, "MP", "Northern Mariana Islands", 15.2, 145.7},
    {72, "PR", "Puerto Rico", 18.2, -66.5}, {78, "VI", "U.S. Virgin Islands", 18.3, -64.9},
    // marine areas (NWS SAME marine codes in the state field): the Great Lakes and the coastal waters; no centre claimed (hasPos stays false)
    {57, "", "Eastern North Pacific, U.S. West Coast waters", 0, 0}, {58, "", "North Pacific near Alaska, Bering Sea and Arctic waters", 0, 0},
    {59, "", "Central Pacific, Hawaiian waters", 0, 0}, {61, "", "South Central Pacific, American Samoa waters", 0, 0},
    {65, "", "Western Pacific, Mariana Islands waters", 0, 0}, {73, "", "Western North Atlantic, U.S. East Coast south of Currituck Beach Light NC, and the Caribbean", 0, 0},
    {75, "", "Gulf of Mexico and U.S. Gulf Coast waters", 0, 0}, {77, "", "Western North Atlantic, U.S. East Coast north of Currituck Beach Light NC", 0, 0},
    {91, "", "Lake Superior", 0, 0}, {92, "", "Lake Michigan", 0, 0}, {93, "", "Lake Huron", 0, 0},
    {94, "", "St. Clair River, Detroit River and Lake St. Clair", 0, 0}, {96, "", "Lake Erie", 0, 0}, {97, "", "Lake Ontario", 0, 0},
};

// Canada: Statistics Canada province and territory codes (the SGC). Weatheradio Canada SAME location codes (CLC) are built on the province, but the
// layout of the digits is not something this table verifies: with the country set to Canada a code is named by its two-digit province part only,
// which is why the setting is the user's choice (the codes 10 - 62 overlap the US state codes) and why the text says so. County names: never.
const Region kCa[] = {
    {10, "NL", "Newfoundland and Labrador", 53.1, -57.7}, {11, "PE", "Prince Edward Island", 46.4, -63.2}, {12, "NS", "Nova Scotia", 45.0, -63.0},
    {13, "NB", "New Brunswick", 46.5, -66.2}, {24, "QC", "Quebec", 52.0, -71.5}, {35, "ON", "Ontario", 50.0, -85.0}, {46, "MB", "Manitoba", 55.0, -98.0},
    {47, "SK", "Saskatchewan", 54.5, -106.0}, {48, "AB", "Alberta", 55.0, -115.0}, {59, "BC", "British Columbia", 54.5, -125.0},
    {60, "YT", "Yukon", 64.3, -135.5}, {61, "NT", "Northwest Territories", 64.8, -119.0}, {62, "NU", "Nunavut", 70.0, -95.0},
};

const Region* findRegion(int fips, Country c) {
    if (c == CountryCanada) { for (const Region& r : kCa) if (r.fips == fips) return &r; return nullptr; }
    for (const Region& r : kUs) if (r.fips == fips) return &r;
    return nullptr;
}

bool digits(const std::string& s, size_t pos, size_t n) {
    if (pos + n > s.size()) return false;
    for (size_t i = 0; i < n; i++) if (s[pos + i] < '0' || s[pos + i] > '9') return false;
    return true;
}
int num(const std::string& s, size_t pos, size_t n) {
    int v = 0;
    for (size_t i = 0; i < n; i++) v = v * 10 + (s[pos + i] - '0');
    return v;
}

struct Counties {
    std::mutex mu;
    std::map<int, std::string> names;   // state * 1000 + county
};
Counties& counties() { static Counties c; return c; }

bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

} // namespace

const char* originatorName(const std::string& org) {
    for (const Named& n : kOrg) if (org == n.code) return n.name;
    return "";
}
const char* eventName(const std::string& code) {
    for (const Named& n : kEvents) if (code == n.code) return n.name;
    return "";
}
static const char* eventKindOf(const std::string& code) {
    for (const Named& n : kEvents) if (code == n.code) return n.kind;
    // an event the table does not have: the last letter says what kind it is (W warning, A watch, E emergency, S statement, T test)
    if (code.size() == 3) {
        switch (code[2]) { case 'W': return "warning"; case 'A': return "watch"; case 'E': return "emergency"; case 'S': return "statement"; case 'T': return "test"; default: break; }
    }
    return "";
}
const char* pcaName(int d) { return d >= 1 && d <= 9 ? kPca[d] : ""; }
const char* stateName(int fips, Country c) { const Region* r = findRegion(fips, c); return r ? r->name : ""; }
std::string stateAbbr(int fips, Country c) { const Region* r = findRegion(fips, c); return r ? r->abbr : ""; }
bool stateCentre(int fips, Country c, double& lat, double& lon) {
    const Region* r = findRegion(fips, c);
    if (!r || (r->lat == 0 && r->lon == 0)) return false;
    lat = r->lat; lon = r->lon;
    return true;
}

bool validChar(uint8_t c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || c == '+' || c == '-' || c == '/' || c == ' ';
}

std::string trimMessage(const std::string& b) {
    size_t s = b.find("ZCZC");
    size_t e = b.find("NNNN");
    if (s == std::string::npos && e == std::string::npos) return "";
    if (s == std::string::npos || (e != std::string::npos && e < s)) return b.substr(e, 4);
    std::string m = b.substr(s);
    // cut after the call sign: the dash that follows "+TTTT-JJJHHMM-LLLLLLLL"
    const size_t p = m.find('+');
    if (p != std::string::npos && m.size() > p + 14 && m[p + 5] == '-' && m[p + 13] == '-') {
        const size_t d = m.find('-', p + 14);
        if (d != std::string::npos && d <= p + 14 + 8) return m.substr(0, d + 1);
    }
    while (!m.empty() && !validChar((uint8_t)m.back())) m.pop_back();
    return m;
}

Location locate(const std::string& code, Country c) {
    Location l;
    l.code = code;
    if (code.size() != 6 || !digits(code, 0, 6)) { l.text = code; return l; }
    l.pca = num(code, 0, 1);
    l.state = num(code, 1, 2);
    l.county = num(code, 3, 3);
    l.pcaName = pcaName(l.pca);
    const Region* r = findRegion(l.state, c);
    if (r) {
        l.stateName = r->name;
        l.hasPos = (r->lat != 0 || r->lon != 0);
        l.lat = r->lat; l.lon = r->lon;
    }
    if (l.county) l.countyName = countyNameOf(l.state, l.county);
    char b[160];
    std::string place;
    if (l.county == 0) place = std::string(l.pca ? "" : "all of ") + (r ? r->name : "state code ") + (r ? "" : std::to_string(l.state));
    else if (!l.countyName.empty()) place = l.countyName + (r ? std::string(", ") + (r->abbr[0] ? r->abbr : r->name) : "");
    else { snprintf(b, sizeof b, "county FIPS %03d%s%s", l.county, r ? ", " : " (state code ", r ? r->name : (std::to_string(l.state) + ")").c_str()); place = b; }
    if (l.pca) l.text = std::string(l.pcaName) + " part of " + place;
    else l.text = place;
    if (!l.text.empty()) l.text[0] = (char)std::toupper((unsigned char)l.text[0]);
    return l;
}

Message parse(const std::string& text, Country c) {
    Message m;
    m.raw = text;
    if (text.compare(0, 4, "NNNN") == 0 && text.size() == 4) { m.valid = true; m.eom = true; return m; }
    auto fail = [&](const char* why) { m.err = why; return m; };
    if (text.compare(0, 5, "ZCZC-") != 0) return fail("does not start with ZCZC-");
    size_t i = 5;
    // ORG-EEE-
    for (int k = 0; k < 3; k++) if (i + k >= text.size() || !(text[i + k] >= 'A' && text[i + k] <= 'Z')) return fail("bad originator");
    m.org = text.substr(i, 3);
    i += 3;
    if (i >= text.size() || text[i] != '-') return fail("missing dash after the originator");
    i++;
    for (int k = 0; k < 3; k++) if (i + k >= text.size() || !(text[i + k] >= 'A' && text[i + k] <= 'Z')) return fail("bad event code");
    m.event = text.substr(i, 3);
    i += 3;
    if (i >= text.size() || text[i] != '-') return fail("missing dash after the event code");
    i++;
    // locations until '+'
    for (;;) {
        if (!digits(text, i, 6)) return fail("bad location code");
        m.locs.push_back(locate(text.substr(i, 6), c));
        i += 6;
        if (i >= text.size()) return fail("message ends in the location list");
        if (text[i] == '+') { i++; break; }
        if (text[i] != '-') return fail("bad separator in the location list");
        i++;
        if (m.locs.size() >= 31) return fail("more than 31 locations");
    }
    if (!digits(text, i, 4)) return fail("bad purge time");
    const int hh = num(text, i, 2), mm = num(text, i + 2, 2);
    if (mm > 59) return fail("purge time minutes over 59");
    m.purgeMin = hh * 60 + mm;
    if (m.purgeMin == 0) return fail("purge time zero");
    i += 4;
    if (i >= text.size() || text[i] != '-') return fail("missing dash after the purge time");
    i++;
    if (!digits(text, i, 7)) return fail("bad issue time");
    m.issueDay = num(text, i, 3); m.issueHour = num(text, i + 3, 2); m.issueMin = num(text, i + 5, 2);
    if (m.issueDay < 1 || m.issueDay > 366) return fail("issue day out of range");
    if (m.issueHour > 23 || m.issueMin > 59) return fail("issue time out of range");
    i += 7;
    if (i >= text.size() || text[i] != '-') return fail("missing dash after the issue time");
    i++;
    const size_t d = text.find('-', i);
    if (d == std::string::npos) return fail("missing dash after the call sign");
    m.callsign = text.substr(i, d - i);
    if (m.callsign.empty() || m.callsign.size() > 8) return fail("call sign length");
    for (char ch : m.callsign) if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '/' || ch == ' ')) return fail("bad call sign");
    if (d + 1 != text.size()) return fail("text after the message");
    m.orgName = originatorName(m.org);
    m.eventName = eventName(m.event);
    m.eventKind = eventKindOf(m.event);
    m.valid = true;
    return m;
}

int64_t issueEpoch(int year, int day, int hour, int minute) {
    if (year < 1970 || day < 1 || day > (leap(year) ? 366 : 365) || hour < 0 || hour > 23 || minute < 0 || minute > 59) return 0;
    // days from 1970-01-01 to January 1st of the year, then the day of the year
    int64_t days = 0;
    for (int y = 1970; y < year; y++) days += leap(y) ? 366 : 365;
    days += day - 1;
    return days * 86400 + hour * 3600 + minute * 60;
}

std::string issueText(int year, int day, int hour, int minute) {
    static const int ml[2][12] = {{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}, {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};
    static const char* const mn[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (!issueEpoch(year, day, hour, minute)) return "";
    int d = day, mo = 0;
    while (mo < 12 && d > ml[leap(year) ? 1 : 0][mo]) { d -= ml[leap(year) ? 1 : 0][mo]; mo++; }
    char b[48];
    snprintf(b, sizeof b, "%s %02d %02d:%02d UTC", mn[mo], d, hour, minute);
    return b;
}

std::string purgeText(int minutes) {
    char b[32];
    if (minutes % 60 == 0) snprintf(b, sizeof b, "%d h", minutes / 60);
    else if (minutes < 60) snprintf(b, sizeof b, "%d min", minutes);
    else snprintf(b, sizeof b, "%d h %d min", minutes / 60, minutes % 60);
    return b;
}

// ---- the county table

static std::vector<std::string> splitCsv(const std::string& line) {
    std::vector<std::string> f;
    std::string cur;
    bool q = false;
    for (char ch : line) {
        if (ch == '"') q = !q;
        else if (ch == ',' && !q) { f.push_back(cur); cur.clear(); }
        else if (ch != '\r') cur += ch;
    }
    f.push_back(cur);
    for (auto& s : f) {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a])) a++;
        while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
        s = s.substr(a, b - a);
    }
    return f;
}

static bool allDigits(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char ch : s) if (ch < '0' || ch > '9') return false;
    return true;
}

size_t loadCountyCsv(const std::string& text, std::string* err) {
    std::map<int, std::string> t;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const auto f = splitCsv(line);
        int st = -1, co = -1;
        std::string name;
        if (f.size() >= 4 && f[0].size() == 2 && !std::isdigit((unsigned char)f[0][0]) && allDigits(f[1], 2) && allDigits(f[2], 3)) {   // Census national_county.txt
            st = atoi(f[1].c_str()); co = atoi(f[2].c_str()); name = f[3];
        } else if (f.size() >= 3 && allDigits(f[0], 2) && allDigits(f[1], 3)) {
            st = atoi(f[0].c_str()); co = atoi(f[1].c_str()); name = f[2];
        } else if (f.size() >= 2 && allDigits(f[0], 5)) {
            st = atoi(f[0].substr(0, 2).c_str()); co = atoi(f[0].substr(2).c_str()); name = f[1];
        } else if (f.size() >= 2 && allDigits(f[0], 6)) {   // a full SAME code: the PCA digit is ignored
            st = atoi(f[0].substr(1, 2).c_str()); co = atoi(f[0].substr(3).c_str()); name = f[1];
        }
        if (st < 0 || co <= 0 || name.empty()) continue;   // header, blank or other lines; county 000 is the state
        t[st * 1000 + co] = name;
    }
    if (t.empty()) { if (err) *err = "no county lines found (expected SSCCC,Name or SS,CCC,Name or the Census national_county.txt)"; return 0; }
    std::lock_guard<std::mutex> lk(counties().mu);
    counties().names = std::move(t);
    return counties().names.size();
}

size_t loadCountyFile(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + path; return 0; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return loadCountyCsv(ss.str(), err);
}

size_t countyCount() { std::lock_guard<std::mutex> lk(counties().mu); return counties().names.size(); }
void clearCounties() { std::lock_guard<std::mutex> lk(counties().mu); counties().names.clear(); }
std::string countyNameOf(int state, int county) {
    std::lock_guard<std::mutex> lk(counties().mu);
    const auto it = counties().names.find(state * 1000 + county);
    return it == counties().names.end() ? std::string() : it->second;
}

} // namespace same
} // namespace dect2
