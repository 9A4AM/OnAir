// Differential test: the C++ TETRA network information layer (a generated copy in dect2::tetra_ref, see cmake/gen_rs_ref.cmake) against the Rust one
// (dect2::tetra when DECT2_RUST_TETRA is used). The same inputs go to both and every result has to be identical (doubles bit for bit): SYNC and SYSINFO
// parsing (valid, mutated, truncated, random), service / security / carrier helpers over all inputs, network time text, access-assign usage, and
// the signalling block walk over generated broadcasts (neighbours, time, encrypted, fragments, fill bits) and noise, over ONAIR_DIFF_ITERS (default 200000)
// seeded inputs per family.
#include "dect2/tetra/tetra_pdu.h"
#include "tetra_pdu_ref.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
namespace A = dect2::tetra;
namespace R = dect2::tetra_ref;
using Bits = std::vector<uint8_t>;

static long fails = 0, checks = 0, okSync = 0, okSys = 0, nwrkSeen = 0, nbrSeen = 0, sysBlocks = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)
static std::mt19937 g(24681);
static bool dEq(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }

template <class S, class T> static bool syncEq(const S& a, const T& b) {
    return a.systemCode == b.systemCode && a.colourCode == b.colourCode && a.tn == b.tn && a.fn == b.fn && a.mn == b.mn && a.sharingMode == b.sharingMode &&
           a.tsReservedFrames == b.tsReservedFrames && a.uplaneDtx == b.uplaneDtx && a.frame18Ext == b.frame18Ext && a.mcc == b.mcc && a.mnc == b.mnc &&
           a.nbrBroadcast == b.nbrBroadcast && a.cellLoad == b.cellLoad && a.lateEntry == b.lateEntry;
}
template <class S, class T> static bool sysEq(const S& a, const T& b) {
    return a.mainCarrier == b.mainCarrier && a.band == b.band && a.offset == b.offset && a.duplex == b.duplex && a.reverse == b.reverse && a.numScch == b.numScch &&
           a.msTxPwrMax == b.msTxPwrMax && a.rxLevAccessMin == b.rxLevAccessMin && a.accessParam == b.accessParam && a.dlTimeout == b.dlTimeout &&
           a.cckFlag == b.cckFlag && a.hyperframeOrCck == b.hyperframeOrCck && a.optFlag == b.optFlag && a.optValue == b.optValue && a.la == b.la &&
           a.subscriberClass == b.subscriberClass && a.bsService == b.bsService;
}
template <class S, class T> static bool nbEq(const S& a, const T& b) {
    return a.cellId == b.cellId && a.reselTypes == b.reselTypes && a.cellLoad == b.cellLoad && a.carrier == b.carrier && a.synced == b.synced && a.hasExt == b.hasExt &&
           a.band == b.band && a.offset == b.offset && a.duplex == b.duplex && a.reverse == b.reverse && a.mcc == b.mcc && a.mnc == b.mnc && a.la == b.la &&
           a.maxTx == b.maxTx && a.minRx == b.minRx && a.subscriberClass == b.subscriberClass && a.bsService == b.bsService && a.timeshare == b.timeshare &&
           a.frameOffset == b.frameOffset;
}
template <class S, class T> static bool blkEq(const S& a, const T& b) {
    if (a.sysinfo != b.sysinfo || a.accessDefine != b.accessDefine || a.resources != b.resources || a.encrypted != b.encrypted || a.fragments != b.fragments) return false;
    if (memcmp(a.byPd, b.byPd, sizeof a.byPd) != 0 || a.nwrk.size() != b.nwrk.size()) return false;
    if (a.sysinfo && !sysEq(a.si, b.si)) return false;
    for (size_t i = 0; i < a.nwrk.size(); i++) {
        const auto& x = a.nwrk[i]; const auto& y = b.nwrk[i];
        if (x.reselect != y.reselect || x.cellLoad != y.cellLoad || x.hasTime != y.hasTime || x.time48 != y.time48 || x.nbrs.size() != y.nbrs.size()) return false;
        for (size_t j = 0; j < x.nbrs.size(); j++) if (!nbEq(x.nbrs[j], y.nbrs[j])) return false;
    }
    return true;
}

static Bits rndBits(size_t n) { Bits b(n); for (auto& v : b) v = g() & 1; return b; }
static void mutate(Bits& b) {
    const int k = (int)(g() % 5);
    for (int i = 0; i < k && !b.empty(); i++) {
        switch (g() % 5) {
        case 0: b[g() % b.size()] ^= 1; break;
        case 1: b.resize(g() % (b.size() + 1)); break;
        case 2: b.erase(b.begin() + (long)(g() % b.size())); break;
        case 3: b.insert(b.begin() + (long)(g() % (b.size() + 1)), (uint8_t)(g() & 1)); break;
        default: { const size_t at = g() % b.size(); for (size_t j = at; j < b.size() && j < at + 8; j++) b[j] = g() & 1; }
        }
    }
}

template <class N> static N rndNwrk() {
    N n;
    n.reselect = (int)(g() & 0xFFFF); n.cellLoad = (int)(g() & 3);
    n.hasTime = g() & 1; n.time48 = ((uint64_t)g() << 16 ^ g()) & 0xFFFFFFFFFFFFull;
    if (g() % 4 == 0) n.time48 = (uint64_t)(g() % 40000000) << 24 | (uint64_t)(g() & 1) << 23 | (uint64_t)(g() % 64) << 17 | (uint64_t)(g() % 64) << 11;
    const int cnt = (int)(g() % 8);
    for (int i = 0; i < cnt; i++) {
        typename std::remove_reference<decltype(n.nbrs[0])>::type nb;
        nb.cellId = (int)(g() & 31); nb.reselTypes = (int)(g() & 3); nb.synced = g() & 1; nb.cellLoad = (int)(g() & 3); nb.carrier = (int)(g() & 4095);
        nb.hasExt = g() & 1; nb.band = (int)(g() & 15); nb.offset = (int)(g() & 3); nb.duplex = (int)(g() & 7); nb.reverse = g() & 1;
        if (g() & 1) nb.mcc = (int)(g() & 1023);
        if (g() & 1) nb.mnc = (int)(g() & 16383);
        if (g() & 1) nb.la = (int)(g() & 16383);
        if (g() & 1) nb.bsService = (int)(g() & 4095);
        n.nbrs.push_back(nb);
    }
    return n;
}

static Bits mkBlock(int kind) {
    const int len = g() % 2 ? 124 : 268;
    switch (kind) {
    case 0: { A::Sysinfo s; s.mainCarrier = (int)(g() & 4095); s.band = (int)(g() & 15); s.optFlag = (int)(g() & 3); s.optValue = g() & 0xFFFFF; s.la = (int)(g() & 16383); s.bsService = (int)(g() & 4095); Bits b = A::encodeSysinfo(s); b.resize((size_t)len, 0); return b; }
    case 1: case 2: case 3: {
        const auto n = rndNwrk<A::NwrkBroadcast>();
        Bits b = A::macBroadcastResource(A::mleNwrkBroadcast(n), len, kind == 3 ? (int)(g() % 4) : 0, g());
        if (kind == 2) { const Bits more = A::macBroadcastResource(A::mleNwrkBroadcast(rndNwrk<A::NwrkBroadcast>()), len, 0, g()); b.insert(b.begin() + 16, more.begin(), more.begin() + 40); }
        return b;
    }
    case 4: return A::macNullPdu(len);
    default: return rndBits(g() % 300);
    }
}

int main() {
    const char* e = getenv("ONAIR_DIFF_ITERS");
    const long iters = e ? atol(e) : 200000;
    for (long it = 0; it < iters; it++) {
        // SYNC
        {
            A::Sync as; R::Sync rs;
            as.mcc = rs.mcc = 777; as.tn = rs.tn = 2;
            R::Sync src; src.systemCode = (int)(g() & 15); src.colourCode = (int)(g() & 63); src.tn = 1 + (int)(g() & 3); src.fn = 1 + (int)(g() % 18); src.mn = 1 + (int)(g() % 60);
            src.sharingMode = (int)(g() & 3); src.tsReservedFrames = (int)(g() & 7); src.uplaneDtx = g() & 1; src.frame18Ext = g() & 1;
            src.mcc = (int)(g() & 1023); src.mnc = (int)(g() & 16383); src.nbrBroadcast = (int)(g() & 3); src.cellLoad = (int)(g() & 3); src.lateEntry = g() & 1;
            Bits b = g() % 4 ? R::encodeSync(src) : rndBits(g() % 70);
            mutate(b);
            const bool a = A::parseSync(b, as), r = R::parseSync(b, rs);
            SAME(a == r && syncEq(as, rs), "sync it=%ld", it);
            okSync += a;
        }
        // SYSINFO
        {
            A::Sysinfo as; R::Sysinfo rs;
            as.la = rs.la = 4242; as.band = rs.band = 5;
            R::Sysinfo src; src.mainCarrier = (int)(g() & 4095); src.band = (int)(g() & 15); src.offset = (int)(g() & 3); src.duplex = (int)(g() & 7); src.reverse = g() & 1;
            src.numScch = (int)(g() & 3); src.msTxPwrMax = (int)(g() & 7); src.rxLevAccessMin = (int)(g() & 15); src.accessParam = (int)(g() & 15); src.dlTimeout = (int)(g() & 15);
            src.cckFlag = g() & 1; src.hyperframeOrCck = (int)(g() & 0xFFFF); src.optFlag = (int)(g() & 3); src.optValue = g() & 0xFFFFF;
            src.la = (int)(g() & 16383); src.subscriberClass = (int)(g() & 0xFFFF); src.bsService = (int)(g() & 4095);
            Bits b = g() % 4 ? R::encodeSysinfo(src) : rndBits(g() % 140);
            mutate(b);
            const bool a = A::parseSysinfo(b, as), r = R::parseSysinfo(b, rs);
            SAME(a == r && sysEq(as, rs), "sysinfo it=%ld", it);
            okSys += a;
            const auto sa = A::parseSecurity(as); const auto sr = R::parseSecurity(rs);
            SAME(sa.valid == sr.valid && sa.authRequired == sr.authRequired && sa.class1 == sr.class1 && sa.class2 == sr.class2 && sa.class3 == sr.class3, "security it=%ld", it);
            const int bs = (int)(g() & 0xFFF) | (g() % 8 == 0 ? (int)g() : 0);
            SAME(A::serviceText(bs) == R::serviceText(bs), "service %d", bs);
            const int band = (int)(g() % 40) - 8, car = (int)(g() % 9000) - 2000, off = (int)(g() % 12) - 4, dup = (int)(g() % 20) - 6;
            const bool rev = g() & 1;
            SAME(dEq(A::dlCarrierHz(band, car, off), R::dlCarrierHz(band, car, off)), "dl");
            SAME(dEq(A::duplexSpacingHz(band, dup), R::duplexSpacingHz(band, dup)), "dup");
            SAME(dEq(A::ulCarrierHz(band, car, off, dup, rev), R::ulCarrierHz(band, car, off, dup, rev)), "ul");
        }
        // network time and AACH
        {
            uint64_t t = ((uint64_t)g() << 24 ^ g()) & 0xFFFFFFFFFFFFull;
            if (g() % 3) t = (uint64_t)(g() % 17000000) << 24 | (t & 0xFFFFFF);
            if (g() % 50 == 0) t |= 0xFFFFFFull << 24;
            SAME(A::networkTimeText(t) == R::networkTimeText(t), "time %llx", (unsigned long long)t);
            const uint16_t aa = (uint16_t)g(); const bool f18 = g() & 1;
            const int u = A::aachDlUsage(aa, f18);
            SAME(u == R::aachDlUsage(aa, f18) && strcmp(A::dlUsageText(u), R::dlUsageText(u)) == 0, "aach");
        }
        // block walk
        {
            Bits b = mkBlock((int)(g() % 6));
            if (g() % 3 == 0) mutate(b);
            A::MacBlock a; R::MacBlock r;
            if (g() % 4 == 0) { a.resources = r.resources = 3; a.byPd[5] = r.byPd[5] = 2; }   // accumulation onto a filled record
            A::parseMacBlock(b, a); R::parseMacBlock(b, r);
            SAME(blkEq(a, r), "block it=%ld len=%zu", it, b.size());
            nwrkSeen += (long)a.nwrk.size(); sysBlocks += a.sysinfo;
            for (const auto& n : a.nwrk) nbrSeen += (long)n.nbrs.size();
        }
    }
    printf("tetra proto rs: %ld checks, %ld mismatches (valid sync %ld, sysinfo %ld, sysinfo blocks %ld, nwrk %ld, neighbours %ld)\n", checks, fails, okSync, okSys, sysBlocks, nwrkSeen, nbrSeen);
    return fails ? 1 : 0;
}
