// Shared by tests/mesh/test_mesh_proto_rs.cpp and rs_part2_mesh.cpp: the real mesh layer (dect2::, Rust-backed under DECT2_RUST_MESH) and its frozen C++
// copy (dect2_mesh_ref::, cmake/gen_mesh_ref.cmake) are compared field by field, doubles bit for bit.
#pragma once
#include "dect2/mesh/mesh_proto.h"
#include "mesh_proto_ref.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace A = dect2;
namespace R = dect2_mesh_ref;

extern long g_fails, g_checks;
extern std::mt19937 g;
#define SAME(c, ...) do { g_checks++; if (!(c)) { if (g_fails++ < 25) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)

inline bool dEq(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }
inline uint32_t r32() { return g(); }
inline uint8_t rb() { return (uint8_t)g(); }
inline long iterations() { const char* e = getenv("ONAIR_DIFF_ITERS"); return e ? atol(e) : 100000; }

// Compares two decode results of the two namespaces (all fields of MeshDecodeResult).
template <class X, class Y> bool nodeEq(const X& a, const Y& b) {
    return (int)a.protocol == (int)b.protocol && a.nodeId == b.nodeId && a.longName == b.longName && a.shortName == b.shortName && a.hwModel == b.hwModel && a.role == b.role &&
           a.hasPosition == b.hasPosition && dEq(a.lat, b.lat) && dEq(a.lon, b.lon) && dEq(a.altM, b.altM) && dEq(a.batteryPct, b.batteryPct) && dEq(a.voltage, b.voltage) &&
           a.hasEnv == b.hasEnv && dEq(a.tempC, b.tempC) && dEq(a.humidity, b.humidity) && dEq(a.pressureHpa, b.pressureHpa) && a.publicKey == b.publicKey &&
           a.positionTime == b.positionTime && dEq(a.channelUtilPct, b.channelUtilPct) && dEq(a.airUtilTxPct, b.airUtilTxPct) && a.uptimeS == b.uptimeS && a.sats == b.sats &&
           a.signatureChecked == b.signatureChecked && a.signatureOk == b.signatureOk;
}
template <class X, class Y> bool msgEq(const X& a, const Y& b) {
    return (int)a.protocol == (int)b.protocol && a.channel == b.channel && a.from == b.from && a.to == b.to && a.text == b.text && a.hops == b.hops && a.packetId == b.packetId &&
           a.replyId == b.replyId && a.senderTime == b.senderTime;
}
template <class X, class Y> bool resEq(const X& a, const Y& b) {
    const auto& p = a.packet;
    const auto& q = b.packet;
    if (a.ok != b.ok || (int)p.protocol != (int)q.protocol || p.type != q.type || p.from != q.from || p.to != q.to || p.packetId != q.packetId || p.hopLimit != q.hopLimit ||
        p.hopStart != q.hopStart || p.channel != q.channel || p.decrypted != q.decrypted || p.note != q.note || p.size != q.size || p.detail != q.detail || p.portnum != q.portnum ||
        p.channelHash != q.channelHash || p.wantAck != q.wantAck || p.viaMqtt != q.viaMqtt || p.nextHop != q.nextHop || p.relayNode != q.relayNode || p.routeType != q.routeType ||
        p.payloadType != q.payloadType || p.payloadVersion != q.payloadVersion || p.pathHashCount != q.pathHashCount || p.pathHashSize != q.pathHashSize || p.path != q.path)
        return false;
    if (a.nodes.size() != b.nodes.size() || a.messages.size() != b.messages.size()) return false;
    for (size_t i = 0; i < a.nodes.size(); i++) if (!nodeEq(a.nodes[i], b.nodes[i])) return false;
    for (size_t i = 0; i < a.messages.size(); i++) if (!msgEq(a.messages[i], b.messages[i])) return false;
    if (a.hasAprs != b.hasAprs || a.aprsSource != b.aprsSource || a.aprsDest != b.aprsDest || a.aprsPath != b.aprsPath || a.aprsInfo != b.aprsInfo || a.batteryPct != b.batteryPct ||
        a.raw != b.raw || a.aprs.type != b.aprs.type || a.aprs.summary != b.aprs.summary || a.aprs.comment != b.aprs.comment || a.aprs.text != b.aprs.text ||
        a.aprs.name != b.aprs.name || a.aprs.hasPos != b.aprs.hasPos || !dEq(a.aprs.lat, b.aprs.lat) || !dEq(a.aprs.lon, b.aprs.lon))
        return false;
    const auto& x = a.lorawan;
    const auto& y = b.lorawan;
    return a.hasLorawan == b.hasLorawan && x.mtype == y.mtype && x.major == y.major && x.uplink == y.uplink && x.confirmed == y.confirmed && x.devAddr == y.devAddr &&
           x.netIdType == y.netIdType && x.nwkId == y.nwkId && x.netId == y.netId && x.network == y.network && x.adr == y.adr && x.adrAckReq == y.adrAckReq && x.ack == y.ack &&
           x.fpending == y.fpending && x.classB == y.classB && x.foptsLen == y.foptsLen && x.fcnt == y.fcnt && x.macCommands == y.macCommands && x.macDetail == y.macDetail &&
           x.devStatusBattery == y.devStatusBattery && x.devStatusMargin == y.devStatusMargin && x.fport == y.fport && x.frmLen == y.frmLen && x.mic == y.mic &&
           x.joinEui == y.joinEui && x.devEui == y.devEui && x.devNonce == y.devNonce && x.rejoinType == y.rejoinType && x.rejoinNetId == y.rejoinNetId && x.vendor == y.vendor;
}

// Random protobuf: n fields with numbers from `nums` (or any), wire types 0 / 1 / 2 / 5 mostly, values sometimes extreme; `garbage` adds invalid bytes.
std::vector<uint8_t> randProto(int fields, const std::vector<int>& nums, int garbagePct);
std::vector<uint8_t> mutateBytes(std::vector<uint8_t> v);
int runMeshProto2();
