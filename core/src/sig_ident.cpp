// Signal identifier (sig_ident.h): measures a carrier from its samples and ranks the modes it could be.
#include "dect2/sig_ident.h"
#include "dect2/dmr_proto.h"
#include "dect2/dpmr_proto.h"
#include "dect2/fftutil.h"
#include "dect2/nxdn_proto.h"
#include "dect2/p25_proto.h"
#include "dect2/pager_proto.h"
#include "dect2/specx_measure.h"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

namespace dect2 {
namespace sigid {

namespace {

constexpr double kPi = 3.14159265358979323846;

double lin(double db) { return std::pow(10.0, db / 10); }
double dB(double x) { return 10 * std::log10(std::max(1e-30, x)); }
size_t pow2At(size_t n) { size_t k = 1; while (k < n) k <<= 1; return k; }
template <class T> double median(std::vector<T> v) {
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + (long)(v.size() / 2), v.end());
    return (double)v[v.size() / 2];
}
template <class T> double quantile(std::vector<T> v, double q) {
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, (size_t)(q * (double)v.size()));
    std::nth_element(v.begin(), v.begin() + (long)k, v.end());
    return (double)v[k];
}

// |FFT|^2 of z (zero padded to a power of two, at most maxN points: z is cut to that)
std::vector<float> powerSpec(const cf32* z, size_t n, size_t maxN = (size_t)1 << 20) {
    size_t N = pow2At(std::max<size_t>(n, 16));
    if (N > maxN) { N = maxN; n = std::min(n, N); }
    std::vector<cf32> b(N, cf32(0, 0));
    std::copy(z, z + n, b.begin());
    Fft f((int)N);
    f.forward(b.data());
    std::vector<float> p(N);
    for (size_t k = 0; k < N; k++) p[k] = std::norm(b[k]);
    return p;
}

struct Line { double hz = 0, db = 0; bool ok = false; };

// The strongest line of a power spectrum P (length N, rate fs, FFT order) with its frequency in [f0, f1] (signed Hz), measured against
// the mean of the bins around it (a window of 2W+1 bins without the 5 in the middle). exclude: bins around this frequency are skipped.
Line findLine(const std::vector<float>& P, double fs, double f0, double f1, double excludeHz = 1e300, double excludeWidth = 0) {
    Line r;
    const int N = (int)P.size();
    if (N < 64) return r;
    const double bin = fs / N;
    const int W = std::max(24, std::min(N / 64, 400));
    int k0 = (int)std::ceil(f0 / bin), k1 = (int)std::floor(f1 / bin);
    k0 = std::max(k0, -N / 2 + 1); k1 = std::min(k1, N / 2 - 1);
    if (k1 < k0) return r;
    // prefix sums over the bins the local means need (circular)
    const int c0 = k0 - W - 1, span = k1 - k0 + 2 * W + 3;
    std::vector<double> cs((size_t)span + 1, 0.0);
    for (int i = 0; i < span; i++) cs[(size_t)i + 1] = cs[(size_t)i] + P[(size_t)(((c0 + i) % N + N) % N)];
    auto sum = [&](int a, int b) { return cs[(size_t)(b - c0 + 1)] - cs[(size_t)(a - c0)]; };   // bins a..b
    double best = 0; int bk = 0;
    for (int k = k0; k <= k1; k++) {
        if (excludeWidth > 0 && std::fabs(k * bin - excludeHz) < excludeWidth) continue;
        const int kk = (k + N) % N;
        const double p3 = P[(size_t)kk] + P[(size_t)((kk + 1) % N)] + P[(size_t)((kk + N - 1) % N)];
        const double around = (sum(k - W, k + W) - sum(k - 2, k + 2)) / (2 * W - 4);
        const double ratio = p3 / 3 / std::max(1e-30, around);
        if (ratio > best) { best = ratio; bk = k; }
    }
    if (best <= 0) return r;
    // parabolic peak on the log spectrum
    const int kk = (bk + N) % N;
    const double a = dB(P[(size_t)((kk + N - 1) % N)]), b = dB(P[(size_t)kk]), c = dB(P[(size_t)((kk + 1) % N)]);
    const double den = a - 2 * b + c, off = std::fabs(den) > 1e-9 ? std::max(-0.5, std::min(0.5, 0.5 * (a - c) / den)) : 0;
    r.hz = (bk + off) * bin;
    r.db = dB(best);
    r.ok = true;
    return r;
}

// one-sided [fmin, fmax] line search of a real feature (mean removed), with a look at f/2 and f/3 (the lowest strong one is the rate)
Line rateLine(const std::vector<float>& feat, double fs, double fmin, double fmax) {
    std::vector<cf32> z(feat.size());
    double m = 0; size_t c = 0;
    for (float v : feat) if (v != 0) { m += v; c++; }   // the mean of the samples that are on (0 = off)
    m /= std::max<size_t>(1, c);
    for (size_t i = 0; i < feat.size(); i++) z[i] = cf32((float)(feat[i] == 0 ? 0 : feat[i] - m), 0);
    const auto P = powerSpec(z.data(), z.size());
    Line l = findLine(P, fs, fmin, fmax);
    if (!l.ok) return l;
    for (int d = 3; d >= 2; d--) {
        const double f = l.hz / d;
        if (f < fmin) continue;
        const Line s = findLine(P, fs, f * 0.985, f * 1.015);
        if (s.ok && s.db > 9 && s.db > l.db - 6) { l = s; break; }
    }
    return l;
}

// 1-D k-means with k = 2 or 4 centres started at the quantiles
struct KM { double c[4] = {0, 0, 0, 0}; int cnt[4] = {0, 0, 0, 0}; double spread = 0; int k = 0; };
KM kmeans(const std::vector<float>& v, int k) {
    KM r; r.k = k;
    if (v.size() < (size_t)(k * 8)) return r;
    for (int j = 0; j < k; j++) r.c[j] = quantile(v, (j + 0.5) / k);
    for (int it = 0; it < 25; it++) {
        double s[4] = {0, 0, 0, 0}; int c[4] = {0, 0, 0, 0};
        for (float x : v) {
            int b = 0;
            for (int j = 1; j < k; j++) if (std::fabs(x - r.c[j]) < std::fabs(x - r.c[b])) b = j;
            s[b] += x; c[b]++;
        }
        for (int j = 0; j < k; j++) if (c[j]) r.c[j] = s[j] / c[j];
        std::sort(r.c, r.c + k);
    }
    double e = 0;
    for (float x : v) {
        int b = 0;
        for (int j = 1; j < k; j++) if (std::fabs(x - r.c[j]) < std::fabs(x - r.c[b])) b = j;
        r.cnt[b]++; e += (x - r.c[b]) * (x - r.c[b]);
    }
    r.spread = std::sqrt(e / v.size());
    return r;
}

// ---------------------------------------------------------------------------------------------------- frame syncs

enum SyncKind { SyncNone = 0, SyncDmr, SyncP25, SyncNxdn, SyncDpmr, SyncPocsag, SyncAis, SyncCount };

// soft 4-level symbols (outer = +-3) against a pattern: windows of normalised correlation >= thr; both polarities, the better one counts.
// period > 0: only hits with another hit a multiple of period symbols away (+-1) count.
int countSync(const std::vector<float>& s, const int* p, int L, double thr, int period = 0) {
    double pp = 0;
    for (int j = 0; j < L; j++) pp += (double)p[j] * p[j];
    std::vector<long> hits[2];
    for (size_t i = 0; i + (size_t)L <= s.size(); i++) {
        double c = 0, ss = 0; bool bad = false;
        for (int j = 0; j < L; j++) { const float v = s[i + (size_t)j]; if (std::isnan(v)) { bad = true; break; } c += v * p[j]; ss += (double)v * v; }
        if (bad || ss <= 0) continue;
        const double r = c / std::sqrt(ss * pp);
        for (int pol = 0; pol < 2; pol++) {
            if ((pol ? -r : r) < thr) continue;
            auto& h = hits[pol];
            if (!h.empty() && (long)i - h.back() < L) continue;
            h.push_back((long)i);
        }
    }
    int best = 0;
    for (auto& h : hits) {
        int n = 0;
        if (period <= 0) n = (int)h.size();
        else for (size_t a = 0; a < h.size(); a++) {
            bool ok = false;
            for (size_t b = 0; b < h.size() && !ok; b++) {
                if (a == b) continue;
                const long d = std::labs(h[a] - h[b]);
                const long q = (d + period / 2) / period;
                if (q >= 1 && q <= 8 && std::labs(d - q * period) <= 1) ok = true;
            }
            if (ok) n++;
        }
        best = std::max(best, n);
    }
    return best;
}

// bits (0/1, 255 = gap) against a 32-bit word with up to maxErr errors, either polarity
int countWord(const std::vector<uint8_t>& b, uint64_t word, int L, int maxErr) {
    int best = 0;
    for (int pol = 0; pol < 2; pol++) {
        int n = 0; long last = -100000;
        for (size_t i = 0; i + (size_t)L <= b.size(); i++) {
            int e = 0; bool bad = false;
            for (int j = 0; j < L && e <= maxErr; j++) {
                const uint8_t v = b[i + (size_t)j];
                if (v > 1) { bad = true; break; }
                const int w = (int)((word >> (L - 1 - j)) & 1) ^ pol;
                if (v != w) e++;
            }
            if (bad || e > maxErr) continue;
            if ((long)i - last < L) continue;
            last = (long)i; n++;
        }
        best = std::max(best, n);
    }
    return best;
}

// ---------------------------------------------------------------------------------------------------- the table of modes

struct Known {
    const char* name; const char* modeId; const char* subId;
    int mod, mod2;                 // the class it shows as (mod2: also accepted, -1 none)
    double rates[4];               // symbol rates; OFDM: useful symbol lengths in seconds
    double bwHz;                   // nominal occupied width (0 any)
    double devOuter;               // FSK outer deviation, Hz (0 not FSK)
    int duty;                      // 0 either, 1 continuous, 2 bursty
    int sync;
    float bands[10];               // MHz (lo, hi) pairs, 0 = end; none = anywhere
    const char* note;
};

const Known kKnown[] = {
    {"DMR", "dmr", "", ModFsk4, -1, {4800}, 9000, 1944, 0, SyncDmr, {136, 174, 403, 470, 806, 870}, "4FSK 4800 Bd, +-648 / +-1944 Hz, 12.5 kHz"},
    {"P25 Phase 1", "p25", "", ModFsk4, -1, {4800}, 8500, 1800, 0, SyncP25, {136, 174, 380, 512, 764, 870}, "C4FM 4800 Bd, +-600 / +-1800 Hz, 12.5 kHz"},
    {"NXDN96", "nxdn", "", ModFsk4, -1, {4800}, 10000, 2400, 0, SyncNxdn, {136, 174, 400, 520}, "4FSK 4800 Bd, +-800 / +-2400 Hz, 12.5 kHz"},
    {"NXDN48", "nxdn", "", ModFsk4, -1, {2400}, 5000, 1050, 0, SyncNxdn, {136, 174, 400, 520}, "4FSK 2400 Bd, +-350 / +-1050 Hz, 6.25 kHz"},
    {"dPMR", "dpmr", "", ModFsk4, -1, {2400}, 5000, 1050, 0, SyncDpmr, {136, 174, 400, 470}, "4FSK 2400 Bd, +-350 / +-1050 Hz, 6.25 kHz"},
    {"M17", "m17", "", ModFsk4, -1, {4800}, 9000, 2400, 0, SyncNone, {144, 148, 420, 450}, "4FSK 4800 Bd, +-800 / +-2400 Hz"},
    {"TETRA", "tetra", "", ModPi4Dqpsk, ModQpsk, {18000}, 22000, 0, 1, SyncNone, {380, 400, 410, 430, 450, 470, 870, 876, 915, 921}, "pi/4-DQPSK 18 kBd, 25 kHz, continuous downlink"},
    {"POCSAG", "pager", "pocsag", ModFsk2, -1, {512, 1200, 2400}, 12000, 4500, 0, SyncPocsag, {25, 1000}, "2FSK 512 / 1200 / 2400 Bd, +-4.5 kHz"},
    {"FLEX", "pager", "flex", ModFsk2, ModFsk4, {1600, 3200}, 12000, 4800, 0, SyncNone, {929, 932, 138, 174}, "2/4FSK 1600 / 3200 Bd, +-4.8 kHz"},
    {"AIS", "ais", "", ModGmsk, ModFsk2, {9600}, 14000, 2400, 2, SyncAis, {161.9f, 162.1f}, "GMSK 9600 Bd, 25 kHz, bursts"},
    {"D-STAR", "dstar", "", ModGmsk, ModFsk2, {4800}, 6000, 1200, 0, SyncNone, {144, 148, 420, 450, 1240, 1300}, "GMSK 4800 Bd, 6.25 kHz"},
    {"Radiosonde", "sonde", "", ModFsk2, ModGmsk, {4800, 2500, 1200}, 10000, 2400, 1, SyncNone, {400, 406}, "2FSK 4800 Bd (RS41) and others, 400-406 MHz"},
    {"GSM-R / GSM", "gsmr", "", ModGmsk, ModFsk2, {270833}, 200000, 67708, 0, SyncNone, {876, 880, 921, 960, 1805, 1880}, "GMSK 270.833 kBd, 200 kHz"},
    {"LBJ railway warning", "lbj", "", ModFm, ModFsk2, {1200}, 25000, 1200, 0, SyncNone, {821}, "FM, 1200 bit/s FSK (Chinese railway)"},
    {"Bluetooth LE", "ble", "", ModGmsk, ModFsk2, {1e6, 2e6}, 1.2e6, 250000, 2, SyncNone, {2400, 2484}, "GFSK 1 Mbit/s, bursts"},
    {"ADS-B / Mode S", "adsb", "", ModPulse, ModAm, {1e6, 2e6}, 2.6e6, 0, 2, SyncNone, {1089, 1091}, "PPM pulses 1 Mbit/s, 1090 MHz bursts"},
    {"Mode S uplink", "uplink", "", ModPulse, ModBpsk, {4e6, 1e6}, 6e6, 0, 2, SyncNone, {1029, 1031}, "1030 MHz interrogations"},
    {"ACARS", "acars", "", ModAm, -1, {2400}, 6000, 0, 2, SyncNone, {129, 137}, "AM-MSK 2400 bit/s, bursts"},
    {"VDL Mode 2", "vdl2", "", ModPsk8, ModQpsk, {10500}, 14000, 0, 2, SyncNone, {136.6f, 137}, "D8PSK 10.5 kBd, 25 kHz, bursts"},
    {"Airband voice", "airband", "", ModAm, ModCarrier, {}, 6000, 0, 0, SyncNone, {108, 137}, "AM voice, 8.33 / 25 kHz"},
    {"AM broadcast", "", "", ModAm, ModCarrier, {}, 9000, 0, 1, SyncNone, {0.15f, 30}, "AM, 9 / 10 kHz"},
    {"Analog TV", "atv", "", ModAm, ModCarrier, {}, 5.5e6, 0, 1, SyncNone, {47, 68, 174, 230, 470, 862}, "AM vestigial sideband, 6-8 MHz"},
    {"CW", "cw", "", ModCarrier, -1, {}, 200, 0, 2, SyncNone, {}, "keyed carrier"},
    {"FM broadcast", "fm", "", ModFm, -1, {}, 180000, 0, 1, SyncNone, {65.8f, 74, 76, 108}, "wide FM, 200 kHz"},
    {"NFM voice / APRS", "packet", "", ModFm, -1, {}, 11000, 0, 0, SyncNone, {136, 174, 400, 470, 26, 30}, "narrow FM, 12.5 / 25 kHz (APRS: AFSK 1200 on FM)"},
    {"DAB / DAB+", "dab", "", ModOfdm, -1, {1e-3, 250e-6, 125e-6, 500e-6}, 1.536e6, 0, 1, SyncNone, {174, 240, 1452, 1492}, "OFDM, 1.536 MHz, Tu 1 ms (mode I)"},
    {"DVB-T / T2", "dvbt", "", ModOfdm, -1, {896e-6, 224e-6, 1024e-6, 256e-6}, 7.6e6, 0, 1, SyncNone, {174, 230, 470, 862}, "OFDM, 6-8 MHz, Tu 224 / 896 us (8 MHz)"},
    {"DVB-T2 (other FFT)", "dvbt", "", ModOfdm, -1, {1792e-6, 3584e-6, 448e-6, 112e-6}, 7.6e6, 0, 1, SyncNone, {174, 230, 470, 862}, "OFDM, 16k / 32k / 4k / 1k"},
    {"ISDB-T", "isdbt", "", ModOfdm, -1, {1008e-6, 504e-6, 252e-6}, 5.6e6, 0, 1, SyncNone, {470, 806}, "OFDM, 5.6 MHz, Tu 252 / 504 / 1008 us"},
    {"DTMB", "dtmb", "", ModOfdm, ModQam, {500e-6}, 7.56e6, 0, 1, SyncNone, {470, 862}, "OFDM 3780 carriers (or single carrier), 8 MHz"},
    {"DRM", "drm", "", ModOfdm, -1, {24e-3, 21.333e-3, 14.666e-3, 9.333e-3}, 10000, 0, 1, SyncNone, {0.1f, 30}, "OFDM, 4.5-20 kHz, Tu 9-24 ms"},
    {"DRM+", "drm", "", ModOfdm, -1, {2.25e-3}, 96000, 0, 1, SyncNone, {47, 108}, "OFDM, 96 kHz, Tu 2.25 ms"},
    {"LTE", "cell", "", ModOfdm, -1, {66.667e-6}, 0, 0, 1, SyncNone, {700, 960, 1710, 2170, 2500, 2700}, "OFDM, 15 kHz carriers, 1.4-20 MHz"},
    {"Wi-Fi", "wifi", "", ModOfdm, -1, {3.2e-6, 12.8e-6}, 0, 0, 2, SyncNone, {2400, 2500, 5150, 5900, 5925, 7125}, "OFDM bursts, 20-160 MHz"},
    {"LoRa", "mesh", "", ModChirp, -1, {}, 0, 0, 2, SyncNone, {433, 435, 863, 870, 902, 928}, "chirp spread spectrum, 62.5-500 kHz"},
    {"DVB-S / S2", "dvbs", "", ModQpsk, ModPsk8, {}, 0, 0, 1, SyncNone, {950, 2150, 10700, 12750}, "QPSK / 8PSK, 1-45 MBd, roll-off 0.20-0.35"},
    {"DVB-C", "dvbc", "", ModQam, -1, {6.875e6, 6.9e6, 5.217e6}, 8e6, 0, 1, SyncNone, {110, 862}, "16-256-QAM, 6.9 MBd"},
    {"Iridium", "iridium", "", ModQpsk, ModBpsk, {25000}, 31500, 0, 2, SyncNone, {1616, 1626.5f}, "DE-QPSK 25 kBd bursts"},
    {"Inmarsat-C", "inmc", "", ModBpsk, -1, {1200}, 2500, 0, 1, SyncNone, {1525, 1560}, "BPSK 1200 Bd"},
    {"Inmarsat Aero", "aero", "", ModBpsk, ModQpsk, {600, 1200, 5250, 10500}, 0, 0, 0, SyncNone, {1525, 1560}, "A/O-QPSK 600-10500 Bd"},
    {"ISM sensor", "ism", "", ModPulse, ModFsk2, {}, 0, 0, 2, SyncNone, {433.05f, 434.79f, 868, 868.6f, 902, 928, 314, 316}, "OOK / FSK bursts"},
};
constexpr int kKnownCount = (int)(sizeof kKnown / sizeof *kKnown);

bool related(int a, int b) {
    auto fam = [](int m) {
        switch (m) {
        case ModFsk2: case ModGmsk: return 1;
        case ModFsk4: return 2;
        case ModBpsk: case ModQpsk: case ModPi4Dqpsk: case ModPsk8: case ModQam: return 3;
        case ModAm: case ModCarrier: return 4;
        case ModPulse: return 5;
        default: return 10 + m;
        }
    };
    return fam(a) == fam(b);
}

// ---------------------------------------------------------------------------------------------------- the steps

// mix the region's centre to 0 and low-pass it to +-cut, every D-th sample (a band-pass FIR on the input: no mixed copy)
std::vector<cf32> downconvert(const cf32* x, size_t n, double fs, double fc, double cut, int D, int L) {
    std::vector<cf32> h((size_t)L);
    const int M = L / 2;
    double g = 0;
    std::vector<double> hr((size_t)L);
    for (int k = 0; k < L; k++) {
        const double t = k - M;
        const double s = t == 0 ? 2 * cut / fs : std::sin(2 * kPi * cut / fs * t) / (kPi * t);
        const double w = 0.42 - 0.5 * std::cos(2 * kPi * k / (L - 1)) + 0.08 * std::cos(4 * kPi * k / (L - 1));
        hr[(size_t)k] = s * w; g += hr[(size_t)k];
    }
    // y(i) = sum h_lp[k] x[i-k] e^{-j w0 (i-k)} = e^{-j w0 i} sum (h_lp[k] e^{j w0 k}) x[i-k]
    for (int k = 0; k < L; k++) {
        const double ph = 2 * kPi * fc / fs * k;
        h[(size_t)k] = cf32((float)(hr[(size_t)k] / g * std::cos(ph)), (float)(hr[(size_t)k] / g * std::sin(ph)));
    }
    std::vector<cf32> y;
    if (n < (size_t)L) return y;
    y.reserve((n - (size_t)L) / (size_t)D + 1);
    const double w0 = 2 * kPi * fc / fs;
    for (size_t i = (size_t)L - 1; i < n; i += (size_t)D) {
        float re = 0, im = 0;
        const cf32* xp = x + i;
        for (int k = 0; k < L; k++) {
            const cf32 a = h[(size_t)k], b = *(xp - k);
            re += a.real() * b.real() - a.imag() * b.imag();
            im += a.real() * b.imag() + a.imag() * b.real();
        }
        const double ph = -w0 * (double)i;
        const double c = std::cos(ph), s = std::sin(ph);
        y.push_back(cf32((float)(re * c - im * s), (float)(re * s + im * c)));
    }
    return y;
}

struct Work {
    std::vector<cf32> y;           // the signal at the centre, rate fsd
    double fsd = 0;
    std::vector<uint8_t> on;       // per sample: the signal is on (a block more each side of a burst)
    std::vector<uint8_t> core;     // on, without that margin (envelope statistics)
    std::vector<std::pair<size_t, size_t>> runs;   // on-runs [a, b)
    double noise = 0;              // noise power per sample in y
    std::vector<float> d;          // FM discriminator, Hz (0 where off)
    std::vector<float> ds;         // smoothed
    std::vector<float> P[4];       // power spectra of (x/|x|)^1, ^2, ^4, ^8
};

void gate(Work& w, Features& f, double sigBw) {
    const size_t m = w.y.size();
    const double blockSec = std::max(20e-6, std::min(1e-3, 40.0 / std::max(1.0, sigBw)));
    const size_t B = std::max<size_t>(4, (size_t)(blockSec * w.fsd));
    const size_t nb = m / B;
    w.on.assign(m, 1);
    w.core.assign(m, 1);
    f.duty = 1; f.bursts = 0;
    if (nb < 8) { w.runs = {{0, m}}; return; }
    std::vector<float> p(nb);
    for (size_t b = 0; b < nb; b++) { double s = 0; for (size_t i = b * B; i < (b + 1) * B; i++) s += std::norm(w.y[i]); p[b] = (float)(s / B); }
    const double p10 = quantile(p, 0.10), p90 = quantile(p, 0.90);
    const double lo = std::max(p10, w.noise);
    if (p90 < 4 * lo || p90 < 2 * w.noise) { w.runs = {{0, m}}; return; }   // under 6 dB of swing: continuous
    const double thr = std::sqrt(lo * p90);
    std::vector<uint8_t> ob(nb);
    for (size_t b = 0; b < nb; b++) ob[b] = p[b] > thr;
    // close one-block holes, then count
    for (size_t b = 1; b + 1 < nb; b++) if (!ob[b] && ob[b - 1] && ob[b + 1]) ob[b] = 1;
    size_t onCount = 0;
    for (size_t b = 0; b < nb; b++) onCount += ob[b];
    f.duty = (double)onCount / nb;
    if (f.duty > 0.97) { w.runs = {{0, m}}; f.duty = 1; return; }
    std::vector<double> lens;
    w.runs.clear();
    for (size_t b = 0; b < nb;) {
        if (!ob[b]) { b++; continue; }
        size_t e = b;
        while (e < nb && ob[e]) e++;
        w.runs.push_back({b > 0 ? (b - 1) * B : 0, std::min(m, (e + 1) * B)});   // a block more each side: the ramps and a preamble
        lens.push_back((double)(e - b) * B / w.fsd);
        b = e;
    }
    for (size_t i = 0; i < m; i++) w.core[i] = (i / B < nb) ? ob[i / B] : 0;
    std::fill(w.on.begin(), w.on.end(), 0);
    for (const auto& r : w.runs) std::fill(w.on.begin() + (long)r.first, w.on.begin() + (long)r.second, 1);
    f.bursts = (int)w.runs.size();
    f.burstMs = median(lens) * 1e3;
}

// u^k (phase only) lines near the centre
void powerLines(Work& w, Features& f) {
    const size_t m = std::min<size_t>(w.y.size(), (size_t)1 << 20);
    std::vector<cf32> z(m);
    const int pw[4] = {1, 2, 4, 8};
    for (int j = 0; j < 4; j++) {
        for (size_t i = 0; i < m; i++) {
            const cf32 v = w.y[i];
            const float a = std::abs(v);
            if (!w.core[i] || a < 1e-12f) { z[i] = 0; continue; }
            cf32 u = v / a, r = u;
            for (int q = 1; q < pw[j]; q++) r *= u;
            z[i] = r;
        }
        w.P[j] = powerSpec(z.data(), m);
        const Line l = findLine(w.P[j], w.fsd, -w.fsd * 0.48, w.fsd * 0.48);
        f.lineDb[j] = l.ok ? l.db : 0; f.lineHz[j] = l.hz;
    }
}

// OFDM: the cyclic prefix shows as a peak of the autocorrelation at the useful symbol length; the symbol period as a line of y(t) y*(t + Tu)
bool ofdmTest(const Work& w, Features& f) {
    const size_t m = std::min<size_t>(w.y.size(), (size_t)1 << 18);
    if (m < 4096) return false;
    const size_t N = pow2At(2 * m);
    std::vector<cf32> b(N, cf32(0, 0));
    for (size_t i = 0; i < m; i++) b[i] = w.on[i] ? w.y[i] : cf32(0, 0);
    Fft F((int)N);
    F.forward(b.data());
    for (auto& v : b) v = cf32(std::norm(v), 0);
    F.inverse(b.data());
    const double r0 = std::abs(b[0]);
    if (r0 <= 0) return false;
    const size_t tmin = std::max<size_t>(8, (size_t)(10 * w.fsd / std::max(1.0, f.obwHz)));
    const size_t tmax = std::min<size_t>(m / 3, (size_t)(0.03 * w.fsd));
    if (tmax <= tmin + 16) return false;
    std::vector<float> mag;
    size_t bt = 0; double bv = 0;
    for (size_t t = tmin; t <= tmax; t++) {
        const double v = std::abs(b[t]) / r0 * (double)m / (double)(m - t);   // fewer overlapping samples at larger lags
        mag.push_back((float)v);
        if (v > bv) { bv = v; bt = t; }
    }
    const double med = median(mag);
    if (bv < 0.03 || bv < 8 * med) return false;
    // the symbol period: a line of p(t) = y(t) y*(t + Tu) between 1 / (1.85 Tu) and 1 / (1.01 Tu)
    const size_t mp = std::min<size_t>(w.y.size() - bt, (size_t)1 << 20);
    std::vector<cf32> p(mp);
    for (size_t i = 0; i < mp; i++) p[i] = (w.on[i] && w.on[i + bt]) ? w.y[i] * std::conj(w.y[i + bt]) : cf32(0, 0);
    const auto P = powerSpec(p.data(), mp);
    const double tu = bt / w.fsd;
    const Line l = findLine(P, w.fsd, 1 / (1.85 * tu), 1 / (1.01 * tu));
    f.ofdmTuSec = tu;
    f.ofdmCorr = bv;
    if (l.ok && l.db > 8) { const double ts = 1 / l.hz; f.ofdmTgSec = std::max(0.0, ts - tu); f.symRate = l.hz; f.symLineDb = l.db; f.symSrc = 5; }
    return true;
}

// LoRa: y(t + T/4) y*(t) of a chirp of width B and length T = 2^SF / B is a tone at B/4 (3/4 of the time); at half the lag, B/8
bool chirpTest(const Work& w, Features& f) {
    double B = f.obwHz > 0 ? f.obwHz : f.bw20Hz;
    static const double kBws[] = {7.8e3, 10.4e3, 15.6e3, 20.8e3, 31.25e3, 41.7e3, 62.5e3, 125e3, 250e3, 500e3, 812.5e3, 1625e3};
    for (double b : kBws) if (std::fabs(B / b - 1) < 0.15) { B = b; break; }
    if (B < 5e3) return false;
    auto tone = [&](size_t L, double& hz) {
        const size_t m = std::min<size_t>(w.y.size() > L ? w.y.size() - L : 0, (size_t)1 << 18);
        if (m < 1024) return 0.0;
        std::vector<cf32> z(m);
        for (size_t i = 0; i < m; i++) z[i] = (w.on[i] && w.on[i + L]) ? w.y[i + L] * std::conj(w.y[i]) : cf32(0, 0);
        const auto P = powerSpec(z.data(), m);
        const Line l = findLine(P, w.fsd, -w.fsd * 0.48, w.fsd * 0.48);
        hz = l.hz;
        return l.ok ? l.db : 0.0;
    };
    int bestSf = 0; double bestDb = 0; bool down = false;
    for (int sf = 6; sf <= 12; sf++) {
        const double T = std::ldexp(1.0, sf) / B;
        const size_t L = (size_t)std::lround(T * w.fsd / 4);
        if (L < 4 || L * 4 > w.y.size()) continue;
        double hz = 0;
        const double db = tone(L, hz);
        if (db < 12 || std::fabs(std::fabs(hz) - B / 4) > 0.08 * B) continue;
        double hz2 = 0;
        const double db2 = tone(L / 2, hz2);
        if (db2 < 8 || std::fabs(std::fabs(hz2) - B / 8) > 0.06 * B || (hz2 > 0) != (hz > 0)) continue;
        if (db > bestDb) { bestDb = db; bestSf = sf; down = hz < 0; }
    }
    if (!bestSf) return false;
    f.chirpBwHz = B; f.loraSf = bestSf;
    f.symRate = B / std::ldexp(1.0, bestSf); f.symLineDb = bestDb; f.symSrc = 4;
    (void)down;
    return true;
}

// symbols of the smoothed discriminator at rate rs: blocks of 128 symbols, each at the phase of the most energy; NaN between bursts
std::vector<float> sampleSymbols(const Work& w, double rs, double mid) {
    std::vector<float> out;
    const double sps = w.fsd / rs;
    if (sps < 2) return out;
    auto at = [&](double t) {
        const size_t i = (size_t)t;
        if (i + 1 >= w.ds.size()) return w.ds.back();
        const double fr = t - i;
        return (float)(w.ds[i] * (1 - fr) + w.ds[i + 1] * fr);
    };
    for (const auto& r : w.runs) {
        const double a = (double)r.first + sps, b = (double)r.second - sps - 1;
        if (b - a < 16 * sps) continue;
        for (double t0 = a; t0 + sps < b;) {
            const int ns = (int)std::min(128.0, std::floor((b - t0) / sps));
            if (ns < 8) break;
            int bp = 0; double be = -1;
            for (int ph = 0; ph < 16; ph++) {
                double e = 0;
                for (int i = 0; i < ns; i++) { const double v = at(t0 + (ph / 16.0) * sps + i * sps) - mid; e += v * v; }
                if (e > be) { be = e; bp = ph; }
            }
            for (int i = 0; i < ns; i++) out.push_back(at(t0 + (bp / 16.0) * sps + i * sps));
            t0 += ns * sps;
        }
        out.push_back(NAN);
    }
    return out;
}

// FSK: levels at the symbol instants
bool fskTest(const Work& w, Features& f, double rs, std::vector<float>& syms, double& mid, double& unit) {
    std::vector<float> on;
    for (size_t i = 0; i < w.ds.size(); i++) if (w.on[i]) on.push_back(w.ds[i]);
    if (on.size() < 256) return false;
    const double m0 = median(on);
    syms = sampleSymbols(w, rs, m0);
    std::vector<float> v;
    for (float s : syms) if (!std::isnan(s)) v.push_back(s);
    if (v.size() < 64) return false;
    const KM k4 = kmeans(v, 4), k2 = kmeans(v, 2);
    const double s1 = k4.c[1] - k4.c[0], s2 = k4.c[2] - k4.c[1], s3 = k4.c[3] - k4.c[2];
    const double span = k4.c[3] - k4.c[0];
    const int minCnt = std::min(std::min(k4.cnt[0], k4.cnt[1]), std::min(k4.cnt[2], k4.cnt[3]));
    const bool four = k4.k == 4 && std::min(s1, std::min(s2, s3)) > 2.2 * k4.spread && minCnt > (int)(0.06 * v.size()) &&
                      span / std::max(1e-9, s2) > 2.0 && span / std::max(1e-9, s2) < 4.6;
    if (four) {
        mid = (k4.c[0] + k4.c[3]) / 2; unit = span / 6;
        f.fskLevels = 4;
        for (int j = 0; j < 4; j++) f.devHz[j] = k4.c[j] - mid;
        f.devSpread = k4.spread;
        return true;
    }
    const double sep = k2.c[1] - k2.c[0];
    const int min2 = std::min(k2.cnt[0], k2.cnt[1]);
    if (k2.k == 2 && sep > 3.0 * k2.spread && min2 > (int)(0.1 * v.size())) {
        mid = (k2.c[0] + k2.c[1]) / 2; unit = sep / 2;
        f.fskLevels = 2;
        f.devHz[0] = k2.c[0] - mid; f.devHz[1] = k2.c[1] - mid;
        f.devSpread = k2.spread;
        return true;
    }
    return false;
}

std::string hzText(double hz) {
    char b[48];
    const double a = std::fabs(hz);
    if (a >= 1e6) snprintf(b, sizeof b, "%.3g MHz", hz / 1e6);
    else if (a >= 1e3) snprintf(b, sizeof b, "%.3g kHz", hz / 1e3);
    else snprintf(b, sizeof b, "%.0f Hz", hz);
    return b;
}
std::string rateText(double rs) {
    char b[48];
    if (rs >= 1e6) snprintf(b, sizeof b, "%.4g MBd", rs / 1e6);
    else if (rs >= 1e4) snprintf(b, sizeof b, "%.4g kBd", rs / 1e3);
    else snprintf(b, sizeof b, "%.0f Bd", rs);
    return b;
}

} // namespace

const char* modName(int m) {
    static const char* const k[ModCount] = {"unknown", "noise", "carrier", "AM", "FM", "2FSK", "4FSK", "GMSK / MSK", "BPSK", "QPSK",
                                            "pi/4-DQPSK", "8PSK", "QAM", "OFDM", "chirp (LoRa)", "pulses (OOK / PPM)"};
    return m >= 0 && m < ModCount ? k[m] : "?";
}

Result identify(const cf32* x, size_t n, double fs, const Options& o) {
    Result R;
    Features& f = R.f;
    n = std::min<size_t>(n, (size_t)4 << 20);
    if (!x || n < 4096 || fs <= 0) { f.why = "too few samples"; return R; }
    f.captureSec = n / fs;
    const bool region = o.hiHz > o.loHz;
    // ---- spectrum (Blackman-Harris, power average) and specx::measure on it
    const double want = region ? (o.hiHz - o.loHz) / 256 : fs / 4096;
    size_t N = pow2At((size_t)std::ceil(fs / std::max(1.0, want)));
    N = std::max<size_t>(512, std::min<size_t>(N, 65536));
    while (N > 256 && N * 8 > n) N >>= 1;
    std::vector<float> win(N);
    double ws = 0, ws2 = 0;
    for (size_t i = 0; i < N; i++) {
        const double t = 2 * kPi * i / N;
        win[i] = (float)(0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t));
        ws += win[i]; ws2 += (double)win[i] * win[i];
    }
    const double enbw = N * ws2 / (ws * ws);
    std::vector<double> acc(N, 0.0);
    const size_t segs = std::min<size_t>(512, (n - N) / (N / 2) + 1);
    const double stride = segs > 1 ? (double)(n - N) / (segs - 1) : 0;
    Fft F((int)N);
    std::vector<cf32> b(N);
    for (size_t s = 0; s < segs; s++) {
        const size_t o0 = (size_t)(s * stride);
        for (size_t i = 0; i < N; i++) b[i] = x[o0 + i] * win[i];
        F.forward(b.data());
        for (size_t i = 0; i < N; i++) acc[i] += std::norm(b[i]);
    }
    std::vector<float> db(N);
    for (size_t i = 0; i < N; i++) db[i] = (float)dB(acc[(i + N / 2) % N] / segs / (ws * ws));
    specx::SpecView v;
    v.db = db.data(); v.n = (int)N; v.binHz = fs / N; v.f0Hz = -fs / 2; v.enbw = enbw;
    specx::Measure ms;
    if (region) ms = specx::measure(v, o.loHz, o.hiHz);
    else {
        const auto found = specx::detect(v, 6, 3);
        for (const auto& c : found) if (!ms.valid || c.powerDb > ms.powerDb) ms = c;
    }
    if (!ms.valid || ms.snrDb < 3) { f.why = "no signal above the noise floor here"; f.mod = ModNoise; R.summary = "nothing but noise"; return R; }
    f.centreHz = ms.obwHz > 0 ? ms.obwCentreHz : ms.centreHz;
    f.bw3Hz = ms.bw3Hz; f.bw20Hz = ms.bw20Hz; f.bw26Hz = ms.bw26Hz; f.obwHz = ms.obwHz;
    f.snrDb = ms.snrDb; f.powerDb = ms.powerDb; f.flatTop = ms.flatTop;
    // ---- down to the signal: centre at 0, filtered to its width with some room, about 6 samples per half width
    const double sigW = std::max(std::max(ms.obwHz, ms.bw20AtFloor ? 0.0 : ms.bw20Hz), 4 * v.binHz);
    double half = 0.5 * sigW * 1.15 + 2 * v.binHz;
    if (region) half = std::min(half, std::max(0.5 * (o.hiHz - o.loHz), 4 * v.binHz));
    half = std::min(half, fs / 2);
    Work w;
    const double cut = std::min(half * 1.2, fs / 2);
    int D = std::max(1, (int)std::floor(fs / std::max(4e3, 6 * half)));
    double noiseBw = fs;
    if (2 * cut > 0.8 * fs) {   // the signal fills the band: only move it to the centre
        D = 1;
        w.y.resize(n);
        double ph = 0; const double dph = -2 * kPi * f.centreHz / fs;
        for (size_t i = 0; i < n; i++) { w.y[i] = x[i] * cf32((float)std::cos(ph), (float)std::sin(ph)); ph += dph; if (ph < -kPi) ph += 2 * kPi; else if (ph > kPi) ph -= 2 * kPi; }
    } else {
        const int L = (int)std::max(31.0, std::min(8191.0, std::ceil(5.5 * fs / std::max(1.0, 0.5 * half)))) | 1;
        w.y = downconvert(x, n, fs, f.centreHz, cut, D, L);
        noiseBw = 2 * cut;
    }
    w.fsd = fs / D;
    if (w.y.size() < 2048) { f.why = "too short a capture for this signal"; return R; }
    w.noise = lin(ms.floorDb) * N / enbw * std::min(1.0, noiseBw / fs);
    gate(w, f, sigW);
    // ---- envelope
    double m2 = 0, m4 = 0; size_t non = 0;
    for (size_t i = 0; i < w.y.size(); i++) if (w.core[i]) { const double p = std::norm(w.y[i]); m2 += p; m4 += p * p; non++; }
    m2 /= std::max<size_t>(1, non); m4 /= std::max<size_t>(1, non);
    const double s2 = std::max(0.1 * m2, m2 - w.noise), s4 = std::max(0.5 * s2 * s2, m4 - 4 * s2 * w.noise - 2 * w.noise * w.noise);
    f.envKurt = s4 / (s2 * s2);
    // ---- lines: the carrier, (x/|x|)^n
    powerLines(w, f);
    {   // carrier: a line within a few bins of the centre of y's spectrum
        const size_t m = std::min<size_t>(w.y.size(), (size_t)1 << 20);
        std::vector<cf32> z(m);
        for (size_t i = 0; i < m; i++) z[i] = w.core[i] ? w.y[i] : cf32(0, 0);
        const auto P = powerSpec(z.data(), m);
        const double tol = std::max(4 * w.fsd / P.size(), std::min(0.02 * sigW, 200.0));
        const Line c = findLine(P, w.fsd, -tol, tol);
        f.carrierDb = c.ok ? c.db : 0;
    }
    // ---- discriminator
    w.d.assign(w.y.size(), 0.f);
    for (size_t i = 1; i < w.y.size(); i++) if (w.on[i] && w.on[i - 1]) w.d[i] = (float)(std::arg(w.y[i] * std::conj(w.y[i - 1])) * w.fsd / (2 * kPi));
    // ---- symbol rate candidates: envelope and discriminator lines
    const double fmin = std::max(30.0, 0.025 * sigW), fmax = std::min(0.45 * w.fsd, 1.6 * sigW);
    std::vector<float> env(w.y.size()), dsq(w.y.size());
    for (size_t i = 0; i < w.y.size(); i++) env[i] = w.core[i] ? std::norm(w.y[i]) : 0.f;
    double dm = 0; size_t dn = 0;
    for (size_t i = 0; i < w.d.size(); i++) if (w.on[i]) { dm += w.d[i]; dn++; }
    dm /= std::max<size_t>(1, dn);
    for (size_t i = 0; i < w.d.size(); i++) dsq[i] = w.core[i] ? (float)((w.d[i] - dm) * (w.d[i] - dm)) : 0.f;
    const Line le = rateLine(env, w.fsd, std::max(fmin, 0.25 * sigW), fmax), ld = rateLine(dsq, w.fsd, fmin, fmax);
    // tones in the discriminator itself: analog FM with a tone (or a dotting preamble)
    double toneFrac = 0;
    {
        std::vector<cf32> z(w.d.size());
        for (size_t i = 0; i < w.d.size(); i++) z[i] = cf32(w.on[i] ? (float)(w.d[i] - dm) : 0.f, 0);
        const auto P = powerSpec(z.data(), z.size());
        const Line t = findLine(P, w.fsd, 20, 0.45 * w.fsd);
        if (t.ok) {
            const size_t Nn = P.size();
            const int k = (int)std::lround(t.hz / (w.fsd / Nn));
            double pl = 0, pt = 0;
            for (size_t i = 1; i < Nn / 2; i++) pt += P[i];
            for (int j = -3; j <= 3; j++) if (k + j > 0 && k + j < (int)Nn / 2) pl += P[(size_t)(k + j)];
            toneFrac = pt > 0 ? pl / pt : 0;
        }
        if (sigW > 80e3 && w.fsd > 45e3) { const Line p = findLine(P, w.fsd, 18900, 19100); f.fmPilot = p.ok && p.db > 15; }
    }
    // ---- smoothed discriminator for the FSK levels
    auto smooth = [&](double rs) {
        const int k = std::max(1, (int)std::lround(0.4 * w.fsd / std::max(1.0, rs)));
        w.ds.assign(w.d.size(), 0.f);
        double acc2 = 0;
        for (size_t i = 0; i < w.d.size(); i++) {
            acc2 += w.d[i];
            if (i >= (size_t)k) acc2 -= w.d[i - (size_t)k];
            const size_t c = i >= (size_t)(k / 2) ? i - (size_t)(k / 2) : 0;
            w.ds[c] = (float)(acc2 / k);
        }
    };
    auto snap = [&](double rs) {
        double best = rs, be = 0.012;
        for (int i = 0; i < kKnownCount; i++) for (double r : kKnown[i].rates) if (r > 1 && std::fabs(rs / r - 1) < be) { be = std::fabs(rs / r - 1); best = r; }
        return best;
    };
    // ---- classification
    std::vector<float> syms; double mid = 0, unit = 1;
    const bool constEnv = f.envKurt < 1.12;
    if (sigW < 400 && f.carrierDb > 15) f.mod = ModCarrier;
    else if (f.envKurt > 1.6 || (f.flatTop && f.envKurt > 1.45)) {
        if (ofdmTest(w, f)) f.mod = ModOfdm;
        else if (f.duty < 0.97 || le.db > 12) { f.mod = ModPulse; if (le.ok && le.db > 10) { f.symRate = le.hz; f.symLineDb = le.db; f.symSrc = 1; } }
        else f.mod = ModQam;
    } else if (f.carrierDb > 12 && f.envKurt > 1.04 && std::max(f.lineDb[2], f.lineDb[3]) < f.carrierDb + 6) f.mod = ModAm;
    if (f.mod == ModUnknown && constEnv) {
        if (sigW >= 5e3 && chirpTest(w, f)) f.mod = ModChirp;
        else if (f.fmPilot) f.mod = ModFm;
        else {
            // FSK at the discriminator's rate (or the envelope's)
            Line r = ld.ok && ld.db > 9 ? ld : (le.ok && le.db > 9 ? le : Line());
            if (r.ok && r.hz >= sigW / 30 && toneFrac < 0.85) {
                const double rs = snap(r.hz);
                smooth(rs);
                if (fskTest(w, f, rs, syms, mid, unit)) {
                    f.symRate = rs; f.symLineDb = r.db; f.symSrc = r.hz == ld.hz ? 2 : 1;
                    if (f.fskLevels == 4) f.mod = ModFsk4;
                    else {
                        const double h = 2 * f.devHz[1] / rs;   // modulation index
                        f.mod = (h > 0.3 && h < 0.7 && f.lineDb[1] > 10) || (h > 0.35 && h < 0.65) ? ModGmsk : ModFsk2;
                    }
                }
            }
            if (f.mod == ModUnknown && f.lineDb[1] > 14) {
                // MSK-like: (x)^2 shows two lines Rs apart; take the rate from the discriminator line if there is one
                f.mod = ModGmsk;
                if (ld.ok && ld.db > 8) { f.symRate = snap(ld.hz); f.symLineDb = ld.db; f.symSrc = 2; }
            }
            if (f.mod == ModUnknown) f.mod = f.carrierDb > 25 && sigW < 400 ? ModCarrier : ModFm;
        }
    }
    if (f.mod == ModUnknown) {
        // linear modulation: the lines of x^2, x^4, x^8 sit at 2, 4, 8 times the carrier's offset D (and Rs apart); pi/4-DQPSK puts its x^4
        // lines Rs/2 beside 4 D, MSK its x^2 lines Rs/2 beside 2 D. Each offset the strongest lines suggest is tried against each class,
        // the simplest that holds wins.
        const double rs = le.ok && le.db > 9 ? snap(le.hz) : 0;
        if (rs > 0) { f.symRate = rs; f.symLineDb = le.db; f.symSrc = 1; }
        const double maxOff = rs > 0 ? std::min(0.15 * sigW, rs / 16) : 0.05 * sigW;   // pi/4-DQPSK is QPSK Rs/8 off: the spectrum says where the centre is
        auto at = [&](int j, double hz) {
            while (hz >= w.fsd / 2) hz -= w.fsd;
            while (hz < -w.fsd / 2) hz += w.fsd;
            const double t = std::max(3 * w.fsd / std::max<size_t>(1, w.P[j].size()), 0.002 * sigW);
            const Line l = findLine(w.P[j], w.fsd, hz - t, hz + t);
            return l.ok ? l.db : 0.0;
        };
        std::vector<double> offs, rates;
        auto addOff = [&](double d) { if (std::fabs(d) < maxOff) offs.push_back(d); };
        addOff(f.lineHz[3] / 8); addOff(f.lineHz[2] / 4); addOff(f.lineHz[1] / 2);
        if (rs > 0) { addOff((f.lineHz[3] - rs) / 8); addOff((f.lineHz[3] + rs) / 8); addOff((f.lineHz[2] - rs / 2) / 4); addOff((f.lineHz[2] + rs / 2) / 4); }
        int bestMod = ModUnknown, bestRank = 99; double bestOff = 0, bestRs = 0;
        for (double d : offs) {
            rates.clear();
            if (rs > 0) rates.push_back(rs);
            for (double g : {2 * std::fabs(f.lineHz[1] - 2 * d), 2 * std::fabs(f.lineHz[2] - 4 * d)})
                if (g > 0.2 * sigW && g < 1.5 * sigW) rates.push_back(g);
            auto consider = [&](int mod, int rank, double r) { if (rank < bestRank) { bestRank = rank; bestMod = mod; bestOff = d; bestRs = r; } };
            // a line holds when it stands out and is about as strong as the strongest of its spectrum
            auto holds = [&](int j, double hz) { const double v = at(j, hz); return v > 18 && v > f.lineDb[j] - 4; };
            if (holds(1, 2 * d) && at(0, d) < at(1, 2 * d) - 6) consider(ModBpsk, 0, 0);
            for (double r : rates) if (holds(1, 2 * d + r / 2) && holds(1, 2 * d - r / 2) && f.envKurt < 1.3) consider(ModGmsk, 1, r);
            if (holds(2, 4 * d)) consider(f.envKurt > 1.5 ? ModQam : ModQpsk, 2, 0);
            for (double r : rates) if (holds(2, 4 * d + r / 2) && holds(2, 4 * d - r / 2) && holds(3, 8 * d)) consider(ModPi4Dqpsk, 3, r);
            if (holds(3, 8 * d)) consider(ModPsk8, 4, 0);
        }
        double off = 0;
        if (bestMod != ModUnknown) {
            f.mod = bestMod; off = bestOff;
            if (bestRs > 0 && (rs <= 0 || std::fabs(bestRs / rs - 1) > 0.05)) { f.symRate = snap(bestRs); f.symSrc = 3; f.symLineDb = 0; }
        } else if (f.carrierDb > 12) f.mod = ModAm;
        else f.mod = f.envKurt > 1.25 ? ModQam : ModFm;
        if (f.mod != ModAm && f.mod != ModFm) f.centreHz += off;   // the carrier's own offset
        if (f.mod == ModGmsk && f.symRate > 0) {   // the bits for the AIS flags
            smooth(f.symRate);
            if (!fskTest(w, f, f.symRate, syms, mid, unit)) syms.clear();
        }
    }
    f.rfHz = o.rfHz + f.centreHz;
    // ---- frame syncs (cheap, where the measurement fits)
    int sync[SyncCount] = {};
    bool tried[SyncCount] = {};
    auto soft = [&]() { std::vector<float> s(syms.size()); for (size_t i = 0; i < syms.size(); i++) s[i] = std::isnan(syms[i]) ? NAN : (float)((syms[i] - mid) / unit); return s; };
    auto bits = [&]() { std::vector<uint8_t> o2(syms.size()); for (size_t i = 0; i < syms.size(); i++) o2[i] = std::isnan(syms[i]) ? 255 : syms[i] > mid ? 1 : 0; return o2; };
    if (f.mod == ModFsk4 && !syms.empty()) {
        const auto s = soft();
        if (std::fabs(f.symRate - 4800) < 50) {
            tried[SyncDmr] = tried[SyncP25] = true;
            for (int k = 0; k < dmr::kSyncCount; k++) {
                int8_t p8[24]; int p[24];
                dmr::syncSymbols(k, p8);
                for (int j = 0; j < 24; j++) p[j] = p8[j];
                sync[SyncDmr] += countSync(s, p, 24, 0.88);
            }
            sync[SyncP25] = countSync(s, p25::kSync, p25::kSyncSym, 0.88);
        }
        if (std::fabs(f.symRate - 4800) < 50 || std::fabs(f.symRate - 2400) < 25) {
            tried[SyncNxdn] = true;
            sync[SyncNxdn] = countSync(s, nxdn::kFsw, nxdn::kFswSym, 0.94, nxdn::kFrameSym);
        }
        if (std::fabs(f.symRate - 2400) < 25) {
            tried[SyncDpmr] = true;
            const int* fs4[3] = {dpmr::kFs1, dpmr::kFs3, dpmr::kFs4};
            for (const int* p : fs4) sync[SyncDpmr] += countSync(s, p, dpmr::kFsLong, 0.88);
            sync[SyncDpmr] += countSync(s, dpmr::kFs2, dpmr::kFsShort, 0.92, 2 * dpmr::kFrameSym);   // frames 0 and 2 of a superframe
        }
    }
    if ((f.mod == ModFsk2 || f.mod == ModGmsk) && !syms.empty()) {
        const auto bt = bits();
        tried[SyncPocsag] = true;
        sync[SyncPocsag] = countWord(bt, pager::kPocsagSync, 32, 2);
        if (std::fabs(f.symRate - 9600) < 100) {
            tried[SyncAis] = true;
            std::vector<uint8_t> nr(bt.size(), 255);   // NRZI: a 1 is no change
            for (size_t i = 1; i < bt.size(); i++) if (bt[i] <= 1 && bt[i - 1] <= 1) nr[i] = bt[i] == bt[i - 1];
            sync[SyncAis] = countWord(nr, 0x007E, 16, 1);   // the training alternates on the line: NRZI zeros, then the flag
        }
    }
    // ---- rank the table
    const double rf = f.rfHz / 1e6;
    for (int i = 0; i < kKnownCount; i++) {
        const Known& k = kKnown[i];
        double sc = 0;
        std::string why;
        if (f.mod == k.mod || f.mod == k.mod2) sc += 35;
        else if (related(f.mod, k.mod)) sc += 12;
        else sc -= 25;
        if (k.rates[0] > 0) {
            if (k.mod == ModOfdm && f.mod == ModOfdm && f.ofdmTuSec > 0) {
                double e = 1e9;
                for (double r : k.rates) if (r > 0) e = std::min(e, std::fabs(f.ofdmTuSec / r - 1));
                sc += e < 0.03 ? 30 : e < 0.08 ? 10 : -20;
            } else if (k.mod != ModOfdm && f.symRate > 0) {
                double e = 1e9;
                for (double r : k.rates) if (r > 0) e = std::min(e, std::fabs(f.symRate / r - 1));
                sc += e < 0.02 ? 25 : e < 0.06 ? 12 : -15;
            }
        }
        if (k.bwHz > 0 && f.obwHz > 0) {
            const double r = f.obwHz / k.bwHz;
            const bool analog = k.rates[0] <= 0 && (k.mod == ModAm || k.mod == ModFm || k.mod == ModCarrier);   // the audio sets the width: a limit
            if (analog) sc += r < 1.5 ? 10 : -10;
            else sc += r > 0.6 && r < 1.5 ? 10 : r > 0.35 && r < 2.5 ? 3 : -10;
        }
        if (f.mod == k.mod && (k.mod == ModChirp || (k.mod == ModFm && f.fmPilot))) sc += 20;   // nothing else looks like these
        if (k.devOuter > 0 && f.fskLevels) {
            const double outer = std::max(std::fabs(f.devHz[0]), std::fabs(f.devHz[f.fskLevels - 1]));
            const double e = std::fabs(outer / k.devOuter - 1);
            sc += e < 0.15 ? 12 : e < 0.3 ? 5 : -8;
        }
        if (k.duty == 1 && f.duty < 0.7) sc -= 5;
        if (k.duty == 2 && f.duty > 0.97) sc -= 3;
        if (k.bands[0] > 0 && o.rfHz > 0) {
            bool in = false;
            for (int j = 0; j + 1 < 10 && k.bands[j] > 0; j += 2) if (rf >= k.bands[j] && rf <= k.bands[j + 1]) in = true;
            sc += in ? 12 : -6;
        }
        Candidate c;
        c.name = k.name; c.modeId = k.modeId; c.subId = k.subId; c.why = k.note;
        if (k.sync && tried[k.sync]) {
            c.syncTried = true; c.syncHits = sync[k.sync];
            sc += c.syncHits >= 2 ? 40 : c.syncHits == 1 ? 15 : -10;
        }
        c.score = std::max(0.0, std::min(100.0, sc));
        if (c.score >= 20) R.cands.push_back(c);
    }
    std::stable_sort(R.cands.begin(), R.cands.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    if (R.cands.size() > 6) R.cands.resize(6);
    // ---- the summary line
    std::string s = modName(f.mod);
    if (f.mod == ModOfdm && f.ofdmTuSec > 0) {
        char t[96]; snprintf(t, sizeof t, ", Tu %.4g us", f.ofdmTuSec * 1e6); s += t;
        if (f.ofdmTgSec > 0) { snprintf(t, sizeof t, ", guard 1/%.0f", f.ofdmTuSec / f.ofdmTgSec); s += t; }
    } else if (f.mod == ModChirp) {
        char t[64]; snprintf(t, sizeof t, ", SF%d, %s", f.loraSf, hzText(f.chirpBwHz).c_str()); s += t;
    } else if (f.symRate > 0) s += ", " + rateText(f.symRate);
    if (f.fskLevels == 4) { char t[64]; snprintf(t, sizeof t, ", +-%.0f / +-%.0f Hz", (f.devHz[2] - f.devHz[1]) / 2, (f.devHz[3] - f.devHz[0]) / 2); s += t; }
    else if (f.fskLevels == 2) { char t[48]; snprintf(t, sizeof t, ", +-%.0f Hz", (f.devHz[1] - f.devHz[0]) / 2); s += t; }
    if (f.fmPilot) s += ", stereo pilot";
    s += ", " + hzText(f.obwHz) + " wide";
    if (f.duty < 0.97) { char t[64]; snprintf(t, sizeof t, ", bursts (%.0f %% on, %.3g ms)", f.duty * 100, f.burstMs); s += t; }
    else s += ", continuous";
    R.summary = s;
    f.valid = true;
    return R;
}

// ---------------------------------------------------------------------------------------------------- Capture

struct Capture::Impl {
    mutable std::mutex mu;
    std::condition_variable cv;
    std::thread th;
    bool stop = false, work = false;
    std::vector<cf32> buf;
    size_t want = 0;
    double rate = 0, cOff = 0, width = 0, rf = 0;
    int state = 0;
    Result res;
    uint64_t seq = 0;
    uint64_t gen = 0;              // start() count: a result of an older start is thrown away
};

Capture::Capture() : p_(new Impl) {
    p_->th = std::thread([this] {
        Impl& m = *p_;
        std::unique_lock<std::mutex> lk(m.mu);
        for (;;) {
            m.cv.wait(lk, [&] { return m.stop || m.work; });
            if (m.stop) return;
            m.work = false;
            std::vector<cf32> x;
            x.swap(m.buf);
            const double rate = m.rate, c = m.cOff, wd = m.width, rf = m.rf;
            const uint64_t g = m.gen;
            lk.unlock();
            Options o;
            o.rfHz = rf;
            if (wd > 0) { o.loHz = c - wd / 2; o.hiHz = c + wd / 2; }
            Result r = identify(x.data(), x.size(), rate, o);
            x.clear(); x.shrink_to_fit();
            lk.lock();
            if (g != m.gen) continue;   // restarted meanwhile
            m.res = std::move(r);
            m.seq++;
            m.state = 3;
        }
    });
}

Capture::~Capture() {
    { std::lock_guard<std::mutex> lk(p_->mu); p_->stop = true; }
    p_->cv.notify_all();
    if (p_->th.joinable()) p_->th.join();
}

void Capture::start(double rate, double centreOffHz, double widthHz, double rfHz, double seconds) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->gen++;
    p_->rate = rate; p_->cOff = centreOffHz; p_->width = widthHz; p_->rf = rfHz;
    p_->want = (size_t)std::min(rate * seconds, 4.0 * (1 << 20));
    p_->buf.clear();
    p_->buf.reserve(p_->want);
    p_->state = rate > 0 ? 1 : 0;
    armed_ = rate > 0;
}

void Capture::cancel() {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->gen++;
    armed_ = false;
    p_->buf.clear(); p_->buf.shrink_to_fit();
    p_->state = 0;
}

void Capture::push(const cf32* x, size_t n) {
    if (!armed()) return;
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->state != 1) return;
    const size_t k = std::min(n, p_->want - p_->buf.size());
    p_->buf.insert(p_->buf.end(), x, x + k);
    if (p_->buf.size() >= p_->want) { armed_ = false; p_->state = 2; p_->work = true; p_->cv.notify_one(); }
}

int Capture::state() const { std::lock_guard<std::mutex> lk(p_->mu); return p_->state; }
double Capture::progress() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return p_->state == 1 && p_->want ? (double)p_->buf.size() / p_->want : p_->state >= 2 ? 1.0 : 0.0;
}
bool Capture::result(Result& out, uint64_t& seq) const {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (p_->seq == seq || p_->state != 3) return false;
    out = p_->res; seq = p_->seq;
    return true;
}
double Capture::regionCentre() const { std::lock_guard<std::mutex> lk(p_->mu); return p_->cOff; }

} // namespace sigid
} // namespace dect2
