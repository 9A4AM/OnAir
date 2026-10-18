// DSM-CC object carousels: a service gateway, directories and files in BIOP objects, modules cut into DDB blocks, compressed (zlib) modules, blocks arriving before the DII, the
// carousel found on its PID in a transport stream, the file tree, saving to a folder (paths cleaned), and damaged sections.
#include "dect2/dvbs/dvbs_ip.h"
#include "dect2/tv/dsmcc.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
using namespace dect2;
#include "../common/test_util.h"

static std::vector<DsmccTestFile> tree() {
    std::vector<DsmccTestFile> f;
    f.push_back({"index.htm", std::vector<uint8_t>{'<', 'h', 't', 'm', 'l', '>', 'h', 'i', '<', '/', 'h', 't', 'm', 'l', '>'}});
    std::vector<uint8_t> img(3000);
    for (size_t i = 0; i < img.size(); i++) img[i] = (uint8_t)(i * 31 + 7);
    f.push_back({"img/logo.png", img});
    f.push_back({"data/a.txt", std::vector<uint8_t>(450, 'a')});
    f.push_back({"data/b.txt", std::vector<uint8_t>(10, 'b')});
    f.push_back({"data/sub/c.bin", std::vector<uint8_t>(1, 0x42)});
    return f;
}

static void feedCarousel(DsmccDemux& d, const std::vector<std::vector<uint8_t>>& secs, uint16_t pid, uint8_t& cc, int rounds, bool blocksFirst) {
    for (int r = 0; r < rounds; r++) {
        std::vector<std::vector<uint8_t>> order = secs;
        if (blocksFirst && r == 0) std::rotate(order.begin(), order.begin() + 2, order.end());   // the DDB blocks before the DSI and DII
        for (const auto& s : order) for (auto& p : dvbsip::packetizeSection(pid, cc, s)) d.packet(p.data());
    }
}

static bool same(const std::vector<DsmccFile>& got, const std::vector<DsmccTestFile>& want) {
    if (got.size() != want.size()) return false;
    for (const auto& w : want) {
        bool ok = false;
        for (const auto& g : got) if (g.path == w.path && g.data == w.data) ok = true;
        if (!ok) return false;
    }
    return true;
}

int main() {
    const auto files = tree();
    for (int compress = 0; compress < 2; compress++) {
        const auto secs = buildObjectCarousel(files, 0x1234, 180, compress != 0);
        DsmccDemux d;
        uint8_t cc = 0;
        feedCarousel(d, secs, 0x0300, cc, 2, true);
        const auto info = d.info();
        CHECK(info.size() == 1 && info[0].pid == 0x0300, "the carousel is found on its PID");
        if (info.size() != 1) continue;
        const DsmccCarouselInfo& ci = info[0];
        printf("  %s: %zu modules, %zu files, %llu of %llu bytes, DSI %llu DII %llu DDB %llu\n", compress ? "zlib modules" : "plain modules", ci.modules.size(), ci.files.size(), (unsigned long long)ci.bytesDone, (unsigned long long)ci.bytesTotal, (unsigned long long)ci.dsi, (unsigned long long)ci.dii, (unsigned long long)ci.ddb);
        CHECK(ci.objectCarousel && ci.gatewayKnown, "an object carousel with its service gateway");
        CHECK(ci.modules.size() == 9 && ci.bytesDone == ci.bytesTotal, "all modules complete (%zu, %llu of %llu)", ci.modules.size(), (unsigned long long)ci.bytesDone, (unsigned long long)ci.bytesTotal);
        bool comp = true;
        for (const auto& m : ci.modules) if (m.compressed != (compress != 0)) comp = false;
        CHECK(comp, "the compression flag of every module");
        CHECK(ci.files.size() == files.size(), "file list: %zu", ci.files.size());
        const auto got = d.files();
        CHECK(got.count(0x0300) && same(got.at(0x0300), files), "the files are exactly the ones sent, with their paths");
        if (compress == 0) {
            // saving to a folder
            const std::string dir = "/tmp/dvbs_dsmcc_test";
            std::system(("rm -rf " + dir).c_str());
            mkdir(dir.c_str(), 0755);
            CHECK(d.extractTo(dir) == files.size(), "files saved");
            {
                std::ifstream gi(dir + "/index.html");
                std::string g((std::istreambuf_iterator<char>(gi)), std::istreambuf_iterator<char>());
                CHECK(g.find("<h1>Carousel files</h1>") != std::string::npos && g.find("PID 0x") != std::string::npos, "gallery index written");
            }
            std::ifstream f(dir + "/pid_0300/img/logo.png", std::ios::binary);
            std::vector<uint8_t> got2((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            CHECK(got2 == files[1].data, "a saved file holds the data");
            std::system(("rm -rf " + dir).c_str());
        }
    }
    // paths are cleaned
    CHECK(dsmccSafePath("../../etc/passwd") == "etc/passwd" && dsmccSafePath("/abs/x") == "abs/x" && dsmccSafePath("a/./b\\c:d") == "a/b/c_d" && dsmccSafePath("..") == "file", "unsafe paths cleaned");
    // a damaged section is counted and ignored; PES PIDs are left alone
    {
        auto secs = buildObjectCarousel(files, 1, 500, false);
        secs[3][20] ^= 0xFF;
        DsmccDemux d;
        uint8_t cc = 0, c2 = 0;
        std::vector<uint8_t> pes(188, 0); pes[0] = 0x47; pes[1] = 0x41; pes[2] = 0x00; pes[3] = 0x10; pes[6] = 1;
        d.packet(pes.data());
        feedCarousel(d, secs, 0x0301, cc, 1, false);
        const auto info = d.info();
        CHECK(info.size() == 1 && info[0].badSections >= 1, "a bad CRC is counted");
        (void)c2;
    }
    return testSummary("dvbs dsmcc");
}
