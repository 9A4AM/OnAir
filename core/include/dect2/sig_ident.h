// Signal identifier: what is this carrier? From a stretch of complex samples it measures the centre, the -3 / -20 / -26 dB and 99 %
// widths (specx::measure on its own spectrum), continuous or bursty (duty), the modulation class (AM, FM, constant envelope, 2- or 4-level
// FSK, GMSK, BPSK / QPSK / pi/4-DQPSK / 8PSK from the |x|^n lines, QAM, OFDM from the flat top and the cyclic prefix correlation, LoRa
// chirps, pulses), the symbol rate (cyclostationary line of |x|^2, of the FM discriminator or of x^n) and, for FSK, the deviation levels.
// Then it ranks the modes it knows from a table (rate, levels, width, band) and, where it is cheap, looks for their frame sync
// (DMR, P25, NXDN, dPMR, POCSAG, AIS flags) to confirm. identify() is a pure function; Capture collects samples from the engine's
// sample path and runs it on its own thread.
#pragma once
#include "ring.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dect2 {
namespace sigid {

enum Mod {
    ModUnknown = 0, ModNoise, ModCarrier, ModAm, ModFm, ModFsk2, ModFsk4, ModGmsk, ModBpsk, ModQpsk, ModPi4Dqpsk, ModPsk8, ModQam,
    ModOfdm, ModChirp, ModPulse, ModCount
};
const char* modName(int m);   // "4FSK", "pi/4-DQPSK", ...

struct Features {
    bool valid = false;
    std::string why;               // when not valid: what was wrong ("no signal above the floor", ...)
    double centreHz = 0;           // the signal's centre, from the samples' centre (the middle of its 99 % band)
    double rfHz = 0;               // the same in RF (Options::rfHz + centreHz)
    double bw3Hz = 0, bw20Hz = 0, bw26Hz = 0, obwHz = 0;   // widths at -3, -20, -26 dB and the 99 % occupied bandwidth
    double snrDb = 0, powerDb = -200;
    bool flatTop = false;
    double duty = 1;               // share of the time the signal is on (1: continuous)
    int bursts = 0;                // bursts seen (0 for a continuous signal)
    double burstMs = 0;            // median length of a burst
    double captureSec = 0;
    double envKurt = 0;            // E|s|^4 / (E|s|^2)^2 with the noise taken out: 1 constant envelope, ~1.3 filtered PSK, 2 Gaussian (OFDM)
    double carrierDb = 0;          // a line at the centre over the signal's own spectrum (AM, an unmodulated carrier)
    double lineDb[4] = {0, 0, 0, 0};    // the strongest line of (x/|x|)^1, ^2, ^4, ^8 near the centre, over the median, dB
    double lineHz[4] = {0, 0, 0, 0};    // and where it sits from the centre (x^n: n times the carrier's offset; MSK, pi/4: +-Rs/2 n-fold)
    int mod = ModUnknown;
    double symRate = 0;            // symbols per second, 0 = none found
    double symLineDb = 0;          // how far its line stands out
    int symSrc = 0;                // 0 none, 1 |x|^2, 2 FM discriminator, 3 x^n lines, 4 chirp length, 5 OFDM symbol
    int fskLevels = 0;             // 2 or 4 for FSK
    double devHz[4] = {0, 0, 0, 0};   // FSK levels, lowest first, around the centre (4FSK: -outer, -inner, +inner, +outer)
    double devSpread = 0;          // rms scatter of the FSK samples around their levels, Hz
    double ofdmTuSec = 0, ofdmTgSec = 0, ofdmCorr = 0;   // OFDM: useful symbol, guard, cyclic prefix correlation
    double chirpBwHz = 0; int loraSf = 0;                 // LoRa
    bool fmPilot = false;          // a 19 kHz pilot in the FM discriminator (stereo broadcast)
};

struct Candidate {
    std::string name;              // "DMR", "NXDN96", ...
    std::string modeId;            // ModeTuning::id of the mode that decodes it ("" none)
    std::string subId;             // SubMode id inside that mode ("" none)
    double score = 0;              // 0 .. 100
    int syncHits = 0;              // frame syncs found (0 not looked for or none)
    bool syncTried = false;
    std::string why;               // what matched: "4FSK 4800 Bd, +-1944 Hz, 12.5 kHz"
};

struct Result {
    Features f;
    std::vector<Candidate> cands;  // best first; only those that score something
    std::string summary;           // "4FSK, 4800 Bd, +-648 / +-1944 Hz, 9.6 kHz wide, continuous"
};

struct Options {
    double rfHz = 0;               // the RF frequency of the samples' centre (the band of a candidate counts)
    double loHz = 0, hiHz = 0;     // the region, from the samples' centre; lo == hi: the strongest signal in the samples
};

Result identify(const cf32* x, size_t n, double rateHz, const Options& o = Options());

// Collects samples from the engine (push() from the analysis thread: a copy while armed, nothing otherwise) and identifies them on a
// thread of its own. start() again restarts it.
class Capture {
public:
    Capture();
    ~Capture();
    // the region [centreOffHz -+ widthHz / 2] from the samples' centre, rfHz the RF of that centre; seconds of signal (at most 4 M samples)
    void start(double rateHz, double centreOffHz, double widthHz, double rfHz, double seconds = 0.6);
    void cancel();
    bool armed() const { return armed_.load(std::memory_order_relaxed); }
    void push(const cf32* x, size_t n);
    int state() const;             // 0 idle, 1 collecting, 2 working it out, 3 done (result ready)
    double progress() const;       // 0..1 while collecting
    bool result(Result& out, uint64_t& seq) const;   // true when a result newer than seq is there (seq updated)
    double regionCentre() const;   // the centreOffHz of the last start()
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    std::atomic<bool> armed_{false};
};

} // namespace sigid
} // namespace dect2
