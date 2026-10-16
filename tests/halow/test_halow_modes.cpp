// Wi-Fi HaLow monitor behaviour above the single-PPDU sweeps (test_halow_sweep_*): sensitivity of every width, the whole schedule at 10 and 20 Msps
// (8 and 16 MHz networks), the sample-rate limits, manual width selection, PPDUs that are only reported (streams, STBC, LDPC, A-MPDU, NDP), beamformed
// long-preamble frames (channel from the D-LTF), invalid MCS / width combinations, the longest PSDU, a random mixture of everything over noise, and
// noise or a tone alone at 20 Msps.
#include "halow_sweep.h"
#include "dect2/dsp/gen_util.h"
#include <random>
using namespace sweep;
#include "../common/test_util.h"

static HalowTelemetry runSchedule(double rate, double secs, int widthMask = 31) {
    HalowGenConfig c;
    c.rate = rate; c.snrDb = 45; c.frames = halowTestSchedule(rate);
    HalowGenerator g(c);
    HalowReceiver r;
    r.configure(rate);
    r.setWidthMask(widthMask);
    r.setCenterHz(905e6);
    std::vector<cf32> buf(16384);
    for (size_t done = 0; done < (size_t)(secs * rate); done += buf.size()) { g.generate(buf.data(), buf.size()); r.feed(buf.data(), buf.size()); }
    r.flush();
    HalowTelemetry t;
    r.telemetry(t, 0);
    return t;
}
static const HlNet* findNet(const HalowTelemetry& t, const char* ssid) { for (const auto& n : t.nets) if (n.ssid == ssid) return &n; return nullptr; }

int main() {
    {   // sensitivity: MCS 0 at 8 dB and the highest MCS at 32 dB in the channel's own band, every width
        for (int w = 0; w < kNumWidths; w++) for (int mcs : {0, w == W2 ? 8 : 9}) {
            HalowGenFrame f;
            f.psdu = payloadFrame(60); f.width = w; f.mcs = mcs;
            const double snr = mcs == 0 ? 8 : 32;
            const Result r = run(f, sets()[0], recommendedRateHz(w), 30, snr);
            printf("  %2d MHz MCS %d at %2.0f dB: %llu of %.0f\n", 1 << w, mcs, snr, (unsigned long long)r.ok, r.sent);
            CHECK(r.ok >= 0.8 * r.sent && r.bad <= 1, "%d MHz MCS %d at %.0f dB: %llu good %llu bad of %.0f", 1 << w, mcs, snr, (unsigned long long)r.ok, (unsigned long long)r.bad, r.sent);
        }
    }
    {   // the schedule at 20 Msps: six networks including the 8 and 16 MHz ones, with their channels from the S1G Operation element
        const HalowTelemetry t = runSchedule(20e6, 1.2);
        printf("  20 Msps schedule: %llu good, %llu bad, %zu networks, widths %llu %llu %llu %llu %llu\n", (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad, t.nets.size(), (unsigned long long)t.perWidth[0],
               (unsigned long long)t.perWidth[1], (unsigned long long)t.perWidth[2], (unsigned long long)t.perWidth[3], (unsigned long long)t.perWidth[4]);
        CHECK(t.blocksBad == 0 && t.nets.size() == 6 && t.maxWidthMhz == 16, "%llu bad, %zu networks, max %d MHz", (unsigned long long)t.blocksBad, t.nets.size(), t.maxWidthMhz);
        for (int w = 0; w < 5; w++) CHECK(t.perWidth[w] > 0, "no PPDU at %d MHz", 1 << w);
        const HlNet* lab = findNet(t, "Lab-8MHz");
        const HlNet* wide = findNet(t, "Wide-16MHz");
        CHECK(lab && lab->seenWidthMhz == 8 && lab->opWidthMhz == 8 && lab->freqMhz == 906.0 && lab->mcs == 2 && lab->security == "WPA3-SAE", "8 MHz network");
        CHECK(wide && wide->seenWidthMhz == 16 && wide->opWidthMhz == 16 && wide->freqMhz == 910.0 && wide->mcs == 1 && wide->security == "Open", "16 MHz network");
        CHECK(t.region == RegUS && t.chanHint == 5, "region %d channel hint %d", t.region, t.chanHint);   // 905 MHz: US 1 MHz channel 5 is 904.5, 6 is 905.0? nearest within 1 MHz
    }
    {   // at 10 Msps the 8 MHz network is there, the 16 MHz one is not
        const HalowTelemetry t = runSchedule(10e6, 1.2);
        CHECK(t.blocksBad == 0 && t.nets.size() == 5 && findNet(t, "Lab-8MHz") && !findNet(t, "Wide-16MHz") && t.maxWidthMhz == 8 && t.perWidth[3] > 0 && t.perWidth[4] == 0, "10 Msps: %zu networks, max %d MHz", t.nets.size(), t.maxWidthMhz);
    }
    {   // a radio that is too slow: 4 Msps sees 1 / 2 / 4 MHz only and says so; 2 Msps 1 and 2 MHz
        CHECK(maxWidthMhz(2e6) == 2 && maxWidthMhz(4e6) == 4 && maxWidthMhz(8e6) == 8 && maxWidthMhz(10e6) == 8 && maxWidthMhz(16e6) == 16 && maxWidthMhz(1.9e6) == 0, "rate limits");
        const HalowTelemetry t = runSchedule(4e6, 1.0);
        CHECK(t.maxWidthMhz == 4 && t.nets.size() == 4 && t.perWidth[3] == 0 && t.perWidth[4] == 0, "4 Msps: max %d MHz, %zu networks", t.maxWidthMhz, t.nets.size());
    }
    {   // a manual width: only 2 MHz frames are read
        const HalowTelemetry t = runSchedule(20e6, 1.0, 1 << W2);
        CHECK(t.widthMask == (1 << W2) && t.perWidth[1] > 0 && t.perWidth[0] == 0 && t.perWidth[2] == 0 && t.perWidth[3] == 0 && t.perWidth[4] == 0, "width mask: %llu %llu %llu %llu %llu", (unsigned long long)t.perWidth[0],
              (unsigned long long)t.perWidth[1], (unsigned long long)t.perWidth[2], (unsigned long long)t.perWidth[3], (unsigned long long)t.perWidth[4]);
        CHECK(findNet(t, "Farm-Sensors") && !findNet(t, "HaLow-Home") && !findNet(t, "OpenCam"), "manual width networks");
    }
    {   // PPDUs that are reported from the SIG field only, at every width
        for (int w = 0; w < kNumWidths; w++) {
            struct K { const char* name; int nss; bool stbc, ldpc, agg, ndp; };
            for (const K& k : {K{"2 streams", 2, false, false, false, false}, K{"STBC", 1, true, false, false, false}, K{"LDPC", 1, false, true, false, false}, K{"A-MPDU", 1, false, false, true, false}, K{"NDP", 1, false, false, false, true}}) {
                HalowGenFrame f;
                f.psdu = payloadFrame(40); f.width = w; f.mcs = 2; f.nss = k.nss; f.stbc = k.stbc; f.ldpc = k.ldpc; f.agg = k.agg; f.ndp = k.ndp;
                if (w == W1 && k.nss == 2 && !k.stbc) f.mcs = 1;
                const Result r = run(f, sets()[0], recommendedRateHz(w), 10);
                const HalowTelemetry& t = r.t;
                const uint64_t got = k.ndp ? t.ndpFrames : k.stbc ? t.stbcFrames : k.ldpc ? t.ldpcFrames : k.agg ? t.aggFrames : t.mimoFrames;
                CHECK(got >= 8 && r.ok == 0 && r.bad == 0 && t.undecoded >= got, "%d MHz %s: counted %llu, %llu good, %llu bad", 1 << w, k.name, (unsigned long long)got, (unsigned long long)r.ok, (unsigned long long)r.bad);
                if (!k.ndp && !t.log.empty()) CHECK(!t.log[0].decoded && t.log[0].nss == (k.stbc ? 1 : k.nss) && t.log[0].widthMhz == 1 << w, "%d MHz %s: log line", 1 << w, k.name);
            }
        }
    }
    {   // a beamformed long-preamble frame: the data goes through a channel the SIG field does not see; the D-LTF carries it
        for (int w : {W2, W4, W8, W16}) for (double tilt : {0.05, -0.12}) {
            HalowGenFrame f;
            f.psdu = payloadFrame(60); f.width = w; f.mcs = w == W2 ? 4 : 6; f.pre = PreLong; f.beamformed = true; f.beamTilt = tilt;
            const Result r = run(f, sets()[0], recommendedRateHz(w), 10);
            CHECK(r.ok >= 9 && r.bad == 0, "beamformed %d MHz tilt %.2f: %llu good, %llu bad", 1 << w, tilt, (unsigned long long)r.ok, (unsigned long long)r.bad);
        }
    }
    {   // a combination the standard does not allow (2 MHz MCS 9 with one stream; MCS 10 above 1 MHz) is not a SIG field
        for (auto [w, mcs] : {std::pair<int, int>{W2, 9}, {W4, 10}, {W8, 10}, {W16, 10}}) {
            HalowGenFrame f;
            f.psdu = payloadFrame(30); f.width = w; f.mcs = mcs;
            const Result r = run(f, sets()[0], recommendedRateHz(w), 10);
            CHECK(r.t.sigOk == 0 && r.ok == 0, "%d MHz MCS %d was accepted (%llu SIG)", 1 << w, mcs, (unsigned long long)r.t.sigOk);
        }
    }
    {   // the longest PSDU (511 bytes) at the slowest and fastest MCS of the widths that keep it short enough to test quickly
        for (auto [w, mcs] : {std::pair<int, int>{W2, 0}, {W4, 0}, {W8, 0}, {W16, 0}, {W16, 9}, {W4, 9}}) {
            HalowGenFrame f;
            f.psdu = payloadFrame(483); f.width = w; f.mcs = mcs;
            CHECK(f.psdu.size() == 511, "psdu size");
            const Result r = run(f, sets()[1], recommendedRateHz(w), 6);
            CHECK(r.ok >= 5 && r.bad == 0, "511 bytes at %d MHz MCS %d: %llu good, %llu bad", 1 << w, mcs, (unsigned long long)r.ok, (unsigned long long)r.bad);
        }
    }
    {   // a random mixture of every width, preamble, MCS, guard interval, level and offset, back to back at 20 Msps over noise
        std::mt19937 rng(77);
        HalowGenConfig c;
        c.rate = 20e6; c.snrDb = 55; c.cfoHz = 8e3; c.dialOffsetHz = 250e3;
        int sent = 0;
        for (int i = 0; i < 160; i++) {
            HalowGenFrame f;
            f.width = (int)(rng() % 5);
            do { f.mcs = (int)(rng() % 11); } while (!mcsValid(f.width, f.mcs, 1));
            f.pre = f.width == W1 ? -1 : (rng() & 1) ? PreLong : PreShort;
            f.sgi = rng() & 1;
            f.psdu = payloadFrame(20 + rng() % 120);
            f.cfoHz = ((int)(rng() % 60000) - 30000);
            f.ampScale = 0.5 + (double)(rng() % 100) / 100.0;
            f.gapSec = 0.0001 + 0.0004 * (double)(rng() % 4);
            c.frames.push_back(f);
            sent++;
        }
        c.loop = false;
        HalowGenerator g(c);
        HalowReceiver r;
        r.configure(c.rate);
        r.setSignalOffset(c.dialOffsetHz);
        std::vector<cf32> buf(16384);
        double total = 0;
        for (const auto& f : c.frames) total += halowFrameSeconds(f) + f.gapSec;
        for (size_t done = 0; done < (size_t)((total + 0.01) * c.rate); done += buf.size()) { g.generate(buf.data(), buf.size()); r.feed(buf.data(), buf.size()); }
        r.flush();
        HalowTelemetry t;
        r.telemetry(t, 0);
        printf("  random mixture: %llu of %d good, %llu bad\n", (unsigned long long)t.blocksOk, sent, (unsigned long long)t.blocksBad);
        CHECK((double)t.blocksOk >= 0.9 * sent && t.blocksBad <= 1, "mixture: %llu good, %llu bad of %d", (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad, sent);
    }
    {   // noise alone and a strong tone, 20 Msps: nothing decoded, hardly any detections
        HalowReceiver r;
        r.configure(20e6);
        std::vector<cf32> buf(65536);
        double ph = 0;
        for (int k = 0; k < 120; k++) {
            for (auto& v : buf) { ph += 2 * 3.14159265358979 * (k < 60 ? 0.0 : 300e3) / 20e6; v = k < 60 ? cf32(0, 0) : cf32((float)std::cos(ph) * 0.3f, (float)std::sin(ph) * 0.3f); }
            genutil::NoiseSource ns(5 + k);
            ns.add(buf.data(), buf.size(), 0.02f);
            r.feed(buf.data(), buf.size());
        }
        r.flush();
        HalowTelemetry t;
        r.telemetry(t, 0);
        CHECK(t.blocksOk == 0 && t.nets.empty() && t.sigOk == 0 && t.preambles < 10, "noise / tone: %llu frames, %llu SIG, %llu detections", (unsigned long long)t.blocksOk, (unsigned long long)t.sigOk, (unsigned long long)t.preambles);
    }
    return testSummary("halow modes");
}
