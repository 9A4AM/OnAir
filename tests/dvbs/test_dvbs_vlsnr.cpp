// DVB-S2X VL-SNR frames (EN 302 307-2 clause 5.5.2.5 and tables 18a to 19d): the codes of all nine modes (BCH, shortening, puncturing, LDPC) through noise at
// 1.5 dB above the ideal Es/N0 of tables 20a to 20c, the VL-SNR header patterns, the PLFRAME layout of figures 17 and 18, the frame builder, and generator ->
// receiver round trips (transport stream equal) for the modes the front end can acquire at the Es/N0 they need. Only generated signals: no real VL-SNR
// transmission was available, see the notes in core/src/dvbs_vlsnr.h on what the text of the standard leaves open.
#include "../../core/src/dvbs/dvbs_vlsnr.h"
#include "dect2/common/stable_random.h"
#include "dect2/dvbs/dvbs_testkit.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include "dect2/common/mathconst.h"

using namespace dect2;
using namespace dect2::dvbs;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main() {
    std::mt19937 rng(4242);

    // ---- table 7: g(x) of the medium frame is the product of the twelve polynomials
    {
        static const char* g[12] = {"0 2 3 5 15", "0 1 4 7 10 11 15", "0 2 4 6 8 10 12 13 15", "0 2 3 5 6 8 10 11 15", "0 1 2 4 6 7 10 12 15", "0 4 6 7 12 13 15",
                                    "0 2 4 5 7 11 12 14 15", "0 2 4 6 8 9 11 14 15", "0 1 2 4 5 7 9 11 12 13 15", "0 1 2 3 4 7 10 11 12 13 15", "0 1 2 4 9 11 15",
                                    "0 2 4 8 10 11 13 14 15"};
        std::vector<uint8_t> prod(1, 1);
        for (const char* s : g) {
            std::vector<uint8_t> p(16, 0);
            for (const char* c = s; *c;) { char* e; const long x = strtol(c, &e, 10); p[(size_t)x] = 1; c = e; while (*c == ' ') c++; }
            std::vector<uint8_t> o(prod.size() + 15, 0);
            for (size_t i = 0; i < prod.size(); i++) if (prod[i]) for (size_t j = 0; j < 16; j++) if (p[j]) o[i + j] ^= 1;
            prod = o;
        }
        CHECK(prod == vlMediumBchGenerator(), "table 7: the product of g1 .. g12 is not the generator of the medium BCH code");
        CHECK(prod.size() == 181, "BCH degree %zu", prod.size() - 1);
    }

    // ---- table 18a / 19a: sizes
    {
        for (int i = 0; i < kVlModes; i++) {
            const VlMode& m = vlMode(i);
            CHECK(m.nbch == m.kbch + (m.nldpc == 32400 ? 180 : m.nldpc == 16200 ? 168 : 192), "%s: Nbch %d", vlModeName(i), m.nbch);
            CHECK(m.nbch + m.xs == m.kldpc, "%s: Nbch + Xs = kldpc", vlModeName(i));
            CHECK(m.symbols == (m.set == 1 ? 30780 : 14976), "%s: %d symbols", vlModeName(i), m.symbols);
            CHECK(m.xp * m.period <= m.nldpc - m.kldpc, "%s: puncturing pattern too long", vlModeName(i));
            CHECK(vlModeByWalsh(m.walsh) == i, "%s: walsh index", vlModeName(i));
        }
        CHECK(vlModeByWalsh(kVlDummyWalsh) < 0 && vlModeByWalsh(6) < 0 && vlModeByWalsh(15) < 0, "dummy and unassigned patterns are no modes");
        CHECK(vlLayout(1).size() == (size_t)(kVlSet1Symbols - 990) && vlLayout(2).size() == (size_t)(kVlSet2Symbols - 990), "layout sizes");
        for (int set = 1; set <= 2; set++) {
            int data = 0, reg = 0, add = 0;
            for (uint8_t k : vlLayout(set)) (k == 0 ? data : k == 1 ? reg : add)++;
            CHECK(data == (set == 1 ? 30780 : 14976), "set %d: %d data symbols", set, data);
            CHECK(reg == (set == 1 ? 22 : 11) * 36 && add == (set == 1 ? 18 * 34 + 3 * 36 : 9 * 32 + 36), "set %d: pilots %d + %d", set, reg, add);
            CHECK(s2PlsFrameSymbols(vlPlsCode(set)) == vlFrameSymbols(set), "set %d: PLS code length", set);
        }
    }

    // ---- the headers: 900 symbols, orthogonal enough to be told apart in noise
    {
        cf32 h[16][900];
        for (int w = 0; w < 16; w++) vlHeaderSymbols(w, h[w]);
        for (int w = 0; w < 16; w++) {
            float sc, sec;
            const int got = vlHeaderDetect(h[w], &sc, &sec);
            CHECK(got == w && sc > 0.999f && sec < 0.01f, "header %d detected as %d (%.3f, second %.3f)", w, got, sc, sec);
        }
        // at Es/N0 = -10 dB: 900 symbols are plenty
        int wrong = 0;
        dect2::normal_distribution<float> nd(0.f, std::sqrt(0.5f * 10.f));
        for (int t = 0; t < 200; t++) {
            const int w = (int)(rng() % 16);
            cf32 x[900];
            for (int j = 0; j < 900; j++) x[j] = h[w][j] + cf32{nd(rng), nd(rng)};
            if (vlHeaderDetect(x) != w) wrong++;
        }
        CHECK(wrong == 0, "%d of 200 headers misread at -10 dB", wrong);
    }

    // ---- every mode: BBFRAME -> BCH -> shorten -> LDPC -> puncture -> map -> noise at QEF + 1.5 dB -> demap -> LDPC -> BCH -> BBFRAME
    {
        for (int i = 0; i < kVlModes; i++) {
            const VlMode& m = vlMode(i);
            std::vector<uint8_t> bb((size_t)m.kbch), tx;
            for (auto& b : bb) b = rng() & 1;
            vlEncode(bb, i, tx);
            CHECK((int)tx.size() == m.txBits, "%s: %zu bits sent, wanted %d", vlModeName(i), tx.size(), m.txBits);
            std::vector<cf32> sym((size_t)m.symbols);
            vlMap(tx.data(), i, sym.data());
            // without noise
            std::vector<float> llr((size_t)m.txBits);
            vlDemap(sym.data(), i, 0.05f, llr.data());
            std::vector<uint8_t> out;
            int it = 0;
            int fixed = vlDecode(llr.data(), i, 100, out, &it);
            CHECK(fixed == 0 && out == bb, "%s: clean frame did not decode (%d)", vlModeName(i), fixed);
            // with noise (sigma2 per real dimension for unit symbol energy)
            const double esn0 = m.esn0 + 1.5;
            const float sigma2 = (float)(0.5 * std::pow(10.0, -esn0 / 10.0));
            dect2::normal_distribution<float> nd(0.f, std::sqrt(sigma2));
            int bad = 0;
            double itSum = 0;
            const int trials = 6;
            for (int t = 0; t < trials; t++) {
                std::vector<cf32> y = sym;
                for (auto& s : y) s += cf32{nd(rng), nd(rng)};
                vlDemap(y.data(), i, sigma2, llr.data());
                fixed = vlDecode(llr.data(), i, 100, out, &it);
                itSum += it;
                if (fixed < 0 || out != bb) bad++;
            }
            CHECK(bad == 0, "%s: %d of %d frames failed at %.2f dB (ideal %.2f)", vlModeName(i), bad, trials, esn0, m.esn0);
            printf("%-14s %s FECFRAME, %d bits sent, %d symbols, %d frames at %.2f dB decoded, %.0f LDPC iterations\n", vlModeName(i), m.frame, m.txBits, m.symbols, trials - bad, esn0, itSum / trials);
        }
    }

    // ---- the frame builder: lengths, PLS code, header, pilots unmodulated after the scrambling is taken out
    {
        for (int mode = -1; mode < kVlModes; mode++) {
            const int set = mode < 0 ? 2 : vlMode(mode).set;
            std::vector<uint8_t> bb(mode < 0 ? 0 : (size_t)vlMode(mode).kbch, 0);
            for (auto& b : bb) b = rng() & 1;
            std::vector<cf32> f;
            vlBuildFrame(mode, bb, 0, 0, f);
            CHECK((int)f.size() == vlFrameSymbols(set), "mode %d: frame of %zu symbols", mode, f.size());
            const PlsResult pr = s2PlsDecode(&f[26]);
            CHECK(pr.code == vlPlsCode(set) && s2PlsKind(pr.code) == 2, "mode %d: PLS code %d", mode, pr.code);
            float sc, sec;
            const int w = vlHeaderDetect(&f[90], &sc, &sec);
            CHECK(w == (mode < 0 ? kVlDummyWalsh : vlMode(mode).walsh) && sc > 0.99f, "mode %d: header %d", mode, w);
            const auto& rn = s2ScramblingRn(0);
            const auto& lay = vlLayout(set);
            double pil = 0;
            int n = 0;
            for (size_t k = 0; k < lay.size(); k++) {
                if (mode >= 0 && !lay[k]) continue;
                const cf32 x = s2RotateByR(f[990 + k], (4 - rn[990 + k - 90]) & 3);
                pil += x.real() * dect2::math::k1OverSqrt2f + x.imag() * dect2::math::k1OverSqrt2f; n++;
            }
            CHECK(std::fabs(pil / n - 1.0) < 1e-4, "mode %d: pilots / dummy symbols are not the scrambled (1 + j) / sqrt 2 (%.4f)", mode, pil / n);
        }
    }

    // ---- generator -> receiver. At these Es/N0 the carrier is acquired by the symbol timing loop and the frame search of the front end
    // (the lowest modes need about 3 dB over the ideal Es/N0 of the tables in the whole chain: timing and carrier recovery at -5 dB; the codes alone decode at 1.5 dB)
    struct Case { int mode; double margin; double secs; int dummyEvery; int shift; bool inverted; };
    const Case cases[] = {{0, 3.0, 5.0, 0, 0, false}, {3, 4.0, 5.0, 0, 0, false}, {2, 4.0, 5.0, 0, 0, false}, {1, 5.0, 5.0, 0, 0, false}, {8, 4.0, 4.0, 0, 0, false}, {7, 4.0, 4.0, 0, 0, false},
                          {6, 4.0, 4.0, 0, 0, false}, {5, 4.0, 6.0, 0, 0, false}, {4, 5.0, 6.0, 0, 0, false},
                          {6, 5.0, 4.0, 3, 0, false},        // a VL-SNR dummy frame after every three data frames
                          {8, 5.0, 4.0, 0, -900, false},     // the scrambling sequence started after the VL-SNR header
                          {7, 5.0, 4.0, 0, 0, true}};        // spectrum inverted
    const int only = std::getenv("VL_MODE") ? std::atoi(std::getenv("VL_MODE")) : -1;
    const double over = std::getenv("VL_MARGIN") ? std::atof(std::getenv("VL_MARGIN")) : -1000;
    const double cfo = std::getenv("VL_CFO") ? std::atof(std::getenv("VL_CFO")) : 20e3;
    for (const Case& cs : cases) {
        if (only >= 0 && cs.mode != only) continue;
        const VlMode& m = vlMode(cs.mode);
        RunConfig rc = makeRun(3, kQpsk, 0, 2e6, 4e6, m.esn0 + (over > -999 ? over : cs.margin), cs.secs);
        rc.sig.tx.vlMode = cs.mode; rc.sig.tx.vlDummyEvery = cs.dummyEvery; rc.sig.tx.vlScrShift = cs.shift; rc.sig.inverted = cs.inverted;
        rc.sig.cfoHz = cfo;
        const RunResult r = runCase(rc);
        const double perSec = dvbsNetBitrate(rc.sig.tx) / 1504.0;
        const std::string what = std::string("VL-SNR ") + vlModeName(cs.mode) + (cs.dummyEvery ? " with dummy frames" : "") + (cs.shift ? " (scrambling from the VL-SNR header)" : "") + (cs.inverted ? " inverted" : "");
        CHECK(r.good >= perSec * (cs.secs - 2.5), "%s: only %llu good packets (wanted %.0f); %s", what.c_str(), (unsigned long long)r.good, perSec * (cs.secs - 2.5), dvbsSummary(r.tel).c_str());
        CHECK(r.bad == 0 && r.gaps == 0, "%s: %llu damaged packets, %llu jumps in the counter", what.c_str(), (unsigned long long)r.bad, (unsigned long long)r.gaps);
        CHECK(r.tel.standard == 3 && r.tel.modcod == vlPlsCode(m.set), "%s: reported as standard %d, MODCOD %d", what.c_str(), r.tel.standard, r.tel.modcod);
        CHECK(r.tel.modulationName == (m.bpsk ? "pi/2 BPSK" : "QPSK") && r.tel.codeRate == std::string(m.rate) + (m.spread == 2 ? " SF2" : ""), "%s: reported as %s %s", what.c_str(), r.tel.modulationName.c_str(), r.tel.codeRate.c_str());
        if (r.good < perSec * (cs.secs - 2.5)) for (size_t i = 0; i < r.log.size() && i < 12; i++) printf("    log: %s\n", r.log[i].c_str());
        printf("%s: %llu packets, first after %.2f s, Es/N0 %.1f dB (ideal %.2f), MER %.1f dB, %s\n", what.c_str(), (unsigned long long)r.good, r.firstPacketSecs, rc.sig.snrDb, m.esn0, r.tel.merDb,
               dvbsSummary(r.tel).c_str());
    }

    // ---- the low roll-offs of EN 302 307-2 table 2: the BBHEADER alternates RO = 11 with 00, 01, 10 for 0.15, 0.10, 0.05 (the receiver reports them as signalled)
    for (double ro : {0.15, 0.10, 0.05}) {
        RunConfig rc = makeRun(3, kQpsk, 0, 2e6, 4e6, 12.0, 5.0);
        rc.sig.tx.rollOff = ro;
        const RunResult r = runCase(rc);
        const double perSec = dvbsNetBitrate(rc.sig.tx) / 1504.0;
        CHECK(r.good >= perSec * 2.0 && r.bad == 0 && r.gaps == 0, "S2X roll-off %.2f: %llu good, %llu bad packets, %llu jumps; %s", ro, (unsigned long long)r.good, (unsigned long long)r.bad,
              (unsigned long long)r.gaps, dvbsSummary(r.tel).c_str());
        CHECK(std::fabs(r.tel.rollOff - ro) < 0.011 && r.tel.rollOffSource >= 1, "S2X roll-off %.2f: reported %.2f (source %d)", ro, r.tel.rollOff, r.tel.rollOffSource);
        printf("S2X roll-off %.2f: %llu packets, reported %.2f (source %d)\n", ro, (unsigned long long)r.good, r.tel.rollOff, r.tel.rollOffSource);
    }

    printf(fails ? "dvbs vlsnr: FAILED (%d)\n" : "dvbs vlsnr: ok\n", fails);
    return fails ? 1 : 0;
}
