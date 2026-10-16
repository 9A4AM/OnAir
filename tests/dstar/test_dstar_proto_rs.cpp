// Differential test: the C++ D-STAR frame layer (a generated copy in dect2::dstar_ref, see cmake/gen_rs_ref.cmake) against the Rust one (dect2::dstar when
// DECT2_RUST is used). The same inputs go to both and every result has to be identical (doubles bit for bit, the fix of a rejected line too): CRC, header
// pack / unpack, flag texts, the header FEC (interleaver, scrambler, convolutional code, soft Viterbi with noisy / NaN input), the AMBE order, NMEA and D-PRS
// lines built, mutated and corrupted, over ONAIR_DIFF_ITERS (default 200000) seeded inputs per family.
#include "dect2/dstar/dstar_proto.h"
#include "dstar_proto_ref.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
namespace A = dect2::dstar;
namespace R = dect2::dstar_ref;

static long fails = 0, checks = 0, parsedNmea = 0, parsedDprs = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)

static std::mt19937 g(97531);
static uint32_t r32() { return g(); }
static bool dEq(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }
static bool fEq(float a, float b) { return memcmp(&a, &b, sizeof a) == 0; }
template <class T, class U> static bool fixEq(const T& a, const U& b) {
    return dEq(a.lat, b.lat) && dEq(a.lon, b.lon) && dEq(a.altM, b.altM) && dEq(a.speedKmh, b.speedKmh) && a.course == b.course && a.altValid == b.altValid &&
           a.velValid == b.velValid && a.call == b.call && a.comment == b.comment && a.aprs == b.aprs;
}
template <class F> static void sentinel(F& f) { f.lat = 1.5; f.lon = 2.5; f.altM = 3.5; f.speedKmh = 4.5; f.course = 77; f.altValid = true; f.call = "OLD"; f.comment = "OLDC"; }

static std::string rndStr(size_t n, const char* alphabet, size_t alen) { std::string s; for (size_t i = 0; i < n; i++) s += alphabet[g() % alen]; return s; }
#define RS(n, lit) rndStr((n), lit, sizeof(lit) - 1)

static std::string mutate(std::string s) {
    const int k = (int)(g() % 4);
    for (int i = 0; i < k && !s.empty(); i++) {
        switch (g() % 6) {
        case 0: s[g() % s.size()] = (char)g(); break;
        case 1: s[g() % s.size()] = "0123456789.,*$:!>/=@ANSEWRC-"[g() % 28]; break;
        case 2: s.erase(g() % s.size(), 1 + g() % 3); break;
        case 3: s.insert(g() % (s.size() + 1), RS(1 + g() % 3, "0123456789,.*/A=")); break;
        case 4: s.resize(g() % (s.size() + 1)); break;
        default: s += RS(g() % 10, "0123456789 \r\n/A="); break;
        }
    }
    return s;
}

static std::string num(double v, const char* fmt) { char b[64]; snprintf(b, sizeof b, fmt, v); return b; }
static std::string nmeaLine(bool fixChecksum) {
    std::string body;
    const char* talker[] = {"GP", "GN", "GL", "XX"};
    const char hemiLat = g() % 2 ? 'N' : 'S', hemiLon = g() % 2 ? 'E' : 'W';
    const std::string lat = num((double)(g() % 90) * 100 + (double)(g() % 6000) / 100.0, "%07.2f"), lon = num((double)(g() % 180) * 100 + (double)(g() % 6000) / 100.0, "%08.2f");
    if (g() % 2) {
        body = std::string(talker[g() % 4]) + "RMC,123519," + (g() % 8 ? "A" : "V") + "," + lat + "," + hemiLat + "," + lon + "," + hemiLon + "," + (g() % 6 ? num((double)(g() % 1000) / 10.0, "%.1f") : "") + "," +
               num((double)(g() % 3600) / 10.0, "%.1f") + ",230394,003.1,W";
    } else {
        body = std::string(talker[g() % 4]) + "GGA,123519," + lat + "," + hemiLat + "," + lon + "," + hemiLon + "," + (g() % 8 ? "1" : (g() % 2 ? "0" : "")) + ",08,0.9," + (g() % 5 ? num((double)(g() % 20000) / 10.0 - 100, "%.1f") : "") + ",M,46.9,M,,";
    }
    std::string l = "$" + body + "*" + (fixChecksum ? A::nmeaChecksum(body) : RS(2, "0123456789ABCDEF"));
    return l;
}
static std::string dprsLine(bool fixCrc) {
    std::string pos;
    const char* lead[] = {"!", "=", "/", "@", "#"};
    const char* l = lead[g() % 5];
    pos += l;
    if (l[0] == '/' || l[0] == '@') pos += "092345z";
    char b[64];
    snprintf(b, sizeof b, "%02u%05.2f%c%c%03u%05.2f%c%c", g() % 90, (double)(g() % 6000) / 100.0, g() % 2 ? 'N' : 'S', '/', g() % 180, (double)(g() % 6000) / 100.0, g() % 2 ? 'E' : 'W', '>');
    pos += b;
    std::string c;
    if (g() % 2) { snprintf(b, sizeof b, "%03u/%03u", g() % 360, g() % 120); c += b; }
    if (g() % 2) c += RS(g() % 12, "hello world mobile");
    if (g() % 2) { snprintf(b, sizeof b, "/A=%06u", g() % 100000); c += b; }
    if (g() % 3 == 0) c += RS(g() % 6, "xyz /A=12");
    if (g() % 4 == 0) c += (g() % 2) ? "\r" : " \r\n";
    std::string rest = "LY1TST-7>API282,DSTAR*:" + pos + c;
    if (g() % 6 == 0) rest = rest.substr(0, g() % (rest.size() + 1));
    unsigned crc = A::dprsCrc(rest + (g() % 2 ? "\r" : ""));
    if (!fixCrc) crc = r32() & 0xFFFF;
    snprintf(b, sizeof b, "$$CRC%04X,", crc);
    return b + rest;
}

int main() {
    const char* it = getenv("ONAIR_DIFF_ITERS");
    const long iters = it ? atol(it) : 200000;
    printf("dstar_proto_rs: implementation under test is dect2::dstar (Rust when DECT2_RUST_DSTAR)\n");

    SAME(!memcmp(A::kFrameSync, R::kFrameSync, 15) && !memcmp(A::kDataSync, R::kDataSync, 24) && !memcmp(A::kEndPattern, R::kEndPattern, 48) && !memcmp(A::kSlowScramble, R::kSlowScramble, 3), "tables");
    for (int f0 = 0; f0 < 256; f0++) { uint8_t f[3] = {(uint8_t)f0, (uint8_t)g(), (uint8_t)g()}; SAME(A::flagText(f) == R::flagText(f), "flagText %d", f0); }
    {   // the interleaver over every input position
        for (int i = 0; i < A::kHeaderBits; i++) {
            uint8_t in[660] = {}, a[660], b[660];
            in[i] = 1;
            A::interleave(in, a); R::interleave(in, b);
            SAME(!memcmp(a, b, 660), "interleave %d", i);
        }
    }

    for (long i = 0; i < iters; i++) {
        // CRC, checksum, byte / bit order, scrambler
        uint8_t d[200]; for (auto& x : d) x = (uint8_t)g();
        const size_t n = g() % 201;
        SAME(A::crc(d, n) == R::crc(d, n), "crc %zu", n);
        const std::string s((const char*)d, n);
        SAME(A::dprsCrc(s) == R::dprsCrc(s) && A::nmeaChecksum(s) == R::nmeaChecksum(s), "dprsCrc / nmeaChecksum");
        const int nb = (int)(g() % 40);
        uint8_t b1[320], b2[320];
        A::bytesToBits(d, nb, b1); R::bytesToBits(d, nb, b2);
        SAME(!memcmp(b1, b2, (size_t)nb * 8), "bytesToBits");
        uint8_t y1[40], y2[40];
        A::bitsToBytes(b1, nb, y1); R::bitsToBytes(b1, nb, y2);
        SAME(!memcmp(y1, y2, (size_t)nb), "bitsToBytes");
        const int sn = (int)(g() % 700);
        uint8_t c1[700], c2[700];
        for (int k = 0; k < 700; k++) c1[k] = c2[k] = (uint8_t)(g() % 9 == 0 ? g() : g() & 1);
        A::scrambleBits(c1, sn); R::scrambleBits(c2, sn);
        SAME(!memcmp(c1, c2, 700), "scrambleBits");
        const int cn = (int)(g() % 340);
        uint8_t e1[700] = {}, e2[700] = {};
        A::convEncode(c1, cn, e1); R::convEncode(c1, cn, e2);
        SAME(!memcmp(e1, e2, 700), "convEncode");

        // header: fields -> bytes -> FEC -> noisy decode
        A::Header ha; R::Header hb;
        for (int k = 0; k < 3; k++) ha.flag[k] = hb.flag[k] = (uint8_t)g();
        const std::string f1 = g() % 3 ? RS(g() % 10, "ABCDEFGHIJ0123 /") : rndStr(g() % 10, "\x01\x7f\xff z", 5);
        ha.rpt2 = hb.rpt2 = f1; ha.rpt1 = hb.rpt1 = RS(g() % 10, "ABCD /"); ha.ur = hb.ur = RS(g() % 10, "CQ X"); ha.my = hb.my = RS(g() % 10, "LY1TST ");
        ha.sfx = hb.sfx = RS(g() % 6, "D75 ");
        uint8_t pa[41], pb[41];
        A::headerPack(ha, pa); R::headerPack(hb, pb);
        SAME(!memcmp(pa, pb, 41), "headerPack");
        uint8_t in41[41];
        if (g() % 2) memcpy(in41, pb, 41); else for (auto& x : in41) x = (uint8_t)g();
        if (g() % 4 == 0) in41[g() % 41] ^= (uint8_t)(1 << (g() % 8));
        A::Header ua; R::Header ub;
        const bool oa = A::headerUnpack(in41, ua), ob = R::headerUnpack(in41, ub);
        SAME(oa == ob && !memcmp(ua.flag, ub.flag, 3) && ua.rpt2 == ub.rpt2 && ua.rpt1 == ub.rpt1 && ua.ur == ub.ur && ua.my == ub.my && ua.sfx == ub.sfx, "headerUnpack");

        if (i % 4 == 0) {   // the whole header on the air, a quarter of the iterations (the decoder is the slow part)
            uint8_t hdr[41]; for (auto& x : hdr) x = (uint8_t)g();
            uint8_t ta[660], tb[660];
            A::headerEncode(hdr, ta); R::headerEncode(hdr, tb);
            SAME(!memcmp(ta, tb, 660), "headerEncode");
            float soft[660];
            const int mode = (int)(g() % 5);
            const float sigma = (float)(g() % 30) / 10.f;
            for (int k = 0; k < 660; k++) {
                const float clean = tb[k] ? 1.f : -1.f;
                const float noise = ((float)(g() & 0xFFFF) / 65536.f - 0.5f) * 2.f * sigma;
                switch (mode) {
                case 0: soft[k] = clean; break;
                case 1: case 2: soft[k] = clean + noise; break;
                case 3: soft[k] = ((float)r32() / 4294967296.f - 0.5f) * 8.f; break;
                default: soft[k] = g() % 50 == 0 ? std::nanf("") : (g() % 40 == 0 ? INFINITY : clean + noise); break;
                }
            }
            uint8_t da[41], db[41];
            memset(da, 0, 41); memset(db, 0, 41);
            const int ea = A::headerDecode(soft, da), eb = R::headerDecode(soft, db);
            SAME(ea == eb && !memcmp(da, db, 41), "headerDecode mode %d errs %d/%d", mode, ea, eb);
            float dil[660], dil2[660];
            A::deinterleave(soft, dil); R::deinterleave(soft, dil2);
            bool same = true; for (int k = 0; k < 660; k++) same = same && fEq(dil[k], dil2[k]);
            SAME(same, "deinterleave");
            // viterbi of a free length
            const int vn = 1 + (int)(g() % 400);
            uint8_t v1[400], v2[400];
            const int ra = A::viterbi(soft, vn > 330 ? 330 : vn, v1), rb = R::viterbi(soft, vn > 330 ? 330 : vn, v2);
            SAME(ra == rb && !memcmp(v1, v2, (size_t)(vn > 330 ? 330 : vn)), "viterbi %d", vn);
        }
        uint8_t ab[72]; for (auto& x : ab) x = (uint8_t)g();
        char fa[4][24], fb[4][24];
        A::ambeFrame(ab, fa); R::ambeFrame(ab, fb);
        SAME(!memcmp(fa, fb, sizeof fa), "ambeFrame");

        // GPS text: built lines (checksum / CRC right or wrong), then mutated
        std::string line = (g() % 2) ? nmeaLine(g() % 5 != 0) : dprsLine(g() % 5 != 0);
        if (g() % 3 == 0) line = mutate(line);
        if (g() % 40 == 0) line = RS(g() % 30, "$$CRC,*>:!0123456789ABCDEFNSEW/=@");
        A::Fix xa; R::Fix xb;
        sentinel(xa); sentinel(xb);
        const bool pn = line.compare(0, 2, "$$") == 0;
        const bool ra = pn ? A::parseDprs(line, xa) : A::parseNmea(line, xa), rb = pn ? R::parseDprs(line, xb) : R::parseNmea(line, xb);
        SAME(ra == rb && fixEq(xa, xb), "%s '%s' -> %d/%d lat %g/%g comment '%s'/'%s'", pn ? "parseDprs" : "parseNmea", line.c_str(), ra, rb, xa.lat, xb.lat, xa.comment.c_str(), xb.comment.c_str());
        if (ra) (pn ? parsedDprs : parsedNmea)++;
        // both parsers on both kinds of line
        A::Fix qa; R::Fix qb;
        sentinel(qa); sentinel(qb);
        SAME((pn ? A::parseNmea(line, qa) : A::parseDprs(line, qa)) == (pn ? R::parseNmea(line, qb) : R::parseDprs(line, qb)) && fixEq(qa, qb), "cross parse");
    }
    printf("dstar_proto_rs: %ld lines parsed as NMEA, %ld as D-PRS\n", parsedNmea, parsedDprs);
    SAME(parsedNmea > iters / 20 && parsedDprs > iters / 20, "the generated lines should parse often enough to be a useful test");
    printf("dstar_proto_rs: %ld checks, %ld mismatches\n", checks, fails);
    return fails ? 1 : 0;
}
