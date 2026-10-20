// Service information beyond the multiplex received: the transponder lists of the NIT (satellite, cable and terrestrial delivery descriptors), other networks, bouquets,
// the services of other transport streams, event counts of other transport streams, local time offsets and the CAT; through the TS demultiplexer from TS packets.
#include "dect2/dvbs/dvbs_ip.h"
#include "dect2/tv/si_tables.h"
#include "dect2/tv/ts.h"
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace dect2;
#include "../common/test_util.h"

static std::vector<uint8_t> bcdNum(uint64_t v, int digits) {   // digits BCD digits, big endian, padded to whole bytes
    std::vector<uint8_t> o((size_t)(digits + 1) / 2, 0);
    for (int i = digits - 1; i >= 0; i--) { const int d = (int)(v % 10); v /= 10; if (i & 1) o[(size_t)i / 2] |= (uint8_t)d; else o[(size_t)i / 2] |= (uint8_t)(d << 4); }
    return o;
}
static std::vector<uint8_t> desc(int tag, const std::vector<uint8_t>& b) { std::vector<uint8_t> o{(uint8_t)tag, (uint8_t)b.size()}; o.insert(o.end(), b.begin(), b.end()); return o; }
static void cat(std::vector<uint8_t>& a, const std::vector<uint8_t>& b) { a.insert(a.end(), b.begin(), b.end()); }
static std::vector<uint8_t> section(int tid, int ext, const std::vector<uint8_t>& body, int secNum = 0) {   // long-form section with CRC-32
    std::vector<uint8_t> s{(uint8_t)tid, 0, 0, (uint8_t)(ext >> 8), (uint8_t)ext, 0xC1, (uint8_t)secNum, (uint8_t)secNum};
    cat(s, body);
    const size_t len = s.size() - 3 + 4;
    s[1] = (uint8_t)(0xB0 | (len >> 8)); s[2] = (uint8_t)len;
    const uint32_t c = dvbsip::crc32Mpeg2(s.data(), s.size());
    for (int sh = 24; sh >= 0; sh -= 8) s.push_back((uint8_t)(c >> sh));
    return s;
}
static std::vector<uint8_t> satDesc(double mhz, double orbit, bool east, int pol, bool s2, int roll, int mod, double ksym, int fec) {
    std::vector<uint8_t> b = bcdNum((uint64_t)std::llround(mhz * 100), 8);
    cat(b, bcdNum((uint64_t)std::llround(orbit * 10), 4));
    b.push_back((uint8_t)((east ? 0x80 : 0) | (pol << 5) | (roll << 3) | (s2 ? 4 : 0) | mod));
    auto sr = bcdNum((uint64_t)std::llround(ksym * 10), 7);   // 28 bits
    b.push_back(sr[0]); b.push_back(sr[1]); b.push_back(sr[2]);
    b.push_back((uint8_t)((sr[3] & 0xF0) | fec));
    return desc(0x43, b);
}
static void feed(TsDemux& d, uint16_t pid, const std::vector<uint8_t>& sec, uint8_t& cc) { for (auto& p : dvbsip::packetizeSection(pid, cc, sec)) d.feed(p.data()); }

int main() {
    TsDemux d;
    uint8_t cc10 = 0, cc11 = 0, cc12 = 0, cc14 = 0, cc1 = 0;
    // NIT actual: network 0x3201 "Test Net", transponders at 19.2E and 13.0E
    {
        std::vector<uint8_t> body;
        std::vector<uint8_t> nd = desc(0x40, {'T', 'e', 's', 't', ' ', 'N', 'e', 't'});
        body.push_back((uint8_t)(0xF0 | (nd.size() >> 8))); body.push_back((uint8_t)nd.size()); cat(body, nd);
        std::vector<uint8_t> loop;
        auto ts = [&](int tsid, int onid, const std::vector<uint8_t>& ds) { loop.push_back((uint8_t)(tsid >> 8)); loop.push_back((uint8_t)tsid); loop.push_back((uint8_t)(onid >> 8)); loop.push_back((uint8_t)onid); loop.push_back((uint8_t)(0xF0 | (ds.size() >> 8))); loop.push_back((uint8_t)ds.size()); cat(loop, ds); };
        std::vector<uint8_t> d1 = satDesc(11494.0, 19.2, true, 0, false, 0, 1, 22000, 3);
        cat(d1, desc(0x41, {0x03, 0xE9, 0x01, 0x03, 0xEA, 0x02}));
        ts(1019, 1, d1);
        ts(1020, 1, satDesc(12188.0, 13.0, true, 1, true, 1, 2, 27500, 4));   // 12188 MHz, vertical, DVB-S2 8PSK
        // a cable and a terrestrial transponder in the same table
        std::vector<uint8_t> cab2 = bcdNum(3140000, 8);
        cab2.push_back(0xFF); cab2.push_back(0xF0);          // reserved 12 bits + FEC_outer 0
        cab2.push_back(3);                                    // 64-QAM
        { auto sr = bcdNum(69000, 7); cab2.push_back(sr[0]); cab2.push_back(sr[1]); cab2.push_back(sr[2]); cab2.push_back((uint8_t)((sr[3] & 0xF0) | 0)); }
        ts(2001, 2, desc(0x44, cab2));
        std::vector<uint8_t> terr{(uint8_t)(0x0000), 0, 0, 0};
        { const uint32_t f = 69000000; terr = {(uint8_t)(f >> 24), (uint8_t)(f >> 16), (uint8_t)(f >> 8), (uint8_t)f}; }   // 690 MHz in units of 10 Hz
        terr.push_back(0x00); for (int i = 0; i < 6; i++) terr.push_back(0);
        ts(3001, 3, desc(0x5A, terr));
        body.push_back((uint8_t)(0xF0 | (loop.size() >> 8))); body.push_back((uint8_t)loop.size()); cat(body, loop);
        feed(d, 0x10, section(0x40, 0x3201, body), cc10);
        // another network (table 0x41)
        std::vector<uint8_t> b2;
        std::vector<uint8_t> nd2 = desc(0x40, {'O', 't', 'h', 'e', 'r'});
        b2.push_back((uint8_t)(0xF0 | (nd2.size() >> 8))); b2.push_back((uint8_t)nd2.size()); cat(b2, nd2);
        std::vector<uint8_t> l2;
        l2 = {0x07, 0xD1, 0x00, 0x09};
        std::vector<uint8_t> ds2 = satDesc(10847.5, 28.2, true, 0, false, 0, 1, 23000, 3);
        l2.push_back((uint8_t)(0xF0 | (ds2.size() >> 8))); l2.push_back((uint8_t)ds2.size()); cat(l2, ds2);
        b2.push_back((uint8_t)(0xF0 | (l2.size() >> 8))); b2.push_back((uint8_t)l2.size()); cat(b2, l2);
        feed(d, 0x10, section(0x41, 0x0A01, b2), cc10);
    }
    // BAT
    {
        std::vector<uint8_t> body;
        std::vector<uint8_t> bd = desc(0x47, {'P', 'a', 'c', 'k'});
        body.push_back((uint8_t)(0xF0 | (bd.size() >> 8))); body.push_back((uint8_t)bd.size()); cat(body, bd);
        std::vector<uint8_t> loop{0x03, 0xFB, 0x00, 0x01};
        std::vector<uint8_t> sl = desc(0x41, {0x03, 0xE9, 0x01, 0x03, 0xEB, 0x19});
        loop.push_back((uint8_t)(0xF0 | (sl.size() >> 8))); loop.push_back((uint8_t)sl.size()); cat(loop, sl);
        body.push_back((uint8_t)(0xF0 | (loop.size() >> 8))); body.push_back((uint8_t)loop.size()); cat(body, loop);
        feed(d, 0x11, section(0x4A, 0x6000, body), cc11);
    }
    // SDT other
    {
        std::vector<uint8_t> body{0x00, 0x09, 0xFF};   // onid 9
        std::vector<uint8_t> nm{0x01, 4, 'P', 'r', 'o', 'v', 4, 'O', 'n', 'e', '!'};
        std::vector<uint8_t> sd = desc(0x48, nm);
        body.push_back(0x07); body.push_back(0xD2);     // service id 2002
        body.push_back(0xFD);
        body.push_back((uint8_t)(0x80 | 0x10 | (sd.size() >> 8))); body.push_back((uint8_t)sd.size()); cat(body, sd);   // running 4, free_CA 1
        feed(d, 0x11, section(0x46, 0x07D1, body), cc11);
    }
    // EIT other: schedule table 0x60 for service 2002 in ts 0x07D1 with two events, one running
    {
        std::vector<uint8_t> body{0x07, 0xD1, 0x00, 0x09, 0x00, 0x60};
        auto event = [&](int id, uint16_t mjd, int hh, int running, const char* title) {
            body.push_back((uint8_t)(id >> 8)); body.push_back((uint8_t)id);
            body.push_back((uint8_t)(mjd >> 8)); body.push_back((uint8_t)mjd); body.push_back((uint8_t)((hh / 10) << 4 | (hh % 10))); body.push_back(0); body.push_back(0);
            body.push_back(0x01); body.push_back(0x00); body.push_back(0x00);   // duration 1:00:00
            std::vector<uint8_t> sev{'e', 'n', 'g', (uint8_t)strlen(title)};
            for (const char* c = title; *c; c++) sev.push_back((uint8_t)*c);
            sev.push_back(0);
            std::vector<uint8_t> dd = desc(0x4D, sev);
            body.push_back((uint8_t)((running << 5) | (dd.size() >> 8))); body.push_back((uint8_t)dd.size()); cat(body, dd);
        };
        event(1, 60000, 20, 4, "News");
        event(2, 60000, 21, 1, "Film");
        feed(d, 0x12, section(0x60, 2002, body), cc12);
    }
    // TOT with the local offset of Lithuania: +2 h, summer time change to +3
    {
        std::vector<uint8_t> t{0xEA, 0x60, 0x12, 0x00, 0x00};   // MJD 60000 12:00:00
        std::vector<uint8_t> lt{'L', 'T', 'U', 0x00 << 2 | 0, 0x02, 0x00, 0xEA, 0x61, 0x01, 0x00, 0x00, 0x03, 0x00};
        std::vector<uint8_t> ld = desc(0x58, lt);
        std::vector<uint8_t> s{0x73, 0, 0};
        cat(s, t);
        s.push_back((uint8_t)(0xF0 | (ld.size() >> 8))); s.push_back((uint8_t)ld.size()); cat(s, ld);
        const size_t len = s.size() - 3 + 4;
        s[1] = (uint8_t)(0xB0 | (len >> 8)); s[2] = (uint8_t)len;
        const uint32_t c = dvbsip::crc32Mpeg2(s.data(), s.size());
        for (int sh = 24; sh >= 0; sh -= 8) s.push_back((uint8_t)(c >> sh));
        feed(d, 0x14, s, cc14);
    }
    // CAT: Conax and Viaccess EMM PIDs (signalling only)
    {
        std::vector<uint8_t> body;
        cat(body, desc(0x09, {0x0B, 0x00, 0xE1, 0x00}));   // Conax, EMM PID 0x100
        cat(body, desc(0x09, {0x05, 0x00, 0xE2, 0x34}));   // Viaccess, EMM PID 0x234
        feed(d, 0x01, section(0x01, 0xFFFF, body), cc1);
    }
    const TsSnapshot sn = d.snapshot();
    CHECK(sn.si != nullptr, "service information collected");
    if (!sn.si) return testSummary("dvbs si");
    const SiSnapshot& si = *sn.si;
    // networks
    CHECK(si.networks.size() == 2, "two networks: %zu", si.networks.size());
    const SiNetwork* a = nullptr; const SiNetwork* o = nullptr;
    for (const auto& n : si.networks) { if (n.id == 0x3201) a = &n; if (n.id == 0x0A01) o = &n; }
    CHECK(a && a->actual && a->name == "Test Net" && a->transponders.size() == 4, "actual network and its four transponders");
    if (a && a->transponders.size() == 4) {
        const SiTransponder* t1 = nullptr; const SiTransponder* t2 = nullptr; const SiTransponder* tc = nullptr; const SiTransponder* tt = nullptr;
        for (const auto& t : a->transponders) { if (t.tsid == 1019) t1 = &t; if (t.tsid == 1020) t2 = &t; if (t.tsid == 2001) tc = &t; if (t.tsid == 3001) tt = &t; }
        CHECK(t1 && t1->kind == 1 && std::fabs(t1->freqMhz - 11494.0) < 0.01 && t1->pol == 0 && !t1->s2 && std::fabs(t1->symbolRateK - 22000) < 0.1 && t1->fecInner == 3 && std::fabs(t1->orbitDeg - 19.2) < 0.01 && t1->services.size() == 2, "19.2E 11494 H 22000 3/4 with two services");
        CHECK(t2 && t2->s2 && t2->pol == 1 && t2->modulation == 2 && t2->rollOff == 1 && std::fabs(t2->freqMhz - 12188.0) < 0.01 && std::fabs(t2->symbolRateK - 27500) < 0.1, "13.0E 12188 V DVB-S2 8PSK roll-off 0.25 27500 4/5");
        CHECK(tc && tc->kind == 2 && std::fabs(tc->freqMhz - 314.0) < 0.001 && tc->modulation == 3 && std::fabs(tc->symbolRateK - 6900) < 0.1, "cable 314 MHz 64QAM 6900");
        CHECK(tt && tt->kind == 3 && std::fabs(tt->freqMhz - 690.0) < 0.001, "terrestrial 690 MHz");
    }
    CHECK(o && !o->actual && o->name == "Other" && o->transponders.size() == 1 && std::fabs(o->transponders[0].freqMhz - 10847.5) < 0.01, "another network with its transponder");
    // bouquet
    CHECK(si.bouquets.size() == 1 && si.bouquets[0].id == 0x6000 && si.bouquets[0].name == "Pack" && si.bouquets[0].services.size() == 2 && si.bouquets[0].services[1].sid == 0x03EB && si.bouquets[0].services[1].type == 0x19, "bouquet with two services");
    // other services
    CHECK(si.otherServices.size() == 1 && si.otherServices[0].sid == 2002 && si.otherServices[0].name == "One!" && si.otherServices[0].provider == "Prov" && si.otherServices[0].freeCa && si.otherServices[0].running == 4, "SDT other");
    // EIT other
    CHECK(si.eitOther.size() == 1 && si.eitOther[0].sid == 2002 && si.eitOther[0].events == 2 && si.eitOther[0].nowTitle == "News", "EIT other: two events, one running");
    // TOT
    CHECK(si.timeOffsets.size() == 1 && si.timeOffsets[0].country == "LTU" && si.timeOffsets[0].offsetMin == 120 && si.timeOffsets[0].nextOffsetMin == 180, "TOT local offset +2 h, next +3 h");
    CHECK(si.totUtc == ((int64_t)60000 - 40587) * 86400 + 12 * 3600, "TOT UTC %lld", (long long)si.totUtc);
    // CAT
    CHECK(si.ca.size() == 2, "two CA systems");
    bool conax = false, viaccess = false;
    for (const auto& c : si.ca) { if (c.caSystemId == 0x0B00 && c.emmPid == 0x100 && c.name == "Conax") conax = true; if (c.caSystemId == 0x0500 && c.emmPid == 0x234 && c.name == "Viaccess") viaccess = true; }
    CHECK(conax && viaccess, "CAT names Conax and Viaccess (signalling only)");
    // a damaged section is ignored
    {
        TsDemux d2;
        uint8_t c = 0;
        auto sec = section(0x4A, 0x6001, {0xF0, 0x00, 0xF0, 0x00});
        sec[9] ^= 0x55;
        feed(d2, 0x11, sec, c);
        CHECK(d2.snapshot().si == nullptr, "a section with a bad CRC adds nothing");
    }
    return testSummary("dvbs si");
}
