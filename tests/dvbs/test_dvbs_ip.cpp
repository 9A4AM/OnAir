// IP over DVB-S/S2: GSE round trips for every variant with fragmentation, the variant picked from the data, a link without IP, ciphertext recognised, a vendor CRC
// trailer, a lost fragment, bridged Ethernet, the blind search, IP fragments, flow statistics and the PCAP file. The tests follow those of DecSAT (Arclamp,
// GPL-3.0-or-later), whose crates this code is adapted from.
#include "dect2/dvbs/dvbs_ip.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
using namespace dect2;
using namespace dect2::dvbsip;
#include "../common/test_util.h"

static std::vector<std::vector<uint8_t>> pdus(int n, uint64_t seed) {
    uint64_t s = seed | 1;
    std::vector<std::vector<uint8_t>> out;
    static const size_t lens[6] = {40, 1500, 300, 9000, 64, 2};
    for (int k = 0; k < n; k++) {
        std::vector<uint8_t> v(lens[k % 6]);
        for (auto& b : v) { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; b = (uint8_t)((s * 0x2545F4914F6CDD1Dull) >> 56); }
        out.push_back(v);
    }
    return out;
}

static std::vector<std::vector<uint8_t>> traffic(int n) {
    std::vector<std::vector<uint8_t>> v;
    static const size_t lens[4] = {20, 1400, 300, 5000};
    for (int k = 0; k < n; k++) {
        const uint8_t src[4] = {10, 0, 0, (uint8_t)(k % 7)}, dst[4] = {192, 168, 1, 1};
        v.push_back(buildUdpV4(src, dst, 4000, (uint16_t)(5000 + k), std::vector<uint8_t>(lens[k % 4], (uint8_t)k)));
    }
    return v;
}

int main() {
    // round trip for every variant, with fragmentation across data fields
    {
        const auto sent = pdus(30, 7);
        for (const Variant& v : kVariants) {
            GseEncapsulator enc(v);
            for (size_t k = 0; k < sent.size(); k++) {
                Label l;
                if (k % 3 == 0) { l.type = 0; l.b = {1, 2, 3, 4, 5, (uint8_t)k}; } else if (k % 3 == 1) { l.type = 1; l.b = {9, 8, (uint8_t)k, 0, 0, 0}; }
                enc.push(0x0800, l, sent[k]);
            }
            GseDecoder dec(v);
            std::vector<Pdu> got;
            while (enc.queued() > 0) { auto f = enc.fill(869); CHECK(f.size() == 869, "field length"); dec.dataField(f.data(), f.size(), false, got); }
            bool same = got.size() == sent.size();
            for (size_t k = 0; same && k < sent.size(); k++) same = got[k].data == sent[k];
            CHECK(same, "%s: the PDUs come back (%zu of %zu)", v.label(), got.size(), sent.size());
            bool crc = true;
            for (const auto& p : got) if (p.crcOk == 0) crc = false;
            CHECK(crc && dec.stats.reassembled > 0, "%s: CRC-32 of fragmented PDUs", v.label());
        }
    }
    // the variant is picked from the data
    {
        const auto sent = traffic(60);
        for (const Variant& w : kVariants) {
            GseEncapsulator enc(w);
            Label l; l.type = 0; l.b = {0, 1, 2, 3, 4, 5};
            for (const auto& p : sent) enc.push(0x0800, l, p);
            GseIp g;
            std::vector<IpPacket> got;
            while (enc.queued() > 0) { auto f = enc.fill(869); g.dataField(f.data(), f.size(), got); }
            const int ch = g.chosen();
            CHECK(ch >= 0 && kVariants[ch].length == w.length, "%s: chosen %d", w.label(), ch);
            int found = 0;
            for (size_t k = sent.size() - 10; k < sent.size(); k++) for (const auto& p : got) if (p.data == sent[k]) { found++; break; }
            CHECK(found == 10, "%s: the last ten packets arrive (%d)", w.label(), found);
            CHECK(g.labels().size() == 1 && g.labels().begin()->second.ip > 50, "labels: one terminal with its IP packets");
        }
    }
    // a link with no IP says what it carries
    {
        GseEncapsulator enc;
        for (int k = 0; k < 40; k++) enc.push(0x0005, Label(), std::vector<uint8_t>(627, (uint8_t)(0x81 ^ k)));
        GseIp g;
        std::vector<IpPacket> got;
        while (enc.queued() > 0) { auto f = enc.fill(633); g.dataField(f.data(), f.size(), got); }
        CHECK(got.empty() && g.chosen() < 0, "no IP");
        CHECK(g.otherProtocols().count(0x0005) && g.otherProtocols().at(0x0005) >= 30, "type 0005h PDUs counted");
        CHECK(!g.otherEncrypted(), "repeated bytes are a format, not ciphertext");
    }
    // random PDUs are called encrypted (and never shown)
    {
        uint64_t x = 0x2545F4914F6CDD1Dull;
        GseEncapsulator enc;
        for (int k = 0; k < 80; k++) {
            std::vector<uint8_t> p{0x81};
            for (int i = 0; i < 626; i++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; p.push_back((uint8_t)(x >> 24)); }
            enc.push(0x0005, Label(), p);
        }
        GseIp g;
        std::vector<IpPacket> got;
        int judged = 0, fields = 0;
        while (enc.queued() > 0) { auto f = enc.fill(633); g.dataField(f.data(), f.size(), got); fields++; if (!judged && g.otherEntropy() > 0) judged = fields; }
        CHECK(g.otherEncrypted(), "%.3f bits per byte", g.otherEntropy());
        CHECK(judged > 0 && judged <= 60, "judged after %d fields", judged);
    }
    // a vendor CRC trailer
    {
        auto vendorPacket = [](bool s, bool e, int lt, const std::vector<uint8_t>& body) {
            const unsigned len = (unsigned)body.size() + 2;
            const unsigned h = (s ? 0x8000u : 0) | (e ? 0x4000u : 0) | ((unsigned)lt << 12) | len;
            std::vector<uint8_t> p{(uint8_t)(h >> 8), (uint8_t)h};
            p.insert(p.end(), body.begin(), body.end());
            return p;
        };
        auto vendorFrame = [](uint8_t sync, const std::vector<std::vector<uint8_t>>& pk) {
            std::vector<uint8_t> field;
            for (const auto& p : pk) field.insert(field.end(), p.begin(), p.end());
            const unsigned dfl = (unsigned)((field.size() + 4) * 8);
            std::vector<uint8_t> f{0x42, 0, 0, 0, (uint8_t)(dfl >> 8), (uint8_t)dfl, sync, 0, 0, 0};
            f.insert(f.end(), field.begin(), field.end());
            const uint32_t c = crc32Msb(f.data(), f.size(), 0);
            for (int sh = 24; sh >= 0; sh -= 8) f.push_back((uint8_t)(c >> sh));
            return f;
        };
        const uint8_t label[6] = {0x00, 0xff, 0xff, 0x10, 0x0c, 0x10};
        std::vector<uint8_t> table;
        for (uint32_t k = 0; k < 300; k++) table.push_back((uint8_t)(k * 7));
        auto clock = [&](uint64_t t) { std::vector<uint8_t> b{0, 1}; for (int sh = 56; sh >= 0; sh -= 8) b.push_back((uint8_t)(t >> sh)); return vendorPacket(true, true, 2, b); };
        std::vector<uint8_t> start{0xfc};
        const unsigned total = (unsigned)(2 + 6 + table.size()) - 9;
        start.push_back((uint8_t)(total >> 8)); start.push_back((uint8_t)total); start.push_back(0); start.push_back(2);
        start.insert(start.end(), label, label + 6);
        start.insert(start.end(), table.begin(), table.begin() + 100);
        std::vector<uint8_t> end{0xfd};
        end.insert(end.end(), table.begin() + 100, table.end());
        end.push_back(0xf7); end.push_back(0x6f); end.push_back(0xfe); end.push_back(0x85);
        const std::vector<std::vector<uint8_t>> frames = {vendorFrame(0, {clock(1000), vendorPacket(true, false, 0, start)}), vendorFrame(1, {vendorPacket(false, true, 0, end), clock(2000)})};
        for (const auto& f : frames) CHECK(GseIp::frameTrailer(f.data(), f.size()), "frame trailer");
        GseIp g;
        std::vector<IpPacket> got;
        for (const auto& f : frames) g.bbframe(f.data(), f.size(), got);
        CHECK(g.trailers() == 2 && got.empty(), "two trailers, no IP");
        CHECK(g.otherProtocols().count(1) && g.otherProtocols().at(1) == 2 && g.otherProtocols().count(2) && g.otherProtocols().at(2) == 1, "the clock PDUs and the table PDU");
        // unchecked, the same PDU has nothing to vouch for it
        GseDecoder d(kVariants[3]);
        std::vector<Pdu> pd;
        for (const auto& f : frames) d.dataField(f.data() + 10, f.size() - 14, false, pd);
        CHECK(d.stats.crcBad == 1, "unchecked: bad CRC %llu", (unsigned long long)d.stats.crcBad);
    }
    // bridged Ethernet
    {
        const uint8_t a[4] = {1, 2, 3, 4}, b[4] = {5, 6, 7, 8};
        auto ip = buildUdpV4(a, b, 1, 2, {'x'});
        std::vector<uint8_t> fr(12, 0xAA);
        fr.push_back(8); fr.push_back(0);
        fr.insert(fr.end(), ip.begin(), ip.end());
        Pdu p; p.protocol = 1; p.data = fr;
        const uint8_t* got; IpInfo info;
        CHECK(GseIp::ipOf(p, got, info) && info.len == ip.size() && !memcmp(got, ip.data(), ip.size()), "bridged frame gives its IP");
    }
    // blind search: IP packets laid raw into the data fields
    {
        const auto sent = traffic(8);
        GseIp g;
        std::vector<IpPacket> got;
        for (int i = 0; i < 52; i++) {
            std::vector<uint8_t> f(3, 0x5A);
            f.insert(f.end(), sent[0].begin(), sent[0].end());
            f.resize(2000, 0x5A);
            g.dataField(f.data(), f.size(), got);
        }
        CHECK(g.source() == 2 && !got.empty(), "blind search takes over");
        bool all = true;
        for (const auto& p : got) if (p.data != sent[0]) all = false;
        CHECK(all, "blind packets are the sent one");
    }
    // MPE and ULE in the transport stream
    {
        auto all = traffic(16);
        std::vector<std::vector<uint8_t>> sent;
        for (const auto& d : all) if (d.size() < 3000) sent.push_back(d);   // a section holds at most 4096 bytes
        // MPE: a PES PID alongside must be left alone; scrambled sections are skipped
        std::vector<std::vector<uint8_t>> ts;
        uint8_t cc = 0;
        const uint8_t mac[6] = {1, 0, 0x5E, 1, 1, 1};
        for (const auto& d : sent) for (auto& p : packetizeSection(0x0FA0, cc, buildMpeSection(mac, d))) ts.push_back(p);
        std::vector<uint8_t> pes(188, 0);
        pes[0] = 0x47; pes[1] = 0x41; pes[2] = 0x00; pes[3] = 0x10; pes[6] = 1;
        MpeExtractor mpe;
        std::vector<std::vector<uint8_t>> got;
        mpe.packet(pes.data(), got);
        for (const auto& p : ts) mpe.packet(p.data(), got);
        CHECK(got == sent, "MPE: the datagrams come back (%zu of %zu)", got.size(), sent.size());
        CHECK(mpe.stats.pids.count(0x0FA0) && !mpe.stats.pids.count(0x0100), "MPE: PID found, the PES PID is not");
        // a scrambled section is skipped, a damaged one counted
        {
            MpeExtractor m2;
            auto sec = buildMpeSection(mac, sent[0]);
            sec[5] = (uint8_t)(sec[5] | 0x10);   // payload scrambling control
            const uint32_t c = crc32Mpeg2(sec.data(), sec.size() - 4);
            for (int i = 0; i < 4; i++) sec[sec.size() - 4 + (size_t)i] = (uint8_t)(c >> (24 - 8 * i));
            std::vector<std::vector<uint8_t>> o2;
            uint8_t c2 = 0;
            for (auto& p : packetizeSection(0x100, c2, sec)) m2.packet(p.data(), o2);
            CHECK(o2.empty() && m2.stats.skipped == 1, "MPE: a scrambled payload is skipped");
            auto bad = buildMpeSection(mac, sent[0]);
            bad[20] ^= 0xFF;
            for (auto& p : packetizeSection(0x100, c2, bad)) m2.packet(p.data(), o2);
            CHECK(o2.empty() && m2.stats.badSections == 1, "MPE: a bad CRC is counted");
        }
        // ULE: IPv4 with and without a destination address, IPv6-less, bridged Ethernet, mixed with an unknown type; spans TS packets
        std::vector<std::vector<uint8_t>> sndus;
        const uint8_t npa[6] = {0, 1, 2, 3, 4, 5};
        for (size_t k = 0; k < sent.size(); k++) sndus.push_back(buildUleSndu(0x0800, sent[k], k % 2 ? npa : nullptr));
        uint8_t cu = 3;
        UleExtractor ule;
        std::vector<std::vector<uint8_t>> ug;
        for (const auto& p : packetizeUle(0x0123, cu, sndus)) ule.packet(p.data(), ug);
        // the first two SNDUs are used to accept the PID: the rest arrive
        CHECK(ug.size() == sent.size() - 2, "ULE: %zu of %zu datagrams (two go to learning the PID)", ug.size(), sent.size());
        bool same = true;
        for (size_t k = 0; k < ug.size(); k++) if (ug[k] != sent[k + 2]) same = false;
        CHECK(same && ule.stats.badCrc == 0 && ule.stats.pids.count(0x0123), "ULE: the datagrams are the ones sent, no CRC errors");
        // video is not taken for ULE
        UleExtractor u2;
        std::vector<std::vector<uint8_t>> o3;
        for (int i = 0; i < 100; i++) { std::vector<uint8_t> v(188, (uint8_t)(i * 13)); v[0] = 0x47; v[1] = (uint8_t)(i % 7 ? 0x01 : 0x41); v[2] = 0x00; v[3] = (uint8_t)(0x10 | (i & 15)); if (!(i % 7)) { v[4] = 0; v[5] = 0; v[6] = 1; } u2.packet(v.data(), o3); }
        CHECK(o3.empty() && u2.stats.ipDatagrams == 0, "ULE: payload of a PES PID is not parsed");
    }
    // IP fragments in any order, statistics, PCAP
    {
        std::vector<uint8_t> payload(2300);
        for (size_t i = 0; i < payload.size(); i++) payload[i] = (uint8_t)(i * 7);
        const uint8_t a[4] = {10, 0, 0, 1}, b[4] = {239, 1, 1, 1};
        auto d = buildUdpV4(a, b, 5004, 6002, payload, true);
        d[4] = 0; d[5] = 77; d[10] = d[11] = 0;
        const uint16_t c = inetChecksum(d.data(), 20); d[10] = (uint8_t)(c >> 8); d[11] = (uint8_t)c;
        auto fr = fragmentIpv4(d, 1500);
        CHECK(fr.size() == 2, "two fragments");
        IpSink sink;
        const std::string path = "/tmp/dvbs_ip_test.pcap";
        CHECK(sink.pcap.open(path), "pcap opens");
        sink.push(fr[1].data(), fr[1].size(), 1.5);
        CHECK(sink.stats.packets == 0, "incomplete: nothing yet");
        sink.push(fr[0].data(), fr[0].size(), 2.5);
        CHECK(sink.stats.packets == 1 && sink.frag.reassembled == 1, "reassembled");
        auto top = sink.stats.topFlows(5);
        CHECK(top.size() == 1 && top[0].key.dport == 6002 && top[0].key.sport == 5004 && top[0].udpChecked == 1 && top[0].udpBad == 0, "flow with ports and a good UDP checksum");
        CHECK(sink.stats.udpPorts[6002] == 1 && sink.stats.protocols[17] == 1, "port and protocol counts");
        sink.pcap.close();
        std::ifstream f(path, std::ios::binary);
        std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(file.size() == 24 + 16 + d.size(), "pcap size %zu", file.size());
        CHECK(file.size() > 40 && file[0] == 0xD4 && file[1] == 0xC3 && file[2] == 0xB2 && file[3] == 0xA1 && file[20] == 101, "pcap header: little endian, link type RAW");
        CHECK(file.size() > 40 && memcmp(&file[40], d.data(), d.size()) == 0, "the packet in the file is the reassembled datagram");
        std::remove(path.c_str());
        // a flow whose UDP checksums fail is marked encrypted
        IpStats s;
        auto good = buildUdpV4(a, b, 1028, 50000, std::vector<uint8_t>(64, 7), true);
        for (int i = 0; i < 20; i++) { auto bad = good; bad[40] ^= 0x5A; IpInfo inf; parseIp(bad.data(), bad.size(), inf); s.add(inf, udpChecksumOk(bad.data(), inf)); }
        CHECK(s.encryptedFlows() == 1, "encrypted flow noticed");
        auto damaged = good; damaged[15] ^= 1;
        IpInfo inf;
        CHECK(!parseIp(damaged.data(), damaged.size(), inf) && !parseIp(good.data(), good.size() - 1, inf) && parseIp(good.data(), good.size(), inf), "validation catches a damaged header and a short packet");
    }
    return testSummary("dvbs ip");
}
