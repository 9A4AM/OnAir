// Differential test: the C++ AX.25 / APRS layer (generated copies in dect2::ax25_ref and dect2::aprs_ref, see cmake/gen_rs_ref.cmake) against the Rust
// one (dect2::ax25 and dect2::aprs when DECT2_RUST is used). The same inputs go to both and every result has to be identical (doubles bit for bit):
// FCS, frames built by the transmitter and damaged / truncated / random, address and path text, frame type names, the HDLC receiver fed with
// transmissions with and without noise, APRS information fields of every type with mutations, latitude / longitude text, ONAIR_DIFF_ITERS (default
// 200000) per family.
#include "dect2/packet/packet_ax25.h"
#include "dect2/packet/packet_aprs.h"
#include "packet_ax25_ref.h"
#include "packet_aprs_ref.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
namespace A = dect2::ax25;
namespace R = dect2::ax25_ref;
namespace PA = dect2::aprs;
namespace PR = dect2::aprs_ref;

static long fails = 0, checks = 0, framesParsed = 0, hdlcGood = 0, hdlcBad = 0, aprsTrue = 0;
static long typeCount[16];
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)

static std::mt19937 g(31337);
static std::string rnd(const char* alphabet, size_t n) { std::string s; for (size_t i = 0; i < n; i++) s += alphabet[g() % strlen(alphabet)]; return s; }
static bool dEq(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }

template <class AD, class BD> static bool addrEq(const AD& a, const BD& b) { return a.call == b.call && a.ssid == b.ssid && a.h == b.h; }
template <class AF, class BF> static bool frameEq(const AF& a, const BF& b) {
    if (!addrEq(a.to, b.to) || !addrEq(a.from, b.from) || a.path.size() != b.path.size() || a.control != b.control || a.pid != b.pid || a.info != b.info) return false;
    for (size_t i = 0; i < a.path.size(); i++) if (!addrEq(a.path[i], b.path[i])) return false;
    return true;
}
template <class AI, class BI> static bool infoEq(const AI& a, const BI& b) {
    const double* wa = &a.wx.windDirDeg; const double* wb = &b.wx.windDirDeg;
    for (int i = 0; i < 9; i++) if (!dEq(*(wa + i), *(wb + i))) return false;
    return a.type == b.type && a.hasPos == b.hasPos && dEq(a.lat, b.lat) && dEq(a.lon, b.lon) && a.symTable == b.symTable && a.symCode == b.symCode &&
           a.hasAlt == b.hasAlt && dEq(a.altM, b.altM) && a.hasCourse == b.hasCourse && a.courseDeg == b.courseDeg && a.hasSpeed == b.hasSpeed &&
           dEq(a.speedKnots, b.speedKnots) && a.name == b.name && a.live == b.live && a.comment == b.comment && a.text == b.text && a.msgNo == b.msgNo &&
           a.isAck == b.isAck && a.isRej == b.isRej && a.micEStatus == b.micEStatus && a.hasWx == b.hasWx && a.summary == b.summary;
}

static std::string callsign(bool valid) {
    if (!valid) return rnd("ABCxyz09 -#", g() % 9);
    return rnd("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 1 + g() % 6);
}

static R::Frame randFrame(bool valid) {
    R::Frame f;
    f.to.call = callsign(valid); f.to.ssid = (int)(g() % 16); f.to.h = g() % 2;
    f.from.call = callsign(valid); f.from.ssid = (int)(g() % 16); f.from.h = g() % 2;
    const int np = (int)(g() % 9);
    for (int i = 0; i < np; i++) { R::Address a; a.call = callsign(valid); a.ssid = (int)(g() % 16); a.h = g() % 3 == 0; f.path.push_back(a); }
    static const uint8_t ctl[] = {0x03, 0x13, 0x00, 0x02, 0x10, 0x01, 0x05, 0x09, 0x0D, 0x2F, 0x3F, 0x6F, 0x43, 0x53, 0x0F, 0x1F, 0x63, 0x73, 0x87, 0xAF, 0xE3, 0xFF};
    f.control = g() % 4 ? 0x03 : ctl[g() % sizeof ctl];
    f.pid = (f.control & 1) == 0 || (f.control & 0xEF) == 0x03 ? (int)(g() % 256) : -1;
    f.info = rnd("!=/@>:;`'_ABCDEFGHIJ0123456789.,-/ \r\n\x01\xff", g() % 120);
    return f;
}

static A::Frame toA(const R::Frame& f) {
    A::Frame a;
    a.to.call = f.to.call; a.to.ssid = f.to.ssid; a.to.h = f.to.h;
    a.from.call = f.from.call; a.from.ssid = f.from.ssid; a.from.h = f.from.h;
    for (const auto& p : f.path) { A::Address x; x.call = p.call; x.ssid = p.ssid; x.h = p.h; a.path.push_back(x); }
    a.control = f.control; a.pid = f.pid; a.info = f.info;
    return a;
}

static void parseBoth(const std::vector<uint8_t>& d) {
    A::Frame a; R::Frame b;
    const bool pa = A::parseFrame(d.data(), d.size(), a), pb = R::parseFrame(d.data(), d.size(), b);
    SAME(pa == pb, "parseFrame result n=%zu", d.size());
    if (pa && pb) {
        SAME(frameEq(a, b), "parseFrame fields n=%zu", d.size());
        SAME(a.tnc2() == b.tnc2() && a.pathStr() == b.pathStr() && a.typeName() == b.typeName() && a.from.str() == b.from.str() && a.isUi() == b.isUi(),
             "frame text n=%zu", d.size());
        framesParsed++;
    }
}

static void testFrames(long iters) {
    for (int c = 0; c < 256; c++) { A::Frame a; R::Frame b; a.control = b.control = (uint8_t)c; SAME(a.typeName() == b.typeName(), "typeName %02x", c); }
    for (long i = 0; i < iters; i++) {
        const R::Frame rf = randFrame(g() % 8 != 0);
        const A::Frame af = toA(rf);
        SAME(af.tnc2() == rf.tnc2() && af.pathStr() == rf.pathStr() && af.typeName() == rf.typeName() && af.to.str() == rf.to.str() && af.from.str() == rf.from.str(),
             "text of built frame: '%s' / '%s' path '%s' / '%s' from '%s' / '%s'", af.tnc2().c_str(), rf.tnc2().c_str(), af.pathStr().c_str(), rf.pathStr().c_str(), af.from.str().c_str(), rf.from.str().c_str());
        std::vector<uint8_t> bytes = A::buildFrame(af);
        SAME(bytes == R::buildFrame(rf), "buildFrame");
        SAME(A::fcsOk(bytes.data(), bytes.size()) == R::fcsOk(bytes.data(), bytes.size()) && A::fcsOk(bytes.data(), bytes.size()), "fcsOk built");
        std::vector<uint8_t> body(bytes.begin(), bytes.end() - 2);
        parseBoth(body);
        const int how = (int)(g() % 5);
        if (how == 1 && !body.empty()) for (int k = (int)(1 + g() % 3); k > 0; k--) body[g() % body.size()] ^= (uint8_t)(1u << (g() % 8));
        else if (how == 2) body.resize(g() % (body.size() + 1));
        else if (how == 3) { body.resize(g() % 80); for (auto& x : body) x = (uint8_t)g(); }
        else if (how == 4 && body.size() > 16) body[g() % 14] = (uint8_t)g();
        parseBoth(body);
        std::vector<uint8_t> damaged = bytes;
        if (!damaged.empty() && g() % 2) damaged[g() % damaged.size()] ^= (uint8_t)(1u << (g() % 8));
        SAME(A::fcs(damaged.data(), damaged.size()) == R::fcs(damaged.data(), damaged.size()), "fcs");
        SAME(A::fcsOk(damaged.data(), damaged.size()) == R::fcsOk(damaged.data(), damaged.size()), "fcsOk damaged");
    }
    for (size_t n = 0; n < 4; n++) { const uint8_t z[4] = {1, 2, 3, 4}; SAME(A::fcsOk(z, n) == R::fcsOk(z, n), "fcsOk short"); }
}

struct Got { std::vector<uint8_t> data; bool ok; };

static void testHdlc(long iters) {
    for (long i = 0; i < iters / 10; i++) {
        const bool nrzi = g() % 3 != 0;
        std::vector<Got> ga, gb;
        A::HdlcRx ha([&](const std::vector<uint8_t>& d, bool ok) { ga.push_back({d, ok}); }, nrzi);
        R::HdlcRx hb([&](const std::vector<uint8_t>& d, bool ok) { gb.push_back({d, ok}); }, nrzi);
        std::vector<uint8_t> bits;
        const int nf = 1 + (int)(g() % 4);
        for (int k = 0; k < nf; k++) {
            const R::Frame rf = randFrame(true);
            const std::vector<uint8_t> fb = R::buildFrame(rf);
            const std::vector<uint8_t> hb2 = R::hdlcBits(fb, 1 + (int)(g() % 6), 1 + (int)(g() % 3));
            bits.insert(bits.end(), hb2.begin(), hb2.end());
            if (g() % 3 == 0) for (int q = (int)(g() % 40); q > 0; q--) bits.push_back((uint8_t)(g() & 1));
        }
        std::vector<uint8_t> line = nrzi ? R::nrziEncode(bits, (uint8_t)(g() & 1)) : bits;
        const int noise = (int)(g() % 4);
        if (noise == 1) for (int q = (int)(g() % 6); q > 0; q--) line[g() % line.size()] ^= 1;
        else if (noise == 2) { const size_t s = g() % line.size(); for (size_t q = s; q < line.size() && q < s + 30; q++) line[q] = (uint8_t)(g() & 1); }
        else if (noise == 3) for (auto& x : line) if (g() % 50 == 0) x ^= 1;
        for (size_t k = 0; k < line.size(); k++) {
            ha.bit(line[k]); hb.bit(line[k]);
            SAME(ha.inFrame() == hb.inFrame() && ha.flagSeen() == hb.flagSeen(), "hdlc state at bit %zu", k);
            if (k % 7 == 0) SAME(ha.takeClockResync() == hb.takeClockResync(), "hdlc resync at bit %zu", k);
            if (g() % 5000 == 0) { ha.reset(); hb.reset(); }
        }
        SAME(ga.size() == gb.size(), "hdlc frames %zu / %zu", ga.size(), gb.size());
        for (size_t k = 0; k < ga.size() && k < gb.size(); k++) SAME(ga[k].ok == gb[k].ok && ga[k].data == gb[k].data, "hdlc frame %zu", k);
        for (const Got& x : ga) (x.ok ? hdlcGood : hdlcBad)++;
    }
    for (long i = 0; i < iters / 20; i++) {   // plain random bit streams, long runs of ones included
        std::vector<Got> ga, gb;
        A::HdlcRx ha([&](const std::vector<uint8_t>& d, bool ok) { ga.push_back({d, ok}); }, false);
        R::HdlcRx hb([&](const std::vector<uint8_t>& d, bool ok) { gb.push_back({d, ok}); }, false);
        const int density = 1 + (int)(g() % 9);
        for (int k = 0; k < 3000; k++) { const int b = (int)(g() % 10) < density; ha.bit(b); hb.bit(b); }
        SAME(ga.size() == gb.size(), "hdlc random frames");
    }
}

static const char* const kInfoSeeds[] = {
    "!4903.50N/07201.75W-Test 001234", "=4903.50N/07201.75W>123/045/A=001234 hello", "/092345z4903.50N/07201.75W>088/036", "@092345/4903.50N/07201.75W_220/004g005t077r000p000P000h50b09900",
    "!4903.5 N/07201.7 W-ambiguous", "!/5L!!<*e7>7P[", "=/5L!!<*e7> sT", "!\\5L!!<*e7OS]Svc", "@092345z/5L!!<*e7_7P[220/004g005t077", ";LEADER   *092345z4903.50N/07201.75W>088/036",
    ";LEADER   _092345z4903.50N/07201.75W>", ")AID #2!4903.50N/07201.75Wa", ")AID #2_4903.50N/07201.75Wa", ">Net Control Center", ">092345zStatus here", ":WB4APR   :Hello World{3",
    ":WB4APR   :ack3", ":WB4APR   :rej7", ":BLNA     :Bulletin text", "T#005,199,000,255,073,123,01101001", "$GPRMC,092751.000,A,5321.6802,N,00630.3372,W,0.06,31.66,280511,,,A*45",
    "<IGATE,MSG_CNT=10,LOC_CNT=5", "?APRS?", "?IGATE?", "}W1AW>APRS,TCPIP*:!4903.50N/07201.75W-", "!!0000006E0000", "_10090556c220s004g005t077r000p000P000h50b09900wRSW",
    "_10090556220/004g005t077r001p002P003h00b10132", "Xunknown type", "`(_f\"Oj/>comment", "'(_f\"Oj/]\"4V}Alt 1234", "`(_f\"Oj/`\"4V}", "`(_f\"Oj/>}ab!}x",
};

static std::string mutateInfo(std::string s) {
    static const char alpha[] = "0123456789NSEW/\\_!=@>:;)<?}`' ABCDEFGHxyz.,-{*#}\r\n\x01\x1c\x1d\xff\x7f";
    if (g() % 6 == 0) return rnd(alpha, g() % 60);
    for (int k = (int)(g() % 5); k > 0; k--) {
        switch (g() % 6) {
        case 0: if (!s.empty()) s[g() % s.size()] = alpha[g() % (sizeof alpha - 1)]; break;
        case 1: if (!s.empty()) s[g() % s.size()] = (char)g(); break;
        case 2: if (!s.empty()) s.erase(g() % s.size(), 1 + g() % 3); break;
        case 3: s.insert(g() % (s.size() + 1), 1, alpha[g() % (sizeof alpha - 1)]); break;
        case 4: s.resize(g() % (s.size() + 1)); break;
        default: s += rnd(alpha, g() % 12); break;
        }
    }
    return s;
}

static std::string randDest() {
    switch (g() % 5) {
    case 0: return "APRS";
    case 1: return rnd("0123456789ABCDEFGHIJKLPQRSTUVWXYZ", 6) + (g() % 2 ? "-7" : "");
    case 2: return "S32U6T";
    case 3: return rnd("0123456789KLPZ", 6);
    default: return rnd("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-", g() % 9);
    }
}

static void testAprs(long iters) {
    const size_t ns = sizeof kInfoSeeds / sizeof *kInfoSeeds;
    for (long i = 0; i < iters; i++) {
        std::string s = (g() % 10) ? kInfoSeeds[g() % ns] : std::string(1, (char)g());
        if (g() % 5) s = mutateInfo(s);
        const std::string dest = randDest();
        PA::Info a; PR::Info b;
        const bool ra = PA::parse(dest, s, a), rb = PR::parse(dest, s, b);
        SAME(ra == rb && infoEq(a, b), "aprs parse dest '%s' info '%s': %d/%d type '%s' / '%s' summary '%s' / '%s'", dest.c_str(), s.c_str(), (int)ra, (int)rb,
             a.type.c_str(), b.type.c_str(), a.summary.c_str(), b.summary.c_str());
        aprsTrue += ra;
        for (size_t k = 0; k < 13; k++) { static const char* n[] = {"Position", "Mic-E", "Object", "Item", "Status", "Message", "Weather", "Telemetry", "Raw GPS", "Capabilities", "Query", "Third-party", "Other"}; if (a.type == n[k]) typeCount[k]++; }
    }
    for (int i = 0; i < 6; i++) { PA::Info a; PR::Info b; SAME(PA::parse("APRS", "", a) == PR::parse("APRS", "", b) && infoEq(a, b), "aprs empty"); }
    static const double special[] = {0.0, -0.0, 25.2, -25.2, 90.0, -180.0, 179.99999, 1e-9, -1e-9, 0.00005, 0.00015, 1234.56789, -1e6, 1e15, NAN, INFINITY, -INFINITY};
    for (double v : special) SAME(PA::fmtLat(v) == PR::fmtLat(v) && PA::fmtLon(v) == PR::fmtLon(v), "fmtLat %g", v);
    for (long i = 0; i < iters; i++) {
        double v = ((double)g() / 4294967296.0 - 0.5) * (g() % 2 ? 360.0 : 4.0);
        SAME(PA::fmtLat(v) == PR::fmtLat(v) && PA::fmtLon(v) == PR::fmtLon(v), "fmtLat %.17g", v);
    }
}

int main() {
    const char* it = getenv("ONAIR_DIFF_ITERS");
    const long iters = it ? atol(it) : 200000;
    testFrames(iters);
    testHdlc(iters);
    testAprs(iters);
    printf("test_packet_proto_rs: %ld checks, %ld mismatches (frames parsed %ld, hdlc frames good %ld bad %ld, aprs parsed %ld; types: pos %ld mic-e %ld obj %ld item %ld status %ld msg %ld wx %ld tel %ld other %ld)\n",
           checks, fails, framesParsed, hdlcGood, hdlcBad, aprsTrue, typeCount[0], typeCount[1], typeCount[2], typeCount[3], typeCount[4], typeCount[5], typeCount[6], typeCount[7], typeCount[12]);
    return fails ? 1 : 0;
}
