// TETRA broadcast PDUs (tetra_pdu.h).
#include "dect2/tetra_pdu.h"
#include <cstdio>
#include <random>

namespace dect2::tetra {

namespace {

struct W {
    Bits b;
    void put(uint64_t v, int n) { for (int i = n - 1; i >= 0; i--) b.push_back((uint8_t)((v >> i) & 1)); }
};

struct R {
    const Bits& b;
    size_t p, end;
    bool ok = true;
    R(const Bits& bits, size_t from, size_t to) : b(bits), p(from), end(std::min(to, bits.size())) {}
    uint64_t get(int n) {
        if (p + (size_t)n > end) { ok = false; p = end; return 0; }
        uint64_t v = 0;
        for (int i = 0; i < n; i++) v = (v << 1) | b[p++];
        return v;
    }
    bool bit() { return get(1) != 0; }
    size_t left() const { return end - p; }
};

} // namespace

// ---------------------------------------------------------------- SYNC

Bits encodeSync(const Sync& s) {
    W w;
    w.put((uint64_t)s.systemCode, 4); w.put((uint64_t)s.colourCode, 6); w.put((uint64_t)(s.tn - 1), 2); w.put((uint64_t)s.fn, 5); w.put((uint64_t)s.mn, 6);
    w.put((uint64_t)s.sharingMode, 2); w.put((uint64_t)s.tsReservedFrames, 3); w.put(s.uplaneDtx, 1); w.put(s.frame18Ext, 1); w.put(0, 1);
    w.put((uint64_t)s.mcc, 10); w.put((uint64_t)s.mnc, 14); w.put((uint64_t)s.nbrBroadcast, 2); w.put((uint64_t)s.cellLoad, 2); w.put(s.lateEntry, 1);
    return w.b;
}

bool parseSync(const Bits& b, Sync& s) {
    R r(b, 0, 60);
    s.systemCode = (int)r.get(4); s.colourCode = (int)r.get(6); s.tn = (int)r.get(2) + 1; s.fn = (int)r.get(5); s.mn = (int)r.get(6);
    s.sharingMode = (int)r.get(2); s.tsReservedFrames = (int)r.get(3); s.uplaneDtx = r.bit(); s.frame18Ext = r.bit(); r.get(1);
    s.mcc = (int)r.get(10); s.mnc = (int)r.get(14); s.nbrBroadcast = (int)r.get(2); s.cellLoad = (int)r.get(2); s.lateEntry = r.bit();
    return r.ok && s.fn >= 1 && s.fn <= 18 && s.mn >= 1 && s.mn <= 60;
}

// ---------------------------------------------------------------- SYSINFO

Bits encodeSysinfo(const Sysinfo& s) {
    W w;
    w.put(2, 2); w.put(0, 2);
    w.put((uint64_t)s.mainCarrier, 12); w.put((uint64_t)s.band, 4); w.put((uint64_t)s.offset, 2); w.put((uint64_t)s.duplex, 3); w.put(s.reverse, 1);
    w.put((uint64_t)s.numScch, 2); w.put((uint64_t)s.msTxPwrMax, 3); w.put((uint64_t)s.rxLevAccessMin, 4); w.put((uint64_t)s.accessParam, 4); w.put((uint64_t)s.dlTimeout, 4);
    w.put(s.cckFlag, 1); w.put((uint64_t)s.hyperframeOrCck, 16);
    w.put((uint64_t)s.optFlag, 2); w.put(s.optValue, 20);
    w.put((uint64_t)s.la, 14); w.put((uint64_t)s.subscriberClass, 16); w.put((uint64_t)s.bsService, 12);
    return w.b;
}

bool parseSysinfo(const Bits& b, Sysinfo& s) {
    R r(b, 0, 124);
    if (r.get(2) != 2 || r.get(2) != 0) return false;
    s.mainCarrier = (int)r.get(12); s.band = (int)r.get(4); s.offset = (int)r.get(2); s.duplex = (int)r.get(3); s.reverse = r.bit();
    s.numScch = (int)r.get(2); s.msTxPwrMax = (int)r.get(3); s.rxLevAccessMin = (int)r.get(4); s.accessParam = (int)r.get(4); s.dlTimeout = (int)r.get(4);
    s.cckFlag = r.bit(); s.hyperframeOrCck = (int)r.get(16);
    s.optFlag = (int)r.get(2); s.optValue = (uint32_t)r.get(20);
    s.la = (int)r.get(14); s.subscriberClass = (int)r.get(16); s.bsService = (int)r.get(12);
    return r.ok;
}

std::string serviceText(int v) {
    static const struct { int bit; const char* name; } t[] = {
        {kSvcRegistration, "registration required"}, {kSvcDeregistration, "de-registration required"}, {kSvcPriorityCell, "priority cell"},
        {kSvcNoMinimumMode, "never minimum mode"}, {kSvcMigration, "migration"}, {kSvcSystemWide, "system wide services"}, {kSvcVoice, "voice"},
        {kSvcCircuitData, "circuit data"}, {kSvcSndcp, "packet data (SNDCP)"}, {kSvcAirEncryption, "air interface encryption"}, {kSvcAdvancedLink, "advanced link"}};
    std::string s;
    for (const auto& e : t) if (v & e.bit) { if (!s.empty()) s += ", "; s += e.name; }
    return s.empty() ? "none" : s;
}

Security parseSecurity(const Sysinfo& s) {
    Security o;
    if (s.optFlag != 3) return o;
    const int sec = (int)((s.optValue >> 12) & 0xFF);
    o.valid = true;
    o.authRequired = (sec >> 7) & 1;
    o.class1 = (sec >> 6) & 1;
    o.class3 = (sec >> 5) & 1;
    o.class2 = !o.class3 && ((sec >> 4) & 1);
    return o;
}

double dlCarrierHz(int band, int carrier, int offset) {
    static const double off[4] = {0, 6250, -6250, 12500};
    return band * 100e6 + carrier * 25e3 + off[offset & 3];
}

double duplexSpacingHz(int band, int duplex) {
    static const int t[8][16] = {   // kHz, -1 reserved (EN 300 392-1 annex / TS 100 392-15)
        {-1, 1600, 10000, 10000, 10000, 10000, 10000, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {-1, 4500, -1, 36000, 7000, -1, -1, -1, 45000, 45000, -1, -1, -1, -1, -1, -1},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {-1, -1, -1, 8000, 8000, -1, -1, -1, 18000, 18000, -1, -1, -1, -1, -1, -1},
        {-1, -1, -1, 18000, 5000, -1, 30000, 30000, -1, 39000, -1, -1, -1, -1, -1, -1},
        {-1, -1, -1, -1, 9500, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};
    const int v = t[duplex & 7][band & 15];
    return v < 0 ? -1.0 : v * 1e3;
}

double ulCarrierHz(int band, int carrier, int offset, int duplex, bool reverse) {
    const double d = duplexSpacingHz(band, duplex);
    if (d < 0) return 0;
    const double dl = dlCarrierHz(band, carrier, offset);
    return reverse ? dl + d : dl - d;
}

// ---------------------------------------------------------------- network time

std::string networkTimeText(uint64_t t) {
    const uint32_t utc2 = (uint32_t)((t >> 24) & 0xFFFFFF);
    const int sign = (int)((t >> 23) & 1), offs = (int)((t >> 17) & 0x3F), year = 2000 + (int)((t >> 11) & 0x3F);
    if (utc2 == 0xFFFFFF) return "";
    uint32_t sec = utc2 * 2;
    int day = (int)(sec / 86400); sec %= 86400;
    static const int md[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    int mon = 0;
    while (mon < 12) { const int d = md[mon] + (mon == 1 && leap ? 1 : 0); if (day < d) break; day -= d; mon++; }
    if (mon >= 12) return "";
    char b[96];
    snprintf(b, sizeof b, "%04d-%02d-%02d %02u:%02u:%02u UTC, local %c%02d:%02d", year, mon + 1, day + 1, sec / 3600, (sec / 60) % 60, sec % 60,
             sign ? '-' : '+', offs * 15 / 60, offs * 15 % 60);
    return b;
}

// ---------------------------------------------------------------- MAC / LLC / MLE

namespace {

const int kAddrLen[8] = {0, 24, 10, 24, 24, 34, 30, 34};

bool parseNeighbour(R& r, Neighbour& n) {
    n.cellId = (int)r.get(5); n.reselTypes = (int)r.get(2); n.synced = r.bit(); n.cellLoad = (int)r.get(2); n.carrier = (int)r.get(12);
    if (r.bit()) {   // O-bit: the type 2 elements, each behind its P-bit
        if (r.bit()) { n.hasExt = true; n.band = (int)r.get(4); n.offset = (int)r.get(2); n.duplex = (int)r.get(3); n.reverse = r.bit(); }
        if (r.bit()) n.mcc = (int)r.get(10);
        if (r.bit()) n.mnc = (int)r.get(14);
        if (r.bit()) n.la = (int)r.get(14);
        if (r.bit()) n.maxTx = (int)r.get(3);
        if (r.bit()) n.minRx = (int)r.get(4);
        if (r.bit()) n.subscriberClass = (int)r.get(16);
        if (r.bit()) n.bsService = (int)r.get(12);
        if (r.bit()) n.timeshare = (int)r.get(5);
        if (r.bit()) n.frameOffset = (int)r.get(6);
    }
    return r.ok;
}

bool parseNwrk(R& r, NwrkBroadcast& n) {
    n.reselect = (int)r.get(16); n.cellLoad = (int)r.get(2);
    int count = 0;
    if (r.left() > 0 && r.bit()) {
        if (r.bit()) { n.hasTime = true; n.time48 = r.get(48); }
        if (r.bit()) count = (int)r.get(3);
    }
    for (int i = 0; i < count && r.ok; i++) { Neighbour nb; if (parseNeighbour(r, nb)) n.nbrs.push_back(nb); }
    return r.ok;
}

void parseTlSdu(const Bits& b, size_t from, size_t to, MacBlock& out) {
    R r(b, from, to);
    const int llc = (int)r.get(4);
    switch (llc) {
    case 0: case 4: r.get(2); break;      // BL-ADATA: N(R), N(S)
    case 1: case 5: r.get(1); break;      // BL-DATA: N(S)
    case 2: case 6: break;                // BL-UDATA
    case 3: case 7: r.get(1); break;      // BL-ACK: N(R)
    default: return;                      // AL- (advanced link) and others: not read
    }
    if (llc >= 4) { if (r.end < r.p + 32) return; r.end -= 32; }   // the frame check sequence
    const int pd = (int)r.get(3);
    if (!r.ok) return;
    out.byPd[pd & 7]++;
    if (pd != 5) return;   // only the mobility link entity's broadcasts are read
    const int type = (int)r.get(3);
    if (type == 2) { NwrkBroadcast n; if (parseNwrk(r, n)) out.nwrk.push_back(n); }
}

// The channel allocation element (21.5.2): its length, or -1 when it cannot be skipped (augmented)
int chanAllocLen(const Bits& b, size_t p) {
    R r(b, p, b.size());
    r.get(2); r.get(4);
    const int ulDl = (int)r.get(2);
    r.get(1); r.get(1); r.get(12);
    if (r.bit()) r.get(10);
    if (r.get(2) == 0) r.get(2);
    if (ulDl == 0) return -1;
    return r.ok ? (int)(r.p - p) : -1;
}

} // namespace

void parseMacBlock(const Bits& b, MacBlock& out) {
    const size_t n = b.size();
    size_t pos = 0;
    while (pos + 16 <= n) {
        R r(b, pos, n);
        const int type = (int)r.get(2);
        if (type == 2) {   // broadcast: SYSINFO or ACCESS-DEFINE
            const int bt = (int)r.get(2);
            if (bt == 0 && n - pos >= 124) { Bits s(b.begin() + (long)pos, b.begin() + (long)pos + 124); Sysinfo si; if (parseSysinfo(s, si)) { out.sysinfo = true; out.si = si; } }
            else if (bt == 1) out.accessDefine = true;
            break;
        }
        if (type == 1) { out.fragments++; break; }   // MAC-FRAG / MAC-END: a continuation, not reassembled
        if (type == 3) break;                         // supplementary / U-signal
        const bool fill = r.bit();
        r.bit();                                      // position of grant
        const int enc = (int)r.get(2);
        r.bit();                                      // random access flag
        const int li = (int)r.get(6);
        const int at = (int)r.get(3);
        if (at == 0) break;                           // null PDU: the rest of the block is empty
        bool frag = false;
        size_t len;
        if (li == 0x3F || li == 0x3E) { frag = li == 0x3F; len = n - pos; }
        else if (li >= 1 && li <= 0x3A) len = (size_t)li * 8;
        else break;
        if (pos + len > n) len = n - pos;
        out.resources++;
        r.get(kAddrLen[at]);                          // the address is skipped, never kept
        if (r.bit()) r.get(4);                        // power control
        if (r.bit()) r.get(8);                        // slot granting
        const bool ca = r.bit();
        if (enc != 0) out.encrypted++;
        else if (r.ok) {
            bool readable = true;
            if (ca) { const int l = chanAllocLen(b, r.p); if (l < 0) readable = false; else r.p += (size_t)l; }
            if (readable && !frag) {
                size_t end = pos + len;
                if (fill) { while (end > r.p && !b[end - 1]) end--; if (end > r.p) end--; }   // fill bits: a 1 then 0s
                if (end > r.p) parseTlSdu(b, r.p, end, out);
            }
        }
        if (frag) { out.fragments++; break; }
        pos += len;
    }
}

int aachDlUsage(uint16_t aa, bool frame18) {
    if (frame18) return -1;
    const int hdr = (aa >> 12) & 3, f1 = (aa >> 6) & 63;
    return hdr == 0 ? 2 : f1;
}

const char* dlUsageText(int u) {
    switch (u) {
    case -1: return "-";
    case 0: return "unallocated";
    case 1: return "assigned control";
    case 2: return "common control";
    case 3: return "reserved";
    default: return "traffic";
    }
}

// ---------------------------------------------------------------- generator PDUs

Bits macNullPdu(int type1Bits) {
    W w;
    w.put(0, 2); w.put(0, 1); w.put(0, 1); w.put(0, 2); w.put(0, 1); w.put(2, 6); w.put(0, 3);
    w.b.resize((size_t)type1Bits, 0);
    return w.b;
}

Bits macBroadcastResource(const Bits& mle, int type1Bits, int encMode, uint32_t seed) {
    W h;
    const int hdr = 2 + 1 + 1 + 2 + 1 + 6 + 3 + 24 + 3;
    int used = hdr + 4 + (int)mle.size();
    const bool fill = used % 8 != 0;
    if (fill) used += 1;
    int li = (used + 7) / 8;
    if (li * 8 > type1Bits) li = type1Bits / 8;
    h.put(0, 2); h.put(fill, 1); h.put(0, 1); h.put((uint64_t)encMode, 2); h.put(0, 1); h.put((uint64_t)li, 6); h.put(1, 3); h.put(0xFFFFFF, 24);
    h.put(0, 1); h.put(0, 1); h.put(0, 1);
    Bits sdu;
    W l; l.put(2, 4);   // LLC BL-UDATA
    sdu = l.b;
    sdu.insert(sdu.end(), mle.begin(), mle.end());
    if (encMode) { std::mt19937 g(seed); for (auto& v : sdu) v = (uint8_t)(g() & 1); }
    Bits out = h.b;
    out.insert(out.end(), sdu.begin(), sdu.end());
    if (fill) out.push_back(1);
    out.resize((size_t)li * 8, 0);
    if ((int)out.size() + 16 <= type1Bits) { const Bits nul = macNullPdu(16); out.insert(out.end(), nul.begin(), nul.end()); }
    out.resize((size_t)type1Bits, 0);
    return out;
}

Bits mleNwrkBroadcast(const NwrkBroadcast& n) {
    W w;
    w.put(5, 3); w.put(2, 3);
    w.put((uint64_t)n.reselect, 16); w.put((uint64_t)n.cellLoad, 2);
    w.put(1, 1);   // O-bit
    w.put(n.hasTime, 1); if (n.hasTime) w.put(n.time48, 48);
    w.put(1, 1); w.put(n.nbrs.size(), 3);
    for (const auto& nb : n.nbrs) {
        w.put((uint64_t)nb.cellId, 5); w.put((uint64_t)nb.reselTypes, 2); w.put(nb.synced, 1); w.put((uint64_t)nb.cellLoad, 2); w.put((uint64_t)nb.carrier, 12);
        w.put(1, 1);
        w.put(nb.hasExt, 1); if (nb.hasExt) { w.put((uint64_t)nb.band, 4); w.put((uint64_t)nb.offset, 2); w.put((uint64_t)nb.duplex, 3); w.put(nb.reverse, 1); }
        w.put(nb.mcc >= 0, 1); if (nb.mcc >= 0) w.put((uint64_t)nb.mcc, 10);
        w.put(nb.mnc >= 0, 1); if (nb.mnc >= 0) w.put((uint64_t)nb.mnc, 14);
        w.put(nb.la >= 0, 1); if (nb.la >= 0) w.put((uint64_t)nb.la, 14);
        w.put(0, 1); w.put(0, 1); w.put(0, 1);
        w.put(nb.bsService >= 0, 1); if (nb.bsService >= 0) w.put((uint64_t)nb.bsService, 12);
        w.put(0, 1); w.put(0, 1);
    }
    w.put(0, 1);   // M-bit: no type 3 elements
    return w.b;
}

} // namespace dect2::tetra
