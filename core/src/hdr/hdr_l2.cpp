// HD Radio Layer 2 and the data services (see hdr_l2.h). The decoding logic is adapted from nrsc5 (GPL-3.0,
// github.com/theori-io/nrsc5): the transfer frame and PDU parsing from src/frame.c, the SIS messages from src/pids.c, the ID3, SIG and LOT
// handling from src/output.c. Nothing here touches the HDC audio codec: the audio packets are counted and, when a sink is set (setAudio),
// handed on with their sequence numbers (hdr_audio.cpp plays them through the user's own library).
#include "hdr_l2_internal.h"
#include "dect2/hdr/hdr_fec.h"
#include "dect2/hdr/hdr_hdc.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>


namespace dect2 { namespace hdr {

L2Decoder::L2Decoder() { reset(); }
L2Decoder::~L2Decoder() = default;

void L2Decoder::reset() {
    sis_ = std::make_unique<Sis>();
    progs_.clear();
    sigServices_.clear();
    sigRaw_.clear();
    lots_.clear();
    portCount_.clear();
    lotVersion_ = 0;
    for (int c = 0; c < 3; c++) pciOk_[c] = pciBad_[c] = 0;
    pdus_ = aasOk_ = aasBad_ = pidsOk_ = pidsBad_ = 0;
    lastPsd_ = lastData_ = -1e9;
    resetTransport();
}

void L2Decoder::resetTransport() {
    for (auto& f : fixed_) f = std::make_unique<Fixed>();
    for (auto& kv : progs_) { kv.second.psd.clear(); kv.second.psdOpen = false; }
}

L2Decoder::Prog& L2Decoder::prog(int n) {
    auto it = progs_.find(n);
    if (it == progs_.end()) { it = progs_.emplace(n, Prog()).first; it->second.p.number = n; }
    return it->second;
}

namespace {

unsigned getBits(const uint8_t* bits, int& off, int n) {
    unsigned r = 0;
    for (int i = 0; i < n; i++) r = (r << 1) | bits[off++];
    return r;
}
int getSigned(const uint8_t* bits, int& off, int n) {
    const int r = (int)getBits(bits, off, n);
    return (r & (1 << (n - 1))) ? r - (1 << n) : r;
}

// nrsc5 pids.c crc7() over the alert text
int crc7(const char* a, int len) {
    const unsigned char poly = 0x09;
    unsigned char reg = 0x42;
    for (int bi = len - 1; bi >= 0; bi--) {
        for (int b = 6; b >= 0; b--) {
            unsigned char bit = ((unsigned char)a[bi] >> b) & 1;
            if (b == 0 && bi > 0) bit ^= ((unsigned char)a[bi - 1] >> 7);
            reg = (unsigned char)(reg << 1);
            reg ^= bit;
            if (reg & 0x80) reg ^= (0x80 | poly);
        }
    }
    for (int b = 6; b >= 0; b--) {
        reg = (unsigned char)(reg << 1);
        if (reg & 0x80) reg ^= (0x80 | poly);
    }
    return reg;
}
}

// ---------------------------------------------------------------- PIDS / SIS (nrsc5 pids.c)

void L2Decoder::pushPids(const uint8_t* bits) {
    uint8_t p[80];
    for (int i = 0; i < 80; i++) p[i] = bits[((i >> 3) << 3) + 7 - (i & 7)];
    if (crc12(p) != (unsigned)((p[68] << 11) | (p[69] << 10) | (p[70] << 9) | (p[71] << 8) | (p[72] << 7) | (p[73] << 6) | (p[74] << 5) | (p[75] << 4) |
                               (p[76] << 3) | (p[77] << 2) | (p[78] << 1) | p[79])) { pidsBad_++; return; }
    pidsOk_++;
    if (p[0] == 0) sisDecode(p + 1);
}

void L2Decoder::sisDecode(const uint8_t* bits) {
    static const int sizes[16] = {32, 22, 58, 32, 27, 58, 27, 22, 58, 58, 27, -1, -1, -1, -1, -1};
    Sis& S = *sis_;
    int off = 0;
    const int payloads = bits[0] + 1;
    off = 1;
    if (S.alertDone) S.alertTimeout++;
    for (int n = 0; n < payloads; n++) {
        if (off > 59) break;
        const int id = (int)getBits(bits, off, 4);
        const int sz = sizes[id];
        if (sz < 0 || off > 63 - sz) break;
        const uint8_t* b = bits + off;
        int o = 0;
        switch (id) {
        case 0: {   // station id
            char cc[3] = {kChars5[getBits(b, o, 5)], 0, 0};
            cc[1] = kChars5[getBits(b, o, 5)];
            o += 3;
            const int fac = (int)getBits(b, o, 19);
            if (S.country != cc || S.facility != fac) { S.country = cc; S.facility = fac; }
            break;
        }
        case 1: {   // short name
            std::string s;
            for (int j = 0; j < 4; j++) s += kChars5[getBits(b, o, 5)];
            while (!s.empty() && s.back() == ' ') s.pop_back();
            if (b[o] == 0 && b[o + 1] == 1) s += "-FM";
            if (s != S.shortName) { S.shortName = s; say("HD Radio: call sign " + s); }
            break;
        }
        case 2: {   // long name
            const int last = (int)getBits(b, o, 3), cur = (int)getBits(b, o, 3);
            int t = 55;
            const int seq = (int)getBits(b, t, 3);
            if (seq != S.longSeq) { memset(S.longName, 0, sizeof S.longName); memset(S.longHave, 0, sizeof S.longHave); S.longSeq = seq; S.longDone = false; }
            for (int j = 0; j < 7; j++) S.longName[cur * 7 + j] = (char)getBits(b, o, 7);
            S.longHave[cur] = true;
            if (S.longSeq >= 0 && !S.longDone) {
                bool all = true;
                for (int j = 0; j <= last; j++) all = all && S.longHave[j];
                if (all) S.longDone = true;
            }
            break;
        }
        case 4: {   // location
            if (b[o++]) { S.lat = getSigned(b, o, 22) / 8192.0; S.alt = (S.alt & 0x0F0) | ((int)getBits(b, o, 4) << 8); }
            else { S.lon = getSigned(b, o, 22) / 8192.0; S.alt = (S.alt & 0xF00) | ((int)getBits(b, o, 4) << 4); }
            break;
        }
        case 5: {   // station message
            const int cur = (int)getBits(b, o, 5), seq = (int)getBits(b, o, 2);
            if (cur == 0) {
                if (seq != S.msgSeq) { memset(S.msg, 0, sizeof S.msg); memset(S.msgHave, 0, sizeof S.msgHave); S.msgSeq = seq; S.msgDone = false; }
                o += 1;   // priority
                S.msgEnc = (int)getBits(b, o, 3);
                S.msgLen = (int)getBits(b, o, 8);
                S.msgCk = (int)getBits(b, o, 7);
                for (int j = 0; j < 4; j++) S.msg[j] = (char)getBits(b, o, 8);
            } else {
                o += 3;
                for (int j = 0; j < 6; j++) S.msg[cur * 6 - 2 + j] = (char)getBits(b, o, 8);
            }
            S.msgHave[cur] = true;
            if (S.msgSeq >= 0 && !S.msgDone) {
                bool all = true;
                for (int j = 0; j < (S.msgLen + 7) / 6; j++) all = all && S.msgHave[j];
                if (all) {
                    unsigned sum = 0;
                    for (int j = 0; j < S.msgLen; j++) sum += (uint8_t)S.msg[j];
                    sum = (((sum >> 8) & 0x7F) + (sum & 0xFF)) & 0x7F;
                    if ((int)sum == S.msgCk) S.msgDone = true;
                }
            }
            break;
        }
        case 6: case 10: {   // service information
            const int cat = (int)getBits(b, o, 2);
            if (cat == 0) {
                const int acc = (int)getBits(b, o, 1), pn = (int)getBits(b, o, 6), type = (int)getBits(b, o, 8);
                o += 5;
                const int se = (int)getBits(b, o, 5);
                if (pn < 64) { S.asd[pn].access = acc; S.asd[pn].type = type; S.asd[pn].sound = se; }
            } else if (cat == 1) {
                const int acc = (int)getBits(b, o, 1), type = (int)getBits(b, o, 9);
                o += 3;
                const int mime = (int)getBits(b, o, 12);
                bool have = false;
                for (const auto& d : S.dsd) if (d.access == acc && d.type == type && d.mime == mime) have = true;
                if (!have && S.dsd.size() < 16) S.dsd.push_back({acc, type, mime});
            }
            break;
        }
        case 8: {   // universal short station name or slogan
            const int cur = (int)getBits(b, o, 4);
            if (b[o++] == 0) {
                if (cur >= 2) break;
                if (cur == 0) {
                    const int enc = (int)getBits(b, o, 3), app = b[o++], len = b[o++] + 1;
                    char first[6];
                    for (int j = 0; j < 6; j++) first[j] = (char)getBits(b, o, 8);
                    if (enc != S.unameEnc || app != S.unameAppend || len != S.unameLen || memcmp(first, S.uname, 6) != 0) {   // a changed name: collect it afresh
                        memset(S.uname, 0, sizeof S.uname); memset(S.unameHave, 0, sizeof S.unameHave); S.unameDone = false;
                    }
                    S.unameEnc = enc; S.unameAppend = app; S.unameLen = len;
                    memcpy(S.uname, first, 6);
                } else {
                    o += 5;
                    for (int j = 0; j < 6; j++) S.uname[cur * 6 + j] = (char)getBits(b, o, 8);
                }
                S.unameHave[cur] = true;
                if (S.unameLen >= 0 && !S.unameDone) {
                    bool all = true;
                    for (int j = 0; j < S.unameLen; j++) all = all && S.unameHave[j];
                    if (all) S.unameDone = true;
                }
            } else {
                if (cur == 0) {
                    S.sloganEnc = (int)getBits(b, o, 3);
                    o += 3;
                    S.sloganLen = (int)getBits(b, o, 7);
                    for (int j = 0; j < 5; j++) S.slogan[j] = (char)getBits(b, o, 8);
                } else {
                    o += 5;
                    for (int j = 0; j < 6; j++) if (cur * 6 - 1 + j < 127) S.slogan[cur * 6 - 1 + j] = (char)getBits(b, o, 8);
                }
                S.sloganHave[cur] = true;
                if (S.sloganLen >= 0 && !S.sloganDone) {
                    bool all = true;
                    for (int j = 0; j < (S.sloganLen + 6) / 6; j++) all = all && S.sloganHave[j];
                    if (all) S.sloganDone = true;
                }
            }
            break;
        }
        case 9: {   // emergency alert
            const int cur = (int)getBits(b, o, 6), seq = (int)getBits(b, o, 2);
            o += 2;
            S.alertTimeout = 0;
            if (cur == 0) {
                if (seq != S.alertSeq) { memset(S.alert, 0, sizeof S.alert); memset(S.alertHave, 0, sizeof S.alertHave); S.alertSeq = seq; S.alertDone = false; }
                S.alertEnc = (int)getBits(b, o, 3);
                S.alertLen = (int)getBits(b, o, 9);
                S.alertCrc = (int)getBits(b, o, 7);
                S.alertCnt = 1 + 2 * (int)getBits(b, o, 5);
                for (int j = 0; j < 3; j++) S.alert[j] = (char)getBits(b, o, 8);
            } else {
                for (int j = 0; j < 6; j++) if (cur * 6 - 3 + j < 500) S.alert[cur * 6 - 3 + j] = (char)getBits(b, o, 8);
            }
            S.alertHave[cur] = true;
            if (S.alertLen >= 0 && !S.alertDone) {
                bool all = true;
                for (int j = 0; j < (S.alertLen + 8) / 6 && j < 64; j++) all = all && S.alertHave[j];
                if (all && crc7(S.alert, S.alertLen) == S.alertCrc && S.alertCnt >= 7 && S.alertLen >= S.alertCnt) {
                    S.alertDone = true;
                    say("HD Radio: emergency alert received");
                }
            }
            break;
        }
        default: break;     // parameter messages and reserved ones
        }
        off += sz;
    }
    if (S.alertDone && S.alertTimeout >= 16) { S.alertDone = false; S.alertSeq = -1; S.alertLen = -1; memset(S.alertHave, 0, sizeof S.alertHave); }
}

// ---------------------------------------------------------------- transfer frames (nrsc5 frame.c)

void L2Decoder::pushTransfer(const uint8_t* bits, int len, int channel) {
    std::vector<uint8_t> pdu;
    uint32_t pci = 0;
    unpackTransfer(bits, len, pdu, pci);
    FrameGeom g;
    if (!frameGeom(len, g)) return;
    uint32_t m = 0;
    channel = chan(channel);
    if (matchPci(pci, g.pciLen, m) < 0) { pciBad_[channel]++; return; }
    pciOk_[channel]++;
    for (auto& kv : progs_) kv.second.bytesNow[channel] = 0;
    frameProcess(pdu, channel, m);
    // audio bit rate of each program on this channel, averaged over about 4 seconds
    const double dur = (len == 146176 || len == 24000 || len == 30000) ? 65536.0 / 44100.0 : 65536.0 / 44100.0 / 8.0;
    const double a = std::min(1.0, dur / 4.0);
    for (auto& kv : progs_) {
        Prog& P = kv.second;
        const double r = (double)P.bytesNow[channel] / dur;
        if (!P.rateInit[channel]) { if (P.bytesNow[channel]) { P.rate[channel] = r; P.rateInit[channel] = true; } }
        else P.rate[channel] += a * (r - P.rate[channel]);
        P.p.kbps = (P.rate[0] + P.rate[1] + P.rate[2]) * 8 / 1000;
    }
}

namespace {
// packets per transfer frame of a stream, which sets where the play position stands (nrsc5 frame.c calc_avg_packets)
unsigned avgPackets(int codec, int stream) {
    switch (codec) {
    case 1: case 2: case 3: return stream == 0 ? 4 : 32;
    case 10: return stream == 0 ? 32 : 4;
    case 13: return 4;
    default: return 32;
    }
}
bool hasAudio(uint32_t pci) { return pci == kPciAudio || pci == kPciAudioOpp || pci == kPciAudioFixed || pci == kPciAudioFixedOpp; }
bool hasFixed(uint32_t pci) { return pci == kPciAudioFixed || pci == kPciAudioFixedOpp || pci == kPciFixed; }
size_t unescape(std::vector<uint8_t>& d) {
    size_t o = 0;
    for (size_t i = 0; i < d.size(); i++) {
        if (d[i] == 0x7D && i + 1 < d.size()) d[o++] = (uint8_t)(d[++i] | 0x20);
        else d[o++] = d[i];
    }
    d.resize(o);
    return o;
}
}

void L2Decoder::frameProcess(std::vector<uint8_t>& buf, int channel, uint32_t pci) {
    size_t audioEnd = buf.size();
    if (hasFixed(pci)) audioEnd = fixedData(buf, channel);
    if (!hasAudio(pci)) return;
    size_t offset = 0;
    while (offset + 96 < audioEnd) {
        const size_t start = offset;
        uint8_t* h = &buf[offset];
        if (!rsDecodeHeader(h, nullptr)) return;
        const int codec = h[8] & 0xF, stream = (h[8] >> 4) & 3;
        const int blend = (h[9] >> 1) & 3;
        const int nop = (h[12] >> 1) & 0x3F, hef = h[12] >> 7, la = h[13];
        offset += 14;
        const int lcb = locBits(codec, stream);
        const size_t locBytes = (size_t)((lcb * nop + 4) / 8);
        if (start + (size_t)la + 1 < offset + locBytes || start + (size_t)la >= audioEnd) return;
        std::vector<int> loc((size_t)nop);
        for (int j = 0; j < nop; j++) {
            const uint8_t* L = &buf[offset];
            int v;
            if (lcb == 16) v = L[2 * j] | (L[2 * j + 1] << 8);
            else if (j % 2 == 0) v = ((L[j / 2 * 3 + 1] & 0xF) << 8) | L[j / 2 * 3];
            else v = (L[j / 2 * 3 + 2] << 4) | (L[j / 2 * 3 + 1] >> 4);
            loc[(size_t)j] = v;
            if (j == 0 && v <= la) return;
            if (j > 0 && v <= loc[(size_t)j - 1]) return;
            if (start + (size_t)v >= audioEnd) return;
        }
        offset += locBytes;
        if (stream >= 2) {
            if (nop == 0) return;
            offset = start + (size_t)loc[(size_t)nop - 1] + 1;
            continue;
        }
        int progNum = 0, access = 0, type = -1;
        if (hef) {   // header expansion fields (nrsc5 parse_hef()); every element's last byte says whether another follows
            const size_t end = audioEnd;
            size_t p = offset;
            bool more = true;
            while (more) {
                if (p >= end) { p = end; break; }
                const uint8_t c = buf[p];
                bool bad = false;
                switch ((c >> 4) & 7) {
                case 1:
                    progNum = (c >> 1) & 7;
                    if (c & 1) { if (p + 2 >= end) bad = true; else p += 2; }
                    break;
                case 2:
                    if (p + 1 >= end) { bad = true; break; }
                    access = (c >> 3) & 1;
                    type = (c & 1) << 7;
                    p++;
                    type |= buf[p] & 0x7F;
                    break;
                case 3:
                    if (p + ((c & 8) ? 4 : 3) >= end) bad = true; else p += (c & 8) ? 4 : 3;
                    break;
                case 4:
                    if (p + ((c & 8) ? 3 : 1) >= end) bad = true; else p += (c & 8) ? 3 : 1;
                    break;
                default: break;
                }
                if (bad) { p = end; break; }
                more = (buf[p++] & 0x80) != 0;
            }
            offset = p;
        }
        Prog& P = prog(progNum);
        pdus_++;
        unsigned seq = 0;
        if (audio_ && stream == 0) {
            // where this PDU's packets sit in the program's 64-slot ring, and where the play position should be (nrsc5 frame.c)
            const unsigned pduSeq = (unsigned)((h[8] >> 6) | ((h[9] & 1) << 2));
            const unsigned latency = (unsigned)((h[10] >> 6) | ((h[11] & 1) << 2));
            const unsigned pfirst = (h[11] >> 1) & 1;
            const unsigned avg = avgPackets(codec, stream);
            seq = (64 + (unsigned)((h[11] >> 3) | ((h[12] & 1) << 5)) - pfirst) % 64;
            unsigned at = (64 + pduSeq * avg - latency * 2) % 64;
            if (((64 + seq - at) % 64) >= 32) at = (at + 32) % 64;
            audio_->align(progNum, stream, at);
        }
        P.p.onAir = true;
        if (stream == 0) {
            P.p.codecMode = codec;
            P.p.blend = blend;
            P.p.access = access;
            if (type >= 0) P.p.type = type;
        }
        // program service data and the opportunistic AAS packets, in an HDLC stream across the PDUs
        const size_t psdEnd = start + (size_t)la + 1;
        if (psdEnd > offset) hdlcFeed(P.psd, P.psdOpen, &buf[offset], psdEnd - offset, 100 + progNum, channel);
        offset = psdEnd;
        for (int j = 0; j < nop; j++) {
            const size_t last = start + (size_t)loc[(size_t)j];
            if (last < offset) return;
            const size_t cnt = last - offset;
            const bool crcOk = crc8(&buf[offset], cnt + 1) == 0;
            if (crcOk) P.p.packetsOk++; else P.p.packetsBad++;
            if (audio_ && stream == 0) {
                const bool first = j == 0 && ((h[11] >> 1) & 1), lastHalf = j == nop - 1 && ((h[11] >> 2) & 1);
                audio_->push(progNum, stream, seq, first ? HdcAudio::HalfBack : lastHalf ? HdcAudio::HalfFront : HdcAudio::Full, crcOk, &buf[offset], cnt);
                seq = (seq + 1) % 64;
            }
            P.bytesNow[channel] += cnt;
            offset = last + 1;
        }
    }
}

// kind: 100 + program for a PSD stream, 0..3 a fixed subchannel, -1 the CCC
void L2Decoder::hdlcFeed(std::vector<uint8_t>& acc, bool& open, const uint8_t* p, size_t n, int kind, int channel) {
    for (size_t i = 0; i < n; i++) {
        const uint8_t b = p[i];
        if (b == 0x7E) {
            if (open) {
                if (kind < 0) cccFrame(acc, channel);
                else aasFrame(acc, kind, channel);
            }
            acc.clear();
            open = true;
        } else if (open) {
            if (acc.size() >= 8212) { open = false; acc.clear(); continue; }
            acc.push_back(b);
        }
    }
}

void L2Decoder::aasFrame(std::vector<uint8_t>& f, int, int) {
    const size_t n = unescape(f);
    if (n == 0) return;                                  // padding
    if (n < 7 || fcs16(f.data(), n) != kFcsGood) { aasBad_++; return; }   // stations abandon frames now and then
    aasOk_++;
    if (f[0] != 0x21) return;
    aasPacket((uint16_t)(f[1] | (f[2] << 8)), (uint16_t)(f[3] | (f[4] << 8)), &f[5], n - 7);
}

void L2Decoder::cccFrame(std::vector<uint8_t>& f, int channel) {
    Fixed& F = *fixed_[chan(channel)];
    const size_t n = unescape(f);
    if (n == 0 || F.ready) return;
    if (fcs16(f.data(), n) != kFcsGood) return;
    for (int i = 0; i < 4; i++) {
        F.sub[i].length = 0;
        if ((size_t)(5 + i * 4) <= n) {
            const int mode = f[(size_t)(1 + i * 4)] | (f[(size_t)(2 + i * 4)] << 8);
            const int len = f[(size_t)(3 + i * 4)] | (f[(size_t)(4 + i * 4)] << 8);
            if (mode == 0) { F.sub[i].length = len; F.sub[i].blocks.clear(); F.sub[i].open = false; }
            else say("HD Radio: fixed data subchannel mode " + std::to_string(mode) + " is not supported");
        }
    }
    F.ready = true;
}

// nrsc5 process_fixed_data(): from the end of the frame backwards, the sync byte, the CCC bytes and the subchannels
size_t L2Decoder::fixedData(std::vector<uint8_t>& buf, int channel) {
    Fixed& F = *fixed_[chan(channel)];
    if (buf.empty()) return 0;
    long p = (long)buf.size() - 1;
    auto widthOf = [](uint8_t b) -> int { if (b == 0) return 1; if ((b >> 4) == (b & 0xF)) return (b & 0xF) * 2; return 0; };
    if (F.syncCount < 2) {
        const int w = widthOf(buf[(size_t)p]);
        if (w > 0 && F.syncWidth == w) F.syncCount++;
        else F.syncCount = 0;
        F.syncWidth = w;
        if (F.syncCount < 2) return (size_t)p;
    }
    p -= F.syncWidth;
    if (p < 0) return 0;
    hdlcFeed(F.ccc, F.cccOpen, &buf[(size_t)p], (size_t)F.syncWidth, -1, channel);
    if (!F.ready) return (size_t)p;
    static const uint8_t bbm[4] = {0x7D, 0x3A, 0xE2, 0x42};
    for (int i = 3; i >= 0; i--) {
        Fixed::Sub& s = F.sub[i];
        if (s.length == 0) continue;
        p -= s.length;
        if (p < 0) return 0;
        for (int j = 0; j < s.length; j++) {
            s.blocks.push_back(buf[(size_t)p + (size_t)j]);
            if (s.blocks.size() == 4 && memcmp(s.blocks.data(), bbm, 4) != 0) s.blocks.erase(s.blocks.begin());   // not aligned yet: skip a byte
            if (s.blocks.size() == 255 + 4) {
                hdlcFeed(s.acc, s.open, &s.blocks[4], 255, i, channel);
                s.blocks.clear();
            }
        }
    }
    return (size_t)p;
}


}} // namespace dect2::hdr
