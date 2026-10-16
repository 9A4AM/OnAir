// HD Radio Layer 2, the data services: AAS packets, ID3 (artist, title, album), SIG (service information guide) and LOT (file transfer
// with the station logo and album art), and the telemetry fill. Adapted from nrsc5 output.c (GPL-3.0).
#include "hdr_l2_internal.h"
#include "dect2/hdr/hdr_fec.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>


namespace dect2 { namespace hdr {

namespace {
std::string latin1(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) {
        const uint8_t c = p[i];
        if (!c) break;
        if (c < 0x80) s += (char)c;
        else { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
    }
    return s;
}
std::string ucs2(const uint8_t* p, size_t n) {
    bool le = true;
    size_t i = 0;
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) { i = 2; }
    else if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) { le = false; i = 2; }
    std::string s;
    for (; i + 1 < n; i += 2) {
        const unsigned c = le ? (p[i] | (p[i + 1] << 8)) : ((p[i] << 8) | p[i + 1]);
        if (!c) break;
        if (c < 0x80) s += (char)c;
        else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
        else { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
    }
    return s;
}
std::string textEnc(int enc, const uint8_t* p, size_t n) { return enc == 1 ? ucs2(p, n) : latin1(p, n); }
std::string textEnc(int enc, const char* p, size_t n) { return textEnc(enc, reinterpret_cast<const uint8_t*>(p), n); }
size_t cstrLen(const char* s, size_t max) { size_t n = 0; while (n < max && s[n]) n++; return n; }
}

// ---------------------------------------------------------------- AAS packets (nrsc5 output.c output_aas_push())

void L2Decoder::aasPacket(uint16_t port, uint16_t, const uint8_t* p, size_t n) {
    auto& pc = portCount_[port];
    pc.first++;
    pc.second += n;
    if (port == 0x5100 || (port >= 0x5201 && port <= 0x5207)) id3(port & 7, p, n);
    else if (port == 0x20) sig(p, n);
    else if (port >= 0x401 && port <= 0x50FF) {
        lastData_ = now_;
        for (const SigService& s : sigServices_)
            for (const SigService::Comp& c : s.comps)
                if (!c.audio && c.port == port && c.type == 3) { lot(port, p, n); return; }
    }
}

void L2Decoder::id3(int program, const uint8_t* buf, size_t len) {
    if (len < 10 || memcmp(buf, "ID3\x03\x00", 5) != 0 || buf[5]) return;
    const size_t total = (size_t)(((buf[6] & 0x7F) << 21) | ((buf[7] & 0x7F) << 14) | ((buf[8] & 0x7F) << 7) | (buf[9] & 0x7F)) + 10;
    if (total > len) return;
    Prog& P = prog(program);
    HdrProgram& H = P.p;
    size_t off = 10;
    auto text = [](const uint8_t* d, size_t n) { return n ? textEnc(d[0], d + 1, n - 1) : std::string(); };
    while (off + 10 <= total) {
        const uint8_t* tag = buf + off;
        const uint8_t* d = tag + 10;
        const size_t fl = ((size_t)tag[4] << 24) | ((size_t)tag[5] << 16) | ((size_t)tag[6] << 8) | tag[7];
        if (off + 10 + fl > total) break;
        if (!memcmp(tag, "TIT2", 4)) H.title = text(d, fl);
        else if (!memcmp(tag, "TPE1", 4)) H.artist = text(d, fl);
        else if (!memcmp(tag, "TALB", 4)) H.album = text(d, fl);
        else if (!memcmp(tag, "TCON", 4)) H.genre = text(d, fl);
        else if (!memcmp(tag, "UFID", 4)) {
            const uint8_t* z = (const uint8_t*)memchr(d, 0, fl);
            if (z) H.ufid = latin1(z + 1, (size_t)(d + fl - z - 1));
        } else if (!memcmp(tag, "COMM", 4) && fl >= 5) {
            const int enc = d[0];
            const uint8_t* e = d + fl;
            const uint8_t* z = d + 4;
            if (enc == 1) { while (z + 1 < e && (z[0] || z[1])) z += 2; z += 2; }
            else { while (z < e && *z) z++; z++; }
            if (z <= e) H.comment = textEnc(enc, z, (size_t)(e - z));
        } else if (!memcmp(tag, "COMR", 4) && fl >= 1) {
            // price, valid until (8 digits), contact URL, received as, seller, description
            const int enc = d[0];
            const uint8_t* e = d + fl;
            const uint8_t* q = d + 1;
            auto field = [&](int en) {
                const uint8_t* s = q;
                if (en == 1) { while (q + 1 < e && (q[0] || q[1])) q += 2; const std::string r = ucs2(s, (size_t)(q - s)); q = std::min(e, q + 2); return r; }
                while (q < e && *q) q++;
                const std::string r = latin1(s, (size_t)(q - s));
                q = std::min(e, q + 1);
                return r;
            };
            H.comPrice = field(0);
            if (q + 8 <= e) { H.comValid = std::string((const char*)q, 4) + "-" + std::string((const char*)q + 4, 2) + "-" + std::string((const char*)q + 6, 2); q += 8; }
            H.comUrl = field(0);
            if (q < e) q++;   // received as
            H.comSeller = field(enc);
            H.comDesc = field(enc);
        } else if (!memcmp(tag, "XHDR", 4) && fl >= 6) {
            H.xhdrMime = d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24);
            const int param = d[4], ext = d[5];
            if (6u + (size_t)ext == fl) {
                if (param == 0 && ext == 2) H.xhdrLot = d[6] | (d[7] << 8);
                else if (param == 1 && ext == 0) H.xhdrLot = -1;
            }
        }
        off += 10 + fl;
    }
    if (H.psdCount == 0) say("HD Radio: HD" + std::to_string(program + 1) + " now playing: " + H.title + (H.artist.empty() ? "" : " / " + H.artist));
    H.psdCount++;
    lastPsd_ = now_;
}

// nrsc5 output.c parse_sig(): the station information guide
void L2Decoder::sig(const uint8_t* buf, size_t len) {
    lastData_ = now_;
    if (len == sigRaw_.size() && std::equal(buf, buf + len, sigRaw_.begin())) return;
    sigRaw_.assign(buf, buf + len);
    std::vector<SigService> svc;
    const uint8_t* p = buf;
    const uint8_t* e = buf + len;
    SigService* cur = nullptr;
    while (p < e) {
        const uint8_t type = *p++;
        if ((type & 0xF0) == 0x40) {
            if (p + 3 > e) break;
            SigService s;
            s.audio = type == 0x40;
            s.number = p[0] | (p[1] << 8);
            svc.push_back(s);
            cur = &svc.back();
            p += 3;
        } else if ((type & 0xF0) == 0x60) {
            if (p >= e) break;
            const int l = *p++;
            if (!cur || l < 1 || p + (l - 1) > e) break;
            if (type == 0x69 && l >= 2) cur->name = latin1(p + 1, (size_t)(l - 2));
            else if (type == 0x67 && l - 1 >= 12) cur->comps.push_back({false, p[0], p[1] | (p[2] << 8), p[5], p[8] | (p[9] << 8) | (p[10] << 16) | ((uint32_t)p[11] << 24), p[3] | (p[4] << 8)});
            else if (type == 0x66 && l - 1 >= 11) cur->comps.push_back({true, p[0], p[1], p[2], p[7] | (p[8] << 8) | (p[9] << 16) | ((uint32_t)p[10] << 24), -1});
            p += l - 1;
        } else break;
    }
    sigServices_ = svc;
    for (const SigService& s : svc) {
        if (!s.audio) continue;
        for (const SigService::Comp& c : s.comps) {
            if (!c.audio || c.port < 0 || c.port > 7) continue;
            Prog& P = prog(c.port);
            P.p.inSig = true;
            if (!s.name.empty()) P.p.name = s.name;
            for (const SigService::Comp& d : s.comps) if (!d.audio && d.mime == kMimePrimaryImage) P.p.artPort = d.port;
        }
    }
    say("HD Radio: station information guide with " + std::to_string(svc.size()) + " services");
}

// nrsc5 output.c process_port(), the LOT case
void L2Decoder::lot(int port, const uint8_t* buf, size_t len) {
    if (len < 8) return;
    int hdrlen = buf[0];
    const int lotId = buf[2] | (buf[3] << 8);
    const uint32_t seq = buf[4] | (buf[5] << 8) | (buf[6] << 16) | ((uint32_t)buf[7] << 24);
    if (hdrlen < 8 || (size_t)hdrlen > len || seq >= 256) return;
    buf += 8; len -= 8; hdrlen -= 8;
    auto key = std::make_pair(port, lotId);
    auto it = lots_.find(key);
    if (it == lots_.end()) {
        if (lots_.size() >= 24) {   // forget the oldest
            auto old = lots_.begin();
            for (auto j = lots_.begin(); j != lots_.end(); ++j) if (j->second.info.timeSec < old->second.info.timeSec) old = j;
            lots_.erase(old);
        }
        it = lots_.emplace(key, Lot()).first;
        it->second.info.port = port;
        it->second.info.lot = lotId;
        it->second.frags.resize(256);
    }
    Lot& L = it->second;
    L.info.timeSec = now_;
    bool fresh = false;
    if (hdrlen > 0) {
        if (hdrlen < 16) return;
        const int year = (buf[7] << 4) | (buf[6] >> 4), mon = buf[6] & 0xF, mday = buf[5] >> 3, hour = ((buf[5] & 7) << 2) | (buf[4] >> 6), min = buf[4] & 0x3F;
        const uint32_t size = buf[8] | (buf[9] << 8) | (buf[10] << 16) | ((uint32_t)buf[11] << 24);
        const uint32_t mime = buf[12] | (buf[13] << 8) | (buf[14] << 16) | ((uint32_t)buf[15] << 24);
        const std::string name = latin1(buf + 16, (size_t)(hdrlen - 16));
        char ex[40];
        snprintf(ex, sizeof ex, "%04d-%02d-%02d %02d:%02d", year, mon, mday, hour, min);
        if (L.info.size && (name != L.info.name || size != L.info.size || mime != L.info.mime || ex != L.info.expires)) {
            for (auto& f : L.frags) f.clear();     // a new file under the same id
            L.info.have = 0; L.info.complete = false;
            fresh = true;
        }
        L.info.name = name; L.info.size = std::min<uint32_t>(size, 65536); L.info.mime = mime; L.info.expires = ex;
        buf += hdrlen; len -= (size_t)hdrlen;
    }
    if (len > 256) return;
    std::vector<uint8_t>& fr = L.frags[seq];
    if (fr.empty() && len) { fr.assign(buf, buf + len); L.info.have += (uint32_t)len; fresh = true; }
    if (fresh && L.info.size && !L.info.complete) {
        const uint32_t nf = (L.info.size + 255) / 256;
        bool all = true;
        for (uint32_t i = 0; i < nf && all; i++) all = !L.frags[i].empty();
        if (all) {
            L.bytes.clear();
            for (uint32_t i = 0; i < nf; i++) L.bytes.insert(L.bytes.end(), L.frags[i].begin(), L.frags[i].end());
            L.bytes.resize(L.info.size);
            L.info.complete = true;
            L.info.version = ++lotVersion_;
            char b[200];
            snprintf(b, sizeof b, "HD Radio: received %s (%u bytes, %s) on port %04X", L.info.name.c_str(), L.info.size, mimeName(L.info.mime), port);
            say(b);
        }
    }
}

bool L2Decoder::lotBytes(int port, int lotId, std::vector<uint8_t>& out) const {
    auto it = lots_.find(std::make_pair(port, lotId));
    if (it == lots_.end() || !it->second.info.complete) return false;
    out = it->second.bytes;
    return true;
}

void L2Decoder::fill(HdrTelemetry& t) const {
    const Sis& S = *sis_;
    t.callSign = S.shortName;
    t.countryCode = S.country;
    t.facilityId = S.facility;
    t.stationName = S.unameDone ? textEnc(S.unameEnc, S.uname, cstrLen(S.uname, 12)) + (S.unameAppend ? "-FM" : "") : std::string();
    t.longName = S.longDone ? latin1(reinterpret_cast<const uint8_t*>(S.longName), cstrLen(S.longName, 56)) : std::string();
    t.slogan = S.sloganDone ? textEnc(S.sloganEnc, S.slogan, (size_t)std::max(0, S.sloganLen)) : std::string();
    t.message = S.msgDone ? textEnc(S.msgEnc, S.msg, (size_t)S.msgLen) : std::string();
    t.alert = S.alertDone && S.alertLen > S.alertCnt ? textEnc(S.alertEnc, S.alert + S.alertCnt, (size_t)(S.alertLen - S.alertCnt)) : std::string();
    t.haveLocation = !std::isnan(S.lat) && !std::isnan(S.lon);
    t.latitude = t.haveLocation ? S.lat : 0;
    t.longitude = t.haveLocation ? S.lon : 0;
    t.altitudeM = S.alt;
    // programs: from the PDUs, the SIG and the SIS service descriptors
    std::map<int, HdrProgram> pm;
    for (const auto& kv : progs_) pm[kv.first] = kv.second.p;
    for (int i = 0; i < 8; i++) {
        if (S.asd[i].type < 0) continue;
        HdrProgram& p = pm[i];
        p.number = i;
        p.inSis = true;
        if (p.type < 0) p.type = S.asd[i].type;
        p.soundExp = S.asd[i].sound;
        p.access = S.asd[i].access;
    }
    t.programs.clear();
    for (const auto& kv : pm) if (kv.second.onAir || kv.second.inSis || kv.second.inSig) t.programs.push_back(kv.second);
    // data services
    t.dataServices.clear();
    for (const SigService& s : sigServices_) {
        int program = -1;
        if (s.audio) for (const SigService::Comp& c : s.comps) if (c.audio) program = c.port;
        for (const SigService::Comp& c : s.comps) {
            if (c.audio) continue;
            HdrDataService d;
            d.fromSig = true; d.service = s.number; d.name = s.name; d.program = program; d.port = c.port; d.aasType = c.type; d.mime = c.mime; d.sdType = c.sdType;
            auto pc = portCount_.find(c.port);
            if (pc != portCount_.end()) { d.packets = pc->second.first; d.bytes = pc->second.second; }
            t.dataServices.push_back(d);
        }
    }
    for (const auto& d : S.dsd) {
        HdrDataService x;
        x.fromSig = false; x.sdType = d.type; x.sisMime = d.mime; x.aasType = -1;
        x.name = "";
        t.dataServices.push_back(x);
    }
    t.lots.clear();
    for (const auto& kv : lots_) t.lots.push_back(kv.second.info);
    std::sort(t.lots.begin(), t.lots.end(), [](const HdrLotInfo& a, const HdrLotInfo& b) { return a.timeSec > b.timeSec; });
    t.lotVersion = lotVersion_;
    t.pidsOk = pidsOk_; t.pidsBad = pidsBad_;
    t.pduCount = pdus_;
    t.aasPackets = aasOk_; t.aasBad = aasBad_;
    t.psdSeen = now_ - lastPsd_ < 10;
    t.dataSeen = now_ - lastData_ < 10;
    t.dataValid = !S.shortName.empty() || !progs_.empty();
}

}} // namespace dect2::hdr
