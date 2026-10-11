// Cell towers, LTE receiver: cell search (PSS / SSS), the MIB on the PBCH, and the system information on SI-RNTI (PCFICH, the common
// search space of the PDCCH, PDSCH). 3GPP TS 36.211 / 36.212 / 36.213. A DCI is accepted only when its CRC matches SI-RNTI: paging
// (P-RNTI), random access and every UE-specific channel are never decoded.
#include "cell_lte_int.h"
#include "dect2/cell_dsp.h"
#include "dect2/fftutil.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace dect2::cell {

namespace {

constexpr double kSyncRate = 1.92e6;   // the central 6 resource blocks, FFT 128
constexpr int kHalf = 9600;            // a half frame at that rate
constexpr int kNfft6 = 128;

const std::array<std::vector<cf32>, 3>& pssTime() {
    static const std::array<std::vector<cf32>, 3> T = [] {
        std::array<std::vector<cf32>, 3> r;
        Fft f(kNfft6);
        for (int n2 = 0; n2 < 3; n2++) {
            cf32 d[62];
            ltePss(n2, d);
            std::vector<cf32> b((size_t)kNfft6, cf32(0, 0));
            for (int n = 0; n < 62; n++) b[(size_t)lteBin(n + 5, 6, kNfft6)] = d[n];
            f.inverse(b.data());
            float e = 0;
            for (auto& v : b) e += std::norm(v);
            for (auto& v : b) v /= std::sqrt(e);
            r[(size_t)n2] = b;
        }
        return r;
    }();
    return T;
}

// SSS sequences: [nid1][subframe 0 / 5][62]
const std::vector<float>& sssTable() {
    static const std::vector<float> T = [] {
        std::vector<float> t((size_t)168 * 2 * 3 * 62);
        for (int n2 = 0; n2 < 3; n2++)
            for (int n1 = 0; n1 < 168; n1++)
                for (int w = 0; w < 2; w++) lteSss(n1, n2, w ? 5 : 0, &t[(((size_t)n2 * 168 + (size_t)n1) * 2 + (size_t)w) * 62]);
        return t;
    }();
    return T;
}
inline const float* sssSeq(int n2, int n1, int w) { return &sssTable()[(((size_t)n2 * 168 + (size_t)n1) * 2 + (size_t)w) * 62]; }

// cross-correlation c[t] = sum conj(tpl[i]) y[t + i], t = 0 .. n - L, by FFT blocks
void xcorr(const cf32* y, size_t n, const std::vector<cf32>& tpl, std::vector<cf32>& c) {
    const int L = (int)tpl.size(), N = 4096, step = N - L;
    c.assign(n >= (size_t)L ? n - (size_t)L + 1 : 0, cf32(0, 0));
    if (c.empty()) return;
    static thread_local std::unique_ptr<Fft> f;
    if (!f) f.reset(new Fft(N));
    std::vector<cf32> T((size_t)N, cf32(0, 0)), B((size_t)N);
    for (int i = 0; i < L; i++) T[(size_t)i] = tpl[(size_t)i];
    f->forward(T.data());
    for (auto& v : T) v = std::conj(v) / (float)N;
    for (size_t s = 0; s < c.size(); s += (size_t)step) {
        for (int i = 0; i < N; i++) B[(size_t)i] = (s + (size_t)i < n) ? y[s + (size_t)i] : cf32(0, 0);
        f->forward(B.data());
        for (int i = 0; i < N; i++) B[(size_t)i] *= T[(size_t)i];
        f->inverse(B.data());
        for (int t = 0; t <= step && s + (size_t)t < c.size(); t++) c[s + (size_t)t] = B[(size_t)t];
    }
}

cf32 dotConj(const cf32* y, const cf32* tpl, int L) {
    cf32 a(0, 0);
    for (int i = 0; i < L; i++) a += std::conj(tpl[i]) * y[i];
    return a;
}

std::vector<cf32> withCfo(const std::vector<cf32>& t, double hz, double fs) {
    std::vector<cf32> o(t.size());
    for (size_t i = 0; i < t.size(); i++) { const double a = 2 * M_PI * hz * (double)i / fs; o[i] = t[i] * cf32((float)std::cos(a), (float)std::sin(a)); }
    return o;
}

struct Cand { int nid2 = 0; double cfo = 0; double t0 = 0; float db = 0; };

// PSS peaks: the folded correlation energy over the first half frames, for each NID2 and carrier offset hypothesis
std::vector<Cand> pssSearch(const std::vector<cf32>& y, double maxCfo, float minDb) {
    std::vector<Cand> out;
    const size_t nUse = std::min(y.size(), (size_t)(8 * kHalf + kNfft6));
    if (nUse < (size_t)(kHalf + kNfft6)) return out;
    const double step = 7500;
    const int nh = std::max(0, (int)std::ceil(maxCfo / step - 0.34));
    std::vector<cf32> c;
    for (int n2 = 0; n2 < 3; n2++) {
        Cand best; float bestDb = -1;
        for (int h = -nh; h <= nh; h++) {
            const double hz = h * step;
            xcorr(y.data(), nUse, withCfo(pssTime()[(size_t)n2], hz, kSyncRate), c);
            std::vector<float> fold((size_t)kHalf, 0.f);
            for (size_t t = 0; t < c.size(); t++) fold[t % kHalf] += std::norm(c[t]);
            double mean = 0;
            for (float v : fold) mean += v;
            mean /= kHalf;
            const int pk = (int)(std::max_element(fold.begin(), fold.end()) - fold.begin());
            const float db = (float)(10 * std::log10(fold[(size_t)pk] / std::max(1e-30, mean)));
            if (db > bestDb) { bestDb = db; best.nid2 = n2; best.cfo = hz; best.t0 = pk; best.db = db; }
        }
        if (bestDb >= minDb) out.push_back(best);
    }
    std::sort(out.begin(), out.end(), [](const Cand& a, const Cand& b) { return a.db > b.db; });
    return out;
}

struct Track {
    std::vector<double> t;          // PSS position per half frame (fit), sync samples
    double t0 = 0, slope = kHalf;   // t(m) = t0 + slope * m
    double cfo = 0;
    int nHalf = 0;
};

// follow the PSS through the block: timing per half frame, clock (slope) and fine carrier offset
bool pssTrack(const std::vector<cf32>& y, const Cand& cd, Track& tr) {
    const auto& P = pssTime()[(size_t)cd.nid2];
    std::vector<cf32> tpl = withCfo(P, cd.cfo, kSyncRate);
    std::vector<double> ms, ts;
    std::vector<cf32> cs;
    cf32 split(0, 0);
    double pred = cd.t0, slope = kHalf;
    for (int m = 0;; m++) {
        const int c0 = (int)std::lround(pred);
        if (c0 - 12 < 0) { pred += slope; continue; }
        if ((size_t)(c0 + 12 + kNfft6) >= y.size()) break;
        int bi = 0; float bv = -1;
        float v[25];
        for (int d = -12; d <= 12; d++) { v[d + 12] = std::norm(dotConj(&y[(size_t)(c0 + d)], tpl.data(), kNfft6)); if (v[d + 12] > bv) { bv = v[d + 12]; bi = d; } }
        double frac = 0;
        if (bi > -12 && bi < 12) { const float a = v[bi + 11], b = v[bi + 12], cc = v[bi + 13]; const float den = a - 2 * b + cc; if (den < 0) frac = 0.5 * (a - cc) / den; }
        const int at = c0 + bi;
        const cf32 c1 = dotConj(&y[(size_t)at], tpl.data(), 64), c2 = dotConj(&y[(size_t)at + 64], tpl.data() + 64, 64);
        split += c2 * std::conj(c1);
        ms.push_back(m); ts.push_back(at + frac); cs.push_back(c1 + c2);
        // the expected place of the next one, from the fit so far
        if (ms.size() >= 2) {
            double sm = 0, st = 0, smm = 0, smt = 0; const double k = (double)ms.size();
            for (size_t i = 0; i < ms.size(); i++) { sm += ms[i]; st += ts[i]; smm += ms[i] * ms[i]; smt += ms[i] * ts[i]; }
            const double den = k * smm - sm * sm;
            if (den > 0) { slope = (k * smt - sm * st) / den; slope = std::max(kHalf * (1 - 1e-4), std::min(kHalf * (1 + 1e-4), slope)); }
            pred = ts.back() + slope;
        } else pred = at + frac + slope;
    }
    if (ms.size() < 2) return false;
    double sm = 0, st = 0, smm = 0, smt = 0; const double k = (double)ms.size();
    for (size_t i = 0; i < ms.size(); i++) { sm += ms[i]; st += ts[i]; smm += ms[i] * ms[i]; smt += ms[i] * ts[i]; }
    const double den = k * smm - sm * sm;
    tr.slope = den > 0 ? (k * smt - sm * st) / den : kHalf;
    if (std::fabs(tr.slope / kHalf - 1) > 1e-4) tr.slope = kHalf;
    tr.t0 = (st - tr.slope * sm) / k;
    tr.nHalf = (int)ms.back() + 1;
    tr.t.resize((size_t)tr.nHalf);
    for (int m = 0; m < tr.nHalf; m++) tr.t[(size_t)m] = tr.t0 + tr.slope * m;
    // fine offset: the phase across the two halves of each PSS, then between PSS 5 ms apart (unambiguous to +-100 Hz)
    double cfo = cd.cfo + std::arg(split) / (2 * M_PI * 64 / kSyncRate);
    std::vector<cf32> t2 = withCfo(P, cfo, kSyncRate);
    cf32 acc(0, 0), prev(0, 0);
    for (int m = 0; m < tr.nHalf; m++) {
        const long at = std::lround(tr.t[(size_t)m]);
        if (at < 0 || (size_t)(at + kNfft6) > y.size()) { prev = cf32(0, 0); continue; }
        const cf32 c = dotConj(&y[(size_t)at], t2.data(), kNfft6);
        acc += c * std::conj(prev);
        prev = c;
    }
    const double dt = tr.slope / kSyncRate;
    if (std::abs(acc) > 0) cfo += std::arg(acc) / (2 * M_PI * dt);
    tr.cfo = cfo;
    return true;
}

struct SssResult { bool ok = false; int nid1 = 0; bool ext = false, tdd = false; int sf0Parity = 0; float score = 0; };

void fft128(const cf32* x, std::vector<cf32>& out) {
    static thread_local std::unique_ptr<Fft> f;
    if (!f) f.reset(new Fft(kNfft6));
    out.assign(x, x + kNfft6);
    f->forward(out.data());
}

SssResult sssDetect(const std::vector<cf32>& ys, int nid2, const Track& tr) {
    struct Hyp { bool ext, tdd; int off; };
    const Hyp hyps[4] = {{false, false, 128 + 9}, {true, false, 128 + 32}, {false, true, 412}, {true, true, 480}};
    cf32 pss[62];
    ltePss(nid2, pss);
    SssResult best;
    std::vector<cf32> P, S;
    for (const Hyp& h : hyps) {
        std::vector<double> acc((size_t)168 * 2, 0.0);
        double norm = 0;
        int used = 0;
        for (int m = 0; m < tr.nHalf; m++) {
            const long p = std::lround(tr.t[(size_t)m]), s = p - h.off;
            if (s < 0 || (size_t)(p + kNfft6) > ys.size()) continue;
            fft128(&ys[(size_t)p], P);
            fft128(&ys[(size_t)s], S);
            float z[62];
            double e = 0;
            for (int n = 0; n < 62; n++) {
                const int b = lteBin(n + 5, 6, kNfft6);
                const cf32 H = P[(size_t)b] * std::conj(pss[n]);
                z[n] = (S[(size_t)b] * std::conj(H)).real();
                e += (double)z[n] * z[n];
            }
            norm += std::sqrt(e * 62);
            used++;
            for (int n1 = 0; n1 < 168; n1++) {
                const float* d0 = sssSeq(nid2, n1, 0);
                const float* d5 = sssSeq(nid2, n1, 1);
                double c0 = 0, c5 = 0;
                for (int n = 0; n < 62; n++) { c0 += z[n] * d0[n]; c5 += z[n] * d5[n]; }
                if (m % 2 == 0) { acc[(size_t)n1 * 2] += c0; acc[(size_t)n1 * 2 + 1] += c5; }
                else { acc[(size_t)n1 * 2] += c5; acc[(size_t)n1 * 2 + 1] += c0; }
            }
        }
        if (!used || norm <= 0) continue;
        for (int i = 0; i < 168 * 2; i++) {
            const float sc = (float)(acc[(size_t)i] / norm);
            if (sc > best.score) { best.score = sc; best.nid1 = i / 2; best.sf0Parity = i % 2; best.ext = h.ext; best.tdd = h.tdd; }
        }
    }
    best.ok = best.score > 0.25f;
    return best;
}

std::vector<std::pair<int, int>> pbchRes(int pci, bool ext) {
    const int N = lteSymbPerSlot(ext);
    std::vector<std::pair<int, int>> res;
    for (int l = N; l < N + 4; l++)
        for (int kk = 0; kk < 72; kk++) {
            const bool rs = (l - N <= 1) || (ext && l - N == 3);
            if (rs && (kk % 3) == (pci % 6) % 3) continue;
            res.push_back({l, kk});
        }
    return res;
}

// gather the received values and the per-port channel of a list of REs, and undo transmit diversity
void txdRes(const LteGrid& y, const LteChan& ch, const std::vector<std::pair<int, int>>& res, int ports, std::vector<float>& llr) {
    std::vector<cf32> r(res.size());
    std::vector<std::vector<cf32>> h((size_t)ports, std::vector<cf32>(res.size()));
    for (size_t i = 0; i < res.size(); i++) {
        r[i] = y.at(res[i].first, res[i].second);
        for (int p = 0; p < ports; p++) h[(size_t)p][i] = ch.h[(size_t)p].at(res[i].first, res[i].second);
    }
    std::vector<const cf32*> hp;
    for (auto& v : h) hp.push_back(v.data());
    llr.clear();
    lteTxdDecode(r.data(), hp, r.size(), ports, ch.noiseVar, llr);
}

uint32_t mibMask(int ports) { return ports == 1 ? 0u : ports == 2 ? 0xFFFFu : 0x5555u; }

// soft agreement of a decoded word, re-encoded, with the received values (1 = all agree, about 0 = noise)
float agreement(const float* e, const Bits& enc) {
    double s = 0, a = 0;
    for (size_t i = 0; i < enc.size(); i++) { s += e[i] * (enc[i] ? -1.0 : 1.0); a += std::fabs(e[i]); }
    return a > 0 ? (float)(s / a) : 0.f;
}

struct MibTry { bool ok = false; LteMibInfo mib; int frame = 0; };

MibTry mibDecode(const std::vector<std::array<std::vector<float>, 3>>& llr, int pci, bool ext) {
    MibTry out;
    const int M = ltePbchBits(ext), per = M / 4;
    const Bits cs = gold((uint32_t)pci, (size_t)M);
    static const int portsOf[3] = {1, 2, 4};
    const int nf = (int)llr.size();
    std::vector<float> e((size_t)M);
    // every 40 ms period (4 frames, the frame of segment j0 first) and its single frames; a 16-bit CRC alone passes noise now and then, so
    // a MIB counts when two decodes from different frames agree (the same set-up, the SFN advancing with the frames)
    std::vector<MibTry> got;
    std::vector<std::pair<int, int>> used;   // frames of each decode
    for (int span = 1; span <= 4; span++)
        for (int f0 = 0; f0 < nf; f0++)
            for (int j0 = 0; j0 + span <= 4; j0++) {
                if (f0 + span > nf) continue;
                for (int pi = 0; pi < 3; pi++) {
                    bool have = true;
                    for (int q = 0; q < span; q++) if (llr[(size_t)(f0 + q)][(size_t)pi].size() < (size_t)per) have = false;
                    if (!have) continue;
                    std::fill(e.begin(), e.end(), 0.f);
                    for (int q = 0; q < span; q++) {
                        const int seg = j0 + q;
                        const auto& L = llr[(size_t)(f0 + q)][(size_t)pi];
                        for (int i = 0; i < per; i++) e[(size_t)(seg * per + i)] = cs[(size_t)(seg * per + i)] ? -L[(size_t)i] : L[(size_t)i];
                    }
                    const std::vector<float> d = convRateRecover(e.data(), (size_t)M, 40);
                    const Bits b = viterbiTb(d.data(), 40);
                    uint32_t par = 0;
                    for (int i = 24; i < 40; i++) par = (par << 1) | b[(size_t)i];
                    if ((crc(b.data(), 24, kCrc16, 16) ^ par) != mibMask(portsOf[pi])) continue;
                    const Bits enc = convRateMatch(convEncode(b), (size_t)M);
                    double s = 0, a = 0;
                    for (int q = 0; q < span; q++) for (int i = 0; i < per; i++) { const size_t k = (size_t)((j0 + q) * per + i); s += e[k] * (enc[k] ? -1.0 : 1.0); a += std::fabs(e[k]); }
                    if (a <= 0 || s / a < 0.2) continue;
                    int v = 0;
                    for (int i = 0; i < 3; i++) v = (v << 1) | b[(size_t)i];
                    if (v > 5) continue;
                    MibTry t;
                    t.ok = true;
                    t.mib.ok = true;
                    t.mib.nRb = lteBwIndexToRb(v);
                    t.mib.phichDur = b[3];
                    t.mib.phichRes = (b[4] << 1) | b[5];
                    int s8 = 0;
                    for (int i = 6; i < 14; i++) s8 = (s8 << 1) | b[(size_t)i];
                    t.mib.sfn = (s8 * 4 + j0) & 1023;
                    t.mib.ports = portsOf[pi];
                    t.frame = f0;
                    for (size_t k = 0; k < got.size(); k++) {
                        const MibTry& o = got[k];
                        const bool apart = f0 >= used[k].second || f0 + span <= used[k].first;
                        if (apart && o.mib.nRb == t.mib.nRb && o.mib.ports == t.mib.ports && o.mib.phichDur == t.mib.phichDur && o.mib.phichRes == t.mib.phichRes &&
                            ((o.mib.sfn - o.frame - (t.mib.sfn - t.frame)) & 1023) == 0)
                            return o;
                    }
                    got.push_back(t);
                    used.push_back({f0, f0 + span});
                }
            }
    return out;
}

struct DciFound { LteDciSi d; Bits payload; };

} // namespace

std::vector<LteCellResult> lteSearchDecode(const cf32* x, size_t n, double fs, double offsetHz, const LteSearchOpts& o) {
    std::vector<LteCellResult> out;
    if (!x || n == 0 || fs < kSyncRate * 0.99) return out;
    const double toSync = kSyncRate / fs;
    const size_t nSync = (size_t)((double)n * toSync);
    if (nSync < (size_t)(3 * kHalf)) return out;
    const std::vector<cf32> y = resampleBlock(x, n, fs, kSyncRate, offsetHz, 1.3e6, 0, nSync);
    std::vector<Cand> cands = pssSearch(y, o.maxCfoHz, o.minPssDb);
    static const bool dbg = getenv("CELL_DEBUG") != nullptr;
    if (dbg) for (const auto& c : cands) fprintf(stderr, "[cell] off %.0f: PSS nid2 %d cfo %.0f t %.0f %.1f dB\n", offsetHz, c.nid2, c.cfo, c.t0, c.db);
    for (const Cand& cd : cands) {
        if ((int)out.size() >= o.maxCells) break;
        Track tr;
        if (!pssTrack(y, cd, tr)) continue;
        std::vector<cf32> ys = y;
        mixDown(ys.data(), ys.size(), tr.cfo, kSyncRate);
        const SssResult ss = sssDetect(ys, cd.nid2, tr);
        if (dbg) fprintf(stderr, "[cell]   track cfo %.0f slope %.3f halves %d; SSS nid1 %d score %.2f ext %d tdd %d\n", tr.cfo, tr.slope, tr.nHalf, ss.nid1, ss.score, ss.ext, ss.tdd);
        if (!ss.ok) continue;
        const int pci = 3 * ss.nid1 + cd.nid2;
        bool dup = false;
        for (const auto& r : out) if (r.found.pci == pci) dup = true;
        if (dup) continue;
        // the first frame start: the PSS of subframe 0 (FDD: end of slot 0; TDD: symbol 2 of subframe 1)
        const double frameOff = !ss.tdd ? 832.0 : ss.ext ? 2272.0 : 2194.0;
        int m0 = ss.sf0Parity;   // the half frames of subframe 0 have this parity
        double fs0 = tr.t0 + tr.slope * m0 - frameOff;
        const double frameLen = 2 * tr.slope;
        while (fs0 - frameLen >= 0) fs0 -= frameLen;
        while (fs0 < 0) fs0 += frameLen;
        LteCellResult R;
        R.found.pci = pci;
        R.found.extCp = ss.ext;
        R.found.tdd = ss.tdd;
        R.found.cfoHz = tr.cfo;
        R.found.clockPpm = (tr.slope / kHalf - 1) * 1e6;
        R.found.pssDb = cd.db;
        R.found.frameStart = fs0 / toSync;
        // PBCH, from the central 6 resource blocks
        LteCellCfg c6;
        c6.pci = pci; c6.extCp = ss.ext; c6.tdd = ss.tdd; c6.nRb = 6; c6.ports = 4;
        const auto pres = pbchRes(pci, ss.ext);
        std::vector<std::array<std::vector<float>, 3>> llr;
        double rsrp6 = 0, rssi6 = 0, nv6 = 0; int nm6 = 0;
        LteGrid g;
        LteChan ch;
        for (int pass = 0; pass < 2; pass++) {
            cf32 rot(0, 0);
            for (int f = 0;; f++) {
                const long s = std::lround(fs0 + f * frameLen);
                if ((size_t)(s + lteSubframeLen(kNfft6) + 8) > ys.size()) break;
                lteOfdmDemod(&ys[(size_t)s], 6, ss.ext, kNfft6, g);
                if (pass == 0) {   // the residual carrier offset: port 0's reference signals of symbol 0 of both slots, 0.5 ms apart (+-1 kHz)
                    cf32 r0[2 * kMaxRb], r7[2 * kMaxRb];
                    const int N = lteSymbPerSlot(ss.ext);
                    lteCrsSeq(pci, 0, 0, ss.ext, r0);
                    lteCrsSeq(pci, 1, 0, ss.ext, r7);
                    for (int k = pci % 6; k < 72; k += 6) {
                        const int mm = k / 6 + kMaxRb - 6;
                        rot += (g.at(N, k) * std::conj(r7[mm])) * std::conj(g.at(0, k) * std::conj(r0[mm]));
                    }
                    continue;
                }
                lteChanEst(g, c6, 0, 6, 0, 4, ch);
                rsrp6 += ch.rsrp; rssi6 += ch.rssi; nv6 += ch.noiseVar; nm6++;
                std::array<std::vector<float>, 3> L;
                static const int portsOf[3] = {1, 2, 4};
                for (int pi = 0; pi < 3; pi++) txdRes(g, ch, pres, portsOf[pi], L[(size_t)pi]);
                llr.push_back(std::move(L));
            }
            if (pass == 0 && std::abs(rot) > 0) {
                const double d = std::arg(rot) / (2 * M_PI * 0.5e-3);
                mixDown(ys.data(), ys.size(), d, kSyncRate);
                tr.cfo += d;
                R.found.cfoHz = tr.cfo;
            }
        }
        if (nm6) {
            R.meas.rsrpDb = (float)(10 * std::log10(std::max(1e-20, rsrp6 / nm6)));
            R.meas.rssiDb = (float)(10 * std::log10(std::max(1e-20, rssi6 / nm6)));
            R.meas.rsrqDb = (float)(10 * std::log10(std::max(1e-20, 6 * rsrp6 / std::max(1e-20, rssi6))));
            R.meas.sinrDb = (float)(10 * std::log10(std::max(1e-20, rsrp6 / std::max(1e-20, nv6))));
        }
        const MibTry mt = mibDecode(llr, pci, ss.ext);
        if (dbg) fprintf(stderr, "[cell]   PCI %d MIB %d: %d RB %d ports sfn %d, RSRP %.1f SINR %.1f\n", pci, mt.ok, mt.mib.nRb, mt.mib.ports, mt.mib.sfn, R.meas.rsrpDb, R.meas.sinrDb);
        if (!mt.ok) continue;
        R.mib = mt.mib;
        R.mib.frameStart = R.found.frameStart;
        const int sfnAt0 = (mt.mib.sfn - mt.frame + 1024 * 4) & 1023;   // SFN of the first whole frame in the block
        R.mib.sfn = sfnAt0;
        // system information: needs the whole carrier inside the block
        const int nRb = mt.mib.nRb;
        const double halfBw = nRb * 90e3 + 30e3;
        if (o.wantSi && !ss.tdd && std::fabs(offsetHz) + halfBw < fs / 2) {
            LteCellCfg c;
            c.pci = pci; c.extCp = ss.ext; c.tdd = false; c.nRb = nRb; c.ports = mt.mib.ports; c.phichDur = mt.mib.phichDur; c.phichRes = mt.mib.phichRes;
            const int nfft = lteNfft(nRb);
            const double rate = lteRate(nfft), r = tr.slope / kHalf;
            const double start = R.found.frameStart;
            const double frameIn = 0.01 * fs * r;
            const int nFrames = (int)std::floor(((double)n - start - 64) / frameIn);
            if (nFrames >= 1) {
                const size_t sfLen = (size_t)lteSubframeLen(nfft);
                const std::vector<cf32> z = resampleBlock(x, n, fs, rate, offsetHz + tr.cfo, 2 * halfBw, start, (size_t)nFrames * 10 * sfLen + 16, r);
                double rsrp = 0, rssi = 0, nv = 0; int nm = 0;
                std::vector<Bits> seen;
                for (int fr = 0; fr < nFrames; fr++)
                    for (int sf = 0; sf < 10; sf++) {
                        const int sfn = (sfnAt0 + fr) & 1023;
                        lteOfdmDemod(&z[((size_t)fr * 10 + (size_t)sf) * sfLen], nRb, ss.ext, nfft, g);
                        lteChanEst(g, c, sf, nRb, 0, c.ports, ch);
                        rsrp += ch.rsrp; rssi += ch.rssi; nv += ch.noiseVar; nm++;
                        // PCFICH
                        const LteControl c1 = lteControl(c, 1, sf);
                        std::vector<std::pair<int, int>> res;
                        for (const auto& rg : c1.pcfich) for (int i = 0; i < 4; i++) res.push_back({rg.l, rg.k[i]});
                        std::vector<float> L;
                        txdRes(g, ch, res, c.ports, L);
                        if (L.size() < 32) continue;
                        const Bits pcs = gold((uint32_t)((sf + 1) * (2 * pci + 1) * 512 + pci), 32);
                        int cfi = 1; float bc = -1e30f;
                        for (int k = 0; k < 3; k++) {
                            float s = 0;
                            for (int i = 0; i < 32; i++) s += (pcs[(size_t)i] ? -L[(size_t)i] : L[(size_t)i]) * (kCfiCode[k][i] ? -1.f : 1.f);
                            if (s > bc) { bc = s; cfi = k + 1; }
                        }
                        R.cfiSeen = cfi;
                        // PDCCH: the quadruplets in their order, descrambled
                        const LteControl ctl = lteControl(c, cfi, sf);
                        const int Mq = (int)ctl.pdcchQuad.size();
                        std::vector<float> pl;
                        pl.reserve((size_t)Mq * 8);
                        for (int q = 0; q < Mq; q++) {
                            const LteReg& rg = ctl.pdcchQuad[(size_t)q];
                            std::vector<std::pair<int, int>> rq;
                            for (int i = 0; i < 4; i++) rq.push_back({rg.l, rg.k[i]});
                            std::vector<float> lq;
                            txdRes(g, ch, rq, c.ports, lq);
                            lq.resize(8, 0.f);
                            pl.insert(pl.end(), lq.begin(), lq.end());
                        }
                        const Bits dcs = gold((uint32_t)(sf * 512 + pci), pl.size());
                        for (size_t i = 0; i < pl.size(); i++) if (dcs[i]) pl[i] = -pl[i];
                        // the common search space: aggregation 4 (4 candidates) and 8 (2)
                        std::vector<DciFound> dcis;
                        for (int L2 : {4, 8}) {
                            const int nPos = ctl.nCce / L2;
                            if (nPos <= 0) continue;
                            const int nc = L2 == 4 ? 4 : 2;
                            for (int m = 0; m < nc; m++) {
                                const int cce = L2 * (m % nPos);
                                const size_t E = (size_t)(72 * L2), off = (size_t)(72 * cce);
                                if (off + E > pl.size()) continue;
                                for (int fmt = 0; fmt < 2; fmt++) {
                                    const int sz = fmt == 0 ? lteDci1ASize(nRb) : lteDci1CSize(nRb);
                                    const size_t K = (size_t)(sz + 16);
                                    const std::vector<float> d = convRateRecover(&pl[off], E, K);
                                    const Bits b = viterbiTb(d.data(), K);
                                    uint32_t par = 0;
                                    for (size_t i = (size_t)sz; i < K; i++) par = (par << 1) | b[i];
                                    if ((crc(b.data(), (size_t)sz, kCrc16, 16) ^ par) != kSiRnti) continue;
                                    if (agreement(&pl[off], convRateMatch(convEncode(b), E)) < 0.3f) continue;
                                    Bits p(b.begin(), b.begin() + sz);
                                    bool again = false;
                                    for (const auto& x2 : dcis) if (x2.payload == p) again = true;
                                    if (again) continue;
                                    DciFound df;
                                    if (!lteDciParse(p, nRb, fmt, df.d)) { R.dciFail++; continue; }
                                    df.payload = p;
                                    dcis.push_back(df);
                                }
                            }
                        }
                        // PDSCH of each SI-RNTI allocation
                        for (const auto& df : dcis) {
                            R.dciSeen++;
                            std::vector<int> p0, p1;
                            ltePrbs(df.d, nRb, p0, p1);
                            bool okPrb = !p0.empty();
                            for (int v : p0) if (v < 0 || v >= nRb) okPrb = false;
                            for (int v : p1) if (v < 0 || v >= nRb) okPrb = false;
                            if (!okPrb) { R.dciFail++; continue; }
                            const auto pres2 = ltePdschRes(c, sf, cfi, p0, p1);
                            std::vector<float> e;
                            txdRes(g, ch, pres2, c.ports, e);
                            const int NL = c.ports > 1 ? 2 : 1;
                            const size_t G = 2 * pres2.size(), E = (size_t)(NL * 2) * (G / (size_t)(NL * 2));
                            if (e.size() < E || E == 0) continue;
                            e.resize(E);
                            const Bits scs = gold(((uint32_t)kSiRnti << 14) + ((uint32_t)sf << 9) + (uint32_t)pci, E);
                            for (size_t i = 0; i < E; i++) if (scs[i]) e[i] = -e[i];
                            const int B = df.d.tbs + 24, K = turboSizeFor(B), F = K - B;
                            if (K == 0) continue;
                            std::vector<int> rvs;
                            if (df.d.format == 0) rvs = {df.d.rv};
                            else rvs = {0, 2, 3, 1};
                            bool got = false;
                            for (int rv : rvs) {
                                std::vector<float> d012;
                                turboRateRecover(e.data(), E, K, F, rv, d012);
                                Bits tb;
                                struct Ck { int F; } ck{F};
                                auto chk = [](const Bits& b, void* p) {
                                    const int f = static_cast<Ck*>(p)->F;
                                    Bits t(b.begin() + f, b.end());
                                    return checkCrc(t, kCrc24A, 24);
                                };
                                if (!turboDecode(d012, K, F, 8, tb, chk, &ck)) continue;
                                Bits msg(tb.begin() + F, tb.begin() + F + df.d.tbs);
                                got = true;
                                bool again = false;
                                for (const auto& s2 : seen) if (s2 == msg) again = true;
                                if (!again) {
                                    seen.push_back(msg);
                                    LteSiMsg sm;
                                    sm.sfn = sfn; sm.subframe = sf; sm.bits = msg; sm.format = df.d.format;
                                    R.si.push_back(std::move(sm));
                                }
                                break;
                            }
                            if (!got) R.tbFail++;
                        }
                    }
                if (nm) {
                    R.meas.rsrpDb = (float)(10 * std::log10(std::max(1e-20, rsrp / nm)));
                    R.meas.rssiDb = (float)(10 * std::log10(std::max(1e-20, rssi / nm)));
                    R.meas.rsrqDb = (float)(10 * std::log10(std::max(1e-20, nRb * rsrp / std::max(1e-20, rssi))));
                    R.meas.sinrDb = (float)(10 * std::log10(std::max(1e-20, rsrp / std::max(1e-20, nv))));
                }
            }
        }
        out.push_back(std::move(R));
    }
    return out;
}

} // namespace dect2::cell
