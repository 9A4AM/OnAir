// GNSS receiver: orchestrates the bands, the acquisition, the tracking channels, the navigation data base and the position solution.
#include "dect2/gnss_rx.h"
#include "dect2/gnss_acq.h"
#include "dect2/gnss_bds.h"
#include "dect2/gnss_codes.h"
#include "dect2/gnss_front.h"
#include "dect2/gnss_msg.h"
#include "dect2/gnss_nav.h"
#include "dect2/gnss_solve.h"
#include "dect2/gnss_track.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <mutex>

namespace dect2 {

namespace {

constexpr double kGpsFsOut = 4.096e6;
constexpr int kGpsN = 4096;
constexpr int kMaxChannels = 40;
constexpr size_t kBlock = 16384;
// The search: a static receiver sees the satellites within +-5 kHz of the radio's own frequency error; that error is up to 2 kHz with a TCXO,
// 31 kHz on a HackRF (20 ppm) and 160 kHz on a dongle without one (100 ppm). The windows widen in that order while nothing is found.
constexpr double kSatDopplerHz = 5500.0;
constexpr double kMidHz = 45000.0, kWideHz = 170000.0;
constexpr int kShortMs = 16, kLongMs = 64;

struct SatNav {
    GpsEphemeris eph;            // the complete, consistent ephemeris
    bool hasEph = false;
    int week = 0;                // full week of the ephemeris (from subframe 1)
    GpsEphemeris pend;           // being collected
    int pendWeek = 0;
    GpsAlmanac alm;
    double ephSignalTime = 0;    // signal time when it was received
    int towLast = -1;
    int lastSfId = 0;            // the last subframe received and when (for the time left until the ephemeris is complete)
    double lastSfTime = -1;
    // SBAS
    int sbasLast = -1;
    uint32_t sbasCount = 0, sbasTypes = 0;
};
// the L1 C/A PRNs share one numbering: GPS 1..32, SBAS 120..158, QZSS 193..202
constexpr int kCaPrns = 203;
// Galileo E1-B: 4 ms codes of 4092 chips with the BOC(1,1) subcarrier, counted in half chips at 2.046 MHz; one code period is 16384 samples of the band
constexpr int kGalPrns = 37;                 // 1..36
constexpr int kGalHalfChips = 2 * kGalE1Len;
constexpr int kGalN = 16384;
constexpr int kGalMs = 4;                    // code periods integrated per search (16 ms, without coherent pairs: a symbol every period)
// BeiDou B1I: 1 ms codes of 2046 chips at 2.046 Mcps, 4096 samples of its own band; ranging codes 1..37
constexpr int kBdsN = 4096;
constexpr int kBdsPrns = 64;

// The search of a band without GPS to lean on: the window widens while nothing is found, as for GPS (planSearch)
struct SearchPlan {
    int stage = 0;
    uint32_t stageRounds = 0;
    int roundHits = 0, lastRoundHits = 0;
    bool longRound = false;
    double lastCfo = 0;
    bool lastCfoValid = false;
};

double cpuNow() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

std::string satName(int sys, int prn) { return gnssSatName(sys, prn); }

} // namespace

struct GnssReceiver::Impl {
    std::mutex procMu, telMu;
    std::atomic<double> centerMhz{1575.42};
    std::atomic<unsigned> sysMask{0xFFFFFFFFu};
    std::atomic<int> weekRef{0};
    std::atomic<double> dopHalf{10000.0};
    std::atomic<double> cfoHintHz{0};
    std::atomic<bool> cfoHintValid{false};
    std::atomic<int> acqRate{10};
    std::atomic<double> elMask{5.0};
    std::atomic<double> hintLat{0}, hintLon{0};
    std::atomic<bool> hintValid{false};
    std::atomic<uint32_t> settingsGen{1};
    std::atomic<double> rateAt{0};
    uint32_t planGen = 0;
    double rate = 0;
    std::function<void(const std::string&)> log;
    std::function<void(const std::vector<GnssMeasurement>&)> measCb;
    uint64_t units = 0;
    double cpuFront = 0, cpuTrack = 0, cpuAcq = 0;

    // front end
    double dcI = 0, dcQ = 0, gain = 1, levelRms = 0;
    bool dcInit = false;
    double clipFrac = 0;
    int64_t inSamples = 0;
    std::vector<cf32> blk;
    std::vector<cf32> pend;          // input waiting for a full block
    // GPS band
    bool gpsActive = false;
    GnssBand gpsBand;
    GnssAcq gpsAcq;
    uint8_t gpsChips[kCaPrns][kGpsCaLen];
    // Galileo E1-B on the same band
    bool galActive = false;
    GnssAcq galAcq;
    GnssSignalSpec galSpec;
    std::vector<uint8_t> galChips[kGalPrns];     // half chips: the code chip xor the subcarrier half (logic 0 = +1)
    double galCredit = 0;
    SatNav gsat[kGalPrns];
    GalNav gnav[kGalPrns];
    bool ggtoValid = false;                      // GST - GPS from word type 10
    double ggtoA0 = 0, ggtoA1 = 0, ggtoT0 = 0;
    int ggtoWn0 = 0, ggtoWn = 0;
    std::vector<int> predictedGal;
    GnssAcq auxAcq;
    double auxCredit = 0;
    uint32_t auxRoundSeen = 0;
    int auxRoundHits = 0, auxEmptyRounds = 0;
    double auxIdleUntil = 0;
    uint32_t galRoundSeen = 0;
    bool galLong = false;
    double codeRateFrac = 0;
    int galRoundHits = 0, galEmptyRounds = 0;
    double galIdleUntil = 0;
    GnssSignalSpec gpsSpec, qzssSpec, sbasSpec;
    // BeiDou B1I band (its own tune, or with L1 at 20 Msps and more)
    bool bdsActive = false;
    GnssBand bdsBand;
    GnssAcq bdsAcq;
    GnssSignalSpec bdsD1Spec, bdsD2Spec;
    std::vector<uint8_t> bdsChips[kBdsPrns];
    SatNav bsat[kBdsPrns];
    BdsNav bnav[kBdsPrns];
    bool bdtGpsValid = false;                    // BDT - GPS from D1 subframe 5 page 9
    double bdtA0 = 0, bdtA1 = 0;
    double bdsCredit = 0;
    SearchPlan bdsPlan;
    uint32_t bdsRoundSeen = 0;
    int bdsRoundHits = 0, bdsEmptyRounds = 0;
    double bdsIdleUntil = 0;
    double acqCredit = 0;
    std::vector<std::unique_ptr<GnssTracker>> trackers;
    // navigation data
    SatNav sat[kCaPrns];
    GpsIono iono;
    GpsUtc utc;
    double almToa = -1;
    int almWna = -1;
    bool almGot = false;
    // fix
    GnssFix fix;
    bool hasPrior = false;
    double prior[3] = {0, 0, 0};
    double nextMeas = 1.0;
    uint64_t okRetired = 0, badRetired = 0;
    uint64_t fixCount = 0;
    double firstFix = -1;
    double lastFixSignal = -1;
    bool clockSet = false;
    double clockOffset = 0;
    uint64_t sbasTotal = 0;
    double cfoEst = 0, driftEst = 0;
    bool cfoValid = false;
    // the search plan: the stage of widening while nothing is found, and the last frequency error that worked
    int stage = 0;
    uint32_t stageRounds = 0;
    int roundHits = 0, lastRoundHits = 0;   // finds in the round in progress and in the last one
    bool longRound = false;
    double lastCfo = 0;
    bool lastCfoValid = false;
    double firstLock = -1;
    std::vector<GnssMeasurement> lastMeas;
    std::vector<int> predictedVisible;
    bool havePredicted = false;
    // per-satellite last report of the fix
    struct SolInfo { bool used = false; float resid = 0; float az = 0, el = 0; bool has = false; };
    SolInfo sol[kCaPrns];
    SolInfo gsol[kGalPrns];
    SolInfo bsol[kBdsPrns];
    SatNav& navOf(int sys, int prn) { return sys == GnssGalileo ? gsat[prn] : sys == GnssBeidou ? bsat[prn] : sat[prn]; }
    const SatNav& navOf(int sys, int prn) const { return sys == GnssGalileo ? gsat[prn] : sys == GnssBeidou ? bsat[prn] : sat[prn]; }
    SolInfo& solOf(int sys, int prn) { return sys == GnssGalileo ? gsol[prn] : sys == GnssBeidou ? bsol[prn] : sol[prn]; }
    const SolInfo& solOf(int sys, int prn) const { return sys == GnssGalileo ? gsol[prn] : sys == GnssBeidou ? bsol[prn] : sol[prn]; }
    // BDT - GPS time less the whole 14 s at BDT t (A0GPS + A1GPS t, ICD 5.2.4.18)
    double bdtAt(double t) const { return bdtGpsValid ? bdtA0 + bdtA1 * t : 0.0; }
    // a satellite's own time (GPS, QZSS: GPS time; Galileo: GST; BeiDou: BDT) on the GPS time scale
    double toGpsTime(int sys, double t) const {
        if (sys == GnssGalileo) return t - ggtoAt(t);
        if (sys == GnssBeidou) { double g = t + kBdtGpsSeconds - bdtAt(t); if (g >= 604800.0) g -= 604800.0; return g; }
        return t;
    }
    static double carrierOf(int sys) { return sys == GnssBeidou ? kBdsB1Hz : kGpsL1Hz; }
    GnssBand& bandOf(const GnssTracker& t) { return t.band == 1 ? bdsBand : gpsBand; }
    // GST - GPS time at GPS time of week t (word type 10: A0G + A1G (t - t0G + 604800 (WN - WN0G)), the 6 bit week difference within +-31)
    double ggtoAt(double t) const {
        if (!ggtoValid) return 0;
        int dw = ((ggtoWn - ggtoWn0) % 64 + 64) % 64;
        if (dw > 31) dw -= 64;
        return ggtoA0 + ggtoA1 * (t - ggtoT0 + 604800.0 * dw);
    }
    // telemetry
    uint64_t seq = 0;
    double nextReport = 0.25;
    GnssTelemetry tel;
    bool telReady = false;
    std::vector<float> acqCorr;

    void logf(const std::string& s) { if (log) log(s); }

    int referenceWeek() const {
        int w = weekRef.load();
        if (w > 0) return w;
        const time_t now = time(nullptr);
        if (now > 1700000000) return (int)((now - 315964800 + 18) / 604800);
        return 2400;
    }

    // ------------------------------------------------------------------ set-up
    void replan() {
        planGen = settingsGen.load();
        const double center = centerMhz.load() * 1e6;
        const unsigned mask = sysMask.load();
        bool wantGps = (mask & gnssSystemBit(GnssGps)) != 0;
        const double off = kGpsL1Hz - center;
        // the band must fit: the signal's main lobe (+-1.023 MHz) inside half the input rate, and the input must be fast enough
        const bool fits = rate >= 2.0e6 && std::fabs(off) + 1.0e6 <= 0.5 * rate;
        wantGps = wantGps && fits;
        if (wantGps) {
            if (!gpsActive || std::fabs(gpsBand.offsetHz() - off) > 1.0 || lastRate != rate || mask != gpsMaskNow) {
                gpsBand.init(rate, off, kGpsFsOut, (size_t)1 << 19);
                GnssAcqConfig ac;
                ac.sys = GnssGps; ac.fftLog2 = 12; ac.fsOut = kGpsFsOut; ac.blocks = kShortMs; ac.rfHz = kGpsL1Hz;
                // a false alarm costs little: the hit is looked for again (confirm) and a channel that does not lock gives up
                ac.pfa = 1e-3;
                const int q = (int)std::ceil(dopHalf.load() / 1000.0);
                ac.qMin = -q; ac.qMax = q - 1;
                for (int p = 1; p <= 32; p++) ac.prns.push_back(p);
                ac.replica = [](int prn, cf32* out, int n) {
                    uint8_t c[kGpsCaLen];
                    if (!l1caChips(prn, c)) return false;
                    // sampled the way a receiver at 4.096 Msps sees the chips: the chip under the centre of each sample
                    for (int i = 0; i < n; i++) {
                        const int idx = (int)(((int64_t)i * 2 + 1) * kGpsCaLen / (2 * (int64_t)n));
                        out[i] = cf32(c[idx] ? -1.f : 1.f, 0.f);
                    }
                    return true;
                };
                gpsAcq.init(ac);
                // QZSS and SBAS have a search of their own (runAuxAcq), so the GPS search runs exactly as without them
                ac.prns.clear();
                if (mask & gnssSystemBit(GnssQzss)) for (int p = 193; p <= 202; p++) ac.prns.push_back(p);
                if (mask & gnssSystemBit(GnssSbas)) for (int p = 120; p <= 158; p++) ac.prns.push_back(p);
                auxAcq.init(ac);
                auxCredit = 0;
                stage = 0; stageRounds = 0;
                for (int p = 1; p < kCaPrns; p++) l1caChips(p, gpsChips[p]);
                gpsSpec.sys = GnssGps; gpsSpec.chipRate = kGpsCaChipRate; gpsSpec.codeLen = kGpsCaLen; gpsSpec.rfHz = kGpsL1Hz; gpsSpec.fsOut = kGpsFsOut; gpsSpec.halfSpacing = 2;
                qzssSpec = gpsSpec; qzssSpec.sys = GnssQzss;
                sbasSpec = gpsSpec; sbasSpec.sys = GnssSbas; sbasSpec.msg = GnssMsgSbas;
                trackers.clear();
                acqCredit = 0;
                galRebuild = true;
            }
            gpsActive = true;
            gpsMaskNow = mask;
        } else {
            gpsActive = false;
            dropBand(0);
        }
        // Galileo E1-B shares the band; its BOC(1,1) lobes reach 2 MHz from the carrier, so the input must be 3.5 Msps or more
        const bool wantGal = gpsActive && (mask & gnssSystemBit(GnssGalileo)) && rate >= 3.5e6;
        if (wantGal) {
            if (!galActive || galRebuild) {
                GnssAcqConfig ac;
                ac.sys = GnssGalileo; ac.fftLog2 = 14; ac.fsOut = kGpsFsOut; ac.blocks = kGalMs; ac.rfHz = kGpsL1Hz;
                ac.pfa = 1e-3; ac.coherentPairs = false; ac.offsets = 2;
                ac.qMin = -24; ac.qMax = 23;
                for (int p = 1; p < kGalPrns; p++) ac.prns.push_back(p);
                ac.replica = [](int prn, cf32* out, int n) {
                    uint8_t c[kGalE1Len];
                    if (!galE1bChips(prn, c)) return false;
                    for (int i = 0; i < n; i++) {
                        const int h = (int)(((int64_t)i * 2 + 1) * kGalHalfChips / (2 * (int64_t)n));
                        out[i] = cf32((c[h / 2] ^ (h & 1)) ? -1.f : 1.f, 0.f);
                    }
                    return true;
                };
                galAcq.init(ac);
                for (int p = 1; p < kGalPrns; p++) {
                    uint8_t c[kGalE1Len];
                    galChips[p].assign((size_t)kGalHalfChips, 0);
                    if (galE1bChips(p, c)) for (int h = 0; h < kGalHalfChips; h++) galChips[p][(size_t)h] = (uint8_t)(c[h / 2] ^ (h & 1));
                }
                galSpec.sys = GnssGalileo; galSpec.chipRate = 2 * kGalE1ChipRate; galSpec.codeLen = kGalHalfChips; galSpec.rfHz = kGpsL1Hz; galSpec.fsOut = kGpsFsOut;
                galSpec.halfSpacing = 1;           // a quarter chip: inside the narrow main peak of BOC(1,1)
                galSpec.msg = GnssMsgInav; galSpec.epochS = 4e-3;
                galCredit = 0;
            }
            galActive = true;
        } else {
            if (galActive) trackers.erase(std::remove_if(trackers.begin(), trackers.end(), [](const std::unique_ptr<GnssTracker>& t) { return t->sys == GnssGalileo; }), trackers.end());
            galActive = false;
        }
        galRebuild = false;
        // BeiDou B1I, 1561.098 MHz: the main lobe (+-2.046 MHz) mostly inside the band; 4 Msps on its own carrier is enough
        const double offB = kBdsB1Hz - center;
        const bool wantBds = (mask & gnssSystemBit(GnssBeidou)) && rate >= 3.9e6 && std::fabs(offB) + 1.0e6 <= 0.5 * rate;
        if (wantBds) {
            if (!bdsActive || std::fabs(bdsBand.offsetHz() - offB) > 1.0 || lastRate != rate) {
                dropBand(1);
                bdsBand.init(rate, offB, kGpsFsOut, (size_t)1 << 19);
                GnssAcqConfig ac;
                ac.sys = GnssBeidou; ac.fftLog2 = 12; ac.fsOut = kGpsFsOut; ac.blocks = kShortMs; ac.rfHz = kBdsB1Hz;
                ac.pfa = 1e-3;
                ac.coherentPairs = false;          // the NH code turns the D1 symbols over from one code period to the next
                const int q = (int)std::ceil(dopHalf.load() / 1000.0);
                ac.qMin = -q; ac.qMax = q - 1;
                for (int p = 1; p <= kBdsMaxPrn; p++) ac.prns.push_back(p);
                ac.replica = [](int prn, cf32* out, int n) {
                    uint8_t c[kBdsLen];
                    if (!bdsB1iChips(prn, c)) return false;
                    for (int i = 0; i < n; i++) {
                        const int idx = (int)(((int64_t)i * 2 + 1) * kBdsLen / (2 * (int64_t)n));
                        out[i] = cf32(c[idx] ? -1.f : 1.f, 0.f);
                    }
                    return true;
                };
                bdsAcq.init(ac);
                for (int p = 1; p <= kBdsMaxPrn; p++) { bdsChips[p].assign((size_t)kBdsLen, 0); bdsB1iChips(p, bdsChips[p].data()); }
                bdsD1Spec.sys = GnssBeidou; bdsD1Spec.chipRate = kBdsChipRate; bdsD1Spec.codeLen = kBdsLen; bdsD1Spec.rfHz = kBdsB1Hz; bdsD1Spec.fsOut = kGpsFsOut;
                bdsD1Spec.halfSpacing = 1;          // half a chip at two samples a chip
                bdsD1Spec.msg = GnssMsgBdsD1; bdsD1Spec.epochS = 1e-3;
                bdsD2Spec = bdsD1Spec; bdsD2Spec.msg = GnssMsgBdsD2;
                bdsCredit = 0; bdsPlan = SearchPlan();
            }
            bdsActive = true;
        } else {
            if (bdsActive) dropBand(1);
            bdsActive = false;
        }
        lastRate = rate;
        tel.activeMask = activeMask();
    }
    // the bands that the tuning and the rate hold, by the system that owns each (L1: GPS)
    unsigned activeMask() const { return (gpsActive ? gnssSystemBit(GnssGps) : 0u) | (bdsActive ? gnssSystemBit(GnssBeidou) : 0u); }
    void dropBand(int b) {
        trackers.erase(std::remove_if(trackers.begin(), trackers.end(), [b](const std::unique_ptr<GnssTracker>& t) { return t->band == b; }), trackers.end());
    }
    bool galRebuild = false;
    double lastRate = 0;
    unsigned gpsMaskNow = 0;
    // the systems searched or tracked now (GnssTelemetry::decodeMask)
    unsigned decodeMask() const {
        const unsigned b = bdsActive ? gnssSystemBit(GnssBeidou) : 0u;
        if (!gpsActive) return b;
        return (gpsMaskNow & (gnssSystemBit(GnssGps) | gnssSystemBit(GnssQzss) | gnssSystemBit(GnssSbas))) | (galActive ? gnssSystemBit(GnssGalileo) : 0u) | b;
    }

    void resetState() {
        pend.clear();
        dcI = dcQ = 0; gain = 1; dcInit = false; levelRms = 0; clipFrac = 0; inSamples = 0;
        gpsBand.reset();
        gpsAcq.reset();
        auxAcq.reset();
        auxRoundSeen = 0; auxEmptyRounds = 0; auxIdleUntil = 0; auxRoundHits = 0;
        galAcq.reset();
        trackers.clear();
        for (auto& s : sat) s = SatNav();
        for (auto& s : gsat) s = SatNav();
        for (auto& g : gnav) g = GalNav();
        for (auto& s : gsol) s = SolInfo();
        ggtoValid = false; galCredit = 0; predictedGal.clear();
        galRoundSeen = 0; galLong = false; galRoundHits = 0; galEmptyRounds = 0; galIdleUntil = 0; codeRateFrac = 0;
        bdsBand.reset(); bdsAcq.reset();
        for (auto& s : bsat) s = SatNav();
        for (auto& n : bnav) n = BdsNav();
        for (auto& s : bsol) s = SolInfo();
        bdtGpsValid = false; bdsCredit = 0; bdsPlan = SearchPlan(); bdsRoundSeen = 0; bdsRoundHits = 0; bdsEmptyRounds = 0; bdsIdleUntil = 0;
        iono = GpsIono(); utc = GpsUtc(); almToa = -1; almWna = -1; almGot = false;
        fix = GnssFix(); clockSet = false; hasPrior = false; nextMeas = 1.0; fixCount = 0; firstFix = -1; lastFixSignal = -1;
        okRetired = badRetired = 0; cfoEst = driftEst = 0; cfoValid = false; sbasTotal = 0;
        stage = 0; stageRounds = 0; firstLock = -1; roundHits = lastRoundHits = 0; longRound = false;
        for (auto& s : sol) s = SolInfo();
        nextReport = 0.25; acqCredit = 0; havePredicted = false;
        tel = GnssTelemetry(); telReady = false;
    }

    // ------------------------------------------------------------------ processing
    bool tracked(int prn) const {
        const int sys = l1caSystem(prn);
        for (auto& t : trackers) if (t->sys == sys && t->prn == prn) return true;
        return false;
    }
    bool trackedGal(int prn) const {
        for (auto& t : trackers) if (t->sys == GnssGalileo && t->prn == prn) return true;
        return false;
    }
    bool trackedSys(int sys, int prn) const {
        for (auto& t : trackers) if (t->sys == sys && t->prn == prn) return true;
        return false;
    }
    bool gpsLocked() const {
        for (auto& t : trackers) if (t->sys == GnssGps && t->carrierLocked()) return true;
        return false;
    }

    void startFromHit(const GnssAcqHit& found) {
        if (trackers.size() >= (size_t)kMaxChannels || tracked(found.prn)) return;
        // the hit is old: look again in the newest samples
        GnssAcqHit h;
        units += (uint64_t)(6 * std::max(8, found.blocks));
        if (!(found.prn > 32 ? auxAcq : gpsAcq).confirm(gpsBand, found.prn, found.dopplerHz, found.blocks, &h)) return;
        auto t = std::make_unique<GnssTracker>();
        const double rc = (kGpsCaChipRate + h.dopplerHz * kGpsCaChipRate / kGpsL1Hz) / kGpsFsOut;
        int64_t idx = gpsBand.end() - 3 * kGpsN;
        if (idx < gpsBand.base()) idx = gpsBand.base();
        double ph = std::fmod(((double)idx - ((double)h.segStart + h.codePhase)) * rc, (double)kGpsCaLen);
        if (ph < 0) ph += kGpsCaLen;
        const int sys = l1caSystem(h.prn);
        t->start(sys == GnssSbas ? sbasSpec : sys == GnssQzss ? qzssSpec : gpsSpec, gpsChips[h.prn], h.prn, h.dopplerHz, idx, ph);
        char b[120];
        snprintf(b, sizeof b, "%s found: Doppler %+.0f Hz, peak %.1f times the noise", satName(sys, h.prn).c_str(), h.dopplerHz, h.ratio);
        logf(b);
        trackers.push_back(std::move(t));
    }

    void startGalFromHit(const GnssAcqHit& found) {
        if (trackers.size() >= (size_t)kMaxChannels || trackedGal(found.prn)) return;
        GnssAcqHit h;
        units += (uint64_t)(4 * 6 * std::max(8, found.blocks));
        if (!galAcq.confirm(gpsBand, found.prn, found.dopplerHz, found.blocks, &h)) return;
        auto t = std::make_unique<GnssTracker>();
        const double rc = galSpec.chipRate * (1.0 + h.dopplerHz / kGpsL1Hz + codeRateFrac) / kGpsFsOut;
        int64_t idx = gpsBand.end() - 2 * kGalN;
        if (idx < gpsBand.base()) idx = gpsBand.base();
        double ph = std::fmod(((double)idx - ((double)h.segStart + h.codePhase)) * rc, (double)kGalHalfChips);
        if (ph < 0) ph += kGalHalfChips;
        t->start(galSpec, galChips[h.prn].data(), h.prn, h.dopplerHz, idx, ph);
        t->setCodeRateHint(codeRateFrac * galSpec.chipRate);
        char b[120];
        snprintf(b, sizeof b, "%s found: Doppler %+.0f Hz, peak %.1f times the noise", satName(GnssGalileo, h.prn).c_str(), h.dopplerHz, h.ratio);
        logf(b);
        trackers.push_back(std::move(t));
    }

    void startBdsFromHit(const GnssAcqHit& found) {
        if (trackers.size() >= (size_t)kMaxChannels || trackedSys(GnssBeidou, found.prn)) return;
        GnssAcqHit h;
        units += (uint64_t)(6 * std::max(8, found.blocks));
        if (!bdsAcq.confirm(bdsBand, found.prn, found.dopplerHz, found.blocks, &h)) return;
        auto t = std::make_unique<GnssTracker>();
        // A sample clock that does not follow the oscillator moves the 2.046 Mcps code by 41 chips a second at 20 ppm: more than the code loop
        // pulls in. The hit and its confirmation are two code phase readings far apart: the code periods between them give the code rate.
        double frac = codeRateFrac;
        {
            const double D = ((double)h.segStart + h.codePhase) - ((double)found.segStart + found.codePhase);
            const double aided = 1.0 + h.dopplerHz / kBdsB1Hz;
            if (D > 0.05 * kGpsFsOut) {
                const double m = std::round(D * aided / kBdsN);
                const double f = m * kBdsN / D - aided;
                if (m > 0 && std::fabs(f) < 1e-4) frac = f;
            }
        }
        const double rc = kBdsChipRate * (1.0 + h.dopplerHz / kBdsB1Hz + frac) / kGpsFsOut;
        int64_t idx = bdsBand.end() - 3 * kBdsN;
        if (idx < bdsBand.base()) idx = bdsBand.base();
        double ph = std::fmod(((double)idx - ((double)h.segStart + h.codePhase)) * rc, (double)kBdsLen);
        if (ph < 0) ph += kBdsLen;
        t->start(bdsIsGeo(h.prn) ? bdsD2Spec : bdsD1Spec, bdsChips[h.prn].data(), h.prn, h.dopplerHz, idx, ph);
        t->band = 1;
        if (frac != 0) t->setCodeRateHint(frac * kBdsChipRate);
        char b[120];
        snprintf(b, sizeof b, "%s found: Doppler %+.0f Hz, peak %.1f times the noise", satName(GnssBeidou, h.prn).c_str(), h.dopplerHz, h.ratio);
        logf(b);
        trackers.push_back(std::move(t));
    }

    // BeiDou: the ephemeris from D1 subframes 1-3 of one frame or D2 pages 1-10; BDT - GPS from D1 subframe 5 page 9
    void handleBds(GnssTracker& t, const GnssSubframeEvent& ev, double signalNow) {
        BdsNav& n = bnav[t.prn];
        SatNav& s = bsat[t.prn];
        s.towLast = (int)ev.towStart;
        s.lastSfId = (int)bdsFraId(ev.bits); s.lastSfTime = signalNow;
        GpsEphemeris e;
        int wk = 0;
        const bool had = n.gpsValid;
        if (bdsHandleSubframe(n, ev.bits, ev.kind == GnssMsgBdsD2, t.prn, &e, &wk)) {
            if (!s.hasEph || s.eph.iodc != e.iodc || s.eph.toe != e.toe || s.eph.health != e.health) {
                s.eph = e; s.hasEph = true; s.week = wk; s.ephSignalTime = signalNow;
                char b[120];
                snprintf(b, sizeof b, "%s ephemeris AODE %d, BDT week %d, health %s", satName(GnssBeidou, t.prn).c_str(), e.iodc, wk, e.health ? "bad" : "ok");
                logf(b);
            }
        }
        if (n.gpsValid && !had) {
            if (!bdtGpsValid) {
                char b[100];
                snprintf(b, sizeof b, "BDT - GPS time offset %+.1f ns (from %s)", n.a0gps * 1e9, satName(GnssBeidou, t.prn).c_str());
                logf(b);
            }
            bdtGpsValid = true; bdtA0 = n.a0gps; bdtA1 = n.a1gps;
        }
    }

    // Galileo I/NAV words: the ephemeris from words 1-4 of one IODnav and word 5 (BGD, health, week); the GST-GPS offset from word 10
    void handleInav(GnssTracker& t, const GnssSubframeEvent& ev, double signalNow) {
        GalNav& g = gnav[t.prn];
        SatNav& s = gsat[t.prn];
        int tow = -1;
        const int type = inavParseWord(ev.word, g, &tow);
        if (tow >= 0) s.towLast = tow;
        s.lastSfId = type; s.lastSfTime = signalNow;
        if (type == 10 && g.ggtoValid) {
            if (!ggtoValid) {
                char b[100];
                snprintf(b, sizeof b, "GST - GPS time offset %+.1f ns (from %s)", g.a0g * 1e9, satName(GnssGalileo, t.prn).c_str());
                logf(b);
            }
            ggtoValid = true; ggtoA0 = g.a0g; ggtoA1 = g.a1g; ggtoT0 = g.t0g; ggtoWn0 = g.wn0g; ggtoWn = g.wn >= 0 ? g.wn : ggtoWn0;
        }
        if (g.complete() && g.wn >= 0) {
            GpsEphemeris e = g.eph;
            e.prn = t.prn; e.galileo = true; e.has1 = e.has2 = e.has3 = true;
            e.iode2 = e.iode3 = e.iodc = g.iod[1];
            e.tgd = g.bgdE1E5b;
            e.health = (g.e1bHs != 0 || g.e1bDvs != 0) ? 1 : 0;
            // the GST week counts from August 1999 (GPS week 1024), 12 bits
            const int ref = referenceWeek();
            int wk = g.wn + 1024;
            while (wk + 2048 <= ref) wk += 4096;
            if (!s.hasEph || s.eph.iodc != e.iodc || s.eph.toe != e.toe || s.eph.health != e.health) {
                s.eph = e; s.hasEph = true; s.week = wk; s.ephSignalTime = signalNow;
                char b[120];
                snprintf(b, sizeof b, "%s ephemeris IODnav %d, week %d, health %s", satName(GnssGalileo, t.prn).c_str(), e.iodc, wk, e.health ? "bad" : "ok");
                logf(b);
            }
        }
    }

    void handleSubframe(GnssTracker& t, const GnssSubframeEvent& ev, double signalNow) {
        if (ev.kind == GnssMsgInav) { handleInav(t, ev, signalNow); return; }
        if (ev.kind == GnssMsgBdsD1 || ev.kind == GnssMsgBdsD2) { handleBds(t, ev, signalNow); return; }
        SatNav& s = sat[t.prn];
        if (ev.kind == GnssMsgSbas) {
            // SBAS: the message type is reported; the corrections are not applied
            const bool fresh = ev.sbasType < 32 ? !(s.sbasTypes & (1u << ev.sbasType)) : false;
            s.sbasLast = ev.sbasType; s.sbasCount++;
            if (ev.sbasType < 32) s.sbasTypes |= 1u << ev.sbasType;
            sbasTotal++;
            if (fresh) {
                char b[100];
                snprintf(b, sizeof b, "%s SBAS message type %d", satName(t.sys, t.prn).c_str(), ev.sbasType);
                logf(b);
            }
            return;
        }
        const unsigned id = lnavSubframeId(ev.sf);
        s.towLast = (int)ev.towStart;
        s.lastSfId = (int)id; s.lastSfTime = signalNow;
        if (id == 1 || id == 2 || id == 3) {
            if (s.pend.prn != t.prn) { s.pend = GpsEphemeris(); s.pend.prn = t.prn; }
            if (id == 1) { lnavParseSf1(ev.sf, s.pend); s.pendWeek = gpsResolveWeek(s.pend.wn, referenceWeek()); }
            else if (id == 2) lnavParseSf2(ev.sf, s.pend);
            else lnavParseSf3(ev.sf, s.pend);
            if (s.pend.complete() && s.pendWeek > 0 && (!s.hasEph || s.eph.iodc != s.pend.iodc || s.eph.toe != s.pend.toe)) {
                s.eph = s.pend; s.hasEph = true; s.week = s.pendWeek; s.ephSignalTime = signalNow;
                char b[120];
                snprintf(b, sizeof b, "%s ephemeris IODE %d, week %d, health %s", satName(t.sys, t.prn).c_str(), s.eph.iode(), s.week, s.eph.health ? "bad" : "ok");
                logf(b);
            }
        } else if ((id == 4 || id == 5) && t.sys == GnssGps) {     // the QZSS pages of subframes 4 and 5 have their own layout (not decoded)
            GpsAlmanac a; int sv = 0; double toa = 0; int wna = 0;
            GpsIono io; GpsUtc ut;
            const int r = lnavParseAlmanacPage(ev.sf, (int)id, &sv, a, io, ut, &toa, &wna);
            if (r == 1 && a.valid) sat[a.prn].alm = a;
            else if (r == 2) {
                if (!iono.valid) logf("ionosphere and UTC parameters received");
                iono = io; utc = ut;
            } else if (r == 3) { almToa = toa; almWna = wna; almGot = true; }
        }
    }

    void runTrackers(double signalNow) {
        for (auto& t : trackers) {
            const bool wasLocked = t->carrierLocked(), wasBit = t->bitSynced(), wasFrame = t->frameSynced();
            const uint64_t okBefore = t->framesOk();
            const GnssBand& band = bandOf(*t);
            while (t->step(band)) {}
            if (t->carrierLocked() && !wasLocked) {
                char b[100]; snprintf(b, sizeof b, "%s locked, %.0f dB-Hz", satName(t->sys, t->prn).c_str(), t->cn0()); logf(b);
                if (firstLock < 0) firstLock = signalNow;
            }
            if (t->bitSynced() && !wasBit) logf(satName(t->sys, t->prn) + " bit sync");
            if (t->frameSynced() && !wasFrame) logf(satName(t->sys, t->prn) + " frame sync");
            (void)okBefore;
            for (auto& ev : t->events()) handleSubframe(*t, ev, signalNow);
            t->events().clear();
        }
        for (size_t i = 0; i < trackers.size();) {
            if (trackers[i]->lost()) {
                okRetired += trackers[i]->framesOk(); badRetired += trackers[i]->framesBad();
                // a lost satellite is found again sooner by a short round than at the end of a long one
                if (trackers[i]->band == 0 && longRound && trackers[i]->lockSeconds() > 0.5) { gpsAcq.endRound(); longRound = false; }
                logf(satName(trackers[i]->sys, trackers[i]->prn) + (trackers[i]->wasPullInTimeout() ? " not confirmed" : " lost"));
                trackers.erase(trackers.begin() + (long)i);
            } else i++;
        }
    }

    bool skipPrn(int prn) const {
        if (tracked(prn)) return true;
        // with a fix and the sky predicted from the almanac, look for the satellites that should be in view; the others now and then
        if (havePredicted && gpsAcq.rounds() % 6 != 5 && std::find(predictedVisible.begin(), predictedVisible.end(), prn) == predictedVisible.end()) return true;
        return false;
    }

    // Where and how long to search next. With satellites locked the radio's error lies within 5.5 kHz of each of their Dopplers, so the others are within
    // 11 kHz of all of them; with a fix it is measured. With nothing locked the window widens round by round (TCXO, HackRF, cheap dongle), then the
    // same with a four times longer integration for weak signals, and starts over.
    void planSearch() {
        double lo = 1e18, hi = -1e18;
        int nLocked = 0, nPull = 0;
        for (auto& t : trackers) {
            if (t->band != 0) continue;
            if (t->carrierLocked()) { lo = std::min(lo, t->dopplerHz()); hi = std::max(hi, t->dopplerHz()); nLocked++; }
            else nPull++;
        }
        const uint32_t r = gpsAcq.rounds();
        double c = 0, h = dopHalf.load();
        int ms = kShortMs;
        const bool newRound = r != stageRounds;
        if (newRound) { lastRoundHits = roundHits; roundHits = 0; }
        if (nLocked > 0) {
            if (fix.valid && cfoValid) { c = cfoEst; h = kSatDopplerHz + 1000.0; }
            else { c = 0.5 * (lo + hi); h = std::max(kSatDopplerHz, 2 * kSatDopplerHz - 0.5 * (hi - lo)) + 500.0; }
            lastCfo = c; lastCfoValid = true;
            stage = 0; stageRounds = r;
            // a long round for the weak satellites once a short one finds nothing new (a short round finds the strong ones, and the lost ones again, sooner)
            if (newRound) longRound = !longRound && lastRoundHits == 0;
            if (longRound) ms = kLongMs;
        } else {
            longRound = false;
            if (newRound) {
                stageRounds = r;
                if (nPull == 0) {
                    stage = (stage + 1) % 6;
                    static const char* what[6] = {"", "+-45 kHz (a radio without a TCXO, such as a HackRF)", "+-170 kHz (a dongle without a TCXO)",
                                                   "with a 64 ms integration for weak signals", "+-45 kHz with a 64 ms integration", "+-170 kHz with a 64 ms integration"};
                    if (stage > 0) logf(std::string("nothing found yet: searching ") + what[stage]);
                }
            }
            const int st = stage % 3;
            if (st == 0) {
                // the frequency error that worked before (this run, or the one remembered for this radio)
                if (lastCfoValid) c = lastCfo; else if (cfoHintValid.load()) c = cfoHintHz.load();
            } else h = st == 1 ? kMidHz : kWideHz;
            if (stage >= 3) ms = kLongMs;
        }
        gpsAcq.setPlan(c, h, ms);
    }

    // Galileo: searched once GPS has a fix. The receiver's frequency error is then measured, so the window is the satellites' Doppler alone
    // (+-5 kHz), and a sky without Galileo costs little (the search sleeps after a round without a find).
    void runGalAcq(double seconds, double signalNow) {
        if (!galActive || !fix.valid || !cfoValid) return;
        // A round that finds nothing while no Galileo satellite is tracked (a sky without Galileo, or a receiver under a roof) puts the search to sleep
        // for a while, longer each time (10 s, 20 s, .. 80 s). With Galileo satellites tracked, every third round integrates three times as long for the
        // weak ones.
        const uint32_t r = galAcq.rounds();
        int nGal = 0;
        for (auto& t : trackers) nGal += t->sys == GnssGalileo;
        if (r != galRoundSeen) {
            galRoundSeen = r;
            if (galRoundHits == 0 && nGal == 0) {
                galIdleUntil = signalNow + 10.0 * (1 << std::min(galEmptyRounds, 3));
                galEmptyRounds++;
            } else galEmptyRounds = 0;
            galRoundHits = 0;
            galLong = nGal > 0 && r % 3 == 2;
        }
        if (signalNow < galIdleUntil) return;
        // the sample clock error against the code, from the GPS channels whose code loop has settled (median)
        {
            std::vector<double> e;
            for (auto& t : trackers) if (t->sys == GnssGps && t->carrierLocked() && t->lockSeconds() > 3.0) e.push_back(t->codeRateCorrection() / kGpsCaChipRate);
            // not before a GPS code loop has settled: a sample clock 20 ppm off smears the narrow BOC peak within one search
            if (e.empty()) return;
            std::sort(e.begin(), e.end());
            codeRateFrac = e[e.size() / 2];
            galAcq.setCodeRateError(codeRateFrac);
        }
        galAcq.setPlan(cfoEst, 5000.0, galLong ? 3 * kGalMs : kGalMs);
        // a unit is one FFT of 16384 points here, about five times the GPS one
        galCredit = std::min(galCredit + seconds * 1000.0 * (double)acqRate.load() / 3.0, 300.0);
        if (galCredit < 8) return;
        std::vector<GnssAcqHit> hits;
        const int used = galAcq.work(gpsBand, (int)galCredit, [this](int p) { return trackedGal(p); }, hits);
        galCredit -= used;
        units += (uint64_t)used * 4;
        galRoundHits += (int)hits.size();
        for (auto& h : hits) startGalFromHit(h);
    }

    // QZSS and SBAS: their own search, once a GPS satellite is locked (in the window the GPS search uses), with half the GPS search's work.
    // A round that finds nothing while none of them is tracked puts it to sleep (10 s, 20 s, .. 80 s).
    void runAuxAcq(double seconds, double signalNow) {
        if (!gpsLocked() || !(gpsMaskNow & (gnssSystemBit(GnssQzss) | gnssSystemBit(GnssSbas)))) return;
        const uint32_t r = auxAcq.rounds();
        int nAux = 0;
        for (auto& t : trackers) nAux += t->sys == GnssQzss || t->sys == GnssSbas;
        if (r != auxRoundSeen) {
            auxRoundSeen = r;
            if (auxRoundHits == 0 && nAux == 0) { auxIdleUntil = signalNow + 10.0 * (1 << std::min(auxEmptyRounds, 3)); auxEmptyRounds++; }
            else auxEmptyRounds = 0;
            auxRoundHits = 0;
        }
        if (signalNow < auxIdleUntil) return;
        // 8 ms: QZSS and SBAS are as strong as GPS; the weak ones come in the rounds after the first
        auxAcq.setPlan(gpsAcq.windowCenterHz(), gpsAcq.windowHalfHz(), auxEmptyRounds == 0 && r % 2 == 1 ? kShortMs : kShortMs / 2);
        auxCredit = std::min(auxCredit + seconds * 1000.0 * (double)acqRate.load() / 2.0, 300.0);
        if (auxCredit < 8) return;
        std::vector<GnssAcqHit> hits;
        const int used = auxAcq.work(gpsBand, (int)auxCredit, [this](int p) { return tracked(p); }, hits);
        auxCredit -= used;
        units += (uint64_t)used;
        auxRoundHits += (int)hits.size();
        for (auto& h : hits) startFromHit(h);
    }

    // The same plan for a band of its own (BeiDou or GLONASS tuned without L1): its own channels say where the radio's error lies, else the
    // window widens round by round. Frequencies scale with the carrier: the remembered error is at L1.
    void planOwn(GnssAcq& acq, SearchPlan& pl, int sys, double rf, const char* name) {
        double lo = 1e18, hi = -1e18;
        int nLocked = 0, nPull = 0;
        for (auto& t : trackers) {
            if (t->sys != sys) continue;
            if (t->carrierLocked()) { lo = std::min(lo, t->dopplerHz()); hi = std::max(hi, t->dopplerHz()); nLocked++; }
            else nPull++;
        }
        const uint32_t r = acq.rounds();
        double c = 0, h = dopHalf.load();
        int ms = kShortMs;
        const bool newRound = r != pl.stageRounds;
        if (newRound) { pl.lastRoundHits = pl.roundHits; pl.roundHits = 0; }
        if (nLocked > 0) {
            c = 0.5 * (lo + hi); h = std::max(kSatDopplerHz, 2 * kSatDopplerHz - 0.5 * (hi - lo)) + 500.0;
            pl.lastCfo = c; pl.lastCfoValid = true;
            pl.stage = 0; pl.stageRounds = r;
            if (newRound) pl.longRound = !pl.longRound && pl.lastRoundHits == 0;
            if (pl.longRound) ms = kLongMs;
        } else {
            pl.longRound = false;
            if (newRound) {
                pl.stageRounds = r;
                if (nPull == 0) {
                    pl.stage = (pl.stage + 1) % 6;
                    static const char* what[6] = {"", "+-45 kHz", "+-170 kHz", "with a 64 ms integration", "+-45 kHz with a 64 ms integration", "+-170 kHz with a 64 ms integration"};
                    if (pl.stage > 0) logf(std::string(name) + ": nothing found yet, searching " + what[pl.stage]);
                }
            }
            const int st = pl.stage % 3;
            if (st == 0) {
                if (pl.lastCfoValid) c = pl.lastCfo; else if (cfoHintValid.load()) c = cfoHintHz.load() * rf / kGpsL1Hz;
            } else h = st == 1 ? kMidHz : kWideHz;
            if (pl.stage >= 3) ms = kLongMs;
        }
        acq.setPlan(c, h, ms);
    }

    // The sample clock error against the code, from the GPS channels whose code loop has settled (median); false while there is none
    bool updateCodeRate(int sys = GnssGps, double chipRate = kGpsCaChipRate) {
        std::vector<double> e;
        for (auto& t : trackers) if (t->sys == sys && t->carrierLocked() && t->lockSeconds() > 3.0) e.push_back(t->codeRateCorrection() / chipRate);
        if (e.empty()) return false;
        std::sort(e.begin(), e.end());
        codeRateFrac = e[e.size() / 2];
        return true;
    }

    // BeiDou: with L1 in the same tune it is searched once GPS has a fix, in the window of the measured frequency error (as Galileo); on a tune of
    // its own it searches like GPS does, the window widening while nothing is found.
    void runBdsAcq(double seconds, double signalNow) {
        if (!bdsActive) return;
        int nB = 0, nLocked = 0;
        for (auto& t : trackers) if (t->sys == GnssBeidou) { nB++; nLocked += t->carrierLocked(); }
        double rateNow = (double)acqRate.load();
        if (gpsActive) {
            // once a GPS satellite is locked the radio's error is known to within the window of the GPS search; with a fix it is measured
            if (!gpsLocked()) return;
            const uint32_t r = bdsAcq.rounds();
            if (r != bdsRoundSeen) {
                bdsRoundSeen = r;
                if (bdsRoundHits == 0 && nB == 0) { bdsIdleUntil = signalNow + 10.0 * (1 << std::min(bdsEmptyRounds, 3)); bdsEmptyRounds++; }
                else bdsEmptyRounds = 0;
                bdsRoundHits = 0;
            }
            if (signalNow < bdsIdleUntil) return;
            if (updateCodeRate()) bdsAcq.setCodeRateError(codeRateFrac);
            const double sc = kBdsB1Hz / kGpsL1Hz;
            if (fix.valid && cfoValid) bdsAcq.setPlan(cfoEst * sc, kSatDopplerHz, nB > 0 && r % 3 == 2 ? kLongMs / 2 : kShortMs);
            else bdsAcq.setPlan(gpsAcq.windowCenterHz() * sc, gpsAcq.windowHalfHz(), kShortMs);
            rateNow *= 0.5;
        } else {
            planOwn(bdsAcq, bdsPlan, GnssBeidou, kBdsB1Hz, "BeiDou");
            if (updateCodeRate(GnssBeidou, kBdsChipRate)) bdsAcq.setCodeRateError(codeRateFrac);
            if (nLocked == 0) rateNow *= 2.0;
        }
        bdsCredit = std::min(bdsCredit + seconds * 1000.0 * rateNow, 600.0);
        if (bdsCredit < 8) return;
        std::vector<GnssAcqHit> hits;
        const int used = bdsAcq.work(bdsBand, (int)bdsCredit, [this](int p) { return trackedSys(GnssBeidou, p); }, hits);
        bdsCredit -= used;
        units += (uint64_t)used;
        bdsRoundHits += (int)hits.size();
        bdsPlan.roundHits += (int)hits.size();
        for (auto& h : hits) startBdsFromHit(h);
    }

    void runAcq(double seconds) {
        runBdsAcq(seconds, (double)inSamples / rate);
        if (!gpsActive) return;
        planSearch();
        int nLocked = 0;
        for (auto& t : trackers) nLocked += t->band == 0 && t->carrierLocked();
        // search less when the satellites expected in view are all tracked; harder while nothing is tracked (the channels cost nothing then)
        const double rateNow = (double)acqRate.load() * (nLocked == 0 ? 2.0 : 1.0);
        acqCredit = std::min(acqCredit + seconds * 1000.0 * rateNow, 600.0);
        if (acqCredit < 8) return;
        std::vector<GnssAcqHit> hits;
        const int used = gpsAcq.work(gpsBand, (int)acqCredit, [this](int p) { return skipPrn(p); }, hits);
        acqCredit -= used;
        units += (uint64_t)used;
        roundHits += (int)hits.size();
        for (auto& h : hits) startFromHit(h);
        runAuxAcq(seconds, (double)inSamples / rate);
        runGalAcq(seconds, (double)inSamples / rate);
    }

    void trimBands() {
        if (gpsActive) {
            int64_t keepFrom = gpsBand.end();
            for (auto& t : trackers) if (t->band == 0) keepFrom = std::min(keepFrom, t->position());
            gpsBand.trimTo(keepFrom - 8192);
        }
        if (bdsActive) {
            int64_t keepFrom = bdsBand.end();
            for (auto& t : trackers) if (t->band == 1) keepFrom = std::min(keepFrom, t->position());
            bdsBand.trimTo(keepFrom - 8192);
        }
    }

    // ------------------------------------------------------------------ the fix
    void measure(double S) {
        std::vector<GnssObs> obs;
        std::vector<GnssMeasurement> meas;
        std::vector<int> prns;
        std::vector<double> towv;
        for (auto& t : trackers) {
            // QZSS keeps GPS time and sends the GPS navigation message: it joins the GPS satellites (IS-QZSS-PNT); SBAS is not used for ranging
            if ((t->sys != GnssGps && t->sys != GnssQzss && t->sys != GnssGalileo && t->sys != GnssBeidou) || !t->carrierLocked() || !t->timeValid()) continue;
            double tow;
            if (!t->transmitTime(S, &tow)) continue;
            GnssMeasurement m;
            m.sys = t->sys; m.prn = t->prn; m.rxTime = S; m.txTow = tow; m.dopplerHz = t->dopplerHz(); m.cn0 = t->cn0();
            meas.push_back(m);
        }
        // satellites with a usable ephemeris for the solution
        double tRef = 0;
        int n = 0;
        // the Galileo times are GST: on the GPS scale with the broadcast offset (a few ns; the reference only needs to be near)
        for (auto& m : meas) { const SatNav& s = navOf(m.sys, m.prn); if (s.hasEph && s.eph.health == 0 && m.cn0 > 24.0) { tRef += m.sys == GnssBeidou ? toGpsTime(m.sys, m.txTow) : m.txTow - (m.sys == GnssGalileo ? ggtoAt(m.txTow) : 0.0); n++; } }
        if (n >= 1) tRef /= n;
        // average in a way that survives the week boundary: the times are within 20 ms of each other, so a plain mean is fine except at the wrap
        const double tr0 = std::round((tRef + 0.075) * 1e6) / 1e6;
        std::vector<size_t> idx;
        for (size_t i = 0; i < meas.size(); i++) {
            const SatNav& s = navOf(meas[i].sys, meas[i].prn);
            if (!s.hasEph || s.eph.health != 0 || meas[i].cn0 < 24.0) continue;
            double tTrue = meas[i].txTow, pos[3], dts = 0;
            for (int k = 0; k < 3; k++) {
                gpsEphemerisState(s.eph, tTrue, pos, &dts);
                tTrue = meas[i].txTow - (dts - s.eph.tgd);
            }
            GnssObs o;
            o.sys = GnssGps; o.prn = meas[i].prn;          // QZSS too: one clock with GPS
            double txGps = meas[i].txTow;
            if (meas[i].sys == GnssGalileo) {
                // GST to GPS time with the offset of word type 10 when it is known: then Galileo shares the GPS clock unknown; without it the solver
                // estimates the offset as a clock of its own (one more unknown)
                if (ggtoValid) txGps -= ggtoAt(meas[i].txTow);
                else o.sys = GnssGalileo;
            } else if (meas[i].sys == GnssBeidou) {
                // BDT is 14 s behind GPS time; with the broadcast A0GPS/A1GPS BeiDou shares the GPS clock unknown, without them it has one of its own
                txGps = toGpsTime(GnssBeidou, meas[i].txTow);
                if (!bdtGpsValid) o.sys = GnssBeidou;
            }
            o.pr = kC * (gpsWrap(tr0 - txGps) + (dts - s.eph.tgd));
            o.sat[0] = pos[0]; o.sat[1] = pos[1]; o.sat[2] = pos[2];
            o.weight = std::min(2.0, std::max(0.05, std::pow(10.0, (meas[i].cn0 - 45.0) / 10.0)));
            obs.push_back(o);
            idx.push_back(i);
        }
        for (auto& s : sol) s = SolInfo();
        for (auto& s : gsol) s = SolInfo();
        for (auto& s : bsol) s = SolInfo();
        bool solved = false;
        GnssSolution sl;
        if (obs.size() >= 4) {
            GnssSolveOptions opt;
            opt.hasIono = iono.valid; opt.iono = iono; opt.tow = tr0;
            opt.hasPrior = hasPrior;
            for (int k = 0; k < 3; k++) opt.prior[k] = prior[k];
            opt.maskDeg = elMask.load();
            solved = gnssSolve(obs, opt, sl);
            if (solved) {
                double lat, lon, h;
                ecefToLla(sl.x, &lat, &lon, &h);
                // sanity: on or near the earth, residuals not absurd
                if (!(h > -1000 && h < 30000) || sl.rmsResidual > 200.0) solved = false;
            }
        }
        for (size_t k = 0; k < obs.size(); k++) {
            if (!solved) break;
            SolInfo& si = solOf(meas[idx[k]].sys, obs[k].prn);
            si.used = sl.used[k] != 0; si.resid = (float)sl.residual[k]; si.az = (float)sl.azDeg[k]; si.el = (float)sl.elDeg[k]; si.has = true;
            meas[idx[k]].inFix = si.used; meas[idx[k]].residualM = sl.residual[k];
        }
        if (solved) {
            fix.valid = true;
            for (int k = 0; k < 3; k++) { fix.ecef[k] = sl.x[k]; prior[k] = sl.x[k]; }
            hasPrior = true;
            ecefToLla(sl.x, &fix.latDeg, &fix.lonDeg, &fix.heightM);
            // the receiver's clock is the signal time plus an offset set at the first fix; its error against GPS time then grows with the oscillator error
            // the clock unknown that stands for GPS time: GPS's own, else that of the first system in the fix (its time moved to the GPS scale)
            int refSys = GnssGps;
            if (!sl.sysUsed[GnssGps]) { static const int order[3] = {GnssGalileo, GnssBeidou, GnssGlonass}; for (int s : order) if (sl.sysUsed[s]) { refSys = s; break; } }
            const double refBias = sl.bias[refSys];
            const double tGpsMeas = tr0 - refBias / kC;
            if (!clockSet) { clockOffset = tGpsMeas - S; clockSet = true; }
            const double gpsBias = kC * ((S + clockOffset) - tGpsMeas);
            for (int s = 0; s < GnssSystems; s++) {
                fix.systemInFix[s] = sl.sysUsed[s];
                fix.clockBiasM[s] = sl.sysUsed[s] ? gpsBias + (sl.bias[s] - refBias) : 0.0;
            }
            fix.nSats = sl.nUsed;
            for (int s = 0; s < GnssSystems; s++) fix.nSatsPerSystem[s] = 0;
            for (size_t k = 0; k < obs.size(); k++) if (sl.used[k]) fix.nSatsPerSystem[meas[idx[k]].sys]++;
            if (fix.nSatsPerSystem[GnssQzss] > 0) { fix.systemInFix[GnssQzss] = true; fix.clockBiasM[GnssQzss] = fix.clockBiasM[GnssGps]; }
            fix.ggtoApplied = false; fix.ggtoNs = ggtoValid ? ggtoAt(tr0) * 1e9 : 0.0;
            if (fix.nSatsPerSystem[GnssGalileo] > 0 && !sl.sysUsed[GnssGalileo]) {
                // the Galileo satellites were moved to GPS time: the receiver clock against GST is the GPS one less the offset
                fix.systemInFix[GnssGalileo] = true; fix.ggtoApplied = true;
                fix.clockBiasM[GnssGalileo] = fix.clockBiasM[GnssGps] - kC * ggtoAt(tr0);
            }
            fix.bdtGpsApplied = false; fix.bdtGpsNs = bdtGpsValid ? bdtAt(tr0 - kBdtGpsSeconds) * 1e9 : 0.0;
            if (fix.nSatsPerSystem[GnssBeidou] > 0 && !sl.sysUsed[GnssBeidou]) {
                // BeiDou moved to GPS time: the receiver clock against BDT (less the whole 14 s) is the GPS one less the offset
                fix.systemInFix[GnssBeidou] = true; fix.bdtGpsApplied = true;
                fix.clockBiasM[GnssBeidou] = fix.clockBiasM[GnssGps] - kC * bdtAt(tr0 - kBdtGpsSeconds);
            }
            fix.hdop = sl.hdop; fix.vdop = sl.vdop; fix.pdop = sl.pdop; fix.tdop = sl.tdop;
            fix.hErrM = sl.hErrM;
            fix.vErrM = sl.vErrM;
            fix.residualRmsM = (float)sl.rmsResidual;
            std::string names;
            for (int s = 0; s < GnssSystems; s++) if (fix.nSatsPerSystem[s] > 0) names += std::string(names.empty() ? "" : " + ") + gnssSystemName(s);
            char b[96];
            snprintf(b, sizeof b, "%s %d satellites", names.c_str(), sl.nUsed);
            fix.type = b;
            // GPS time of the measurement: the reference time less the clock error
            const double tGps = tr0 - refBias / kC;
            int week = 0;
            for (auto& m : meas) if (m.sys == GnssGps && sat[m.prn].hasEph) { week = sat[m.prn].week; break; }
            if (week == 0) for (auto& m : meas) if ((m.sys == GnssGps || m.sys == GnssQzss) && sat[m.prn].hasEph) { week = sat[m.prn].week; break; }
            if (week == 0) for (auto& m : meas) if (m.sys == GnssBeidou && bsat[m.prn].hasEph) { week = bsat[m.prn].week + kBdtGpsWeeks; break; }
            if (week == 0) for (auto& m : meas) if (m.sys == GnssGalileo && gsat[m.prn].hasEph) { week = gsat[m.prn].week; break; }
            double tow = tGps;
            if (tow < 0) { tow += 604800.0; week--; }
            if (tow >= 604800.0) { tow -= 604800.0; week++; }
            fix.gpsWeek = week; fix.gpsTow = tow;
            fix.leapSeconds = utc.valid ? utc.dtls : -1;
            fix.timeValid = week > 0;
            if (fix.timeValid) gpsTimeToCalendar(week, tow, utc.valid ? utc.dtls : 18, &fix.year, &fix.month, &fix.day, &fix.hour, &fix.minute, &fix.second);
            fix.fixCount = (uint32_t)(++fixCount);
            lastFixSignal = S;
            if (firstFix < 0) {
                firstFix = S;
                char m[160];
                snprintf(m, sizeof m, "first fix after %.1f s: %.5f %.5f, %.0f m, %d satellites, HDOP %.1f", S, fix.latDeg, fix.lonDeg, fix.heightM, fix.nSats, fix.hdop);
                logf(m);
            }
            fix.firstFixSecs = firstFix;
            estimateClock(sl, obs, meas, idx, tr0);
            predictSky(tGps);
        } else if (fix.valid && S - lastFixSignal > 20.0) {
            fix.valid = false; fix.type = "no fix";
            logf("position fix lost");
        }
        lastMeas = meas;
        if (measCb) measCb(meas);
    }

    // The receiver's own frequency error from the Doppler of the satellites: measured minus what the geometry says, and the clock drift
    void estimateClock(const GnssSolution& sl, const std::vector<GnssObs>& obs, const std::vector<GnssMeasurement>& meas, const std::vector<size_t>& idx, double tr0) {
        std::vector<double> res;
        for (size_t k = 0; k < obs.size(); k++) {
            if (!sl.used[k]) continue;
            const GnssMeasurement& m = meas[idx[k]];
            const SatNav& s = navOf(m.sys, obs[k].prn);
            double tTx = m.txTow;
            double p1[3], p2[3], c1, c2;
            gpsEphemerisState(s.eph, tTx - 0.25, p1, &c1);
            gpsEphemerisState(s.eph, tTx + 0.25, p2, &c2);
            const double r1 = std::sqrt((p1[0] - sl.x[0]) * (p1[0] - sl.x[0]) + (p1[1] - sl.x[1]) * (p1[1] - sl.x[1]) + (p1[2] - sl.x[2]) * (p1[2] - sl.x[2]));
            const double r2 = std::sqrt((p2[0] - sl.x[0]) * (p2[0] - sl.x[0]) + (p2[1] - sl.x[1]) * (p2[1] - sl.x[1]) + (p2[2] - sl.x[2]) * (p2[2] - sl.x[2]));
            const double rr = (r2 - r1) / 0.5;
            // the satellite clock rate in the transmitted frequency is negligible here (a few mHz)
            // at the signal's own carrier; the radio's error is reported as seen at L1
            const double fc = carrierOf(m.sys);
            const double expected = -rr / kC * fc;
            res.push_back((m.dopplerHz - expected) * (kGpsL1Hz / fc));
        }
        (void)tr0;
        if (res.size() >= 3) {
            std::sort(res.begin(), res.end());
            const double med = res[res.size() / 2];
            cfoEst = med;
            cfoValid = true;
            driftEst = -med / kGpsL1Hz * kC;
        }
    }

    void predictSky(double tow) {
        predictedVisible.clear();
        havePredicted = true;
        for (int p = 1; p <= 32; p++) {
            double pos[3];
            bool ok = false;
            if (sat[p].hasEph) { gpsEphemerisState(sat[p].eph, tow, pos, nullptr); ok = true; }
            else if (sat[p].alm.valid) { gpsAlmanacPos(sat[p].alm, tow, pos); ok = true; }
            if (!ok) continue;
            double az, el;
            azElFromEcef(fix.ecef, pos, &az, &el);
            if (el > -2.0) predictedVisible.push_back(p);
        }
        gpsAcq.setPriority(predictedVisible);
        predictedGal.clear();
        for (int p = 1; p < kGalPrns; p++) {
            if (!gsat[p].hasEph) continue;
            double pos[3], az, el;
            gpsEphemerisState(gsat[p].eph, tow, pos, nullptr);
            azElFromEcef(fix.ecef, pos, &az, &el);
            if (el > -2.0) predictedGal.push_back(p);
        }
        // only the satellites with an ephemeris are known: the prediction just puts them first, the round still holds every other one
        galAcq.setPriority(predictedGal);
        predictedGal.clear();
        if (bdsActive) {
            std::vector<int> pb;
            for (int p = 1; p <= kBdsMaxPrn; p++) {
                if (!bsat[p].hasEph) continue;
                double pos[3], az, el;
                gpsEphemerisState(bsat[p].eph, tow - kBdtGpsSeconds, pos, nullptr);
                azElFromEcef(fix.ecef, pos, &az, &el);
                if (el > -2.0) pb.push_back(p);
            }
            bdsAcq.setPriority(pb);
        }
    }

    // ------------------------------------------------------------------ telemetry
    void buildTelemetry(double signalNow) {
        GnssTelemetry t;
        t.seq = ++seq;
        t.inputRate = rate;
        t.centerMhz = centerMhz.load();
        t.systemsMask = sysMask.load() & (gnssSystemBit(GnssGps) | gnssSystemBit(GnssQzss) | gnssSystemBit(GnssSbas) | gnssSystemBit(GnssGalileo) | gnssSystemBit(GnssBeidou));
        t.activeMask = activeMask();
        t.decodeMask = decodeMask();
        t.sbasMessages = sbasTotal;
        t.signalSecs = signalNow;
        struct Row { const GnssTracker* t; };
        std::vector<const GnssTracker*> order;
        for (auto& tr : trackers) order.push_back(tr.get());
        std::sort(order.begin(), order.end(), [](const GnssTracker* a, const GnssTracker* b) { return a->cn0() > b->cn0(); });
        float best = 0;
        uint64_t ok = okRetired, bad = badRetired;
        for (auto* tr : order) {
            GnssChannel c;
            c.sys = tr->sys; c.prn = tr->prn;
            int st = tr->state();
            const SatNav& s = navOf(tr->sys, tr->prn);
            if (st >= GnssChFrameSync && s.hasEph) st = GnssChEphemeris;
            c.state = st;
            c.cn0 = tr->cn0();
            c.dopplerHz = tr->dopplerHz();
            c.codePhase = tr->codePhase() * (tr->sys == GnssGalileo ? 0.5 : 1.0);   // Galileo: counted in half chips
            const SolInfo& si = solOf(tr->sys, tr->prn);
            if (fix.valid && s.hasEph) {
                double pos[3], az, el;
                gpsEphemerisState(s.eph, fix.gpsTow - (tr->sys == GnssBeidou ? kBdtGpsSeconds : 0.0), pos, nullptr);
                azElFromEcef(fix.ecef, pos, &az, &el);
                c.hasAzEl = true; c.azDeg = (float)az; c.elDeg = (float)el;
            } else if (si.has) { c.hasAzEl = true; c.azDeg = si.az; c.elDeg = si.el; }
            c.used = si.used && fix.valid;
            c.residualM = si.used ? si.resid : 0.f;
            c.health = s.hasEph ? (s.eph.health ? 1 : 0) : (s.alm.valid ? (s.alm.health ? 1 : 0) : -1);
            c.framesOk = tr->framesOk(); c.framesBad = tr->framesBad();
            c.lockSecs = (float)tr->lockSeconds();
            c.cn0Hist = tr->cn0History();
            if (tr->carrierLocked()) best = std::max(best, c.cn0);
            ok += tr->framesOk(); bad += tr->framesBad();
            t.channels.push_back(c);
            if (tr->carrierLocked()) t.nTracked++;
            GnssNavInfo ni;
            ni.sys = tr->sys; ni.prn = tr->prn;
            ni.hasEphemeris = s.hasEph;
            if (s.hasEph) {
                ni.iode = s.eph.iode();
                ni.health = s.eph.health ? 1 : 0;
                ni.week = s.week;
                ni.svClockBiasUs = (float)(s.eph.af0 * 1e6);
                if (fix.valid) ni.ephAgeS = (float)gpsWrap(fix.gpsTow - (tr->sys == GnssBeidou ? kBdtGpsSeconds : 0.0) - s.eph.toe);
            }
            ni.hasAlmanac = s.alm.valid;
            ni.towS = s.towLast;
            ni.sbasLastType = s.sbasLast; ni.sbasMessages = s.sbasCount; ni.sbasTypesSeen = s.sbasTypes;
            if (tr->sys == GnssGalileo && gnav[tr->prn].ggtoValid) { ni.ggtoValid = true; ni.ggtoNs = (float)(gnav[tr->prn].a0g * 1e9); }
            // subframes 1, 2, 3 come every 30 s, 6 s apart: the time left is until the last missing one has arrived
            if (s.hasEph) { ni.ephParts = 3; ni.ephEtaS = 0; }
            else if (tr->sys == GnssSbas) { ni.ephParts = 0; ni.ephEtaS = -1; }
            else if (tr->sys == GnssBeidou) { ni.ephParts = bnav[tr->prn].parts(); ni.ephEtaS = -1; }
            else if (tr->sys == GnssGalileo) {
                // words 1-4 of one IODnav (shown as parts of 4), then word 5
                const GalNav& g = gnav[tr->prn];
                int parts = 0;
                for (int k = 1; k <= 4; k++) parts += g.iod[k] >= 0 && g.iod[k] == g.iod[1];
                ni.ephParts = parts; ni.ephEtaS = -1;
            }
            else {
                const bool has[4] = {false, s.pend.prn == tr->prn && s.pend.has1, s.pend.prn == tr->prn && s.pend.has2, s.pend.prn == tr->prn && s.pend.has3};
                ni.ephParts = (int)has[1] + (int)has[2] + (int)has[3];
                if (s.lastSfTime >= 0 && s.lastSfId >= 1 && tr->frameSynced()) {
                    double wait = 0;
                    for (int j = 1; j <= 3; j++) if (!has[j]) { int k = (j - s.lastSfId + 5) % 5; if (k == 0) k = 5; wait = std::max(wait, 6.0 * k); }
                    ni.ephEtaS = (float)std::max(0.0, wait - (signalNow - s.lastSfTime));
                }
            }
            t.nav.push_back(ni);
        }
        t.blocksOk = ok; t.blocksBad = bad;
        t.snrDb = best;
        t.fix = fix;
        t.dataValid = fix.valid;
        t.state = fix.valid ? 2 : (t.nTracked > 0 ? 1 : 0);
        t.cfoHz = cfoEst;
        t.fix.clockDriftMps = driftEst;
        t.ionoValid = iono.valid; t.utcValid = utc.valid; t.leapSeconds = utc.valid ? utc.dtls : -1;
        for (int i = 0; i < 4; i++) { t.ionoAlpha[i] = (float)iono.alpha[i]; t.ionoBeta[i] = (float)iono.beta[i]; }
        int na = 0;
        for (int p = 1; p <= 32; p++) if (sat[p].alm.valid) na++;
        t.almanacGps = na;
        // the sky: satellites above the horizon by the ephemeris or the almanac, from the fix
        if (fix.valid) {
            for (int p = 1; p <= 32 && t.sky.size() < 64; p++) {
                double pos[3];
                bool fromEph = false;
                if (sat[p].hasEph) { gpsEphemerisState(sat[p].eph, fix.gpsTow, pos, nullptr); fromEph = true; }
                else if (sat[p].alm.valid) gpsAlmanacPos(sat[p].alm, fix.gpsTow, pos);
                else continue;
                double az, el;
                azElFromEcef(fix.ecef, pos, &az, &el);
                if (el < 0) continue;
                GnssSky sk;
                sk.sys = GnssGps; sk.prn = p; sk.azDeg = (float)az; sk.elDeg = (float)el; sk.fromEphemeris = fromEph;
                for (auto& tr : trackers) if (tr->sys == GnssGps && tr->prn == p) { sk.tracked = tr->carrierLocked(); sk.cn0 = sk.tracked ? tr->cn0() : 0.f; }
                sk.used = sol[p].used;
                sk.health = sat[p].hasEph ? (sat[p].eph.health ? 1 : 0) : (sat[p].alm.health ? 1 : 0);
                t.sky.push_back(sk);
            }
            // Galileo from its ephemeris
            for (int p = 1; p < kGalPrns && t.sky.size() < 64; p++) {
                if (!gsat[p].hasEph) continue;
                double pos[3], az, el;
                gpsEphemerisState(gsat[p].eph, fix.gpsTow + ggtoAt(fix.gpsTow), pos, nullptr);
                azElFromEcef(fix.ecef, pos, &az, &el);
                if (el < 0) continue;
                GnssSky sk;
                sk.sys = GnssGalileo; sk.prn = p; sk.azDeg = (float)az; sk.elDeg = (float)el; sk.fromEphemeris = true;
                for (auto& tr : trackers) if (tr->sys == GnssGalileo && tr->prn == p) { sk.tracked = tr->carrierLocked(); sk.cn0 = sk.tracked ? tr->cn0() : 0.f; }
                sk.used = gsol[p].used;
                sk.health = gsat[p].eph.health ? 1 : 0;
                t.sky.push_back(sk);
            }
            // QZSS from its ephemeris (SBAS: the geostationary position is in message type 9, not decoded, so it has no place in the sky plot)
            for (int p = 193; p <= 202 && t.sky.size() < 64; p++) {
                if (!sat[p].hasEph) continue;
                double pos[3], az, el;
                gpsEphemerisState(sat[p].eph, fix.gpsTow, pos, nullptr);
                azElFromEcef(fix.ecef, pos, &az, &el);
                if (el < 0) continue;
                GnssSky sk;
                sk.sys = GnssQzss; sk.prn = p; sk.azDeg = (float)az; sk.elDeg = (float)el; sk.fromEphemeris = true;
                for (auto& tr : trackers) if (tr->sys == GnssQzss && tr->prn == p) { sk.tracked = tr->carrierLocked(); sk.cn0 = sk.tracked ? tr->cn0() : 0.f; }
                sk.used = sol[p].used;
                sk.health = sat[p].eph.health ? 1 : 0;
                t.sky.push_back(sk);
            }
            // BeiDou from its ephemeris (BDT)
            for (int p = 1; p <= kBdsMaxPrn && t.sky.size() < 64; p++) {
                if (!bsat[p].hasEph) continue;
                double pos[3], az, el;
                double tb = fix.gpsTow - kBdtGpsSeconds + bdtAt(fix.gpsTow - kBdtGpsSeconds);
                if (tb < 0) tb += 604800.0;
                gpsEphemerisState(bsat[p].eph, tb, pos, nullptr);
                azElFromEcef(fix.ecef, pos, &az, &el);
                if (el < 0) continue;
                GnssSky sk;
                sk.sys = GnssBeidou; sk.prn = p; sk.azDeg = (float)az; sk.elDeg = (float)el; sk.fromEphemeris = true;
                for (auto& tr : trackers) if (tr->sys == GnssBeidou && tr->prn == p) { sk.tracked = tr->carrierLocked(); sk.cn0 = sk.tracked ? tr->cn0() : 0.f; }
                sk.used = bsol[p].used;
                sk.health = bsat[p].eph.health ? 1 : 0;
                t.sky.push_back(sk);
            }
        }
        // search: the L1 one, or the BeiDou one on a tune of its own
        t.searching = gpsActive || bdsActive;
        if (gpsActive) {
            t.searchSys = gpsAcq.currentPrn() > 0 ? l1caSystem(gpsAcq.currentPrn()) : GnssGps; t.searchPrn = gpsAcq.currentPrn();
            t.searchProgress = gpsAcq.progress(); t.searchRounds = gpsAcq.rounds();
            t.acqSys = gpsAcq.lastPrn() > 0 ? l1caSystem(gpsAcq.lastPrn()) : GnssGps; t.acqPrn = gpsAcq.lastPrn(); t.acqDopplerHz = (float)gpsAcq.lastDoppler(); t.acqPeakToNoise = gpsAcq.lastRatio();
            t.acqCorr = gpsAcq.lastCorr(); t.acqPeakIndex = gpsAcq.lastPeakIndex();
            t.searchCenterHz = (float)gpsAcq.windowCenterHz(); t.searchHalfHz = (float)gpsAcq.windowHalfHz(); t.searchMs = gpsAcq.blocksNow();
            t.searchStage = t.nTracked > 0 ? 0 : stage;
        } else if (bdsActive) {
            t.searchSys = GnssBeidou; t.searchPrn = bdsAcq.currentPrn();
            t.searchProgress = bdsAcq.progress(); t.searchRounds = bdsAcq.rounds();
            t.acqSys = GnssBeidou; t.acqPrn = bdsAcq.lastPrn(); t.acqDopplerHz = (float)bdsAcq.lastDoppler(); t.acqPeakToNoise = bdsAcq.lastRatio();
            t.acqCorr = bdsAcq.lastCorr(); t.acqPeakIndex = bdsAcq.lastPeakIndex();
            t.searchCenterHz = (float)bdsAcq.windowCenterHz(); t.searchHalfHz = (float)bdsAcq.windowHalfHz(); t.searchMs = bdsAcq.blocksNow();
            t.searchStage = t.nTracked > 0 ? 0 : bdsPlan.stage;
        }
        for (auto& tr : trackers) t.nPullIn += !tr->carrierLocked();
        t.firstLockSecs = firstLock;
        // scatter of the strongest locked channel
        for (auto* tr : order) {
            if (!tr->carrierLocked() || tr->scatter().size() < 20) continue;
            t.scatterSys = tr->sys; t.scatterPrn = tr->prn;
            float mx = 1e-9f;
            for (auto& pr : tr->scatter()) mx = std::max(mx, std::max(std::fabs(pr.first), std::fabs(pr.second)));
            for (auto& pr : tr->scatter()) { t.scatterI.push_back(pr.first / mx); t.scatterQ.push_back(pr.second / mx); }
            break;
        }
        t.levelDbfs = (float)(20 * std::log10(std::max(levelRms, 1e-6)));
        t.clipPercent = (float)(clipFrac * 100.0);
        t.dcI = (float)dcI; t.dcQ = (float)dcQ;
        char b[120];
        snprintf(b, sizeof b, "%d tracked, %d with ephemeris, %s", t.nTracked, (int)(std::count_if(sat + 1, sat + kCaPrns, [](const SatNav& s) { return s.hasEph; }) + std::count_if(gsat + 1, gsat + kGalPrns, [](const SatNav& s) { return s.hasEph; }) + std::count_if(bsat + 1, bsat + kBdsPrns, [](const SatNav& s) { return s.hasEph; })), fix.valid ? fix.type.c_str() : "no fix");
        t.status = b;
        std::lock_guard<std::mutex> lk(telMu);
        tel = std::move(t);
        telReady = true;
    }

    // ------------------------------------------------------------------ one block of input
    void processBlock(const cf32* x, size_t m) {
        // One pass: remove the DC and scale with the estimates of the blocks before, and measure this block for the next one
        blk.resize(m);
        const float dr = (float)dcI, di = (float)dcQ, gf = (float)gain;
        const float* xf = reinterpret_cast<const float*>(x);
        float* of = reinterpret_cast<float*>(blk.data());
        float sr[4] = {}, si[4] = {}, sq[4] = {};
        size_t clip = 0;
        size_t i = 0;
        for (; i + 4 <= m; i += 4) {
            for (int l = 0; l < 4; l++) {
                const float a = xf[2 * (i + l)], b = xf[2 * (i + l) + 1];
                sr[l] += a; si[l] += b; sq[l] += a * a + b * b;
                of[2 * (i + l)] = (a - dr) * gf;
                of[2 * (i + l) + 1] = (b - di) * gf;
                clip += (std::fabs(a) > 0.98f) | (std::fabs(b) > 0.98f);
            }
        }
        for (; i < m; i++) {
            const float a = xf[2 * i], b = xf[2 * i + 1];
            sr[0] += a; si[0] += b; sq[0] += a * a + b * b;
            of[2 * i] = (a - dr) * gf; of[2 * i + 1] = (b - di) * gf;
            clip += (std::fabs(a) > 0.98f) | (std::fabs(b) > 0.98f);
        }
        const double mr = ((double)sr[0] + sr[1] + sr[2] + sr[3]) / (double)m, mi = ((double)si[0] + si[1] + si[2] + si[3]) / (double)m;
        const double pw = ((double)sq[0] + sq[1] + sq[2] + sq[3]) / (double)m - mr * mr - mi * mi;
        const double dt = (double)m / rate;
        if (!dcInit) { dcI = mr; dcQ = mi; dcInit = true; }
        else { const double a = std::min(1.0, dt / 0.3); dcI += (mr - dcI) * a; dcQ += (mi - dcQ) * a; }
        const double rms = std::sqrt(std::max(pw, 0.0) / 2.0);
        levelRms = levelRms == 0 ? rms : levelRms + (rms - levelRms) * std::min(1.0, dt / 0.5);
        clipFrac += ((double)clip / (double)m - clipFrac) * std::min(1.0, dt / 1.0);
        const double g = 1.0 / std::max(levelRms, 1e-6);
        gain = (inSamples == 0) ? g : gain + (g - gain) * std::min(1.0, dt / 0.5);
        inSamples += (int64_t)m;
        const double c0 = cpuNow();
        if (gpsActive) gpsBand.process(blk.data(), m);
        if (bdsActive) bdsBand.process(blk.data(), m);
        const double now = (double)inSamples / rate;
        const double c1 = cpuNow();
        runTrackers(now);
        const double c2 = cpuNow();
        runAcq(dt);
        const double c3 = cpuNow();
        cpuFront += (c1 - c0);
        cpuTrack += (c2 - c1);
        cpuAcq += (c3 - c2);
        trimBands();
        // measurements: a second apart, once every channel has run past the instant
        {
            double tEnd = 1e18;
            bool any = false;
            if (gpsActive && gpsBand.end() > 0) { tEnd = std::min(tEnd, gpsBand.timeOf((double)gpsBand.end())); any = true; }
            if (bdsActive && bdsBand.end() > 0) { tEnd = std::min(tEnd, bdsBand.timeOf((double)bdsBand.end())); any = true; }
            if (any) while (tEnd - 0.08 > nextMeas) { measure(nextMeas); nextMeas += 1.0; }
        }
        if (now >= nextReport) { nextReport += 0.25; buildTelemetry(now); }
    }
};

GnssReceiver::GnssReceiver() : p_(std::make_unique<Impl>()) {}
GnssReceiver::~GnssReceiver() = default;

void GnssReceiver::configure(double inputRateHz) {
    std::lock_guard<std::mutex> lk(p_->procMu);
    p_->rate = inputRateHz;
    p_->rateAt = inputRateHz;
    p_->resetState();
    p_->lastRate = 0;
    p_->replan();
}
bool GnssReceiver::ready() const { return p_->rateAt.load() >= gnssTuning().minSampleRate - 1; }

void GnssReceiver::reset() {
    std::lock_guard<std::mutex> lk(p_->procMu);
    p_->resetState();
    p_->lastRate = 0;
    if (p_->rate > 0) p_->replan();
}

void GnssReceiver::feed(const cf32* x, size_t n) {
    Impl& p = *p_;
    std::lock_guard<std::mutex> lk(p.procMu);
    if (p.rate <= 0 || n == 0) return;
    if (p.planGen != p.settingsGen.load()) p.replan();
    // The input is cut into blocks of a fixed size whatever the size of the chunks it arrives in: the result does not depend on the chunking
    size_t i = 0;
    if (!p.pend.empty()) {
        const size_t take = std::min(kBlock - p.pend.size(), n);
        p.pend.insert(p.pend.end(), x, x + take);
        i = take;
        if (p.pend.size() == kBlock) { p.processBlock(p.pend.data(), kBlock); p.pend.clear(); }
    }
    while (n - i >= kBlock) { p.processBlock(x + i, kBlock); i += kBlock; }
    if (i < n) p.pend.insert(p.pend.end(), x + i, x + n);
}

bool GnssReceiver::telemetry(GnssTelemetry& out, uint64_t lastSeq) {
    std::lock_guard<std::mutex> lk(p_->telMu);
    if (!p_->telReady || p_->tel.seq <= lastSeq) return false;
    out = p_->tel;
    return true;
}

void GnssReceiver::setLogCallback(std::function<void(const std::string&)> cb) {
    std::lock_guard<std::mutex> lk(p_->procMu);
    p_->log = std::move(cb);
}
void GnssReceiver::setMeasurementCallback(std::function<void(const std::vector<GnssMeasurement>&)> cb) {
    std::lock_guard<std::mutex> lk(p_->procMu);
    p_->measCb = std::move(cb);
}
void GnssReceiver::setCenterMhz(double mhz) { p_->centerMhz = mhz; p_->settingsGen++; }
void GnssReceiver::setSystems(unsigned mask) { p_->sysMask = mask; p_->settingsGen++; }
void GnssReceiver::setWeekReference(int w) { p_->weekRef = w; }
void GnssReceiver::setSearchRange(double hz) { p_->dopHalf = hz; p_->settingsGen++; p_->lastRate = 0; }
void GnssReceiver::setFrequencyHint(double hz, bool valid) { p_->cfoHintHz = hz; p_->cfoHintValid = valid; }
void GnssReceiver::setAcquisitionRate(int u) { p_->acqRate = std::max(1, u); }
void GnssReceiver::setApproxPosition(double lat, double lon, bool valid) { p_->hintLat = lat; p_->hintLon = lon; p_->hintValid = valid; }
void GnssReceiver::setElevationMask(double deg) { p_->elMask = deg; }
bool GnssReceiver::getEphemeris(int prn, GpsEphemeris& e, int* week) const {
    std::lock_guard<std::mutex> lk(p_->procMu);
    if (prn < 1 || prn > 32 || !p_->sat[prn].hasEph) return false;
    e = p_->sat[prn].eph;
    if (week) *week = p_->sat[prn].week;
    return true;
}
bool GnssReceiver::getEphemerisOf(int sys, int prn, GpsEphemeris& e, int* week) const {
    std::lock_guard<std::mutex> lk(p_->procMu);
    if (sys == GnssGalileo) {
        if (prn < 1 || prn >= kGalPrns || !p_->gsat[prn].hasEph) return false;
        e = p_->gsat[prn].eph;
        if (week) *week = p_->gsat[prn].week;
        return true;
    }
    if (sys == GnssBeidou) {
        if (prn < 1 || prn >= kBdsPrns || !p_->bsat[prn].hasEph) return false;
        e = p_->bsat[prn].eph;
        if (week) *week = p_->bsat[prn].week;
        return true;
    }
    if (prn < 1 || prn >= kCaPrns || l1caSystem(prn) != sys || !p_->sat[prn].hasEph) return false;
    e = p_->sat[prn].eph;
    if (week) *week = p_->sat[prn].week;
    return true;
}
bool GnssReceiver::getAlmanac(int prn, GpsAlmanac& a) const {
    std::lock_guard<std::mutex> lk(p_->procMu);
    if (prn < 1 || prn > 32 || !p_->sat[prn].alm.valid) return false;
    a = p_->sat[prn].alm;
    return true;
}
bool GnssReceiver::getIonoUtc(GpsIono& i, GpsUtc& u) const {
    std::lock_guard<std::mutex> lk(p_->procMu);
    i = p_->iono; u = p_->utc;
    return p_->iono.valid;
}
uint64_t GnssReceiver::workUnitsUsed() const { return p_->units; }
void GnssReceiver::cpuBreakdown(double* front, double* track, double* acq) const { *front = p_->cpuFront; *track = p_->cpuTrack; *acq = p_->cpuAcq; }

ModeTuning gnssTuning() {
    ModeTuning t;
    t.stdMode = 14; t.id = "gnss"; t.name = "GNSS";
    t.minMhz = 1550; t.maxMhz = 1610; t.defMhz = 1575.42;
    t.sampleRate = 4e6;          // the C/A main lobe is 2.046 MHz wide; 4 Msps is the lowest comfortable rate for GPS alone (BeiDou with it needs 20 Msps near 1568 MHz, GLONASS 10 Msps near 1602 MHz)
    t.basebandHz = 2.5e6;
    t.bandwidthMhz = 2.046;
    t.minSampleRate = 2.046e6;
    return t;
}

int gnssTuneOf(double mhz) {
    for (int k = 0; k < (int)(sizeof kGnssTuneMhz / sizeof *kGnssTuneMhz); k++) if (std::fabs(mhz - kGnssTuneMhz[k]) < 0.001) return k;
    return GnssTuneCustom;
}
double gnssAutoRate(double mhz) {
    (void)mhz;
    return gnssTuning().sampleRate;      // L1 and B1I: the main lobe of their 1 ms codes in 4 Msps
}
const char* gnssTuneKey(double mhz) {
    switch (gnssTuneOf(mhz)) {
    case GnssTuneB1I: return "b1i";
    default: return "";
    }
}

} // namespace dect2
