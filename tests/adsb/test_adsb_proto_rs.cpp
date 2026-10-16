// Differential test: the C++ ADS-B / Mode S message layer (a generated copy in dect2::adsb_ref, see cmake/gen_rs_ref.cmake) against the Rust one
// (dect2::adsb when DECT2_RUST is used). The same inputs go to both and every result has to be identical (doubles bit for bit): every code of the
// altitude / identity / character tables, CRC and single-bit correction on frames and damaged frames, CPR with real and absurd values (NaN references
// too), geometry, Comm-B registers built to pass each BDS inference plus damaged ones, and decoded messages of every DF and type code. ONAIR_DIFF_ITERS
// (default 200000) seeded random inputs per family.
#include "dect2/adsb/adsb_msg.h"
#include "adsb_msg_ref.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <random>
namespace A = dect2::adsb;
namespace R = dect2::adsb_ref;

static long fails = 0, checks = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)

static bool dEq(double a, double b) { return memcmp(&a, &b, sizeof a) == 0 || (std::isnan(a) && std::isnan(b)); }
static std::mt19937_64 g(20261016);

static bool commbEq(const A::CommB& a, const R::CommB& b) {
    return a.bds == b.bds &&
           a.callsign == b.callsign &&
           a.hasSelAlt == b.hasSelAlt &&
           a.selAltMcpFt == b.selAltMcpFt &&
           a.hasSelAltFms == b.hasSelAltFms &&
           a.selAltFmsFt == b.selAltFmsFt &&
           a.hasBaro == b.hasBaro &&
           dEq(a.baroMb, b.baroMb) &&
           a.hasRoll == b.hasRoll &&
           dEq(a.rollDeg, b.rollDeg) &&
           a.hasTrack == b.hasTrack &&
           dEq(a.trackDeg, b.trackDeg) &&
           a.hasGs == b.hasGs &&
           a.gsKt == b.gsKt &&
           a.hasTrackRate == b.hasTrackRate &&
           dEq(a.trackRateDps, b.trackRateDps) &&
           a.hasTas == b.hasTas &&
           a.tasKt == b.tasKt &&
           a.hasHeading == b.hasHeading &&
           dEq(a.headingDeg, b.headingDeg) &&
           a.hasIas == b.hasIas &&
           a.iasKt == b.iasKt &&
           a.hasMach == b.hasMach &&
           dEq(a.mach, b.mach) &&
           a.hasBaroRate == b.hasBaroRate &&
           a.baroRateFpm == b.baroRateFpm &&
           a.hasInertialRate == b.hasInertialRate &&
           a.inertialRateFpm == b.inertialRateFpm &&
           a.subnetVersion == b.subnetVersion &&
           a.acasOperating == b.acasOperating &&
           a.continuation == b.continuation &&
           a.gicbMask == b.gicbMask &&
           a.ara == b.ara &&
           a.rac == b.rac &&
           a.raTerminated == b.raTerminated &&
           a.multipleThreats == b.multipleThreats &&
           a.tti == b.tti &&
           a.threatIcao == b.threatIcao &&
           a.fom == b.fom &&
           a.hasWind == b.hasWind &&
           a.windKt == b.windKt &&
           dEq(a.windDirDeg, b.windDirDeg) &&
           a.hasSat == b.hasSat &&
           dEq(a.satC, b.satC) &&
           a.hasPressure == b.hasPressure &&
           a.pressureHpa == b.pressureHpa &&
           a.hasHumidity == b.hasHumidity &&
           dEq(a.humidityPct, b.humidityPct) &&
           a.turbulence == b.turbulence &&
           a.windShear == b.windShear &&
           a.microburst == b.microburst &&
           a.icing == b.icing &&
           a.wakeVortex == b.wakeVortex &&
           a.hasRadioHeight == b.hasRadioHeight &&
           a.radioHeightFt == b.radioHeightFt;
}
static bool msgEq(const A::Msg& a, const R::Msg& b) {
    return a.df == b.df &&
           a.nbits == b.nbits &&
           a.icao == b.icao &&
           a.ca == b.ca &&
           a.ground == b.ground &&
           a.alert == b.alert &&
           a.spi == b.spi &&
           a.fs == b.fs &&
           a.vs == b.vs &&
           a.hasAlt == b.hasAlt &&
           a.altFt == b.altFt &&
           a.altGnss == b.altGnss &&
           a.hasSquawk == b.hasSquawk &&
           a.squawk == b.squawk &&
           a.tc == b.tc &&
           a.st == b.st &&
           a.hasIdent == b.hasIdent &&
           a.callsign == b.callsign &&
           a.catSet == b.catSet &&
           a.catCode == b.catCode &&
           a.hasCpr == b.hasCpr &&
           a.cprOdd == b.cprOdd &&
           a.surface == b.surface &&
           a.cprLat == b.cprLat &&
           a.cprLon == b.cprLon &&
           a.hasMove == b.hasMove &&
           dEq(a.moveKt, b.moveKt) &&
           a.hasTrack == b.hasTrack &&
           dEq(a.trackDeg, b.trackDeg) &&
           a.hasVel == b.hasVel &&
           dEq(a.speedKt, b.speedKt) &&
           a.speedKind == b.speedKind &&
           a.hasHeading == b.hasHeading &&
           dEq(a.headingDeg, b.headingDeg) &&
           a.headingIsTrack == b.headingIsTrack &&
           a.hasVrate == b.hasVrate &&
           a.vrateFpm == b.vrateFpm &&
           a.vrateBaro == b.vrateBaro &&
           a.hasGnssDiff == b.hasGnssDiff &&
           a.gnssDiffFt == b.gnssDiffFt &&
           a.emergency == b.emergency &&
           a.hasSelAlt == b.hasSelAlt &&
           a.selAltFt == b.selAltFt &&
           a.selAltFms == b.selAltFms &&
           a.hasBaro == b.hasBaro &&
           dEq(a.baroMb, b.baroMb) &&
           a.hasSelHdg == b.hasSelHdg &&
           dEq(a.selHdgDeg, b.selHdgDeg) &&
           a.tsNacp == b.tsNacp &&
           a.autopilot == b.autopilot &&
           a.vnav == b.vnav &&
           a.altHold == b.altHold &&
           a.approach == b.approach &&
           a.lnav == b.lnav &&
           a.tsModeValid == b.tsModeValid &&
           a.adsbVersion == b.adsbVersion &&
           a.nacp == b.nacp &&
           a.nicSuppA == b.nicSuppA &&
           a.sil == b.sil &&
           a.gva == b.gva &&
           a.nicBaro == b.nicBaro &&
           a.callsign == b.callsign && commbEq(a.commb, b.commb);
}

using Bytes = std::vector<uint8_t>;
static Bytes randBytes(size_t n) { Bytes v(n); for (auto& c : v) c = (uint8_t)g(); return v; }
static double rndLat() { return (double)(int64_t)(g() % 18000001) / 100000.0 - 90.0; }
static double rndLon() { return (double)(int64_t)(g() % 36000001) / 100000.0 - 180.0; }
static double odd() {   // absurd doubles now and then
    static const double v[] = {std::numeric_limits<double>::quiet_NaN(), INFINITY, -INFINITY, 0.0, -0.0, 90.0, -90.0, 180.0, -180.0, 360.0, 1e300, -1e300, 1e-300, 89.999999, 87.0, -87.0};
    return v[g() % (sizeof v / sizeof v[0])];
}

// ---- Comm-B registers built to pass the checks of one BDS code (then maybe damaged)
static void setb(Bytes& mb, int first, int count, uint32_t v) { A::setBits(mb.data(), first, count, v); }
static uint32_t rb(int bits) { return (uint32_t)(g() & ((1ull << bits) - 1)); }
static Bytes makeCommB(int kind) {
    Bytes mb(7, 0);
    switch (kind) {
    case 0: mb[0] = 0x20; { std::string s = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "; for (int i = 0; i < 8; i++) setb(mb, 9 + 6 * i, 6, (uint32_t)A::charTo6bit(s[g() % s.size()])); } break;
    case 1: // 4,0
        setb(mb, 1, 1, 1); setb(mb, 2, 12, rb(12)); setb(mb, 14, 1, g() & 1); setb(mb, 15, 12, rb(12)); setb(mb, 27, 1, 1); setb(mb, 28, 12, 1000 + g() % 1000);
        if (!(mb[1] | mb[2])) setb(mb, 2, 12, 5);
        { if (g() % 3 == 0) setb(mb, 1, 1, 0), setb(mb, 2, 12, 0); } break;
    case 2: // 5,0
        for (int st : {1, 12, 24, 35, 46}) if (g() % 6) setb(mb, st, 1, 1);
        if (mb[0] & 0x80) setb(mb, 2, 10, rb(10) % 200);
        setb(mb, 13, 11, rb(11)); setb(mb, 25, 10, rb(10) % 300); setb(mb, 36, 10, rb(10)); setb(mb, 47, 10, rb(10) % 250);
        // zero the values whose status is 0
        if (!A::getBits(mb.data(), 1, 1)) setb(mb, 2, 10, 0);
        if (!A::getBits(mb.data(), 12, 1)) setb(mb, 13, 11, 0);
        if (!A::getBits(mb.data(), 24, 1)) setb(mb, 25, 10, 0);
        if (!A::getBits(mb.data(), 35, 1)) setb(mb, 36, 10, 0);
        if (!A::getBits(mb.data(), 46, 1)) setb(mb, 47, 10, 0);
        break;
    case 3: // 6,0
        for (int st : {1, 13, 24, 35, 46}) if (g() % 6) setb(mb, st, 1, 1);
        setb(mb, 2, 11, rb(11)); setb(mb, 14, 10, rb(10) % 400); setb(mb, 25, 10, rb(10) % 250); setb(mb, 36, 10, rb(10)); setb(mb, 47, 10, rb(10));
        if (!A::getBits(mb.data(), 1, 1)) setb(mb, 2, 11, 0);
        if (!A::getBits(mb.data(), 13, 1)) setb(mb, 14, 10, 0);
        if (!A::getBits(mb.data(), 24, 1)) setb(mb, 25, 10, 0);
        if (!A::getBits(mb.data(), 35, 1)) setb(mb, 36, 10, 0);
        if (!A::getBits(mb.data(), 46, 1)) setb(mb, 47, 10, 0);
        break;
    case 4: mb[0] = 0x10; setb(mb, 9, 1, g() & 1); setb(mb, 17, 7, g() % 7); setb(mb, 38, 1, g() & 1); setb(mb, 25, 13, rb(13) & 0x1fff); break;
    case 5: setb(mb, 1, 24, rb(24) | 0x200000); setb(mb, 7, 1, 1); if (g() % 4 == 0) setb(mb, 30, 3, rb(3)); break;
    case 6: // 3,0
        mb[0] = 0x30; setb(mb, 9, 14, rb(14)); setb(mb, 23, 4, rb(4) & 3); setb(mb, 27, 1, g() & 1); setb(mb, 28, 1, g() & 1); setb(mb, 29, 2, g() % 3);
        { int tti = (int)A::getBits(mb.data(), 29, 2); if (tti == 1) setb(mb, 31, 24, rb(24) | 1); else if (tti == 2) { setb(mb, 31, 13, rb(13)); setb(mb, 44, 7, 1 + g() % 100); setb(mb, 51, 6, 1 + g() % 60); } } break;
    case 7: // 4,4
        setb(mb, 1, 4, 1 + g() % 4); setb(mb, 5, 1, 1); setb(mb, 6, 9, rb(9) % 250); setb(mb, 15, 9, rb(9));
        { int sat = (int)(g() % 400) - 200; setb(mb, 24, 1, sat < 0); setb(mb, 25, 10, (uint32_t)sat & 0x3ff); }
        if (g() & 1) { setb(mb, 35, 1, 1); setb(mb, 36, 11, 100 + g() % 1000); }
        if (g() & 1) { setb(mb, 47, 1, 1); setb(mb, 48, 2, rb(2)); }
        if (g() & 1) { setb(mb, 50, 1, 1); setb(mb, 51, 6, rb(6)); }
        break;
    case 8: // 4,5
        for (int i = 0; i < 5; i++) if (g() & 1) { setb(mb, 1 + 3 * i, 1, 1); setb(mb, 2 + 3 * i, 2, rb(2)); }
        setb(mb, 16, 1, 1); { int sat = (int)(g() % 400) - 200; setb(mb, 17, 1, sat < 0); setb(mb, 18, 9, (uint32_t)sat & 0x1ff); }
        if (g() & 1) { setb(mb, 27, 1, 1); setb(mb, 28, 11, 100 + g() % 1000); }
        if (g() & 1) { setb(mb, 39, 1, 1); setb(mb, 40, 12, g() % 320); }
        if (!A::getBits(mb.data(), 1, 12)) setb(mb, 1, 1, 1), setb(mb, 2, 2, 1);
        break;
    default: mb = randBytes(7); break;
    }
    if (g() % 3 == 0) setb(mb, 1 + (int)(g() % 56), 1, g() & 1);
    return mb;
}

// ---- messages of every format
static Bytes makeMessage(int& nbits) {
    static const int dfs[] = {0, 4, 5, 11, 16, 17, 18, 19, 20, 21, 24, 25, 26, 27, 1, 3, 12, 22, 28, 31};
    const int df = dfs[g() % 20];
    nbits = A::dfLength(df) ? A::dfLength(df) : (g() & 1 ? 56 : 112);
    if (g() % 40 == 0) nbits = (g() & 1) ? 112 : 56;   // length that does not fit the DF
    Bytes m = randBytes((size_t)nbits / 8);
    A::setBits(m.data(), 1, 5, (uint32_t)df);
    if (df == 17 || df == 18 || df == 19) {
        static const int tcs[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 18, 19, 19, 19, 20, 22, 28, 29, 29, 31, 31, 0, 23, 24, 25, 26, 27, 30};
        const int tc = g() % 4 ? tcs[g() % (sizeof tcs / sizeof tcs[0])] : (int)(g() % 33);
        A::setBits(m.data(), 33, 5, (uint32_t)(tc & 31));
        if (g() % 3 == 0) A::setBits(m.data(), 6, 3, g() % 8 < 5 ? 0 : (uint32_t)(4 + g() % 4));
        if (tc == 19) A::setBits(m.data(), 38, 3, g() % 7);
        if (tc == 29) A::setBits(m.data(), 38, 2, g() % 4);
        if (tc == 31 || tc == 28) A::setBits(m.data(), 38, 3, g() % 4);
        if (tc >= 9 && tc <= 22 && g() % 2) A::setBits(m.data(), 48, 1, 1);   // Q bit of the altitude
    }
    if (df == 4 || df == 20) { if (g() % 2) A::setBits(m.data(), 20, 13, (A::getBits(m.data(), 20, 13) | 0x10) & ~0x40u); }
    if (df == 20 || df == 21) {
        const Bytes mb = makeCommB((int)(g() % 10));
        for (int i = 0; i < 7; i++) m[4 + (size_t)i] = mb[(size_t)i];
    }
    return m;
}

int main() {
    const char* it = getenv("ONAIR_DIFF_ITERS");
    const long iters = it ? atol(it) : 200000;

    // CRC and single-bit correction
    for (int df = -5; df < 40; df++) SAME(A::dfLength(df) == R::dfLength(df), "dfLength %d", df);
    for (int n : {56, 112}) for (int p = 0; p < n; p++) {
        const uint32_t s = A::singleBitSyndrome(n, p);
        SAME(s == R::singleBitSyndrome(n, p), "singleBitSyndrome %d %d", n, p);
        SAME(A::singleBitPosition(n, s) == R::singleBitPosition(n, s) && A::singleBitPosition(n, s) == p, "singleBitPosition %d %d", n, p);
    }
    for (long i = 0; i < iters; i++) {
        const int n = g() & 1 ? 112 : 56;
        const uint32_t r = (uint32_t)g() & (g() & 1 ? 0xFFFFFFu : 0xFFFFFFFFu);
        SAME(A::singleBitPosition(n, r) == R::singleBitPosition(n, r), "singleBitPosition %u", r);
        Bytes m = randBytes((size_t)n / 8);
        SAME(A::crc24(m.data(), n) == R::crc24(m.data(), n), "crc24 n=%d", n);
        SAME(A::crcParity(m.data(), n) == R::crcParity(m.data(), n), "crcParity n=%d", n);
        // a valid frame, then one or two flipped bits: the remainder, and the position it points at
        const uint32_t par = R::crcParity(m.data(), n);
        m[(size_t)n / 8 - 3] = (uint8_t)(par >> 16); m[(size_t)n / 8 - 2] = (uint8_t)(par >> 8); m[(size_t)n / 8 - 1] = (uint8_t)par;
        SAME(A::crc24(m.data(), n) == 0 && R::crc24(m.data(), n) == 0, "valid frame remainder");
        const int p = (int)(g() % (uint64_t)n);
        m[(size_t)p / 8] ^= (uint8_t)(0x80 >> (p & 7));
        const uint32_t rem = A::crc24(m.data(), n);
        SAME(rem == R::crc24(m.data(), n) && A::singleBitPosition(n, rem) == p && R::singleBitPosition(n, rem) == p, "single bit %d of %d", p, n);
        const int q = (int)(g() % (uint64_t)n);
        m[(size_t)q / 8] ^= (uint8_t)(0x80 >> (q & 7));
        SAME(A::crc24(m.data(), n) == R::crc24(m.data(), n), "crc24 two bits");
        const uint32_t r2 = A::crc24(m.data(), n);
        SAME(A::singleBitPosition(n, r2) == R::singleBitPosition(n, r2), "singleBitPosition two bits");
    }

    // codes: every value of the small tables
    for (unsigned v = 0; v < 0x4000; v++) {
        int a = 12345, b = 12345;
        const bool r1 = A::decodeAc13(v, a), r2 = R::decodeAc13(v, b);
        SAME(r1 == r2 && (!r1 || a == b), "decodeAc13 %u", v);
        a = b = 12345;
        const bool s1 = A::decodeAlt12(v, a), s2 = R::decodeAlt12(v, b);
        SAME(s1 == s2 && (!s1 || a == b), "decodeAlt12 %u", v);
        SAME(A::decodeId13(v) == R::decodeId13(v), "decodeId13 %u", v);
        const bool t1 = A::gillhamToAltitude(v, a = 12345), t2 = R::gillhamToAltitude(v, b = 12345);
        SAME(t1 == t2 && (!t1 || a == b), "gillhamToAltitude %u", v);
    }
    for (unsigned v : {0x7FFFu, 0xFFFFu, 0x10000u, 0xFFFFFFFFu, 0x80000000u, 0x12345678u}) {
        int a = 0, b = 0;
        SAME(A::decodeAc13(v, a) == R::decodeAc13(v, b) && a == b, "decodeAc13 wide %u", v);
        SAME(A::decodeAlt12(v, a) == R::decodeAlt12(v, b) && a == b, "decodeAlt12 wide %u", v);
        SAME(A::decodeId13(v) == R::decodeId13(v), "decodeId13 wide %u", v);
    }
    for (int alt = -3000; alt <= 130000; alt++) {
        unsigned a = 7, b = 7;
        const bool r1 = A::altitudeToGillham(alt, a), r2 = R::altitudeToGillham(alt, b);
        SAME(r1 == r2 && (!r1 || a == b), "altitudeToGillham %d", alt);
    }
    for (int sq = -2000; sq <= 12000; sq++) SAME(A::encodeId13(sq) == R::encodeId13(sq), "encodeId13 %d", sq);
    for (int v : {-2147483647 - 1, 2147483647, 99999, 100000, 7777777}) SAME(A::encodeId13(v) == R::encodeId13(v), "encodeId13 %d", v);
    for (unsigned c = 0; c < 400; c++) SAME(A::charFrom6bit(c) == R::charFrom6bit(c), "charFrom6bit %u", c);
    for (int c = 0; c < 256; c++) SAME(A::charTo6bit((char)c) == R::charTo6bit((char)c), "charTo6bit %d", c);
    for (int tc = -2; tc < 9; tc++) for (int ca = -2; ca < 10; ca++) {
        SAME(A::categoryCode(tc, ca) == R::categoryCode(tc, ca), "categoryCode %d %d", tc, ca);
        if (ca >= 0 && ca < 8) SAME(A::categoryName(tc, ca) == R::categoryName(tc, ca), "categoryName %d %d", tc, ca);
    }
    for (int s = -3; s < 11; s++) SAME(!strcmp(A::emergencyName(s), R::emergencyName(s)), "emergencyName %d", s);

    // CPR: zone counts over a latitude sweep, real positions, random 17 bit values, absurd references
    for (int i = -100000; i <= 100000; i++) { const double lat = i * 0.0009; SAME(A::cprNl(lat) == R::cprNl(lat), "cprNl %f", lat); }
    for (int i = 0; i < 100; i++) { const double v = odd(); SAME(A::cprNl(v) == R::cprNl(v), "cprNl odd"); }
    for (double lat : {(double)NAN, (double)INFINITY, -(double)INFINITY, 87.0, 86.9999999, 10.47047130, 14.82817437, 87.00000001}) SAME(A::cprNl(lat) == R::cprNl(lat), "cprNl %f", lat);
    long cprGlobal = 0, cprLocal = 0, cprSurf = 0;
    for (long i = 0; i < iters; i++) {
        const bool useReal = g() % 2;
        const double lat = rndLat(), lon = rndLon();
        int le, xe, lo, xo;
        if (useReal) { A::cprEncodeAirborne(lat, lon, false, le, xe); A::cprEncodeAirborne(lat, lon, true, lo, xo); int a, b, c, d; R::cprEncodeAirborne(lat, lon, false, a, b); R::cprEncodeAirborne(lat, lon, true, c, d); SAME(a == le && b == xe && c == lo && d == xo, "cprEncodeAirborne %f %f", lat, lon); }
        else { le = (int)(g() % 131072); xe = (int)(g() % 131072); lo = (int)(g() % 131072); xo = (int)(g() % 131072); }
        const bool oddLatest = g() & 1;
        double rlat = g() % 8 == 0 ? odd() : lat + ((double)(g() % 2000) / 1000.0 - 1), rlon = g() % 8 == 0 ? odd() : lon + ((double)(g() % 2000) / 1000.0 - 1);
        double a1 = 11, a2 = 12, b1 = 11, b2 = 12;
        bool r1 = A::cprGlobalAirborne(le, xe, lo, xo, oddLatest, a1, a2), r2 = R::cprGlobalAirborne(le, xe, lo, xo, oddLatest, b1, b2);
        cprGlobal += r1;
        SAME(r1 == r2 && (!r1 || (dEq(a1, b1) && dEq(a2, b2))), "cprGlobalAirborne %d %d %d %d %d", le, xe, lo, xo, (int)oddLatest);
        const bool o = g() & 1;
        a1 = b1 = 11; a2 = b2 = 12;
        r1 = A::cprLocalAirborne(le, xe, o, rlat, rlon, a1, a2); r2 = R::cprLocalAirborne(le, xe, o, rlat, rlon, b1, b2);
        cprLocal += r1;
        SAME(r1 == r2 && (!r1 || (dEq(a1, b1) && dEq(a2, b2))), "cprLocalAirborne %d %d ref %f %f", le, xe, rlat, rlon);
        a1 = b1 = 11; a2 = b2 = 12;
        r1 = A::cprGlobalSurface(le, xe, lo, xo, oddLatest, rlat, rlon, a1, a2); r2 = R::cprGlobalSurface(le, xe, lo, xo, oddLatest, rlat, rlon, b1, b2);
        cprSurf += r1;
        SAME(r1 == r2 && (!r1 || (dEq(a1, b1) && dEq(a2, b2))), "cprGlobalSurface %d %d %d %d ref %f %f", le, xe, lo, xo, rlat, rlon);
        a1 = b1 = 11; a2 = b2 = 12;
        r1 = A::cprLocalSurface(lo, xo, o, rlat, rlon, a1, a2); r2 = R::cprLocalSurface(lo, xo, o, rlat, rlon, b1, b2);
        SAME(r1 == r2 && (!r1 || (dEq(a1, b1) && dEq(a2, b2))), "cprLocalSurface");
        int e1, e2, f1, f2;
        A::cprEncodeSurface(lat, lon, o, e1, e2); R::cprEncodeSurface(lat, lon, o, f1, f2);
        SAME(e1 == f1 && e2 == f2, "cprEncodeSurface %f %f", lat, lon);
    }

    // geometry and models
    for (long i = 0; i < iters; i++) {
        const double la1 = g() % 20 == 0 ? odd() : rndLat(), lo1 = g() % 20 == 0 ? odd() : rndLon(), la2 = g() % 20 == 0 ? odd() : rndLat(), lo2 = g() % 20 == 0 ? odd() : rndLon();
        SAME(dEq(A::distanceNm(la1, lo1, la2, lo2), R::distanceNm(la1, lo1, la2, lo2)), "distanceNm %f %f %f %f", la1, lo1, la2, lo2);
        SAME(dEq(A::bearingDeg(la1, lo1, la2, lo2), R::bearingDeg(la1, lo1, la2, lo2)), "bearingDeg");
        SAME(dEq(A::magDeclinationDeg(la1, lo1), R::magDeclinationDeg(la1, lo1)), "magDeclinationDeg %.17g %.17g: %.17g / %.17g", la1, lo1, A::magDeclinationDeg(la1, lo1), R::magDeclinationDeg(la1, lo1));
        const double trk = (double)(g() % 3600) / 10, gs = (double)(g() % 6000) / 10, hdg = (double)(g() % 3600) / 10, tas = (double)(g() % 6000) / 10, mach = (double)(g() % 1000) / 1000 - 0.1;
        double f1 = 1, k1 = 2, f2 = 1, k2 = 2;
        A::windTriangle(trk, gs, hdg, tas, f1, k1); R::windTriangle(trk, gs, hdg, tas, f2, k2);
        SAME(dEq(f1, f2) && dEq(k1, k2), "windTriangle");
        SAME(dEq(A::satFromTasMach(tas, mach), R::satFromTasMach(tas, mach)), "satFromTasMach");
        const double alt = (double)((int64_t)(g() % 600000) - 100000) / 10.0;
        SAME(dEq(A::isaPressureHpa(alt), R::isaPressureHpa(alt)) && dEq(A::isaTempC(alt), R::isaTempC(alt)), "isa %f", alt);
    }
    for (unsigned ara = 0; ara < 16384; ara++) for (unsigned rac = 0; rac < 16; rac++) for (int t = 0; t < 2; t++)
        if (t == 0 || (ara & 0xF) == 0) SAME(A::raText(ara, rac, t) == R::raText(ara, rac, t), "raText %u %u %d", ara, rac, t);

    // Comm-B registers
    std::map<int, long> found;
    for (long i = 0; i < iters * 2; i++) {
        const Bytes mb = makeCommB((int)(g() % 10));
        const int alt = g() % 3 == 0 ? A::kNoAltitude : (int)(g() % 50000) - 1000;
        const A::CommB a = A::inferCommB(mb.data(), alt);
        const R::CommB b = R::inferCommB(mb.data(), alt);
        found[a.bds]++;
        SAME(commbEq(a, b), "inferCommB bds %02x / %02x", a.bds, b.bds);
    }
    for (long i = 0; i < iters; i++) {   // weather registers need a matching altitude: pick the altitude from the temperature that was built
        Bytes mb = makeCommB(7 + (int)(g() % 2));
        const int alt = (int)(g() % 40000);
        const A::CommB a = A::inferCommB(mb.data(), alt);
        const R::CommB b = R::inferCommB(mb.data(), alt);
        found[a.bds]++;
        SAME(commbEq(a, b), "inferCommB weather");
    }
    for (long i = 0; i < iters / 4; i++) {   // fully random fields and the all-zero field
        const Bytes mb = i % 10 ? randBytes(7) : Bytes(7, 0);
        SAME(commbEq(A::inferCommB(mb.data(), A::kNoAltitude), R::inferCommB(mb.data(), A::kNoAltitude)), "inferCommB random");
    }

    // decoded messages
    long decodedOk = 0, withPos = 0, withVel = 0, withCommB = 0;
    for (long i = 0; i < iters * 3; i++) {
        int nbits;
        Bytes m = makeMessage(nbits);
        if (g() % 5 == 0) { const int p = (int)(g() % (uint64_t)nbits); m[(size_t)p / 8] ^= (uint8_t)(0x80 >> (p & 7)); }
        A::Msg a; R::Msg b;
        const bool r1 = A::decodeMsg(m.data(), nbits, a), r2 = R::decodeMsg(m.data(), nbits, b);
        decodedOk += r1; withPos += a.hasCpr; withVel += a.hasVel; withCommB += a.commb.bds != 0;
        SAME(r1 == r2 && msgEq(a, b), "decodeMsg df %d tc %d/%d nbits %d", a.df, a.tc, b.tc, nbits);
    }
    // a few real frames of the Riddle
    for (const char* hx : {"8D4840D6202CC371C32CE0576098", "8D40621D58C382D690C8AC2863A7", "8D40621D58C386435CC412692AD6", "8D485020994409940838175B284F", "8D4840D6202CC371C32CE0576098"}) {
        Bytes m(14);
        for (int i = 0; i < 14; i++) m[(size_t)i] = (uint8_t)strtol(std::string(hx + 2 * i, 2).c_str(), nullptr, 16);
        A::Msg a; R::Msg b;
        SAME(A::decodeMsg(m.data(), 112, a) == R::decodeMsg(m.data(), 112, b) && msgEq(a, b), "decodeMsg %s", hx);
        SAME(A::toHex(m.data(), 112) == R::toHex(m.data(), 112), "toHex");
    }

    printf("adsb_proto_rs: %ld checks, %ld mismatches\n", checks, fails);
    printf("  coverage: %ld global CPR fixes, %ld local, %ld surface; %ld messages decoded (%ld with a CPR frame, %ld with a velocity, %ld with a Comm-B register)\n", cprGlobal, cprLocal, cprSurf, decodedOk, withPos, withVel, withCommB);
    printf("  Comm-B inference results:");
    for (auto& kv : found) printf(" %02x:%ld", kv.first, kv.second);
    printf("\n");
    return fails ? 1 : 0;
}
