// Headless receiver: prints sync / L1 telemetry once per second. Useful for bring-up and logs.
//   dect2cli --hackrf [--freq MHz] [--lna dB] [--vga dB] [--amp] [--bw MHz] [--secs N]
//   dect2cli --file path.cs8 --rate Msps [--format cs8|cu8|cf32] [--bw MHz] [--secs N]
//   dect2cli --record out.cs8 --secs N   (HackRF only; raw IQ capture at the 2x native rate)
#include "dect2/engine.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>

using namespace dect2;

int main(int argc, char** argv) {
    Engine e;
    DeviceInfo dev;
    TuneSettings tune;
    FileOptions file;
    double freq = 522, secs = 10;
    bool useFile = false, haveDev = false;
    std::string record;
    OutputConfig out;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? argv[++i] : (char*)""; };
        if (a == "--hackrf") haveDev = true;
        else if (a == "--freq") freq = atof(next());
        else if (a == "--lna") tune.lnaDb = atoi(next());
        else if (a == "--vga") tune.vgaDb = atoi(next());
        else if (a == "--amp") tune.ampOn = true;
        else if (a == "--bw") tune.bandwidthMhz = atof(next());
        else if (a == "--secs") secs = atof(next());
        else if (a == "--file") { useFile = true; file.path = next(); file.format = guessFormat(file.path); }
        else if (a == "--rate") file.sampleRate = atof(next()) * 1e6;
        else if (a == "--format") { std::string f = next(); file.format = f == "cu8" ? FileFormat::CU8 : f == "cf32" ? FileFormat::CF32 : FileFormat::CS8; }
        else if (a == "--record") record = next();
        else if (a == "--ts") out.file = true, out.path = next();
        else if (a == "--udp") { std::string hp = next(); auto c = hp.find(':'); out.udp = true; out.host = hp.substr(0, c); if (c != std::string::npos) out.port = atoi(hp.c_str() + c + 1); }
        else if (a == "--rtp") out.rtp = true;
        else if (a == "--service") out.serviceId = atoi(next());
        else if (a == "--no-null") out.dropNull = true;
    }
    tune.centerHz = freq * 1e6;
    tune.sampleRate = tune.bandwidthMhz >= 7 ? 10e6 : 8e6; // HackRF Pro: exact tuning only at <= 10 Msps
    if (useFile) { dev.kind = DeviceInfo::File; dev.name = file.path; file.loop = false; }
    else {
        std::string err;
        auto list = listHackrfDevices(err);
        if (list.empty()) { fprintf(stderr, "no HackRF found %s\n", err.c_str()); return 1; }
        dev = list[0];
        printf("device: %s\n", dev.name.c_str());
    }
    (void)haveDev;
    if (getenv("DECT2_BBDUMP")) e.setPlpDump([](const PlpResult& r) {
        static int shown = 0;
        for (auto& f : r.frames) if (!f.bits.empty() && shown < 6) { shown++; printf("BBFRAME blk %d hdr:", f.blockIndex); for (int B = 0; B < 14; B++) { unsigned v = 0; for (int i = 0; i < 8; i++) v = (v << 1) | f.bits[B * 8 + i]; printf(" %02x", v); } printf("  crc %d upl %d dfl %d syncd %d\n", f.header.crcOk, f.header.upl, f.header.dfl, f.header.syncd); }
    });
    e.setOutputs(out);
    if (!e.start(dev, tune, file)) {
        size_t n; for (auto& l : e.logSnapshot(n)) fprintf(stderr, "%s\n", l.c_str());
        return 1;
    }
    size_t shown = 0;
    RxTelemetry t; uint64_t seq = 0;
    SpectrumFrame sf; uint64_t sseq = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < secs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        e.latestRx(t, 0);
        e.latestSpectrum(sf, 0);
        size_t n;
        auto lines = e.logSnapshot(n);
        for (; shown < lines.size(); shown++) printf("  log: %s\n", lines[shown].c_str());
        printf("level %6.1f dBFS clip %.3f%% | state %d P1 %llu (s1 %d fft %d) GI %s | CFO %+8.1f Hz | CP %.2f | L1 pre %llu/%llu post %llu/%llu | data %d SNR %.1f dB | sym %llu sinceP1 %.2f | PLP %llu fr, blocks %llu ok/%llu bad, MER %.1f dB preBER %.1e it %.1f %.0f ms | dropped %llu\n",
               sf.stats.rmsDbfs, sf.stats.clipFraction * 100, t.state, (unsigned long long)t.p1Count, t.p1.s1, t.fftN,
               t.giIdx >= 0 ? guardName(t.giIdx) : "-", t.cfoHz, t.cpCorr, (unsigned long long)t.l1preGood, (unsigned long long)(t.l1preGood + t.l1preBad),
               (unsigned long long)t.l1postGood, (unsigned long long)(t.l1postGood + t.l1postBad), t.dataValid, t.dataSnrDb, (unsigned long long)t.symbols, t.secSinceP1, (unsigned long long)t.plpFrames, (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad, t.plpMerDb, t.plpPreBer, t.plpIters, t.plpDecodeMs, (unsigned long long)e.droppedSamples());
    }
    if (t.l1preGood) {
        const L1Pre& p = t.l1pre;
        printf("\nL1-pre: type %d ext %d s1 %d s2 %d gi %s papr %d l1mod %d cod %d fec %d postSize %d postInfo %d PP%d tx %d cell %d net 0x%04X sys 0x%04X frames %d dataSyms %d regen %d ext %d rf %d/%d ver %d scr %d lite %d\n",
               p.type, p.bwtExt, p.s1, p.s2, guardName(p.guardInterval), p.papr, p.l1Mod, p.l1Cod, p.l1Fec, p.postSize, p.postInfoSize, p.pilotPattern + 1,
               p.txIdAvail, p.cellId, p.networkId, p.systemId, p.numFrames, p.numDataSyms, p.regen, p.postExtension, p.numRf, p.curRf, p.version, p.postScrambled, p.lite);
    }
    if (t.l1postGood) {
        const L1Post& q = t.l1post;
        printf("L1-post: plps %d aux %d rf %.3f MHz\n", q.numPlp, q.numAux, q.rf.empty() ? 0.0 : q.rf[0].freq / 1e6);
        for (auto& c : q.plps) printf("  PLP %d type %d payload %d cod %d mod %d rot %d fec %d blocksMax %d TI %d/%d group %d mode %d\n", c.id, c.type, c.payloadType, c.cod, c.mod, c.rotation, c.fecType, c.numBlocksMax, c.timeIlLength, c.timeIlType, c.groupId, c.plpMode);
    }
    {
        TsSnapshot ts = e.tsSnapshot();
        BbStats bb = e.bbStats();
        printf("\nBB frames %llu (lost %llu) packets %llu resyncs %llu  mode %s ISSYI %d NPD %d\n", (unsigned long long)bb.frames, (unsigned long long)bb.framesLost, (unsigned long long)bb.packets, (unsigned long long)bb.resyncs, bb.hem ? "HEM" : "normal", bb.issyi, bb.npd);
        printf("TS: network \"%s\" onid 0x%04X tsid 0x%04X  %s  mux %.0f kbit/s (null %.0f)  CC errors %llu\n", ts.networkName.c_str(), ts.onid, ts.tsid, ts.utc.c_str(), ts.muxKbps, ts.nullKbps, (unsigned long long)ts.ccErrors);
        for (auto& sv : ts.services) {
            printf("  [%4d] %-24s %-8s lcn %-3d pmt 0x%04X %s\n", sv.id, sv.name.c_str(), sv.typeName(), sv.lcn, sv.pmtPid, sv.caFlag ? "(scrambled)" : "");
            for (auto& st : sv.streams) printf("          pid 0x%04X %-10s %-14s %s %.0f kbit/s\n", st.pid, st.kind.c_str(), st.codec.c_str(), st.lang.c_str(), st.kbps);
            if (!sv.now.empty()) printf("          now: %s | next: %s\n", sv.now.c_str(), sv.next.c_str());
        }
        OutputStats os = e.outputStats();
        if (os.fileOpen) printf("file: %llu packets, %llu bytes\n", (unsigned long long)os.filePackets, (unsigned long long)os.fileBytes);
        if (os.udpOpen) printf("udp: %llu datagrams, dropped %llu\n", (unsigned long long)os.udpDatagrams, (unsigned long long)os.udpDropped);
    }
    e.stop();
    { size_t n; auto lines = e.logSnapshot(n); for (; shown < lines.size(); shown++) printf("  log: %s\n", lines[shown].c_str()); }
    return 0;
}
