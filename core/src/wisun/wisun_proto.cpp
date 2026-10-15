// Wi-SUN FAN / IEEE 802.15.4g SUN FSK frame layers (see wisun_proto.h).
#include "dect2/wisun_proto.h"
#include "dect2/zigbee_proto.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dect2 {
namespace wisun {

// ---------------------------------------------------------------- PHY

Phr parsePhr(uint16_t v) {
    Phr p;
    p.modeSwitch = (v >> 15) & 1;
    p.fcs2 = (v >> 12) & 1;
    p.whitened = (v >> 11) & 1;
    p.length = v & 0x7FF;
    return p;
}

uint16_t buildPhr(const Phr& p) {
    return (uint16_t)((p.modeSwitch ? 1u << 15 : 0u) | (p.fcs2 ? 1u << 12 : 0u) | (p.whitened ? 1u << 11 : 0u) | ((unsigned)p.length & 0x7FF));
}

namespace {
// The whitening octets: the PN9 register (all ones) stepped once per bit, its output the bit shifted out after the first nine
struct Pn9 {
    uint8_t b[2048];
    Pn9() {
        unsigned r = 0x1FF;
        auto step = [&r]() { const unsigned o = r & 1; const unsigned nb = (r ^ (r >> 5)) & 1; r = (r >> 1) | (nb << 8); return o; };
        for (int i = 0; i < 9; i++) step();
        for (int k = 0; k < 2048; k++) {
            unsigned v = 0;
            for (int j = 0; j < 8; j++) v |= step() << j;
            b[k] = (uint8_t)v;
        }
    }
};
const Pn9& pn9() { static const Pn9 t; return t; }

struct Crc32Table {
    uint32_t t[256];
    Crc32Table() {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            t[i] = c;
        }
    }
};
const Crc32Table& crcTable() { static const Crc32Table t; return t; }
}

void whiten(uint8_t* d, size_t n) {
    const Pn9& p = pn9();
    for (size_t i = 0; i < n; i++) d[i] ^= p.b[i % 2048];
}

uint32_t crc32(const uint8_t* d, size_t n) {
    const Crc32Table& t = crcTable();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = (c >> 8) ^ t.t[(c ^ d[i]) & 0xFF];
    return c ^ 0xFFFFFFFFu;
}

uint16_t crc16(const uint8_t* d, size_t n) { return zigbee::crc16(d, n); }

bool fcsOk(const uint8_t* p, size_t n, bool fcs2) {
    if (fcs2) {
        if (n < 3) return false;
        return crc16(p, n - 2) == (uint16_t)(p[n - 2] | p[n - 1] << 8);
    }
    if (n < 5) return false;
    const uint32_t f = (uint32_t)p[n - 4] | (uint32_t)p[n - 3] << 8 | (uint32_t)p[n - 2] << 16 | (uint32_t)p[n - 1] << 24;
    return crc32(p, n - 4) == f;
}

void addFcs(std::vector<uint8_t>& m, bool fcs2) {
    if (fcs2) {
        const uint16_t c = crc16(m.data(), m.size());
        m.push_back((uint8_t)c); m.push_back((uint8_t)(c >> 8));
    } else {
        const uint32_t c = crc32(m.data(), m.size());
        for (int i = 0; i < 4; i++) m.push_back((uint8_t)(c >> (8 * i)));
    }
}

// ---------------------------------------------------------------- channel plans

const std::vector<ChannelPlan>& channelPlans() {
    // Wi-SUN PHY specification: the 915 - 928 MHz Australia / New Zealand and 902 - 928 MHz North America operating classes
    static const std::vector<ChannelPlan> v = {
        {"ANZ", 1, 915.2, 200, 64, {50000, 0}},
        {"ANZ", 2, 915.4, 400, 32, {100000, 150000}},
        {"NA", 1, 902.2, 200, 129, {50000, 0}},
        {"NA", 2, 902.4, 400, 64, {100000, 150000}},
        {"NA", 3, 902.6, 600, 42, {200000, 300000}},
    };
    return v;
}

std::vector<ChannelPlan> plansOf(const std::string& region) {
    std::vector<ChannelPlan> out;
    for (const ChannelPlan& p : channelPlans()) if (region == p.region) out.push_back(p);
    return out;
}

int channelOf(const ChannelPlan& p, double mhz) {
    const double k = (mhz - p.ch0Mhz) * 1000.0 / p.spacingKhz;
    const int ch = (int)(k + (k >= 0 ? 0.5 : -0.5));
    if (ch < 0 || ch >= p.channels) return -1;
    return std::abs(k - ch) <= 0.5 ? ch : -1;
}

const char* regDomainName(int c) {
    switch (c) {
    case 0x00: return "World";
    case 0x01: return "North America";
    case 0x02: return "Japan";
    case 0x03: return "Europe";
    case 0x04: return "China";
    case 0x05: return "India";
    case 0x06: return "Mexico";
    case 0x07: return "Brazil";
    case 0x08: return "Australia / NZ";
    case 0x09: return "Korea";
    case 0x0A: return "Philippines";
    case 0x0B: return "Malaysia";
    case 0x0C: return "Hong Kong";
    case 0x0D: return "Singapore";
    case 0x0E: return "Thailand";
    case 0x0F: return "Vietnam";
    default: return "?";
    }
}

// ---------------------------------------------------------------- names

const char* frameTypeName(int t) { return zigbee::frameTypeName(t); }

const char* uttName(int t) {
    switch (t) {
    case PA: return "PAN Advert";
    case PAS: return "PAN Advert Solicit";
    case PC: return "PAN Config";
    case PCS: return "PAN Config Solicit";
    case DataFrame: return "Data";
    case AckFrame: return "Ack";
    case Eapol: return "EAPOL";
    case LPA: return "LFN PAN Advert";
    case LPAS: return "LFN PAN Advert Solicit";
    case LPC: return "LFN PAN Config";
    case LPCS: return "LFN PAN Config Solicit";
    case ExtData: return "Extended Data";
    default: return "?";
    }
}

const char* uttShort(int t) {
    static const char* n[kUttTypes] = {"PA", "PAS", "PC", "PCS", "Data", "Ack", "EAPOL", "LPA", "LPAS", "LPC", "LPCS", "ExtData"};
    return t >= 0 && t < kUttTypes ? n[t] : "?";
}

const char* chFunctionName(int f) {
    switch (f) {
    case 0: return "fixed";
    case 1: return "TR51CF";
    case 2: return "DH1CF";
    case 3: return "vendor";
    default: return "?";
    }
}

std::string scheduleText(const Schedule& s) {
    if (!s.present) return "";
    char b[200];
    std::string plan;
    if (s.chPlan == 0) { snprintf(b, sizeof b, "%s class %d", regDomainName(s.regDomain), s.opClass); plan = b; }
    else if (s.chPlan == 1) {
        static const int kSp[4] = {200, 400, 600, 100};
        snprintf(b, sizeof b, "%.3f MHz + %d x %d kHz", s.ch0Mhz, s.numChannels, s.spacingCode >= 0 && s.spacingCode < 4 ? kSp[s.spacingCode] : 0);
        plan = b;
    } else if (s.chPlan == 2) { snprintf(b, sizeof b, "%s plan %d", regDomainName(s.regDomain), s.chPlanId); plan = b; }
    std::string fn = chFunctionName(s.chFunction);
    if (s.chFunction == 0 && s.fixedChannel >= 0) { snprintf(b, sizeof b, "fixed ch %d", s.fixedChannel); fn = b; }
    if (s.bcastIntervalMs) snprintf(b, sizeof b, "%s, %s, every %u ms, dwell %d ms", plan.c_str(), fn.c_str(), s.bcastIntervalMs, s.dwellMs);
    else snprintf(b, sizeof b, "%s, %s, dwell %d ms", plan.c_str(), fn.c_str(), s.dwellMs);
    std::string out = b;
    if (s.excludedCount) { snprintf(b, sizeof b, ", %d excluded", s.excludedCount); out += b; }
    return out;
}

std::string euiText(uint64_t e) { return zigbee::addrText(e); }

std::string srcText(const Frame& f) {
    if (f.srcMode == 3) return euiText(f.srcExt);
    if (f.srcMode == 2) return zigbee::shortText(f.srcShort);
    return "-";
}
std::string dstText(const Frame& f) {
    if (f.destMode == 3) return euiText(f.destExt);
    if (f.destMode == 2) return f.destShort == 0xFFFF ? "broadcast" : zigbee::shortText(f.destShort);
    return f.type == Data || f.type == Ack ? "broadcast" : "-";
}

// ---------------------------------------------------------------- parsing

namespace {
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t le24(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16; }
uint32_t le32(const uint8_t* p) { return le24(p) | (uint32_t)p[3] << 24; }
uint64_t le64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }

int popcount8(uint8_t v) { int n = 0; while (v) { v &= (uint8_t)(v - 1); n++; } return n; }

// The channel information shared by the US-IE and the BS-IE, from p (n octets left)
void readChannels(const uint8_t* p, size_t n, Schedule& s) {
    if (n < 1) return;
    const uint8_t c = p[0];
    s.chPlan = c & 7; s.chFunction = (c >> 3) & 7; s.excluded = (c >> 6) & 3;
    size_t i = 1;
    if (s.chPlan == 0) { if (i + 2 > n) return; s.regDomain = p[i]; s.opClass = p[i + 1]; i += 2; }
    else if (s.chPlan == 1) {
        if (i + 6 > n) return;
        s.ch0Mhz = le24(p + i) / 1000.0; s.spacingCode = p[i + 3] & 0x0F; s.numChannels = le16(p + i + 4); i += 6;
    } else if (s.chPlan == 2) { if (i + 2 > n) return; s.regDomain = p[i]; s.chPlanId = p[i + 1]; i += 2; }
    if (s.chFunction == 0) { if (i + 2 > n) return; s.fixedChannel = le16(p + i); i += 2; }
    if (s.excluded == 1 && i < n) {
        const int ranges = p[i++];
        for (int r = 0; r < ranges && i + 4 <= n; r++, i += 4) s.excludedCount += le16(p + i + 2) - le16(p + i) + 1;
    } else if (s.excluded == 2) {
        for (; i < n; i++) s.excludedCount += popcount8(p[i]);
    }
}

void readUs(const uint8_t* p, size_t n, Schedule& s) {
    if (n < 4) return;
    s.present = true;
    s.dwellMs = p[0]; s.clockDrift = p[1]; s.timingAccuracy = p[2];
    readChannels(p + 3, n - 3, s);
}

void readBs(const uint8_t* p, size_t n, Schedule& s) {
    if (n < 10) return;
    s.present = true;
    s.bcastIntervalMs = le32(p); s.bsi = le16(p + 4); s.dwellMs = p[6]; s.clockDrift = p[7]; s.timingAccuracy = p[8];
    readChannels(p + 9, n - 9, s);
}

void addIe(std::string& s, const char* name) { if (!s.empty()) s += ' '; s += name; }

// The Wi-SUN header IE (WH-IE, element ID 0x2A): its sub-ID first
void readWh(const uint8_t* p, size_t n, Frame& f) {
    if (n < 1) return;
    const int sub = p[0];
    p++; n--;
    char b[16];
    switch (sub) {
    case 0x01: if (n >= 4) { f.utt = p[0] & 0x0F; f.ufsi = le24(p + 1); } addIe(f.ies, "UTT"); break;
    case 0x02: if (n >= 5) { f.hasBt = true; f.bsn = le16(p); f.bio = le24(p + 2); } addIe(f.ies, "BT"); break;
    case 0x03: if (n >= 2) { f.hasFc = true; f.txFlow = p[0]; f.rxFlow = p[1]; } addIe(f.ies, "FC"); break;
    case 0x04: if (n >= 1) f.rslDbm = p[0] - 174; addIe(f.ies, "RSL"); break;
    case 0x05: addIe(f.ies, "MHDS"); break;
    case 0x06: addIe(f.ies, "VH"); break;
    case 0x09: if (n >= 8) f.eaEui = le64(p); addIe(f.ies, "EA"); break;
    case 0x0A: addIe(f.ies, "LUTT"); break;
    case 0x0B: addIe(f.ies, "LBT"); break;
    case 0x0C: addIe(f.ies, "NR"); break;
    case 0x0D: addIe(f.ies, "LUS"); break;
    case 0x0E: addIe(f.ies, "FLUS"); break;
    case 0x0F: addIe(f.ies, "LBS"); break;
    case 0x10: addIe(f.ies, "LND"); break;
    case 0x11: addIe(f.ies, "LTO"); break;
    case 0x12: addIe(f.ies, "PANID"); break;
    case 0x80: if (n >= 4) { f.hasLbc = true; f.lfnBcastMs = le24(p); } addIe(f.ies, "LBC"); break;
    default: snprintf(b, sizeof b, "WH%02X", sub); addIe(f.ies, b); break;
    }
}

// The Wi-SUN payload IE (WP-IE, group 0x4): nested sub-IEs
void readWp(const uint8_t* p, size_t n, Frame& f) {
    size_t i = 0;
    char b[16];
    while (i + 2 <= n) {
        const uint16_t d = le16(p + i);
        i += 2;
        const bool lng = d >> 15;
        const size_t len = lng ? (d & 0x7FF) : (d & 0xFF);
        const int sub = lng ? (d >> 11) & 0x0F : (d >> 8) & 0x7F;
        if (i + len > n) break;
        const uint8_t* q = p + i;
        if (lng) {
            switch (sub) {
            case 1: readUs(q, len, f.us); addIe(f.ies, "US"); break;
            case 2: readBs(q, len, f.bs); addIe(f.ies, "BS"); break;
            case 3: addIe(f.ies, "VP"); break;
            case 4: addIe(f.ies, "LCP"); break;
            default: snprintf(b, sizeof b, "WPL%X", sub); addIe(f.ies, b); break;
            }
        } else {
            switch (sub) {
            case 0x04:
                if (len >= 5) {
                    f.hasPanIe = true; f.panSize = le16(q); f.routingCost = le16(q + 2);
                    f.useParentBs = q[4] & 1; f.rplRouting = (q[4] >> 1) & 1; f.tpsVersion = (q[4] >> 5) & 7;
                }
                addIe(f.ies, "PAN");
                break;
            case 0x05: f.hasNetName = true; f.netName.assign((const char*)q, len > 32 ? 32 : len); addIe(f.ies, "NETNAME"); break;
            case 0x06: if (len >= 2) f.panVersion = le16(q); addIe(f.ies, "PANVER"); break;
            case 0x07: f.hasGtkHash = true; addIe(f.ies, "GTKHASH"); break;
            case 0x08:
                if (len >= 1) { const int c = q[0] & 0x0F; for (int k = 0; k < c && 1 + (size_t)k < len; k++) f.phyModes.push_back(q[1 + k]); }
                addIe(f.ies, "POM");
                break;
            case 0x40: if (len >= 2) f.lfnVersion = le16(q); addIe(f.ies, "LFNVER"); break;
            case 0x41: addIe(f.ies, "LGTKHASH"); break;
            default: snprintf(b, sizeof b, "WP%02X", sub); addIe(f.ies, b); break;
            }
        }
        i += len;
    }
}

// The MPX IE (group 0x3): transaction control, multiplex ID or KMP ID; the upper layer frame follows in the IE
void readMpx(const uint8_t* p, size_t n, Frame& f) {
    addIe(f.ies, "MPX");
    if (n < 1) return;
    const int transfer = p[0] & 7;
    size_t i = 1;
    if (transfer == 0 || transfer == 1) {   // full frame (with multiplex ID), first fragment
        if (i + 2 > n) return;
        f.mpxId = le16(p + i); i += 2;
        if (f.mpxId == 0x0001 && i < n) f.kmpId = p[i++];   // a KMP: its ID
    }
    if (f.kmpId == 1 && i + 4 <= n) {   // IEEE 802.1X: an EAPOL frame
        f.eapolType = p[i + 1];
        if (f.eapolType == 0 && i + 4 + 4 <= n) {
            f.eapCode = p[i + 4];
            if ((f.eapCode == 1 || f.eapCode == 2) && i + 9 <= n) f.eapType = p[i + 8];
        }
    }
}

const char* eapCodeName(int c) {
    switch (c) {
    case 1: return "request";
    case 2: return "response";
    case 3: return "success";
    case 4: return "failure";
    default: return "?";
    }
}
const char* eapTypeName(int t) {
    switch (t) {
    case 1: return "identity";
    case 13: return "TLS";
    default: return "";
    }
}
}

Frame parse(const uint8_t* d, size_t n) {
    Frame f;
    if (n < 2) return f;
    const uint16_t fc = le16(d);
    f.type = fc & 7;
    f.secured = (fc >> 3) & 1;
    f.pending = (fc >> 4) & 1;
    f.ackReq = (fc >> 5) & 1;
    f.panCompress = (fc >> 6) & 1;
    f.seqSuppressed = (fc >> 8) & 1;
    f.ieList = (fc >> 9) & 1;
    f.destMode = (fc >> 10) & 3;
    f.version = (fc >> 12) & 3;
    f.srcMode = (fc >> 14) & 3;
    size_t i = 2;
    if (!(f.version == 2 && f.seqSuppressed)) { if (i >= n) return f; f.seq = d[i++]; }
    // which PAN IDs are present (802.15.4-2015 table 7-2 for frame version 2, the 2006 rules before it)
    const bool dA = f.destMode != 0, sA = f.srcMode != 0;
    if (f.version == 2) {
        if (!dA && !sA) f.hasDestPan = f.panCompress;
        else if (dA && !sA) f.hasDestPan = !f.panCompress;
        else if (!dA && sA) f.hasSrcPan = !f.panCompress;
        else if (f.destMode == 3 && f.srcMode == 3) f.hasDestPan = !f.panCompress;
        else { f.hasDestPan = true; f.hasSrcPan = !f.panCompress; }
    } else {
        f.hasDestPan = dA;
        f.hasSrcPan = sA && !(f.panCompress && dA);
    }
    auto need = [&](size_t k) { return i + k <= n; };
    if (f.hasDestPan) { if (!need(2)) return f; f.destPan = le16(d + i); i += 2; }
    if (f.destMode == 2) { if (!need(2)) return f; f.destShort = le16(d + i); i += 2; }
    else if (f.destMode == 3) { if (!need(8)) return f; f.destExt = le64(d + i); i += 8; }
    if (f.hasSrcPan) { if (!need(2)) return f; f.srcPan = le16(d + i); i += 2; }
    if (f.srcMode == 2) { if (!need(2)) return f; f.srcShort = le16(d + i); i += 2; }
    else if (f.srcMode == 3) { if (!need(8)) return f; f.srcExt = le64(d + i); i += 8; }
    if (f.secured) {
        if (!need(5)) return f;
        const uint8_t sc = d[i];
        f.secLevel = sc & 7; f.keyIdMode = (sc >> 3) & 3;
        const bool noCounter = (sc >> 5) & 1;
        i++;
        if (!noCounter) { f.frameCounter = le32(d + i); i += 4; }
        static const int kKeyLen[4] = {0, 1, 5, 9};
        if (!need((size_t)kKeyLen[f.keyIdMode])) return f;
        if (f.keyIdMode) f.keyIndex = d[i + kKeyLen[f.keyIdMode] - 1];
        i += (size_t)kKeyLen[f.keyIdMode];
    }
    f.ok = true;
    // header IEs (always in the clear), then payload IEs and payload (encrypted when secured)
    bool payloadIes = false;
    if (f.ieList) {
        while (i + 2 <= n) {
            const uint16_t ie = le16(d + i);
            const size_t len = ie & 0x7F;
            const int id = (ie >> 7) & 0xFF;
            i += 2;
            if (i + len > n) { f.ok = false; break; }
            if (id == 0x2A) readWh(d + i, len, f);
            else if (id == 0x7E) { addIe(f.ies, "HT1"); i += len; payloadIes = true; break; }
            else if (id == 0x7F) { addIe(f.ies, "HT2"); i += len; break; }
            else { char b[8]; snprintf(b, sizeof b, "H%02X", id); addIe(f.ies, b); }
            i += len;
        }
    }
    if (f.secured) {
        f.encrypted = true;
        f.payloadAt = i; f.payloadLen = n - i;
    } else {
        if (payloadIes) {
            while (i + 2 <= n) {
                const uint16_t ie = le16(d + i);
                const size_t len = ie & 0x7FF;
                const int group = (ie >> 11) & 0x0F;
                i += 2;
                if (i + len > n) break;
                if (group == 0x4) readWp(d + i, len, f);
                else if (group == 0x3) readMpx(d + i, len, f);
                else if (group == 0xF) { addIe(f.ies, "PT"); i += len; break; }
                else { char b[8]; snprintf(b, sizeof b, "P%X", group); addIe(f.ies, b); }
                i += len;
            }
        }
        f.payloadAt = i; f.payloadLen = n - i;
    }
    // the one-line summary
    char b[200];
    std::string s = f.utt >= 0 ? uttName(f.utt) : frameTypeName(f.type);
    if (f.hasNetName) { s += " \""; s += f.netName; s += '"'; }
    if (f.hasPanIe) { snprintf(b, sizeof b, ", PAN size %d, routing cost %d", f.panSize, f.routingCost); s += b; }
    if (f.panVersion >= 0) { snprintf(b, sizeof b, ", PAN version %d", f.panVersion); s += b; }
    if (f.eapolType >= 0) {
        if (f.eapolType == 0 && f.eapCode >= 0) {
            snprintf(b, sizeof b, ", EAP %s%s%s", eapCodeName(f.eapCode), f.eapType >= 0 && *eapTypeName(f.eapType) ? " " : "", f.eapType >= 0 ? eapTypeName(f.eapType) : "");
            s += b;
        } else if (f.eapolType == 3) s += ", EAPOL-Key";
    }
    if (f.eaEui) { s += ", authenticator "; s += euiText(f.eaEui); }
    if (f.rslDbm > -999) { snprintf(b, sizeof b, ", RSL %d dBm", f.rslDbm); s += b; }
    if (f.encrypted) { snprintf(b, sizeof b, ", encrypted (key %d)", f.keyIndex); s += b; }
    f.info = s;
    return f;
}

// ---------------------------------------------------------------- building

namespace {
void put16(std::vector<uint8_t>& v, unsigned x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
void put24(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 3; i++) v.push_back((uint8_t)(x >> (8 * i))); }
void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
void put64(std::vector<uint8_t>& v, uint64_t x) { for (int i = 0; i < 8; i++) v.push_back((uint8_t)(x >> (8 * i))); }

void headerIe(std::vector<uint8_t>& v, int id, const std::vector<uint8_t>& body) {
    put16(v, (unsigned)(body.size() & 0x7F) | (unsigned)id << 7);
    v.insert(v.end(), body.begin(), body.end());
}
void wh(std::vector<uint8_t>& v, int sub, std::vector<uint8_t> body) {
    body.insert(body.begin(), (uint8_t)sub);
    headerIe(v, 0x2A, body);
}
void channels(std::vector<uint8_t>& v, const Schedule& s) {
    v.push_back((uint8_t)((s.chPlan & 7) | (s.chFunction & 7) << 3));
    if (s.chPlan == 0) { v.push_back((uint8_t)s.regDomain); v.push_back((uint8_t)s.opClass); }
    else if (s.chPlan == 1) { put24(v, (uint32_t)(s.ch0Mhz * 1000 + 0.5)); v.push_back((uint8_t)s.spacingCode); put16(v, (unsigned)s.numChannels); }
    else { v.push_back((uint8_t)s.regDomain); v.push_back((uint8_t)s.chPlanId); }
    if (s.chFunction == 0) put16(v, (unsigned)s.fixedChannel);
}
void subShort(std::vector<uint8_t>& v, int sub, const std::vector<uint8_t>& body) {
    put16(v, (unsigned)(body.size() & 0xFF) | (unsigned)sub << 8);
    v.insert(v.end(), body.begin(), body.end());
}
void subLong(std::vector<uint8_t>& v, int sub, const std::vector<uint8_t>& body) {
    put16(v, (unsigned)(body.size() & 0x7FF) | (unsigned)sub << 11 | 0x8000u);
    v.insert(v.end(), body.begin(), body.end());
}
void payloadIe(std::vector<uint8_t>& v, int group, const std::vector<uint8_t>& body) {
    put16(v, (unsigned)(body.size() & 0x7FF) | (unsigned)group << 11 | 0x8000u);
    v.insert(v.end(), body.begin(), body.end());
}
}

std::vector<uint8_t> buildMpdu(const Build& b) {
    std::vector<uint8_t> v;
    const int dm = b.dst ? 3 : 0, sm = b.src ? 3 : 0;
    const bool seqSup = b.seq < 0;
    // PAN IDs: with a destination (both extended) none is sent (PAN ID compression); a frame without one carries the source PAN when asked
    const bool panComp = dm != 0 ? true : !b.srcPanPresent;
    const unsigned fc = Data | (b.secured ? 1u << 3 : 0u) | (b.ackReq ? 1u << 5 : 0u) | (panComp ? 1u << 6 : 0u) | (seqSup ? 1u << 8 : 0u) | 1u << 9 |
                        (unsigned)dm << 10 | 2u << 12 | (unsigned)sm << 14;
    put16(v, fc);
    if (!seqSup) v.push_back((uint8_t)b.seq);
    if (dm == 0 && sm == 3 && b.srcPanPresent) put16(v, b.pan);
    if (dm == 3) put64(v, b.dst);
    if (sm == 3) put64(v, b.src);
    if (b.secured) {
        v.push_back(0x0E);   // ENC-MIC-64, key identifier mode 1
        put32(v, b.frameCounter);
        v.push_back((uint8_t)b.keyIndex);
    }
    {
        std::vector<uint8_t> u = {(uint8_t)(b.utt & 0x0F)};
        put24(u, b.ufsi);
        wh(v, 0x01, u);
    }
    if (b.bt) { std::vector<uint8_t> u; put16(u, (unsigned)b.bsn); put24(u, b.bio); wh(v, 0x02, u); }
    if (b.rslDbm > -999) wh(v, 0x04, {(uint8_t)(b.rslDbm + 174)});
    if (b.ea) { std::vector<uint8_t> u; put64(u, b.ea); wh(v, 0x09, u); }
    if (b.lbc) { std::vector<uint8_t> u; put24(u, b.lfnBcastMs); u.push_back(5); wh(v, 0x80, u); }
    std::vector<uint8_t> pies;
    if (!b.secured) {
        std::vector<uint8_t> wp;
        if (b.us.present) {
            std::vector<uint8_t> u = {(uint8_t)b.us.dwellMs, (uint8_t)b.us.clockDrift, (uint8_t)b.us.timingAccuracy};
            channels(u, b.us);
            subLong(wp, 1, u);
        }
        if (b.bs.present) {
            std::vector<uint8_t> u;
            put32(u, b.bs.bcastIntervalMs); put16(u, (unsigned)b.bs.bsi);
            u.push_back((uint8_t)b.bs.dwellMs); u.push_back((uint8_t)b.bs.clockDrift); u.push_back((uint8_t)b.bs.timingAccuracy);
            channels(u, b.bs);
            subLong(wp, 2, u);
        }
        if (b.panIe) {
            std::vector<uint8_t> u;
            put16(u, (unsigned)b.panSize); put16(u, (unsigned)b.routingCost);
            u.push_back((uint8_t)((b.useParentBs ? 1 : 0) | 2 | (b.tpsVersion & 7) << 5));
            subShort(wp, 0x04, u);
        }
        if (!b.netName.empty()) subShort(wp, 0x05, std::vector<uint8_t>(b.netName.begin(), b.netName.end()));
        if (b.panVersion >= 0) { std::vector<uint8_t> u; put16(u, (unsigned)b.panVersion); subShort(wp, 0x06, u); }
        if (b.gtkHash) subShort(wp, 0x07, std::vector<uint8_t>(32, 0x5A));
        if (!b.phyModes.empty()) {
            std::vector<uint8_t> u = {(uint8_t)b.phyModes.size()};
            for (int m : b.phyModes) u.push_back((uint8_t)m);
            subShort(wp, 0x08, u);
        }
        if (!wp.empty()) payloadIe(pies, 0x4, wp);
        if (b.mpxId >= 0) {
            std::vector<uint8_t> u = {0x00};   // full frame
            put16(u, (unsigned)b.mpxId);
            if (b.kmpId >= 0) u.push_back((uint8_t)b.kmpId);
            u.insert(u.end(), b.payload.begin(), b.payload.end());
            payloadIe(pies, 0x3, u);
        }
    }
    if (!pies.empty() || b.secured) {
        headerIe(v, 0x7E, {});
        v.insert(v.end(), pies.begin(), pies.end());
    } else {
        headerIe(v, 0x7F, {});
    }
    if (b.secured || b.mpxId < 0) v.insert(v.end(), b.payload.begin(), b.payload.end());
    return v;
}

} // namespace wisun
} // namespace dect2
