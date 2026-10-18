// Input streams of a DVB-S2 carrier: GSE in high-efficiency mode (packets sliced across BBFRAMEs, CRC-8 XOR 1) and a multistream carrier (ISI 0 the transport
// stream, ISI 1 GSE) through the whole chain, the HEM deframer on its own, and the stream table.
#include "dect2/dvbs/dvbs_gen.h"
#include "dect2/dvbs/dvbs_ip.h"
#include "dect2/dvbs/dvbs_rx.h"
#include "dect2/dvbs/dvbs_testkit.h"
#include <cstdio>
using namespace dect2;
using namespace dect2::dvbs;
#include "../common/test_util.h"

static DvbsTelemetry run(bool hem, bool mis, double secs) {
    DvbsSignalConfig c;
    c.tx.standard = 2; c.tx.mod = kQpsk; c.tx.rate = 5; c.tx.symbolRate = 2e6; c.tx.hem = hem; c.tx.mis = mis;
    c.sampleRate = 4e6; c.snrDb = s2QefEsN0(kQpsk, 5, false) + 4.0;
    dvbsip::GseEncapsulator enc;
    uint32_t n = 0;
    c.tx.gseField = [&](size_t bytes) {
        while (enc.queued() < 8) {
            const uint8_t src[4] = {10, 0, 0, (uint8_t)(n % 5)}, dst[4] = {192, 168, 7, 1};
            static const size_t lens[4] = {60, 900, 3000, 200};
            std::vector<uint8_t> payload(lens[n % 4]);
            for (size_t i = 0; i < payload.size(); i++) payload[i] = (uint8_t)(n * 31 + i * 7);
            dvbsip::Label l; l.type = 0; l.b = {0, 1, 2, 3, 4, (uint8_t)(n % 3)};
            enc.push(0x0800, l, dvbsip::buildUdpV4(src, dst, 4000, (uint16_t)(5000 + n % 4), payload, true));
            n++;
        }
        return hem ? enc.packets(bytes) : enc.fill(bytes);
    };
    uint64_t pn = 0;
    c.ts = [&pn](uint8_t* p) { testPacket(pn++, p); };
    DvbsSignal sig(c);
    DvbsReceiver rx;
    rx.setBlocking(true);
    rx.configure(c.sampleRate);
    std::vector<cf32> buf(65536);
    const size_t total = (size_t)(secs * c.sampleRate);
    for (size_t done = 0; done < total; done += buf.size()) {
        sig.generate(buf.data(), buf.size());
        for (auto& v : buf) { auto q = [](float x) { return std::round(std::min(127.f, std::max(-128.f, x * 128.f))) / 128.f; }; v = cf32(q(v.real()), q(v.imag())); }
        rx.feed(buf.data(), buf.size());
    }
    rx.flush();
    DvbsTelemetry t;
    rx.telemetry(t, 0);
    return t;
}

int main() {
    // the deframer on its own: slices across fields
    {
        dvbsip::GseEncapsulator enc;
        std::vector<std::vector<uint8_t>> sent;
        for (int k = 0; k < 40; k++) {
            static const size_t lens[7] = {40, 1500, 300, 9000, 64, 2, 700};
            std::vector<uint8_t> p(lens[k % 7]);
            for (size_t i = 0; i < p.size(); i++) p[i] = (uint8_t)(i * 31 + (size_t)k * 7);
            dvbsip::Label l; l.type = 0; l.b = {0, 6, 0x39, 0, 0, (uint8_t)k};
            enc.push(0x0800, l, p);
            sent.push_back(p);
        }
        std::vector<uint8_t> stream;
        std::vector<size_t> starts;
        while (enc.queued() > 0) {
            auto run = enc.packets(4096);
            size_t q = 0;
            while (q + 2 <= run.size()) { starts.push_back(stream.size() + q); q += 2 + (size_t)(((run[q] << 8) | run[q + 1]) & 0xFFF); }
            stream.insert(stream.end(), run.begin(), run.end());
        }
        dvbsip::HemDeframer d;
        dvbsip::GseDecoder dec;
        std::vector<dvbsip::Pdu> got;
        const size_t field = 1123;
        bool cross = false;
        for (size_t lo = 0; lo < stream.size(); lo += field) {
            const size_t n = std::min(field, stream.size() - lo);
            uint16_t syncd = 0xFFFF;
            for (size_t s : starts) if (s >= lo && s < lo + n) { syncd = (uint16_t)((s - lo) * 8); break; }
            if (syncd != 0) cross = true;
            std::vector<uint8_t> whole;
            d.dataField(&stream[lo], n, syncd, whole);
            dec.dataField(whole.data(), whole.size(), false, got);
        }
        bool same = got.size() == sent.size();
        for (size_t k = 0; same && k < sent.size(); k++) same = got[k].data == sent[k];
        CHECK(same && cross, "HEM: the sliced PDUs come back whole (%zu of %zu)", got.size(), sent.size());
        CHECK(d.stats.joined > 10 && d.stats.mismatches == 0, "HEM: joined %llu, mismatches %llu", (unsigned long long)d.stats.joined, (unsigned long long)d.stats.mismatches);
    }
    // GSE-HEM through the chain
    {
        const DvbsTelemetry t = run(true, false, 4.0);
        printf("  HEM: %s, %llu packets, streams %zu: %s\n", t.ip.source.c_str(), (unsigned long long)t.ip.packets, t.streams.size(), t.streams.empty() ? "" : t.streams[0].content.c_str());
        CHECK(t.standard == 2, "S2");
        CHECK(t.ip.packets > 100 && t.ip.ipv4 == t.ip.packets, "IP packets %llu", (unsigned long long)t.ip.packets);
        CHECK(t.streams.size() == 1 && t.streams[0].content == "GSE-HEM" && t.streams[0].hem, "the stream is GSE-HEM");
        CHECK(t.ip.source.find("HEM") != std::string::npos, "source: %s", t.ip.source.c_str());
    }
    // a multistream carrier
    {
        const DvbsTelemetry t = run(false, true, 5.0);
        printf("  MIS: %s, %llu IP packets, %llu TS packets, streams %zu\n", t.ip.source.c_str(), (unsigned long long)t.ip.packets, (unsigned long long)t.packets, t.streams.size());
        CHECK(t.streams.size() == 2, "two streams: %zu", t.streams.size());
        if (t.streams.size() == 2) {
            CHECK(t.streams[0].isi == 0 && t.streams[0].content == "transport stream" && t.streams[0].decoded && !t.streams[0].single, "ISI 0 is the transport stream, decoded");
            CHECK(t.streams[1].isi == 1 && t.streams[1].content == "GSE" && !t.streams[1].decoded, "ISI 1 is GSE");
            CHECK(t.streams[0].frames > 20 && t.streams[1].frames > 20 && t.streams[1].bps > 0, "frames and bit rates");
        }
        CHECK(t.packets > 200 && t.tsLock, "the transport stream of ISI 0 is decoded: %llu packets", (unsigned long long)t.packets);
        CHECK(t.ip.packets > 50, "and the IP of ISI 1 at the same time: %llu", (unsigned long long)t.ip.packets);
        CHECK(t.ip.source.find("ISI 1") != std::string::npos || t.ip.source.find("GSE") != std::string::npos, "source: %s", t.ip.source.c_str());
    }
    return testSummary("dvbs streams");
}
