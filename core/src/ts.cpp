#include "dect2/ts.h"
#include <algorithm>
#include <cstring>
#include <iconv.h>

namespace dect2 {

uint32_t mpegCrc32(const uint8_t* p, int n) {
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

static std::string convert(const char* charset, const uint8_t* p, int n) {
    iconv_t cd = iconv_open("UTF-8", charset);
    if (cd == (iconv_t)-1) return std::string((const char*)p, n);
    std::string out(n * 4 + 8, '\0');
    char* in = (char*)p;
    size_t inLeft = n, outLeft = out.size();
    char* o = &out[0];
    iconv(cd, &in, &inLeft, &o, &outLeft);
    iconv_close(cd);
    out.resize(out.size() - outLeft);
    return out;
}

std::string dvbText(const uint8_t* p, int n) {
    if (n <= 0) return {};
    std::string s;
    auto clean = [](std::string t) {
        std::string r;
        for (size_t i = 0; i < t.size(); i++) {
            unsigned char c = t[i];
            if (c == 0xC2 && i + 1 < t.size() && (unsigned char)t[i + 1] >= 0x80 && (unsigned char)t[i + 1] < 0xA0) { i++; continue; } // C1 controls
            if (c == 0x0A || c == 0x0D) { r += ' '; continue; }
            if (c >= 0x20 || c >= 0x80) r += (char)c;
        }
        return r;
    };
    unsigned first = p[0];
    if (first >= 0x20) s = convert("ISO-8859-1", p, n);                       // default table (close to ISO 6937)
    else if (first >= 0x01 && first <= 0x0B && first != 0x0 && n > 1) {
        static const char* cs[] = {"", "ISO-8859-5", "ISO-8859-6", "ISO-8859-7", "ISO-8859-8", "ISO-8859-9", "ISO-8859-10", "ISO-8859-11", "", "ISO-8859-13", "ISO-8859-14", "ISO-8859-15"};
        s = convert(cs[first], p + 1, n - 1);
    } else if (first == 0x10 && n > 3) {
        char name[24]; snprintf(name, sizeof name, "ISO-8859-%d", (p[1] << 8) | p[2]);
        s = convert(name, p + 3, n - 3);
    } else if (first == 0x11 && n > 1) s = convert("UCS-2BE", p + 1, n - 1);
    else if (first == 0x15 && n > 1) s = std::string((const char*)p + 1, n - 1);
    else if (first == 0x14 && n > 1) s = convert("BIG5", p + 1, n - 1);
    else s = convert("ISO-8859-1", p + 1, n - 1);
    return clean(s);
}

const char* TsService::typeName() const {
    switch (type) {
    case 0x01: case 0x11: case 0x16: case 0x17: case 0x19: case 0x1A: case 0x1F: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: return "TV";
    case 0x02: case 0x0A: return "radio";
    case 0x0C: return "data";
    case 0x03: return "teletext";
    default: return type ? "other" : "?";
    }
}

void TsDemux::reset() { *this = TsDemux(); }

void TsDemux::markLoss() { for (auto& kv : pids_) kv.second.resync = true; for (auto& kv : sec_) kv.second.active = false; }

void TsDemux::feed(const uint8_t* p) {
    if (p[0] != 0x47) return;
    const int pid = ((p[1] & 0x1F) << 8) | p[2];
    const bool tei = p[1] & 0x80, pusi = p[1] & 0x40;
    const int scr = (p[3] >> 6) & 3, afc = (p[3] >> 4) & 3, cc = p[3] & 15;
    total_++; totalWin_++;
    PidState& st = pids_[pid];
    st.packets++; st.bytesWin += 188;
    if (pid == 0x1FFF) { nulls_++; nullWin_++; return; }
    if (tei) { tei_++; st.tei++; return; }
    st.scrambled = scr != 0;
    if (afc & 1) {
        if (!st.resync && st.lastCc >= 0 && cc != ((st.lastCc + 1) & 15) && !(cc == st.lastCc && afc == 2)) { st.ccErrors++; ccErr_++; }
        st.lastCc = cc;
        st.resync = false;
    } else if (afc == 2) st.resync = false;
    if (!(afc & 1) || scr) return;
    int off = 4;
    if (afc == 3) off += 1 + p[4];
    if (off >= 188) return;
    // only PSI/SI PIDs and PMT PIDs carry sections we care about
    const bool psi = pid == 0 || pid == 0x10 || pid == 0x11 || pid == 0x12 || pid == 0x14 || pmtToService_.count(pid);
    if (!psi) return;
    SectionBuf& sb = sec_[pid];
    const uint8_t* d = p + off;
    int n = 188 - off;
    if (pusi) {
        int ptr = d[0];
        if (sb.active && ptr > 0 && ptr < n) { sb.data.insert(sb.data.end(), d + 1, d + 1 + ptr); }
        // the previous section may be complete now
        auto tryEmit = [&]() {
            while (sb.data.size() >= 3) {
                int sl = ((sb.data[1] & 0x0F) << 8) | sb.data[2];
                if ((int)sb.data.size() < sl + 3) break;
                section(pid, sb.data.data(), sl + 3);
                sb.data.erase(sb.data.begin(), sb.data.begin() + sl + 3);
                if (!sb.data.empty() && sb.data[0] == 0xFF) { sb.data.clear(); break; }
            }
        };
        if (sb.active) tryEmit();
        sb.data.assign(d + 1 + ptr, d + n);
        sb.active = true;
        tryEmit();
    } else if (sb.active) {
        sb.data.insert(sb.data.end(), d, d + n);
        while (sb.data.size() >= 3) {
            int sl = ((sb.data[1] & 0x0F) << 8) | sb.data[2];
            if ((int)sb.data.size() < sl + 3) break;
            section(pid, sb.data.data(), sl + 3);
            sb.data.erase(sb.data.begin(), sb.data.begin() + sl + 3);
            if (!sb.data.empty() && sb.data[0] == 0xFF) { sb.data.clear(); break; }
        }
    }
}

void TsDemux::section(int pid, const uint8_t* s, int len) {
    if (len < 8) return;
    const int tid = s[0];
    const bool syntax = s[1] & 0x80;
    if (syntax && mpegCrc32(s, len) != 0) return;
    if (tid == 0x00) parsePat(s, len);
    else if (tid == 0x02) parsePmt(pid, s, len);
    else if (tid == 0x42) parseSdt(s, len);
    else if (tid == 0x40) parseNit(s, len);
    else if (tid == 0x4E) parseEit(s, len);
    else if (tid == 0x70 || tid == 0x73) parseTdt(s, len);
}

void TsDemux::parsePat(const uint8_t* s, int len) {
    tsid_ = (s[3] << 8) | s[4];
    for (int i = 8; i + 4 <= len - 4; i += 4) {
        int prog = (s[i] << 8) | s[i + 1];
        int pid = ((s[i + 2] & 0x1F) << 8) | s[i + 3];
        if (prog == 0) continue; // network PID
        TsService& sv = services_[prog];
        sv.id = prog;
        sv.pmtPid = pid;
        pmtToService_[pid] = prog;
    }
}

static const char* streamKind(int st, std::string& codec, bool& isVideo, bool& isAudio) {
    isVideo = isAudio = false;
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
    default: break;
    }
    return isVideo ? "video" : isAudio ? "audio" : "data";
}

void TsDemux::parsePmt(int pid, const uint8_t* s, int len) {
    auto it = pmtToService_.find(pid);
    if (it == pmtToService_.end()) return;
    TsService& sv = services_[it->second];
    int prog = (s[3] << 8) | s[4];
    if (prog != sv.id) return;
    sv.pcrPid = ((s[8] & 0x1F) << 8) | s[9];
    int pil = ((s[10] & 0x0F) << 8) | s[11];
    bool ca = false;
    for (int i = 12; i + 2 <= 12 + pil && i + 2 <= len; ) { int tag = s[i], l = s[i + 1]; if (tag == 0x09) ca = true; i += 2 + l; }
    std::vector<TsStream> streams;
    int i = 12 + pil;
    while (i + 5 <= len - 4) {
        TsStream st;
        st.streamType = s[i];
        st.pid = ((s[i + 1] & 0x1F) << 8) | s[i + 2];
        int esl = ((s[i + 3] & 0x0F) << 8) | s[i + 4];
        bool isV, isA;
        st.kind = streamKind(st.streamType, st.codec, isV, isA);
        for (int j = i + 5; j + 2 <= i + 5 + esl && j + 2 <= len; ) {
            int tag = s[j], l = s[j + 1];
            if (j + 2 + l > len) break;
            const uint8_t* d = s + j + 2;
            switch (tag) {
            case 0x09: ca = true; break;
            case 0x0A: if (l >= 3) st.lang.assign((const char*)d, 3); break;
            case 0x6A: st.codec = "AC-3"; st.kind = "audio"; break;
            case 0x7A: st.codec = "E-AC-3"; st.kind = "audio"; break;
            case 0x7B: st.codec = "DTS"; st.kind = "audio"; break;
            case 0x7C: st.codec = "AAC"; st.kind = "audio"; break;
            case 0x59: st.codec = "DVB subtitles"; st.kind = "subtitles"; if (l >= 3) st.lang.assign((const char*)d, 3); break;
            case 0x56: st.codec = "Teletext"; st.kind = "teletext"; if (l >= 3) st.lang.assign((const char*)d, 3); break;
            case 0x46: st.codec = "VBI teletext"; st.kind = "teletext"; break;
            default: break;
            }
            j += 2 + l;
        }
        if (st.codec.empty()) { char b[24]; snprintf(b, sizeof b, "type 0x%02X", st.streamType); st.codec = b; }
        streams.push_back(st);
        i += 5 + esl;
    }
    sv.streams = streams;
    sv.havePmt = true;
    sv.caFlag = sv.caFlag || ca;
}

void TsDemux::parseSdt(const uint8_t* s, int len) {
    tsid_ = (s[3] << 8) | s[4];
    onid_ = (s[8] << 8) | s[9];
    int i = 11;
    while (i + 5 <= len - 4) {
        int sid = (s[i] << 8) | s[i + 1];
        bool freeCa = s[i + 3] & 0x10;
        int dl = ((s[i + 3] & 0x0F) << 8) | s[i + 4];
        TsService& sv = services_[sid];
        sv.id = sid;
        sv.haveSdt = true;
        sv.caFlag = sv.caFlag || freeCa;
        for (int j = i + 5; j + 2 <= i + 5 + dl && j + 2 <= len; ) {
            int tag = s[j], l = s[j + 1];
            if (j + 2 + l > len) break;
            if (tag == 0x48 && l >= 3) {
                const uint8_t* d = s + j + 2;
                sv.type = d[0];
                int pl = d[1];
                if (2 + pl + 1 <= l) {
                    sv.provider = dvbText(d + 2, pl);
                    int nl = d[2 + pl];
                    if (3 + pl + nl <= l) sv.name = dvbText(d + 3 + pl, nl);
                }
            }
            j += 2 + l;
        }
        i += 5 + dl;
    }
}

void TsDemux::parseNit(const uint8_t* s, int len) {
    int ndl = ((s[8] & 0x0F) << 8) | s[9];
    for (int j = 10; j + 2 <= 10 + ndl && j + 2 <= len; ) {
        int tag = s[j], l = s[j + 1];
        if (tag == 0x40) network_ = dvbText(s + j + 2, l);
        j += 2 + l;
    }
    // transport stream loop: logical channel numbers (descriptor 0x83 in private data, e.g. EACEM / UK)
    int i = 10 + ndl;
    if (i + 2 > len) return;
    int tsl = ((s[i] & 0x0F) << 8) | s[i + 1];
    i += 2;
    int end = std::min(len - 4, i + tsl);
    while (i + 6 <= end) {
        int dl = ((s[i + 4] & 0x0F) << 8) | s[i + 5];
        for (int j = i + 6; j + 2 <= i + 6 + dl && j + 2 <= len; ) {
            int tag = s[j], l = s[j + 1];
            if (tag == 0x83) for (int k = 0; k + 4 <= l; k += 4) {
                int sid = (s[j + 2 + k] << 8) | s[j + 3 + k];
                int lcn = ((s[j + 4 + k] & 0x03) << 8) | s[j + 5 + k];
                services_[sid].id = sid;
                services_[sid].lcn = lcn;
            }
            j += 2 + l;
        }
        i += 6 + dl;
    }
}

void TsDemux::parseEit(const uint8_t* s, int len) {
    int sid = (s[3] << 8) | s[4];
    int secNum = s[6];
    if (secNum > 1 || len < 18) return;
    int i = 14;
    std::string nameOut;
    if (i + 12 <= len - 4) {
        int dl = ((s[i + 10] & 0x0F) << 8) | s[i + 11];
        for (int j = i + 12; j + 2 <= i + 12 + dl && j + 2 <= len; ) {
            int tag = s[j], l = s[j + 1];
            if (tag == 0x4D && l >= 5) {
                const uint8_t* d = s + j + 2;
                int nl = d[3];
                if (4 + nl <= l) nameOut = dvbText(d + 4, nl);
            }
            j += 2 + l;
        }
    }
    TsService& sv = services_[sid];
    sv.id = sid;
    (secNum == 0 ? sv.now : sv.next) = nameOut;
}

void TsDemux::parseTdt(const uint8_t* s, int len) {
    if (len < 8) return;
    int mjd = (s[3] << 8) | s[4];
    auto bcd = [](int v) { return (v >> 4) * 10 + (v & 15); };
    int hh = bcd(s[5]), mm = bcd(s[6]), ss = bcd(s[7]);
    int y = (int)((mjd - 15078.2) / 365.25);
    int m = (int)((mjd - 14956.1 - (int)(y * 365.25)) / 30.6001);
    int d = mjd - 14956 - (int)(y * 365.25) - (int)(m * 30.6001);
    int k = (m == 14 || m == 15) ? 1 : 0;
    y += k; m = m - 1 - k * 12;
    char b[40]; snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d UTC", 1900 + y, m, d, hh, mm, ss);
    utc_ = b;
}

void TsDemux::advance(double dt) {
    if (dt <= 0) return;
    const double a = 0.35;
    for (auto& kv : pids_) {
        double k = kv.second.bytesWin * 8.0 / dt / 1000.0;
        kv.second.kbps = kv.second.kbps * (1 - a) + k * a;
        kv.second.bytesWin = 0;
    }
    muxKbps_ = muxKbps_ * (1 - a) + (totalWin_ * 188 * 8.0 / dt / 1000.0) * a;
    nullKbps_ = nullKbps_ * (1 - a) + (nullWin_ * 188 * 8.0 / dt / 1000.0) * a;
    totalWin_ = nullWin_ = 0;
}

TsSnapshot TsDemux::snapshot() const {
    TsSnapshot sn;
    sn.networkName = network_; sn.utc = utc_; sn.onid = onid_; sn.tsid = tsid_;
    sn.totalPackets = total_; sn.nullPackets = nulls_; sn.teiPackets = tei_; sn.ccErrors = ccErr_;
    sn.muxKbps = muxKbps_; sn.nullKbps = nullKbps_;
    std::map<int, std::string> label;
    for (auto& kv : services_) {
        TsService sv = kv.second;
        for (auto& st : sv.streams) {
            auto it = pids_.find(st.pid);
            if (it != pids_.end()) { st.kbps = it->second.kbps; st.scrambled = it->second.scrambled; }
            label[st.pid] = (sv.name.empty() ? "service " + std::to_string(sv.id) : sv.name) + " " + st.kind + " (" + st.codec + ")";
        }
        label[sv.pmtPid] = "PMT " + (sv.name.empty() ? std::to_string(sv.id) : sv.name);
        if (sv.pmtPid || sv.haveSdt || sv.havePmt) sn.services.push_back(sv);
    }
    std::sort(sn.services.begin(), sn.services.end(), [](const TsService& a, const TsService& b) {
        if (a.lcn != b.lcn) return (a.lcn ? a.lcn : 9999) < (b.lcn ? b.lcn : 9999);
        return a.id < b.id;
    });
    label[0] = "PAT"; label[0x10] = "NIT"; label[0x11] = "SDT/BAT"; label[0x12] = "EIT"; label[0x14] = "TDT/TOT"; label[0x1FFF] = "null";
    label[0x13] = "RST"; label[0x1F] = "DIT"; label[0x1E] = "ST";
    for (auto& kv : pids_) {
        TsPid p;
        p.pid = kv.first; p.packets = kv.second.packets; p.ccErrors = kv.second.ccErrors; p.tei = kv.second.tei;
        p.kbps = kv.second.kbps; p.scrambled = kv.second.scrambled;
        auto it = label.find(kv.first);
        p.label = it != label.end() ? it->second : "";
        sn.pids.push_back(p);
    }
    return sn;
}

} // namespace dect2
