// The C++ implementation of ts_proto.h (the reference of the differential test against rust/onair_ts, and what DECT2_RUST=OFF builds use).
// ISO/IEC 13818-1 (packets, PAT, PMT, CRC), ETSI EN 300 468 (SDT, NIT, EIT, TDT, descriptors), ATSC A/65 (TVCT).
#include "dect2/tv/ts_proto.h"
#ifndef DECT2_RUST_TS
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace dect2::tsp {

namespace {
inline int u16(const uint8_t* s, int i) { return (s[i] << 8) | s[i + 1]; }
inline int bcd(int v) { return (v >> 4) * 10 + (v & 15); }
inline std::string lang3(const uint8_t* p) { return std::string((const char*)p, 3); }
} // namespace

// CRC-32/MPEG-2: polynomial 0x04C11DB7, start 0xFFFFFFFF, no reflection, no final XOR.
uint32_t crc32(const uint8_t* p, int n) {
    static uint32_t tab[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i << 24;
            for (int k = 0; k < 8; k++) c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
            tab[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) crc = (crc << 8) ^ tab[(crc >> 24) ^ p[i]];
    return crc;
}

bool packetHeader(const uint8_t* p, int n, PacketHeader& h) {
    if (n < 4 || !p || p[0] != 0x47) return false;
    h.pid = ((p[1] & 0x1F) << 8) | p[2];
    h.tei = p[1] & 0x80; h.pusi = p[1] & 0x40;
    h.scr = (p[3] >> 6) & 3; h.afc = (p[3] >> 4) & 3; h.cc = p[3] & 15;
    h.payloadOff = -1;
    if (h.afc & 1) {
        int off = 4;
        if (h.afc == 3) off += 1 + (n > 4 ? p[4] : 0);
        h.payloadOff = off >= 188 ? -1 : off;
    }
    return true;
}

void patPacket(int tsid, int sid, int pmtPid, int cc, uint8_t* out) {
    uint8_t sec[16];
    sec[0] = 0x00; sec[1] = 0xB0; sec[2] = 13;
    sec[3] = tsid >> 8; sec[4] = tsid & 0xFF;
    sec[5] = 0xC1; sec[6] = 0; sec[7] = 0;
    sec[8] = sid >> 8; sec[9] = sid & 0xFF;
    sec[10] = 0xE0 | (pmtPid >> 8); sec[11] = pmtPid & 0xFF;
    uint32_t crc = crc32(sec, 12);
    sec[12] = crc >> 24; sec[13] = crc >> 16; sec[14] = crc >> 8; sec[15] = crc;
    memset(out, 0xFF, 188);
    out[0] = 0x47; out[1] = 0x40; out[2] = 0x00; out[3] = 0x10 | (cc & 15);
    out[4] = 0x00;
    memcpy(out + 5, sec, 16);
}

bool parsePat(const uint8_t* s, int len, int& tsid, std::vector<PatEntry>& out) {
    out.clear();
    if (len < 8) return false;
    tsid = u16(s, 3);
    for (int i = 8; i + 4 <= len - 4; i += 4) {
        PatEntry e;
        e.prog = u16(s, i);
        e.pid = ((s[i + 2] & 0x1F) << 8) | s[i + 3];
        if (e.prog == 0) continue;   // network PID
        out.push_back(e);
    }
    return true;
}

// Codec name and kind of a stream_type (ISO/IEC 13818-1 table 2-34, plus the Chinese AVS family of GB/T 17975.1).
static const char* streamKind(int st, std::string& codec) {
    bool isVideo = false, isAudio = false;
    switch (st) {
    case 0x01: codec = "MPEG-1 video"; isVideo = true; break;
    case 0x02: codec = "MPEG-2"; isVideo = true; break;
    case 0x03: codec = "MPEG-1 audio"; isAudio = true; break;
    case 0x04: codec = "MPEG audio"; isAudio = true; break;
    case 0x0F: codec = "AAC"; isAudio = true; break;
    case 0x11: codec = "HE-AAC"; isAudio = true; break;
    case 0x1B: codec = "H.264"; isVideo = true; break;
    case 0x24: codec = "HEVC"; isVideo = true; break;
    case 0x81: codec = "AC-3"; isAudio = true; break;
    case 0x87: codec = "E-AC-3"; isAudio = true; break;
    case 0x42: codec = "AVS"; isVideo = true; break;
    case 0xD2: codec = "AVS2"; isVideo = true; break;
    case 0xD4: codec = "AVS3"; isVideo = true; break;
    default: break;
    }
    return isVideo ? "video" : isAudio ? "audio" : "data";
}

bool parsePmt(const uint8_t* s, int len, Pmt& out) {
    out = Pmt();
    if (len < 12) return false;
    out.prog = u16(s, 3);
    out.pcrPid = ((s[8] & 0x1F) << 8) | s[9];
    const int pil = ((s[10] & 0x0F) << 8) | s[11];
    bool ca = false;
    for (int i = 12; i + 2 <= 12 + pil && i + 2 <= len; ) { int tag = s[i], l = s[i + 1]; if (tag == 0x09) ca = true; i += 2 + l; }
    int i = 12 + pil;
    while (i + 5 <= len - 4) {
        PmtStream st;
        st.streamType = s[i];
        st.pid = ((s[i + 1] & 0x1F) << 8) | s[i + 2];
        const int esl = ((s[i + 3] & 0x0F) << 8) | s[i + 4];
        st.kind = streamKind(st.streamType, st.codec);
        for (int j = i + 5; j + 2 <= i + 5 + esl && j + 2 <= len; ) {
            const int tag = s[j], l = s[j + 1];
            if (j + 2 + l > len) break;
            const uint8_t* d = s + j + 2;
            switch (tag) {
            case 0x09: ca = true; break;
            case 0x05: if (l >= 4 && memcmp(d, "DRA1", 4) == 0) { st.codec = "DRA"; st.kind = "audio"; } break;   // registration: DRA sound (GB/T 22726)
            case 0x0A: if (l >= 3) st.lang = lang3(d); break;
            case 0x6A: st.codec = "AC-3"; st.kind = "audio"; break;
            case 0x7A: st.codec = "E-AC-3"; st.kind = "audio"; break;
            case 0x7B: st.codec = "DTS"; st.kind = "audio"; break;
            case 0x7C: st.codec = "AAC"; st.kind = "audio"; break;
            case 0x59:   // subtitling descriptor (EN 300 468 section 6.2.41)
                st.codec = "DVB subtitles"; st.kind = "subtitles"; if (l >= 3) st.lang = lang3(d);
                for (int k = 0; k + 8 <= l; k += 8) {
                    PmtSub e; e.lang = lang3(d + k); e.type = d[k + 3];
                    e.page = (d[k + 4] << 8) | d[k + 5]; e.ancPage = (d[k + 6] << 8) | d[k + 7];
                    st.subs.push_back(e);
                }
                break;
            case 0x56:   // teletext descriptor (EN 300 468 section 6.2.43)
                st.codec = "Teletext"; st.kind = "teletext"; if (l >= 3) st.lang = lang3(d);
                for (int k = 0; k + 5 <= l; k += 5) {
                    PmtSub e; e.teletext = true; e.lang = lang3(d + k); e.type = d[k + 3] >> 3;
                    const int mag = (d[k + 3] & 7) ? (d[k + 3] & 7) : 8, b = d[k + 4];
                    if ((b >> 4) > 9 || (b & 15) > 9) continue;
                    e.page = mag * 100 + (b >> 4) * 10 + (b & 15);
                    st.subs.push_back(e);
                }
                break;
            case 0x46: st.codec = "VBI teletext"; st.kind = "teletext"; break;
            default: break;
            }
            j += 2 + l;
        }
        if (st.codec.empty()) { char b[24]; snprintf(b, sizeof b, "type 0x%02X", st.streamType); st.codec = b; }
        out.streams.push_back(st);
        i += 5 + esl;
    }
    out.ca = ca;
    return true;
}

bool parseSdt(const uint8_t* s, int len, int& tsid, int& onid, std::vector<SdtService>& out) {
    out.clear();
    if (len < 11) return false;
    tsid = u16(s, 3);
    onid = u16(s, 8);
    int i = 11;
    while (i + 5 <= len - 4) {
        SdtService sv;
        sv.sid = u16(s, i);
        sv.freeCa = s[i + 3] & 0x10;
        const int dl = ((s[i + 3] & 0x0F) << 8) | s[i + 4];
        for (int j = i + 5; j + 2 <= i + 5 + dl && j + 2 <= len; ) {
            const int tag = s[j], l = s[j + 1];
            if (j + 2 + l > len) break;
            if (tag == 0x48 && l >= 3) {   // service descriptor
                const uint8_t* d = s + j + 2;
                sv.hasType = true; sv.type = d[0];
                const int pl = d[1];
                if (2 + pl + 1 <= l) {
                    sv.hasProvider = true; sv.provider = {(uint32_t)(j + 4), (uint32_t)pl};
                    const int nl = d[2 + pl];
                    if (3 + pl + nl <= l) { sv.hasName = true; sv.name = {(uint32_t)(j + 5 + pl), (uint32_t)nl}; }
                }
            }
            j += 2 + l;
        }
        out.push_back(sv);
        i += 5 + dl;
    }
    return true;
}

bool parseNit(const uint8_t* s, int len, Nit& out) {
    out = Nit();
    if (len < 10) return false;
    const int ndl = ((s[8] & 0x0F) << 8) | s[9];
    for (int j = 10; j + 2 <= 10 + ndl && j + 2 <= len; ) {
        const int tag = s[j], l = s[j + 1];
        if (tag == 0x40) { out.hasName = true; out.name = {(uint32_t)(j + 2), (uint32_t)std::min(l, len - j - 2)}; }   // network name descriptor (cut at the section end)
        j += 2 + l;
    }
    // transport stream loop: logical channel numbers (descriptor 0x83 in private data, e.g. EACEM / UK)
    int i = 10 + ndl;
    if (i + 2 > len) return true;
    const int tsl = ((s[i] & 0x0F) << 8) | s[i + 1];
    i += 2;
    const int end = std::min(len - 4, i + tsl);
    while (i + 6 <= end) {
        const int dl = ((s[i + 4] & 0x0F) << 8) | s[i + 5];
        for (int j = i + 6; j + 2 <= i + 6 + dl && j + 2 <= len; ) {
            const int tag = s[j], l = s[j + 1], lc = std::min(l, len - j - 2);
            if (tag == 0x83) for (int k = 0; k + 4 <= lc; k += 4) {
                NitLcn e;
                e.sid = u16(s, j + 2 + k);
                e.lcn = ((s[j + 4 + k] & 0x03) << 8) | s[j + 5 + k];
                out.lcn.push_back(e);
            }
            j += 2 + l;
        }
        i += 6 + dl;
    }
    return true;
}

// ATSC A/65 virtual channel table: for each channel a 7 character name, major.minor number, the program number it maps to and the service type.
bool parseTvct(const uint8_t* s, int len, int& tsid, std::vector<Tvct>& out) {
    out.clear();
    if (len < 14) return false;
    tsid = u16(s, 3);
    const int n = s[9];
    int i = 10;
    for (int c = 0; c < n && i + 32 <= len - 4; c++) {
        Tvct v;
        for (int k = 0; k < 7; k++) {
            const int hi = s[i + 2 * k], lo = s[i + 2 * k + 1];
            if (hi == 0 && lo == 0) break;
            v.name += hi == 0 && lo >= 0x20 && lo < 0x7F ? (char)lo : '?';
        }
        v.major = ((s[i + 14] & 0x0F) << 6) | (s[i + 15] >> 2);
        v.minor = ((s[i + 15] & 0x03) << 8) | s[i + 16];
        v.prog = u16(s, i + 24);
        v.stype = s[i + 27] & 0x3F;   // 2 digital TV, 3 audio, 4 data
        const int dl = ((s[i + 30] & 0x03) << 8) | s[i + 31];
        if (v.prog != 0 && v.prog != 0xFFFF) out.push_back(v);
        i += 32 + dl;
    }
    return true;
}

// TDT / TOT: the MJD to calendar conversion of EN 300 468 Annex C.
bool parseTdt(const uint8_t* s, int len, Tdt& out) {
    if (len < 8) return false;
    const int mjd = u16(s, 3);
    const int hh = bcd(s[5]), mm = bcd(s[6]), ss = bcd(s[7]);
    int y = (int)((mjd - 15078.2) / 365.25);
    int m = (int)((mjd - 14956.1 - (int)(y * 365.25)) / 30.6001);
    const int d = mjd - 14956 - (int)(y * 365.25) - (int)(m * 30.6001);
    const int k = (m == 14 || m == 15) ? 1 : 0;
    y += k; m = m - 1 - k * 12;
    char b[40]; snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d UTC", 1900 + y, m, d, hh, mm, ss);
    out.text = b;
    out.unixSec = (int64_t)(mjd - 40587) * 86400 + hh * 3600 + mm * 60 + ss;
    return true;
}

bool parseEit(const uint8_t* s, int len, Eit& out) {
    out = Eit();
    if (len < 18) return false;
    const int tid = s[0];
    out.sid = u16(s, 3);
    out.secNum = s[6];
    // present / following event name for the service list
    if (tid == 0x4E && out.secNum <= 1) {
        out.legacy = true;
        const int i = 14;
        if (i + 12 <= len - 4) {
            const int dl = ((s[i + 10] & 0x0F) << 8) | s[i + 11];
            for (int j = i + 12; j + 2 <= i + 12 + dl && j + 2 <= len; ) {
                const int tag = s[j], l = s[j + 1];
                if (j + 2 + l > len) break;
                if (tag == 0x4D && l >= 5) {
                    const uint8_t* d = s + j + 2;
                    const int nl = d[3];
                    if (4 + nl <= l) { out.hasName = true; out.nowNextName = {(uint32_t)(j + 6), (uint32_t)nl}; }
                }
                j += 2 + l;
            }
        }
    }
    // the guide: every event in the section
    int i = 14;
    const int end = len - 4;
    while (i + 12 <= end) {
        EitEvent ev;
        ev.eventId = u16(s, i);
        const int mjd = u16(s, i + 2);
        const bool undefined = s[i + 2] == 0xFF && s[i + 3] == 0xFF && s[i + 4] == 0xFF;
        ev.start = undefined ? 0 : (int64_t)(mjd - 40587) * 86400 + bcd(s[i + 4]) * 3600 + bcd(s[i + 5]) * 60 + bcd(s[i + 6]);
        ev.duration = bcd(s[i + 7]) * 3600 + bcd(s[i + 8]) * 60 + bcd(s[i + 9]);
        ev.running = s[i + 10] >> 5;
        const int dl = ((s[i + 10] & 0x0F) << 8) | s[i + 11];
        const int dend = std::min(end, i + 12 + dl);
        for (int j = i + 12; j + 2 <= dend; ) {
            const int tag = s[j], l = s[j + 1];
            if (j + 2 + l > dend) break;
            const uint8_t* d = s + j + 2;
            const int dpos = j + 2;
            if (tag == 0x4D && l >= 5) {              // short event descriptor
                ev.lang = lang3(d);
                const int nl = d[3];
                if (4 + nl < l) {
                    ev.hasTitle = true; ev.title = {(uint32_t)(dpos + 4), (uint32_t)nl};
                    const int tl = d[4 + nl];
                    if (5 + nl + tl <= l) { ev.hasText = true; ev.text = {(uint32_t)(dpos + 5 + nl), (uint32_t)tl}; }
                }
            } else if (tag == 0x4E && l >= 6) {       // extended event descriptor (several pieces, in order)
                const int num = d[0] >> 4;
                const int itemsLen = d[4];
                if (5 + itemsLen < l) {
                    const int tl = d[5 + itemsLen];
                    if (6 + itemsLen + tl <= l) { ev.ext[num] = {(uint32_t)(dpos + 6 + itemsLen), (uint32_t)tl}; ev.extMask |= (uint16_t)(1u << num); }
                }
            } else if (tag == 0x54 && l >= 2) {       // content descriptor
                ev.genre = d[0] >> 4;
            }
            j += 2 + l;
        }
        i = dend;
        if (ev.start == 0 || ev.duration <= 0) continue;
        out.events.push_back(ev);
    }
    return true;
}

} // namespace dect2::tsp
#endif
