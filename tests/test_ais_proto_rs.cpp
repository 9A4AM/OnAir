// Differential test: the C++ AIS frame layer (a generated copy in dect2::ais_ref, see cmake/gen_ais_ref.cmake) against the Rust one (dect2::ais when
// DECT2_RUST is used). The same inputs go to both and every result has to be identical: the known gpsd / AIS-catcher sentences, frames built here,
// and ONAIR_DIFF_ITERS (default 200000) seeded random and mutated frames, bit streams with values other than 0 and 1, and damaged sentences.
#include "dect2/ais_proto.h"
#include "ais_proto_ref.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
namespace A = dect2::ais;
namespace R = dect2::ais_ref;

static long fails = 0, checks = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)

template <class T> static bool bitsEq(const T& a, const T& b) { return memcmp(&a, &b, sizeof a) == 0; }

static bool msgEq(const A::AisMsg& a, const R::AisMsg& b) {
    return a.valid == b.valid && a.type == b.type && a.repeat == b.repeat && a.mmsi == b.mmsi && a.cls == b.cls && a.hasPos == b.hasPos &&
           bitsEq(a.lat, b.lat) && bitsEq(a.lon, b.lon) && bitsEq(a.sog, b.sog) && bitsEq(a.cog, b.cog) && a.heading == b.heading && a.hasRot == b.hasRot &&
           bitsEq(a.rotDegMin, b.rotDegMin) && a.navStatus == b.navStatus && a.second == b.second && a.name == b.name && a.callsign == b.callsign &&
           a.destination == b.destination && a.imo == b.imo && a.shipType == b.shipType && a.dimA == b.dimA && a.dimB == b.dimB && a.dimC == b.dimC &&
           a.dimD == b.dimD && a.etaMonth == b.etaMonth && a.etaDay == b.etaDay && a.etaHour == b.etaHour && a.etaMin == b.etaMin &&
           bitsEq(a.draughtM, b.draughtM) && a.epfd == b.epfd && a.partNo == b.partNo && a.vendor == b.vendor && a.aidType == b.aidType &&
           a.offPosition == b.offPosition && a.virtualAid == b.virtualAid && a.altitudeM == b.altitudeM && a.year == b.year && a.month == b.month &&
           a.day == b.day && a.hour == b.hour && a.minute == b.minute && a.sec == b.sec && a.text == b.text && a.dac == b.dac && a.fi == b.fi &&
           a.destMmsi == b.destMmsi && a.bits == b.bits;
}

static void compareBits(const A::Bits& d, std::mt19937& g) {
    SAME(A::fcsRegister(d.data(), d.size()) == R::fcsRegister(d.data(), d.size()), "fcsRegister n=%zu", d.size());
    SAME(A::fcsOk(d) == R::fcsOk(d), "fcsOk");
    SAME(A::stuff(d) == R::stuff(d), "stuff n=%zu", d.size());
    { A::Bits o1, o2; const bool r1 = A::destuff(d, o1), r2 = R::destuff(d, o2); SAME(r1 == r2 && (!r1 || o1 == o2), "destuff n=%zu", d.size()); }
    const int lv = (int)(g() % 4) - 1;
    SAME(A::nrziEncode(d, lv) == R::nrziEncode(d, lv), "nrziEncode");
    SAME(A::nrziDecode(d, lv) == R::nrziDecode(d, lv), "nrziDecode");
    { A::Bits x = d, y = d; A::reverseOctets(x); R::reverseOctets(y); SAME(x == y, "reverseOctets"); }
    { const A::HdlcResult h1 = A::hdlcFrames(d); const R::HdlcResult h2 = R::hdlcFrames(d); SAME(h1.good == h2.good && h1.bad == h2.bad, "hdlcFrames n=%zu good %zu/%zu bad %d/%d", d.size(), h1.good.size(), h2.good.size(), h1.bad, h2.bad); }
    SAME(A::burstLineBits(d) == R::burstLineBits(d), "burstLineBits");
    const size_t pos = g() % (d.size() + 20), len = g() % 70;
    SAME(A::getU(d, pos, len) == R::getU(d, pos, len), "getU %zu %zu", pos, len);
    SAME(A::getS(d, pos, len) == R::getS(d, pos, len), "getS %zu %zu", pos, len);
    SAME(A::getText(d, pos, len) == R::getText(d, pos, len), "getText %zu %zu", pos, len);
}

static void compareMessage(const A::Bits& p) {
    A::AisMsg a; R::AisMsg b;
    const bool ra = A::decodeMessage(p, a), rb = R::decodeMessage(p, b);
    SAME(ra == rb && msgEq(a, b), "decodeMessage n=%zu type %d/%d valid %d/%d", p.size(), a.type, b.type, (int)a.valid, (int)b.valid);
    for (int dec = 0; dec < 2; dec++) {
        SAME(A::acceptableAis(a, dec != 0) == R::acceptableAis(b, dec != 0), "acceptableAis");
    }
    int fa = 0, fb = 0;
    const std::string sa = A::armour(p, fa), sb = R::armour(p, fb);
    SAME(sa == sb && fa == fb, "armour");
    for (int fill = -2; fill < 9; fill++) SAME(A::unarmour(sa, fill) == R::unarmour(sa, fill), "unarmour fill %d", fill);
    SAME(A::toNmea(p, 'B', 3) == R::toNmea(p, 'B', 3) && A::toNmea(p, 'A', -13) == R::toNmea(p, 'A', -13), "toNmea");
}

static void compareSentence(const std::string& s) {
    std::string c1 = "x", c2 = "x"; int f1 = 77, f2 = 77, n1 = 77, n2 = 77, k1 = 77, k2 = 77; char h1 = 'q', h2 = 'q';
    const bool r1 = A::parseNmea(s, c1, f1, n1, k1, &h1), r2 = R::parseNmea(s, c2, f2, n2, k2, &h2);
    SAME(r1 == r2 && c1 == c2 && f1 == f2 && n1 == n2 && k1 == k2 && h1 == h2, "parseNmea '%.60s' %d/%d fill %d/%d cnt %d/%d no %d/%d", s.c_str(), r1, r2, f1, f2, n1, n2, k1, k2);
    std::string c3 = "y", c4 = "y"; int a1 = 5, a2 = 5, a3 = 5, a4 = 5, a5 = 5, a6 = 5;
    SAME(A::parseNmea(s, c3, a1, a2, a3) == R::parseNmea(s, c4, a4, a5, a6) && c3 == c4 && a1 == a4 && a2 == a5 && a3 == a6, "parseNmea without channel");
    SAME(A::nmeaChecksum(s) == R::nmeaChecksum(s), "nmeaChecksum");
}

static const char* kSentences[] = {
    "!AIVDM,1,1,,B,177KQJ5000G?tO`K>RA1wUbN0TKH,0*5C",
    "!AIVDM,1,1,,A,15RTgt0PAso;90TKcjM8h6g208CQ,0*4A", "!AIVDM,1,1,,A,16SteH0P00Jt63hHaa6SagvJ087r,0*42",
    "!AIVDM,1,1,,B,25Cjtd0Oj;Jp7ilG7=UkKBoB0<06,0*60", "!AIVDM,1,1,,A,38Id705000rRVJhE7cl9n;160000,0*40",
    "!AIVDM,1,1,,A,403OviQuMGCqWrRO9>E6fE700@GO,0*4D", "!AIVDM,1,1,,A,91b77=h3h00nHt0Q3r@@07000<0b,0*69",
    "!AIVDM,1,1,,A,B52K>;h00Fc>jpUlNV@ikwpUoP06,0*4C", "!AIVDM,1,1,,B,C5N3SRgPEnJGEBT>NhWAwwo862PaLELTBJ:V00000000S0D:R220,0*0B",
    "!AIVDM,1,1,,A,H42O55i18tMET00000000000000,2*6D", "!AIVDM,1,1,,A,H42O55lti4hhhilD3nink000?050,0*40",
    "!AIVDM,1,1,,A,>5?Per18=HB1U:1@E=B0m<L,2*51", "!AIVDM,1,1,,A,>3R1p10E3;;R0USCR0HO>0@gN10kGJp,2*7F", "!AIVDM,1,1,,A,>4aDT81@E=@,2*2E",
    "!AIVDM,1,1,,B,6B?n;be:cbapalgc;i6?Ow4,2*4A", "!AIVDM,1,1,,A,85Mwp`1Kf3aCnsNvBWLi=wQuNhA5t43N`5nCuI=p<IBfVqnMgPGs,0*47",
    "!AIVDM,2,1,1,A,55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp8,0*1C", "!AIVDM,2,2,1,A,88888888880,2*25",
    "!AIVDM,2,1,2,A,542M92h00001@<7;?G0PD4i@R0<tqA8tj37>220o0h:2240Ht500000000000000,0*3C", "!AIVDM,2,2,2,A,0000002,2*24",
    "!AIVDM,1,1,,A,15RTgt0PAso;90TKcjM8h6g208CQ,0*4B", "$AIVDM,1,1,,A,,0*26", "!AIVDM,1,1,,,x,0*00", "!AIVDM,0,1,,A,xx,0*00", "!AIVDM,1,1,,A,xx,9*00",
    "!AIVDM,1,1,,A,15RTgt0PAso;90TKcjM8h6g208CQ,0*4a", "!AIVDM,1,1,,A,15RTgt0PAso;90TKcjM8h6g208CQ,0*4", "!AIVDM,1,1,,A,15RTgt0PAso;90TKcjM8h6g208CQ,0*"};

static std::mt19937 gen(20261016);

static A::Bits randomMessage(int type) {
    A::Bits b;
    A::putU(b, 0, 6, (uint32_t)type);
    A::putU(b, 6, 2, gen() & 3);
    A::putU(b, 8, 30, (gen() & 1) ? 200000000u + gen() % 600000000u : gen() % 1000000000u);
    static const size_t lens[] = {168, 168, 168, 168, 424, 420, 72, 88, 56, 40, 168, 312, 272, 360, 160, 168, 96};
    size_t n = lens[gen() % (sizeof lens / sizeof *lens)];
    if (gen() % 5 == 0) n = 30 + gen() % 400;
    for (size_t i = 38; i < n; i++) b.push_back(0), A::putU(b, i, 1, gen() & 1);
    if (b.size() < n) b.resize(n, 0);
    // sometimes the "not available" sentinels
    if ((gen() & 7) == 0 && n >= 120) { A::putS(b, 61, 28, 0x6791AC0); A::putS(b, 89, 27, 0x3412140); }
    return b;
}

static void mutate(A::Bits& v) {
    const int k = 1 + (int)(gen() % 5);
    for (int j = 0; j < k; j++) {
        switch (gen() % 7) {
        case 0: if (!v.empty()) v[gen() % v.size()] ^= 1; break;
        case 1: if (!v.empty()) v[gen() % v.size()] = (uint8_t)gen(); break;           // values other than 0 and 1
        case 2: if (!v.empty()) v.erase(v.begin() + (long)(gen() % v.size())); break;
        case 3: v.insert(v.begin() + (long)(gen() % (v.size() + 1)), (uint8_t)(gen() & 1)); break;
        case 4: if (v.size() > 2) v.resize(gen() % v.size()); break;
        case 5: { const size_t a = gen() % (v.size() + 1); for (int q = 0; q < 6; q++) v.insert(v.begin() + (long)a, 1); break; }   // a run of ones: flags, aborts
        default: if (!v.empty()) { const size_t a = gen() % v.size(); const size_t l = gen() % 20; for (size_t q = a; q < v.size() && q < a + l; q++) v[q] = gen() & 1; } break;
        }
    }
}

static void mutateText(std::string& s) {
    const int k = 1 + (int)(gen() % 3);
    for (int j = 0; j < k && !s.empty(); j++) {
        switch (gen() % 5) {
        case 0: s[gen() % s.size()] = (char)gen(); break;
        case 1: s.erase(gen() % s.size(), 1); break;
        case 2: s.insert(gen() % (s.size() + 1), 1, ",*!$ 0123456789ABCDEFabcdef-+\t\n"[gen() % 31]); break;
        case 3: s.resize(gen() % s.size()); break;
        default: s[gen() % s.size()] = ",*"[gen() & 1]; break;
        }
    }
}

int main() {
    const char* e = getenv("ONAIR_DIFF_ITERS");
    const long iters = e ? atol(e) : 200000;
    printf("test_ais_proto_rs: A = %s, reference = C++ copy\n", A::protoImplementation());

    // known sentences: decode, re-frame, and the damaged versions
    for (const char* s : kSentences) {
        compareSentence(s);
        std::string chars; int fill = 0, cnt = 0, no = 0;
        if (!R::parseNmea(s, chars, fill, cnt, no)) continue;
        const A::Bits p = R::unarmour(chars, fill);
        compareMessage(p);
        compareBits(p, gen);
        A::Bits line = A::burstLineBits(p);
        compareBits(line, gen);
        compareBits(A::nrziDecode(line), gen);
    }
    // frames built here from scratch (bit by bit, not with the library): flags, stuffing, X.25 FCS, NRZI, with the octet order of the air
    for (int t : {1, 2, 3, 4, 5, 9, 18, 19, 21, 24, 27}) for (int rep = 0; rep < 20; rep++) {
        A::Bits p = randomMessage(t), fr = p;
        for (size_t i = 0; i < fr.size(); i += 8) { size_t e8 = std::min(i + 8, fr.size()); std::reverse(fr.begin() + (long)i, fr.begin() + (long)e8); }
        uint16_t r = 0xFFFF;
        for (uint8_t b : fr) { const bool fb = ((b ^ r) & 1) != 0; r >>= 1; if (fb) r ^= 0x8408; }
        r = (uint16_t)~r;
        for (int i = 0; i < 16; i++) fr.push_back((r >> i) & 1);
        A::Bits st, d = {0, 1, 1, 1, 1, 1, 1, 0};
        for (int ones = 0; uint8_t b : fr) { st.push_back(b); ones = b ? ones + 1 : 0; if (ones == 5) { st.push_back(0); ones = 0; } }
        d.insert(d.end(), st.begin(), st.end());
        for (uint8_t b : {0, 1, 1, 1, 1, 1, 1, 0}) d.push_back(b);
        A::Bits line;
        int lv = 1;
        for (uint8_t b : d) { if (!b) lv ^= 1; line.push_back((uint8_t)lv); }
        const A::Bits dec = A::nrziDecode(line);
        SAME(A::hdlcFrames(dec).good == R::hdlcFrames(dec).good, "built frame type %d: Rust and C++ frames differ", t);
        if (p.size() >= 48 && p.size() <= 1000) SAME(A::hdlcFrames(dec).good.size() == 1 && A::hdlcFrames(dec).good[0] == p, "built frame type %d decodes to the message (%zu bits)", t, p.size());
        compareBits(dec, gen);
        compareMessage(p);
    }

    for (long it = 0; it < iters; it++) {
        const int kind = (int)(gen() % 6);
        if (kind == 0) {            // a message of a random type and length
            compareMessage(randomMessage((int)(gen() % 64)));
        } else if (kind == 1) {     // a valid burst, damaged
            A::Bits p = randomMessage((int)(gen() % 28));
            A::Bits line = A::burstLineBits(p);
            if (gen() & 1) line = A::nrziDecode(line);
            mutate(line);
            compareBits(line, gen);
        } else if (kind == 2) {     // two bursts in one stream, one damaged
            A::Bits l1 = A::nrziDecode(A::burstLineBits(randomMessage(1 + (int)(gen() % 27)))), l2 = A::nrziDecode(A::burstLineBits(randomMessage(18)));
            if (gen() & 1) mutate(l2);
            l1.insert(l1.end(), l2.begin(), l2.end());
            compareBits(l1, gen);
            compareMessage(R::hdlcFrames(l1).good.empty() ? l1 : R::hdlcFrames(l1).good[0]);
        } else if (kind == 3) {     // pure noise, sometimes with values above 1
            A::Bits v(gen() % 1300);
            const bool wide = gen() & 1;
            for (auto& b : v) b = wide ? (uint8_t)gen() : (uint8_t)(gen() & 1);
            compareBits(v, gen);
        } else if (kind == 4) {     // sentences
            std::string s = kSentences[gen() % (sizeof kSentences / sizeof *kSentences)];
            mutateText(s);
            compareSentence(s);
        } else {                    // payload mutations, then message level
            A::Bits p = randomMessage(1 + (int)(gen() % 27));
            mutate(p);
            compareMessage(p);
        }
    }
    printf("test_ais_proto_rs: %ld checks, %ld mismatches\n", checks, fails);
    return fails ? 1 : 0;
}
