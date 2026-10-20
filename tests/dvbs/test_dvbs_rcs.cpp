// DVB-RCS / RCS2 signalling of an interactive network, through the TS demultiplexer: the PMT's RCS content descriptors, the RMT, SCT, FCT, TCT and TIM tables, the NCR
// and its clock error, and a table that is declared and never sent.
#include "dect2/dvbs/dvbs_ip.h"
#include "dect2/tv/rcs.h"
#include "dect2/tv/ts.h"
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace dect2;
#include "../common/test_util.h"

static void ncrPacket(TsDemux& d, uint64_t ticks) {
    uint8_t p[188];
    memset(p, 0xFF, sizeof p);
    p[0] = 0x47; p[1] = 0x16; p[2] = 0x00; p[3] = 0x20; p[4] = 183; p[5] = 0x10;
    const uint64_t base = ticks / 300, ext = ticks % 300;
    p[6] = (uint8_t)(base >> 25); p[7] = (uint8_t)(base >> 17); p[8] = (uint8_t)(base >> 9); p[9] = (uint8_t)(base >> 1);
    p[10] = (uint8_t)(((base & 1) << 7) | 0x7E | (ext >> 8)); p[11] = (uint8_t)ext;
    d.feed(p);
}

int main() {
    TsDemux d;
    const RcsTestNetwork net = buildRcsTestNetwork();
    uint8_t cc[8192] = {0};
    for (int round = 0; round < 3; round++)
        for (size_t i = 0; i < net.sections.size(); i++)
            for (auto& p : dvbsip::packetizeSection(net.pids[i], cc[net.pids[i]], net.sections[i])) d.feed(p.data());
    for (uint64_t k = 0; k < 100; k++) { ncrPacket(d, 1000000 + k * 1080021); d.advance(0.04); }
    const TsSnapshot sn = d.snapshot();
    CHECK(sn.rcs != nullptr, "RCS view present");
    if (!sn.rcs) return 1;
    const RcsView& v = *sn.rcs;
    CHECK(v.networkName == "TEST NET" && v.networkId == 1, "network '%s' id %d", v.networkName.c_str(), v.networkId);
    CHECK(v.linkages.size() == 1 && v.linkages[0].linkageType == 0x81 && v.linkages[0].interactiveNetworkId == 0x6E, "linkage");
    CHECK(v.forward.size() == 1, "forward links: %zu", v.forward.size());
    if (!v.forward.empty()) {
        const RcsForwardLink& f = v.forward[0];
        CHECK(f.satelliteId == 0x22 && f.beamId == 1511 && f.nccId == 10, "forward ids");
        CHECK(std::fabs(f.frequencyHz - 11.7e9) < 1 && std::fabs(f.orbitalDeg - 19.2) < 1e-9, "forward position: %.0f Hz, %.1f deg", f.frequencyHz, f.orbitalDeg);
        CHECK(f.standard == 2 && std::fabs(f.rollOff - 0.20) < 1e-9 && std::fabs(f.symbolRate - 2e6) < 1 && f.isi == 0, "forward link: standard %d, roll-off %.2f, %.0f Bd, ISI %d", f.standard, f.rollOff, f.symbolRate, f.isi);
    }
    CHECK(v.returns.size() == 1 && v.returns[0].gatewayId == 1 && v.returns[0].superframeId == 3 && std::fabs(v.returns[0].txOffsetHz + 1000) < 1e-9, "return link");
    CHECK(v.superframes.size() == 1 && v.superframes[0].id == 3 && std::fabs(v.superframes[0].centreHz - 14.25e9) < 1 && v.superframes[0].frames.size() == 1, "superframe");
    CHECK(v.frameTypes.size() == 1 && v.frameTypes[0].timeslots == 24 && v.frameTypes[0].groups.size() == 1 && v.frameTypes[0].groups[0].timeslotType == 5 && v.frameTypes[0].groups[0].count == 24, "frame type");
    CHECK(v.timeslots.size() == 1 && v.timeslots[0].turbo && std::fabs(v.timeslots[0].symbolRate - 256000) < 1e-9, "timeslot");
    if (!v.timeslots.empty()) {
        const RcsTimeslot& t = v.timeslots[0];
        CHECK(!strcmp(rcsRateName(t.codeRate), "2/3") && !strcmp(rcsModulationName(t.modulation), "QPSK") && !strcmp(rcsPayloadName(t.payload), "TRF, MPEG-TS packets"), "timeslot formats: %s %s %s", rcsRateName(t.codeRate), rcsModulationName(t.modulation), rcsPayloadName(t.payload));
    }
    CHECK(v.timStatus == 0 && v.timDescriptors.size() == 1 && v.timDescriptors[0] == 0xAC, "broadcast TIM");
    CHECK(v.declaredMissing.size() == 1 && v.declaredMissing[0].first == 0x29 && v.declaredMissing[0].second == 0xA5, "the TBTP is declared and never sent");
    CHECK(v.tables.count(0x41) && v.tables.at(0x41).sections == 3, "RMT sections");
    CHECK(v.haveNcr && v.ncr.pid == 0x1600 && std::fabs(v.ncr.intervalS - 0.04) < 1e-6, "NCR interval %.6f s", v.ncr.intervalS);
    CHECK(v.ncr.havePpm && std::fabs(v.ncr.ppm - 19.4) < 1.0, "NCR clock %.1f ppm", v.ncr.ppm);
    CHECK(rcsTableLabel(0xD1) == "user D1h" && rcsTableLabel(0xA8) == "A8h" && rcsTableLabel(0x3E) == "MPE" && rcsTableLabel(0xB0) == "TIM", "table labels");
    return testSummary("dvbs rcs");
}
