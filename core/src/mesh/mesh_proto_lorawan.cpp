// LoRaWAN PHY payload headers (no keys, nothing decrypted): see mesh_proto.h.
// Facts: LoRaWAN L2 1.0.4 (TS001-1.0.4) chapters 4 (MAC frame formats) and 5 (MAC commands), LoRaWAN 1.1 (rejoin requests, the 1.1
// MAC commands), LoRaWAN Backend Interfaces 1.0 (TS002) 6.1 (NetID and DevAddr types), RP002-1.0.4 (EU863-870 and US902-928 channels).
#include "mesh_proto_internal.h"
#include <algorithm>
#include <cstring>

namespace dect2 {

namespace {

struct MacDef { uint8_t cid; const char* up; int upLen; const char* down; int downLen; };
// name and payload length of each command (-1: not sent in that direction)
const MacDef kMac[] = {
    {0x01, "ResetInd", 1, "ResetConf", 1},
    {0x02, "LinkCheckReq", 0, "LinkCheckAns", 2},
    {0x03, "LinkADRAns", 1, "LinkADRReq", 4},
    {0x04, "DutyCycleAns", 0, "DutyCycleReq", 1},
    {0x05, "RXParamSetupAns", 1, "RXParamSetupReq", 4},
    {0x06, "DevStatusAns", 2, "DevStatusReq", 0},
    {0x07, "NewChannelAns", 1, "NewChannelReq", 5},
    {0x08, "RXTimingSetupAns", 0, "RXTimingSetupReq", 1},
    {0x09, "TxParamSetupAns", 0, "TxParamSetupReq", 1},
    {0x0A, "DlChannelAns", 1, "DlChannelReq", 4},
    {0x0B, "RekeyInd", 1, "RekeyConf", 1},
    {0x0C, "ADRParamSetupAns", 0, "ADRParamSetupReq", 1},
    {0x0D, "DeviceTimeReq", 0, "DeviceTimeAns", 5},
    {0x0E, nullptr, -1, "ForceRejoinReq", 2},
    {0x0F, "RejoinParamSetupAns", 1, "RejoinParamSetupReq", 1},
    {0x10, "PingSlotInfoReq", 1, "PingSlotInfoAns", 0},
    {0x11, "PingSlotChannelAns", 1, "PingSlotChannelReq", 4},
    {0x12, "BeaconTimingReq", 0, "BeaconTimingAns", 3},
    {0x13, "BeaconFreqAns", 1, "BeaconFreqReq", 3},
    {0x20, "DeviceModeInd", 1, "DeviceModeConf", 1},
};
const MacDef* macDef(uint8_t cid) {
    for (const auto& m : kMac) if (m.cid == cid) return &m;
    return nullptr;
}

// NetIDs of a few big networks (LoRa Alliance NetID list). Type 0 unless said: the NwkID is the NetID.
struct NetDef { int type; uint32_t nwkId; const char* name; };
const NetDef kNets[] = {
    {0, 0x00, "Experimental / private"}, {0, 0x01, "Experimental / private"},
    {0, 0x02, "Actility"}, {0, 0x03, "Proximus"}, {0, 0x04, "Swisscom"}, {0, 0x07, "Bouygues Telecom"}, {0, 0x08, "Orbiwise"},
    {0, 0x09, "Senet"}, {0, 0x0A, "KPN"}, {0, 0x0B, "Everynet"}, {0, 0x0F, "SK Telecom"}, {0, 0x10, "Sagemcom"}, {0, 0x11, "Orange"},
    {0, 0x12, "A2A Smart City"}, {0, 0x13, "The Things Network"}, {0, 0x24, "Helium"},
    {6, 0x53, "Helium"},                     // NetID 0xC00053, DevAddr 0xFC01 4C00 - 0xFC01 4FFF
};
const int kNwkBits[8] = {6, 6, 9, 11, 12, 13, 15, 17};

// IEEE OUIs of common LoRaWAN device makers (the first three bytes of the DevEUI)
struct OuiDef { uint32_t oui; const char* name; };
const OuiDef kOuis[] = {
    {0xA84041, "Dragino"}, {0x0004A3, "Microchip"}, {0x24E124, "Milesight"}, {0xAC1F09, "RAKwireless"}, {0x0018B2, "Adeunis"},
    {0x00137A, "Netvox"}, {0x647FDA, "Tektelic"}, {0x58A0CB, "The Things Industries"}, {0x2CF7F1, "Seeed (SenseCAP)"},
    {0x0016C0, "Semtech"}, {0x0080E1, "STMicroelectronics"}, {0xA81758, "Elsys"},
    {0x70B3D5, "IEEE small-vendor block"},
};

uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
uint64_t le64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }
uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3]; }
void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
void putMic(std::vector<uint8_t>& v, uint32_t mic) { for (int i = 3; i >= 0; i--) v.push_back((uint8_t)(mic >> (8 * i))); }

// the FOpts MAC commands: names, and the values of the ones that say something about the device or the link
void macCommands(const uint8_t* p, size_t n, bool uplink, MeshDecodeResult::Lorawan& w) {
    size_t i = 0;
    while (i < n) {
        const uint8_t cid = p[i];
        if (cid >= 0x80) { w.macCommands.push_back(lorawanMacName(cid, uplink)); return; }   // proprietary: its length is unknown
        const MacDef* d = macDef(cid);
        const int len = !d ? -1 : uplink ? d->upLen : d->downLen;
        if (len < 0) { w.macCommands.push_back(lorawanMacName(cid, uplink) + " (unknown: 1.1 encrypted FOpts?)"); return; }
        if (i + 1 + (size_t)len > n) { w.macCommands.push_back(std::string(uplink ? d->up : d->down) + " (truncated)"); return; }
        w.macCommands.push_back(uplink ? d->up : d->down);
        const uint8_t* a = p + i + 1;
        std::string v;
        if (uplink && cid == 0x06) {
            const int margin = (a[1] & 0x20) ? (int)(a[1] & 0x3F) - 64 : (a[1] & 0x3F);
            w.devStatusBattery = a[0]; w.devStatusMargin = margin;
            v = a[0] == 0 ? meshFmt("external power, margin %d dB", margin) : a[0] == 255 ? meshFmt("battery unknown, margin %d dB", margin)
                          : meshFmt("battery %.0f %%, margin %d dB", a[0] * 100.0 / 254, margin);
        } else if (uplink && (cid == 0x03 || cid == 0x05 || cid == 0x07 || cid == 0x0A)) {
            v = meshFmt("%s 0x%02X", uplink ? d->up : d->down, a[0]);
        } else if (uplink && cid == 0x01) v = meshFmt("LoRaWAN 1.%d", a[0] & 15);
        else if (!uplink && cid == 0x02) v = meshFmt("margin %d dB, %d gateways", a[0], a[1]);
        else if (!uplink && cid == 0x03) v = meshFmt("DR%d, TX power %d, channel mask 0x%04X", a[0] >> 4, a[0] & 15, a[1] | a[2] << 8);
        else if (!uplink && cid == 0x0D) v = meshFmt("GPS time %u s", le32(a));
        if (!v.empty()) w.macDetail += (w.macDetail.empty() ? "" : ", ") + v;
        i += 1 + (size_t)len;
    }
}

} // namespace

const char* lorawanMTypeName(int t) {
    static const char* const n[] = {"Join request", "Join accept", "Unconfirmed up", "Unconfirmed down", "Confirmed up", "Confirmed down", "Rejoin request", "Proprietary"};
    return t >= 0 && t < 8 ? n[t] : "?";
}

std::string lorawanMacName(uint8_t cid, bool uplink) {
    if (cid >= 0x80) return meshFmt("proprietary 0x%02X", cid);
    const MacDef* d = macDef(cid);
    const char* nm = d ? (uplink ? d->up : d->down) : nullptr;
    return nm ? std::string(nm) : meshFmt("CID 0x%02X", cid);
}

std::string lorawanNetwork(uint32_t devAddr, int* typeOut, uint32_t* nwkOut, uint32_t* netOut) {
    int type = 0;
    while (type < 8 && (devAddr >> (31 - type) & 1)) type++;
    if (typeOut) *typeOut = type < 8 ? type : -1;
    if (type >= 8) return "reserved DevAddr";
    const int bits = kNwkBits[type];
    const uint32_t nwk = (devAddr >> (32 - (type + 1) - bits)) & ((1u << bits) - 1);
    const uint32_t net = type <= 2 ? ((uint32_t)type << 21 | nwk) : 0;
    if (nwkOut) *nwkOut = nwk;
    if (netOut) *netOut = net;
    for (const auto& d : kNets) if (d.type == type && d.nwkId == nwk) return d.name;
    if (type <= 2) return meshFmt("NetID 0x%06X", net);
    return meshFmt("NetID type %d, NwkID 0x%X", type, nwk);
}

const char* lorawanVendor(uint64_t eui) {
    if ((eui >> 28) == 0x70B3D57EDull) return "The Things Network block";
    const uint32_t oui = (uint32_t)(eui >> 40);
    for (const auto& o : kOuis) if (o.oui == oui) return o.name;
    return "";
}

std::string meshHexString(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) s += meshFmt(i ? " %02X" : "%02X", p[i]);
    return s;
}

std::string lorawanEui(uint64_t eui) { return meshFmt("%016llX", (unsigned long long)eui); }

MeshDecodeResult meshDecodeLorawan(const uint8_t* p, size_t n) {
    MeshDecodeResult r;
    r.packet.protocol = MeshProtocol::LoraWan;
    r.packet.size = n;
    if (!p || n < 1) { r.packet.note = "empty"; return r; }
    MeshDecodeResult::Lorawan& w = r.lorawan;
    w.mtype = p[0] >> 5;
    w.major = p[0] & 3;
    if (w.mtype != 7 && ((p[0] & 0x1C) || w.major != 0)) { r.packet.note = "not LoRaWAN R1 (RFU or major bits set)"; return r; }
    w.uplink = w.mtype == 0 || w.mtype == 2 || w.mtype == 4 || w.mtype == 6;
    w.confirmed = w.mtype == 4 || w.mtype == 5;
    MeshPacketInfo& pk = r.packet;
    pk.type = lorawanMTypeName(w.mtype);
    switch (w.mtype) {
    case 0: {   // MHDR JoinEUI(8) DevEUI(8) DevNonce(2) MIC(4), each little endian
        if (n != 23) { pk.note = meshFmt("join request of %zu bytes (23 expected)", n); return r; }
        w.joinEui = le64(p + 1); w.devEui = le64(p + 9); w.devNonce = (uint16_t)(p[17] | p[18] << 8); w.mic = be32(p + 19);
        w.vendor = lorawanVendor(w.devEui);
        pk.from = lorawanEui(w.devEui); pk.to = lorawanEui(w.joinEui);
        pk.detail = meshFmt("DevEUI %s%s, JoinEUI %s, DevNonce %u, MIC %08X", pk.from.c_str(), w.vendor.empty() ? "" : (" (" + w.vendor + ")").c_str(),
                            pk.to.c_str(), w.devNonce, w.mic);
        break;
    }
    case 1:     // encrypted with the device's key: only its length says 1.0 / 1.1 CFList or not
        if (n != 17 && n != 33) { pk.note = meshFmt("join accept of %zu bytes (17 or 33 expected)", n); return r; }
        w.mic = be32(p + n - 4);
        pk.detail = meshFmt("%zu bytes encrypted%s", n - 1, n == 33 ? " (with a channel list)" : "");
        pk.note = "encrypted";
        break;
    case 6: {   // 1.1: type 0 / 2: NetID(3) DevEUI(8) RJcount0(2); type 1: JoinEUI(8) DevEUI(8) RJcount1(2)
        if (n < 2) { pk.note = "short rejoin request"; return r; }
        w.rejoinType = p[1];
        if ((w.rejoinType == 0 || w.rejoinType == 2) && n == 19) {
            w.rejoinNetId = (uint32_t)p[2] | (uint32_t)p[3] << 8 | (uint32_t)p[4] << 16;
            w.devEui = le64(p + 5); w.devNonce = (uint16_t)(p[13] | p[14] << 8);
        } else if (w.rejoinType == 1 && n == 24) {
            w.joinEui = le64(p + 2); w.devEui = le64(p + 10); w.devNonce = (uint16_t)(p[18] | p[19] << 8);
            pk.to = lorawanEui(w.joinEui);
        } else { pk.note = meshFmt("rejoin request type %d of %zu bytes", w.rejoinType, n); return r; }
        w.mic = be32(p + n - 4);
        w.vendor = lorawanVendor(w.devEui);
        pk.from = lorawanEui(w.devEui);
        pk.detail = meshFmt("type %d, DevEUI %s%s, RJcount %u, MIC %08X", w.rejoinType, pk.from.c_str(), w.vendor.empty() ? "" : (" (" + w.vendor + ")").c_str(), w.devNonce, w.mic);
        break;
    }
    case 7:     // proprietary: the layout after MHDR is the network's own
        pk.detail = meshFmt("%zu bytes, layout unknown", n - 1);
        pk.note = "proprietary";
        break;
    default: {  // data: MHDR DevAddr(4) FCtrl(1) FCnt(2) FOpts(0..15) [FPort(1) FRMPayload] MIC(4)
        if (n < 12) { pk.note = meshFmt("data frame of %zu bytes (12 at least)", n); return r; }
        w.devAddr = le32(p + 1);
        const uint8_t fc = p[5];
        w.adr = fc & 0x80; w.ack = fc & 0x20; w.foptsLen = fc & 15;
        if (w.uplink) { w.adrAckReq = fc & 0x40; w.classB = fc & 0x10; } else w.fpending = fc & 0x10;
        w.fcnt = (uint16_t)(p[6] | p[7] << 8);
        if ((size_t)(8 + w.foptsLen + 4) > n) { pk.note = meshFmt("FOptsLen %d does not fit %zu bytes", w.foptsLen, n); return r; }
        if (n > (size_t)(8 + w.foptsLen + 4)) {
            w.fport = p[8 + w.foptsLen];
            w.frmLen = n - (size_t)(8 + w.foptsLen + 1 + 4);
        }
        w.mic = be32(p + n - 4);
        macCommands(p + 8, (size_t)w.foptsLen, w.uplink, w);
        w.network = lorawanNetwork(w.devAddr, &w.netIdType, &w.nwkId, &w.netId);
        pk.from = meshFmt("%08X", w.devAddr);
        pk.packetId = w.fcnt;
        std::string d;
        auto add = [&](const std::string& s) { d += (d.empty() ? "" : ", ") + s; };
        if (w.adr) add("ADR");
        if (w.adrAckReq) add("ADRACKReq");
        if (w.ack) add("ACK");
        if (w.fpending) add("FPending");
        if (w.classB) add("class B");
        for (const auto& m : w.macCommands) add(m);
        if (!w.macDetail.empty()) add(w.macDetail);
        if (w.fport == 0) add(meshFmt("MAC commands in the payload, %zu bytes encrypted", w.frmLen));
        else if (w.fport > 0) add(meshFmt("FPort %d, %zu bytes encrypted", w.fport, w.frmLen));
        if (w.fport == 0 && w.foptsLen > 0) add("FOpts with FPort 0 (not allowed)");
        add(meshFmt("MIC %08X", w.mic));
        pk.detail = d;
        pk.note = w.fport >= 0 ? "payload encrypted" : "no payload";
        break;
    }
    }
    r.ok = true;
    r.hasLorawan = true;
    pk.decrypted = false;
    return r;
}

std::vector<MeshLoraSettings> lorawanPresets(int region) {
    std::vector<MeshLoraSettings> v;
    auto add = [&](double mhz, int sf, double bw) {
        MeshLoraSettings s;
        s.name = meshFmt("LoRaWAN %.1f", mhz);
        s.freqHz = mhz * 1e6; s.sf = sf; s.bwHz = bw; s.cr = 5; s.preamble = 8; s.syncWord = 0x34;
        s.ldro = meshLoraLdro(sf, bw);
        v.push_back(s);
    };
    if (region == 1) {
        for (int c = 0; c < 64; c++) for (int sf = 7; sf <= 10; sf++) add(902.3 + 0.2 * c, sf, 125e3);   // DR0 - DR3
        for (int c = 0; c < 8; c++) add(903.0 + 1.6 * c, 8, 500e3);                                        // DR4
    } else {
        static const double eu[] = {868.1, 868.3, 868.5, 867.1, 867.3, 867.5, 867.7, 867.9};
        for (double f : eu) for (int sf = 7; sf <= 12; sf++) add(f, sf, 125e3);                            // DR5 - DR0
        add(868.3, 7, 250e3);                                                                              // DR6
    }
    return v;
}

std::vector<uint8_t> lorawanBuild(const LorawanTx& t) {
    std::vector<uint8_t> v;
    v.push_back((uint8_t)((t.mtype & 7) << 5));
    if (t.mtype == 7) { v.insert(v.end(), t.frm.begin(), t.frm.end()); putMic(v, t.mic); return v; }
    const bool up = t.mtype == 2 || t.mtype == 4;
    put32(v, t.devAddr);
    const size_t fl = std::min<size_t>(15, t.fopts.size());
    v.push_back((uint8_t)((t.adr ? 0x80 : 0) | (up && t.adrAckReq ? 0x40 : 0) | (t.ack ? 0x20 : 0) | (!up && t.fpending ? 0x10 : 0) | fl));
    v.push_back((uint8_t)(t.fcnt & 0xFF)); v.push_back((uint8_t)(t.fcnt >> 8));
    v.insert(v.end(), t.fopts.begin(), t.fopts.begin() + (long)fl);
    if (t.fport >= 0) { v.push_back((uint8_t)t.fport); v.insert(v.end(), t.frm.begin(), t.frm.end()); }
    putMic(v, t.mic);
    return v;
}

std::vector<uint8_t> lorawanJoinRequest(uint64_t joinEui, uint64_t devEui, uint16_t devNonce, uint32_t mic) {
    std::vector<uint8_t> v;
    v.push_back(0x00);
    for (int i = 0; i < 8; i++) v.push_back((uint8_t)(joinEui >> (8 * i)));
    for (int i = 0; i < 8; i++) v.push_back((uint8_t)(devEui >> (8 * i)));
    v.push_back((uint8_t)(devNonce & 0xFF)); v.push_back((uint8_t)(devNonce >> 8));
    putMic(v, mic);
    return v;
}

} // namespace dect2
