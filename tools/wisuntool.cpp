// Wi-SUN FAN tool: decode a recording, or write the test signal.
//   wisuntool rx <file> [--rate Hz] [--center Hz] [--region ANZ|NA] [--secs s]
//       a SigMF recording (.sigmf-meta / .sigmf-data: rate and centre from the meta), or .cf32 / .cs16 / .cs8 with --rate and --center;
//       prints every frame, then the networks, nodes and links
//   wisuntool gen <out.cf32> [--rate Hz] [--secs s] [--snr dB]
//       the test schedule (wisun_gen.h) around 921.5 MHz
#include "dect2/iq_file.h"
#include "dect2/wisun_gen.h"
#include "dect2/wisun_rx.h"
#include "dect2/wisun_tel.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace dect2;

static void printNew(const WisunTelemetry& t, size_t& printed) {
    // the log is newest first: the entries beyond what was printed, oldest of them first
    size_t fresh = (size_t)(t.blocksOk - printed);
    if (fresh > t.log.size()) fresh = t.log.size();
    for (size_t i = fresh; i-- > 0;) {
        const WsFrame& f = t.log[i];
        printf("%9.4f %8.3f MHz ch %2d %3d kbit/s %6.1f dBFS %5.1f dB  %-23s > %-23s %4d  %s  [%s]\n", f.sec, f.mhz, f.channel, f.bitRate / 1000, f.rssi, f.snrDb,
               f.src.c_str(), f.dst.c_str(), f.len, f.info.c_str(), f.ies.c_str());
        if (getenv("WISUN_HEX")) printf("          %s\n", f.hex.c_str());
    }
    printed = (size_t)t.blocksOk;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: wisuntool rx <file> [--rate Hz] [--center Hz] [--region ANZ|NA] [--secs s]\n       wisuntool gen <out.cf32> [--rate Hz] [--secs s] [--snr dB]\n");
        return 2;
    }
    const std::string cmd = argv[1], path = argv[2];
    double rate = 0, center = 0, secs = 0, snr = 30;
    std::string region;
    for (int i = 3; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        if (k == "--rate") rate = atof(argv[i + 1]);
        else if (k == "--center") center = atof(argv[i + 1]);
        else if (k == "--region") region = argv[i + 1];
        else if (k == "--secs") secs = atof(argv[i + 1]);
        else if (k == "--snr") snr = atof(argv[i + 1]);
    }
    if (cmd == "gen") {
        if (rate <= 0) rate = 20e6;
        if (secs <= 0) secs = 1;
        WisunGenConfig c;
        c.rate = rate; c.snrDb = snr; c.centerMhz = wisunTuning().defMhz;
        c.frames = wisunTestSchedule(c.centerMhz, std::min(6.0, 0.4 * rate / 1e6));
        WisunGenerator g(c);
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) { perror(path.c_str()); return 1; }
        std::vector<cf32> buf(1 << 16);
        for (size_t left = (size_t)(secs * rate); left > 0;) {
            const size_t n = std::min(left, buf.size());
            g.generate(buf.data(), n);
            fwrite(buf.data(), sizeof(cf32), n, f);
            left -= n;
        }
        fclose(f);
        return 0;
    }
    if (cmd != "rx") { fprintf(stderr, "unknown command %s\n", cmd.c_str()); return 2; }
    std::string data = path;
    FileFormat fmt = FileFormat::CF32;
    uint64_t offset = 0;
    IqFileInfo info;
    std::string err;
    if (probeIqFile(path, info, err)) {
        data = info.dataPath; fmt = info.format; offset = info.offsetBytes;
        if (rate <= 0) rate = info.sampleRate;
        if (center <= 0) center = info.centerHz;
    } else if (!err.empty()) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    else {
        const std::string ext = path.substr(path.find_last_of('.') + 1);
        fmt = ext == "cs16" ? FileFormat::CI16 : ext == "cs8" ? FileFormat::CS8 : FileFormat::CF32;
    }
    if (rate <= 0 || center <= 0) { fprintf(stderr, "the sample rate and the centre frequency are needed (--rate, --center)\n"); return 2; }
    FILE* f = fopen(data.c_str(), "rb");
    if (!f) { perror(data.c_str()); return 1; }
    fseek(f, (long)offset, SEEK_SET);
    WisunReceiver rx;
    rx.setCenterHz(center);
    if (!region.empty()) rx.setRegion(region);
    rx.configure(rate);
    rx.setOffline(true);
    const size_t chunk = 1 << 16;
    const int bytesPer = fmt == FileFormat::CF32 ? 8 : fmt == FileFormat::CI16 || fmt == FileFormat::CU16 ? 4 : 2;
    std::vector<uint8_t> raw(chunk * (size_t)bytesPer);
    std::vector<cf32> buf(chunk);
    WisunTelemetry t;
    uint64_t seq = 0;
    size_t printed = 0, total = 0;
    const size_t limit = secs > 0 ? (size_t)(secs * rate) : (size_t)-1;
    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        const size_t want = std::min(chunk, limit - total);
        if (want == 0) break;
        const size_t n = fread(raw.data(), (size_t)bytesPer, want, f);
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) {
            if (fmt == FileFormat::CF32) memcpy(&buf[i], raw.data() + 8 * i, 8);
            else if (fmt == FileFormat::CI16) {
                int16_t a, b; memcpy(&a, raw.data() + 4 * i, 2); memcpy(&b, raw.data() + 4 * i + 2, 2);
                buf[i] = cf32(a / 32768.f, b / 32768.f);
            } else if (fmt == FileFormat::CU8) buf[i] = cf32((raw[2 * i] - 127.5f) / 128.f, (raw[2 * i + 1] - 127.5f) / 128.f);
            else buf[i] = cf32((int8_t)raw[2 * i] / 128.f, (int8_t)raw[2 * i + 1] / 128.f);
        }
        rx.feed(buf.data(), n);
        total += n;
        if (rx.telemetry(t, seq)) { seq = t.seq; printNew(t, printed); }
    }
    fclose(f);
    const std::vector<cf32> tail((size_t)(0.3 * rate), cf32(0, 0));   // flush the last bursts and the report
    rx.feed(tail.data(), tail.size());
    rx.flush();
    if (rx.telemetry(t, seq)) printNew(t, printed);
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("\n%.1f s of signal in %.1f s: %s; %llu corrected, %llu bad FCS, %llu FEC frames, %llu bursts, %llu dropped; region %s, %.1f - %.1f MHz, %d bins of %.1f kHz\n",
           total / rate, wall, wisunSummary(t).c_str(), (unsigned long long)t.corrected, (unsigned long long)t.blocksBad, (unsigned long long)t.fecFrames, (unsigned long long)t.bursts, (unsigned long long)t.dropped,
           t.region.c_str(), t.loMhz, t.hiMhz, t.bins, t.binKhz);
    printf("%llu mode switches:", (unsigned long long)t.modeSwitches);
    for (const auto& m : t.switchModes) printf(" 0x%02X x%u", m.first, m.second);
    printf("\n");
    for (const WsPan& p : t.pans)
        printf("PAN 0x%04X \"%s\" size %d, version %d, border router %s, %d nodes, %llu frames; %s\n", p.pan, p.netName.c_str(), p.panSize, p.panVersion,
               p.borderRouter.empty() ? "-" : p.borderRouter.c_str(), p.nodes, (unsigned long long)p.packets, p.unicast.c_str());
    for (const WsNode& n : t.nodes)
        printf("node %s %-14s %-24s PAN %5d  %4llu frames  %6.1f dBFS  %6.3f MHz  %s\n", n.eui.c_str(), n.role.c_str(), n.vendor.c_str(), n.pan,
               (unsigned long long)n.packets, n.rssi, n.lastMhz, n.last.c_str());
    for (const WsLink& l : t.links)
        printf("link %s > %s  %llu frames, %llu acks, RSL %d dBm\n", l.src.c_str(), l.dst.c_str(), (unsigned long long)l.frames, (unsigned long long)l.acks, l.rslDbm);
    return 0;
}
