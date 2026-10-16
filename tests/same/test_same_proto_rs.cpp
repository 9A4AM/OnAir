// Differential test: the C++ SAME layer (a generated copy in dect2::same_ref, see cmake/gen_rs_ref.cmake) against the Rust one (dect2::same when
// DECT2_RUST is used). The same inputs go to both and every result has to be identical: built and known messages, the tables over every code,
// the time helpers over their whole range, county tables, and ONAIR_DIFF_ITERS (default 200000) seeded random / mutated / truncated inputs per family.
#include "dect2/same/same_proto.h"
#include "same_proto_ref.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
namespace A = dect2::same;
namespace R = dect2::same_ref;

static long fails = 0, checks = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)

static bool dEq(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }
static bool locEq(const A::Location& a, const R::Location& b) {
    return a.code == b.code && a.pca == b.pca && a.state == b.state && a.county == b.county && a.pcaName == b.pcaName && a.stateName == b.stateName &&
           a.countyName == b.countyName && a.text == b.text && a.hasPos == b.hasPos && dEq(a.lat, b.lat) && dEq(a.lon, b.lon);
}
static bool msgEq(const A::Message& a, const R::Message& b) {
    if (a.valid != b.valid || a.eom != b.eom || a.raw != b.raw || a.err != b.err || a.org != b.org || a.orgName != b.orgName || a.event != b.event ||
        a.eventName != b.eventName || a.eventKind != b.eventKind || a.purgeMin != b.purgeMin || a.issueDay != b.issueDay || a.issueHour != b.issueHour ||
        a.issueMin != b.issueMin || a.callsign != b.callsign || a.locs.size() != b.locs.size())
        return false;
    for (size_t i = 0; i < a.locs.size(); i++) if (!locEq(a.locs[i], b.locs[i])) return false;
    return true;
}

static void compareMessage(const std::string& s) {
    for (int c = 0; c < 2; c++) {
        const A::Message a = A::parse(s, (A::Country)c);
        const R::Message b = R::parse(s, (R::Country)c);
        SAME(msgEq(a, b), "parse '%s' country %d: err '%s' / '%s'", s.c_str(), c, a.err.c_str(), b.err.c_str());
    }
    SAME(A::trimMessage(s) == R::trimMessage(s), "trimMessage '%s'", s.c_str());
}

static std::mt19937 g(12345);
static std::string rnd(const char* alphabet, size_t n) { std::string s; for (size_t i = 0; i < n; i++) s += alphabet[g() % strlen(alphabet)]; return s; }

static std::string validMessage() {
    static const char* orgs[] = {"WXR", "EAS", "PEP", "CIV", "ABC"};
    static const char* events[] = {"TOR", "SVR", "RWT", "EAN", "FFW", "ZZW", "QQA", "BLU", "HLS", "XYZ"};
    std::string s = std::string("ZCZC-") + orgs[g() % 5] + "-" + events[g() % 10];
    const int n = 1 + (int)(g() % 5);
    for (int i = 0; i < n; i++) {
        char b[8];
        snprintf(b, sizeof b, "%d%02d%03d", (int)(g() % 10), (int)(g() % 100), (int)(g() % 1000));
        s += std::string("-") + b;
    }
    char t[48];
    snprintf(t, sizeof t, "+%02d%02d-%03d%02d%02d-", (int)(g() % 12), (int)(g() % 60), 1 + (int)(g() % 366), (int)(g() % 24), (int)(g() % 60));
    s += t;
    s += rnd("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/ ", 1 + g() % 8) + "-";
    return s;
}

static std::string mutate(std::string s) {
    const int k = 1 + (int)(g() % 4);
    for (int i = 0; i < k && !s.empty(); i++) {
        switch (g() % 6) {
        case 0: s[g() % s.size()] = (char)(g() & 0xFF); break;
        case 1: s[g() % s.size()] = "0123456789+-/ ZCNAW"[g() % 19]; break;
        case 2: s.erase(g() % s.size(), 1 + g() % 3); break;
        case 3: s.insert(g() % (s.size() + 1), rnd("0123456789-+ ZN", 1 + g() % 3)); break;
        case 4: s.resize(g() % (s.size() + 1)); break;
        default: s = rnd("-+0123456789ZCNAWXYR/ ", g() % 80); break;
        }
    }
    if (g() % 4 == 0) s = rnd("\x01\x7f\xff-ab", g() % 5) + s + rnd("\x01\xff-ab+", g() % 5);
    return s;
}

static void compareCounties(const std::string& csv) {
    std::string e1, e2;
    const size_t n1 = A::loadCountyCsv(csv, &e1), n2 = R::loadCountyCsv(csv, &e2);
    SAME(n1 == n2 && e1 == e2, "loadCountyCsv n %zu/%zu", n1, n2);
    SAME(A::countyCount() == R::countyCount(), "countyCount");
    for (int i = 0; i < 40; i++) {
        const int st = (int)(g() % 100), co = (int)(g() % 1000);
        SAME(A::countyNameOf(st, co) == R::countyNameOf(st, co), "countyNameOf %d %d", st, co);
    }
}

int main() {
    const char* it = getenv("ONAIR_DIFF_ITERS");
    const long iters = it ? atol(it) : 200000;
    printf("same_proto_rs: implementation under test is %s\n", "dect2::same (Rust when DECT2_RUST_SAME)");

    // tables: every three letter code, every digit
    for (int a = 'A'; a <= 'Z'; a++) for (int b = 'A'; b <= 'Z'; b++) for (int c = 'A'; c <= 'Z'; c++) {
        const std::string s = {(char)a, (char)b, (char)c};
        SAME(!strcmp(A::originatorName(s), R::originatorName(s)), "originatorName %s", s.c_str());
        SAME(!strcmp(A::eventName(s), R::eventName(s)), "eventName %s", s.c_str());
    }
    for (const char* s : {"", "T", "TO", "TORR", "tor", "WXR ", "PEP", "EAN", "NIC"}) {
        SAME(!strcmp(A::originatorName(s), R::originatorName(s)), "originatorName '%s'", s);
        SAME(!strcmp(A::eventName(s), R::eventName(s)), "eventName '%s'", s);
    }
    for (int d = -3; d < 14; d++) SAME(!strcmp(A::pcaName(d), R::pcaName(d)), "pcaName %d", d);
    for (int c = 0; c < 2; c++) for (int f = -3; f < 130; f++) {
        SAME(!strcmp(A::stateName(f, (A::Country)c), R::stateName(f, (R::Country)c)), "stateName %d", f);
        SAME(A::stateAbbr(f, (A::Country)c) == R::stateAbbr(f, (R::Country)c), "stateAbbr %d", f);
        double l1 = 1, o1 = 2, l2 = 1, o2 = 2;
        const bool r1 = A::stateCentre(f, (A::Country)c, l1, o1), r2 = R::stateCentre(f, (R::Country)c, l2, o2);
        SAME(r1 == r2 && dEq(l1, l2) && dEq(o1, o2), "stateCentre %d", f);
    }
    for (int c = 0; c < 256; c++) SAME(A::validChar((uint8_t)c) == R::validChar((uint8_t)c), "validChar %d", c);
    for (int d = 0; d < 1000; d++) {   // the event kind of unknown codes comes through parse
        char b[64]; snprintf(b, sizeof b, "ZCZC-WXR-%c%c%c-029001+0030-1231415-KEAX/NWS-", 'A' + d % 26, 'A' + (d / 26) % 26, "WAESTX"[d % 6]);
        compareMessage(b);
    }

    // known messages
    for (const char* s : {"ZCZC-WXR-TOR-029001-029003+0030-1231415-KEAX/NWS-", "NNNN", "ZCZC-EAS-RWT-048201+0015-3661200-WABC/FM -", "ZCZC-PEP-EAN-099000+0100-0010000-PEP/1   -",
                          "ZCZC-WXR-SVR-129201+0045-2051620-KWNS/NWS-", "xxZCZC-WXR-TOR-029001+0030-1231415-KEAX/NWS-yy\xff", "garbage NNNN more", "ZCZC-WXR-TOR-029001+0030-1231415-KEAX/NWS", ""})
        compareMessage(s);

    // county tables (own copy per implementation), then locations with and without names
    compareCounties("SSCCC,Name\n48201,Harris County\n48113,\"Dallas, TX\"\n01,001,Autauga\nAL,01,003,Baldwin County,H1\n129201,Pca County\n0,0,Zero\n");
    for (long i = 0; i < iters / 20; i++) {
        std::string csv;
        const int lines = (int)(g() % 12);
        for (int k = 0; k < lines; k++) {
            switch (g() % 7) {
            case 0: csv += rnd("0123456789", 5) + "," + rnd("abcXYZ \"", g() % 12); break;
            case 1: csv += rnd("0123456789", 2) + "," + rnd("0123456789", 3) + "," + rnd("abc ", g() % 8); break;
            case 2: csv += rnd("ABab", 2) + "," + rnd("0123456789", 2) + "," + rnd("0123456789", 3) + ",\"" + rnd("ab, ", g() % 9) + "\",H1"; break;
            case 3: csv += rnd("0123456789", 6) + "," + rnd("xyz", g() % 5); break;
            case 4: csv += rnd("0123456789,\" \t\r", g() % 20); break;
            case 5: csv += mutate(rnd("0123456789,", 8)); break;
            default: csv += "State,County"; break;
            }
            csv += (g() % 5 ? "\n" : "\r\n");
        }
        compareCounties(csv);
    }
    compareCounties("48201,Harris County\n01001,Autauga County\n48000,All of Texas\n");   // the table the locations below use

    // locations: random codes, known and unknown states, both countries
    for (long i = 0; i < iters; i++) {
        std::string code;
        switch (g() % 5) {
        case 0: code = rnd("0123456789", 6); break;
        case 1: { char b[8]; snprintf(b, sizeof b, "%d%02d%03d", (int)(g() % 10), (int)(g() % 100), (int)(g() % 3) ? 201 : (int)(g() % 1000)); code = b; } break;
        case 2: { char b[8]; snprintf(b, sizeof b, "%d%02d%03d", (int)(g() % 10), g() % 2 ? 48 : 1, g() % 2 ? 201 : 1); code = b; } break;
        case 3: code = rnd("0123456789", g() % 9); break;
        default: code = mutate(rnd("0123456789", 6)); break;
        }
        for (int c = 0; c < 2; c++) {
            const A::Location a = A::locate(code, (A::Country)c);
            const R::Location b = R::locate(code, (R::Country)c);
            SAME(locEq(a, b), "locate '%s' country %d: '%s' / '%s'", code.c_str(), c, a.text.c_str(), b.text.c_str());
        }
    }

    // messages: valid ones, mutations, random bytes; the county table above is loaded in both
    for (long i = 0; i < iters; i++) {
        std::string s;
        switch (g() % 8) {
        case 0: s = validMessage(); break;
        case 1: case 2: case 3: s = mutate(validMessage()); break;
        case 4: s = validMessage(); s.resize(g() % (s.size() + 1)); break;
        case 5: s = rnd("ZCNW-+0123456789/ ABCXYZ", g() % 90); break;
        case 6: s = rnd("\x01\x02\x7f\x80\xff-+ZCNA09", g() % 90); break;
        default: { std::string v = validMessage(); s = rnd("xx\xff -", g() % 4) + v + rnd("-zZ\x01", g() % 4); } break;
        }
        compareMessage(s);
    }

    // time helpers: the whole range of days and times, years around the leap rules and wild ones
    for (int year : {1969, 1970, 1971, 1999, 2000, 2023, 2024, 2025, 2100, 2400, 3000, 9999})
        for (int day = -1; day <= 368; day++) for (int hour : {-1, 0, 7, 23, 24}) for (int minute : {-1, 0, 30, 59, 60}) {
            SAME(A::issueEpoch(year, day, hour, minute) == R::issueEpoch(year, day, hour, minute), "issueEpoch %d %d %d %d", year, day, hour, minute);
            SAME(A::issueText(year, day, hour, minute) == R::issueText(year, day, hour, minute), "issueText %d %d %d %d", year, day, hour, minute);
        }
    for (long i = 0; i < iters; i++) {
        const int year = 1960 + (int)(g() % 8000), day = (int)(g() % 400) - 10, hour = (int)(g() % 30) - 3, minute = (int)(g() % 70) - 5;
        SAME(A::issueEpoch(year, day, hour, minute) == R::issueEpoch(year, day, hour, minute), "issueEpoch %d %d %d %d", year, day, hour, minute);
        SAME(A::issueText(year, day, hour, minute) == R::issueText(year, day, hour, minute), "issueText %d %d %d %d", year, day, hour, minute);
        const int mins = (int)(g() % 12000) - 100;
        SAME(A::purgeText(mins) == R::purgeText(mins), "purgeText %d", mins);
    }
    for (int m : {0, 1, 59, 60, 61, 119, 120, -1, -60, -61, 1000000, 2147483647, -2147483647 - 1}) SAME(A::purgeText(m) == R::purgeText(m), "purgeText %d", m);

    printf("same_proto_rs: %ld checks, %ld mismatches\n", checks, fails);
    return fails ? 1 : 0;
}
