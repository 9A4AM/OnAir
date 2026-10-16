// Decode e-Czas time packets from discriminator WAV recordings (scratchpad/eczas_recordings/).
#include "dect2/eczas_rx.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

using namespace dect2::eczas;

static int fails = 0;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL line %d: ", __LINE__);                                                                        \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
            fails++;                                                                                                   \
        }                                                                                                              \
    } while (0)

static bool loadWavMono16(const std::string& path, std::vector<int16_t>& pcm, double& rate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char h[12];
    if (!f.read(h, 12) || std::memcmp(h, "RIFF", 4)) return false;
    int ch = 1, bits = 16;
    rate = 0;
    for (;;) {
        char id[4];
        uint32_t n = 0;
        if (!f.read(id, 4) || !f.read(reinterpret_cast<char*>(&n), 4)) return false;
        if (!std::memcmp(id, "fmt ", 4)) {
            uint16_t v[8]{};
            f.read(reinterpret_cast<char*>(v), 16);
            ch = v[1];
            bits = v[7];
            uint32_t sr = 0;
            std::memcpy(&sr, reinterpret_cast<char*>(v) + 4, 4);
            rate = sr;
            f.seekg((long)n - 16, std::ios::cur);
        } else if (!std::memcmp(id, "data", 4)) {
            if (bits != 16 || ch != 1) return false;
            pcm.resize(n / 2);
            f.read(reinterpret_cast<char*>(pcm.data()), (long)n);
            return f.good();
        } else
            f.seekg((long)n + (n & 1), std::ios::cur);
    }
}

static std::string formatUtc(int64_t utcSec) {
    std::time_t t = (std::time_t)utcSec;
    std::tm gt{};
#if defined(_WIN32)
    gmtime_s(&gt, &t);
#else
    gmtime_r(&t, &gt);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d UTC", gt.tm_year + 1900, gt.tm_mon + 1, gt.tm_mday, gt.tm_hour,
                  gt.tm_min, gt.tm_sec);
    return buf;
}

int main() {
    const std::string wav = std::string(DECT2_SOURCE_DIR) + "/scratchpad/eczas_recordings/224k_2102.wav";
    std::vector<int16_t> pcm;
    double rate = 0;
    if (!loadWavMono16(wav, pcm, rate)) {
        printf("%s: not found, skipped\n", wav.c_str());
        return 0;
    }
    printf("loaded %s: %.0f Hz, %zu samples (%.1f s)\n", wav.c_str(), rate, pcm.size(), pcm.size() / rate);

    const auto pkts = eczasDecodeRecording(pcm.data(), pcm.size(), rate);
    CHECK(!pkts.empty(), "no time packet decoded");
    if (pkts.empty()) return fails ? 1 : 0;

    const EczasRxPacket& p = pkts[0];
    CHECK(p.time.status == DecodeStatus::CrcOk, "status %d", (int)p.time.status);
    CHECK(p.time.timeMarker == kTimeMarker, "marker 0x%x", p.time.timeMarker);
    printf("decoded %zu packet(s); first utcSec=%lld (%s), TZ +%d h, periods3s=%u\n", pkts.size(), (long long)p.time.utcSec,
           formatUtc(p.time.utcSec).c_str(), p.time.tzHours, p.time.periods3s);
    printf("raw frame:");
    for (int i = 0; i < kFrameBytes; i++) printf(" %02X", p.frame[i]);
    printf("\n");

    return fails ? 1 : 0;
}
