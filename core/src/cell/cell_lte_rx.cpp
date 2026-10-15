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
constexpr float kSssSigExtra = 5.0f;    // SSS significance for a second / third cell behind one PSS timing
constexpr float kSssListAlone = 5.5f;   // SSS significance that lists a cell whose MIB did not decode (noise alone stays under 4.5)

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

// PSS peaks: the folded correlation energy over the first half frames, for each NID2 and carrier offset hypothesis. Several peaks per NID2
// (cells of other sites arrive at other times), the strongest first; a peak blocks +-64 samples (33 us: its echoes) around it.
std::vector<Cand> pssSearch(const std::vector<cf32>& y, double maxCfo, float minDb, int halves = 8, int perNid2 = 3) {
    std::vector<Cand> out;
    const size_t nUse = std::min(y.size(), (size_t)(halves * kHalf + kNfft6));
    if (nUse < (size_t)(kHalf + kNfft6)) return out;
    const double step = 7500;
    const int nh = std::max(0, (int)std::ceil(maxCfo / step - 0.34));
    std::vector<cf32> c;
    std::vector<std::vector<float>> folds;
    std::vector<double> means;
    for (int n2 = 0; n2 < 3; n2++) {
        folds.clear(); means.clear();
        for (int h = -nh; h <= nh; h++) {
            xcorr(y.data(), nUse, withCfo(pssTime()[(size_t)n2], h * step, kSyncRate), c);
            std::vector<float> fold((size_t)kHalf, 0.f);
            for (size_t t = 0; t < c.size(); t++) fold[t % kHalf] += std::norm(c[t]);
            double mean = 0;
            for (float v : fold) mean += v;
            means.push_back(std::max(1e-30, mean / kHalf));
            folds.push_back(std::move(fold));
        }
        for (int k = 0; k < perNid2; k++) {
            int bh = 0, bt = 0; float bdb = -100;
            for (size_t h = 0; h < folds.size(); h++) {
                const auto& f = folds[h];
                const int pk = (int)(std::max_element(f.begin(), f.end()) - f.begin());
                const float db = (float)(10 * std::log10(std::max(1e-30, (double)f[(size_t)pk]) / means[h]));
                if (db > bdb) { bdb = db; bh = (int)h; bt = pk; }
            }
            if (bdb < (k == 0 ? minDb : std::max(minDb, 12.0f))) break;   // a later peak must stand clear of the first one's sidelobes
            Cand cd; cd.nid2 = n2; cd.cfo = (bh - nh) * step; cd.t0 = bt; cd.db = bdb;
            out.push_back(cd);
            for (auto& f : folds) for (int d = -64; d <= 64; d++) f[(size_t)((bt + d + kHalf) % kHalf)] = 0;
        }
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

struct SssResult { bool ok = false; int nid1 = 0; bool ext = false, tdd = false; int sf0Parity = 0; float score = 0, sig = 0; };

void fft128(const cf32* x, std::vector<cf32>& out) {
    static thread_local std::unique_ptr<Fft> f;
    if (!f) f.reset(new Fft(kNfft6));
    out.assign(x, x + kNfft6);
    f->forward(out.data());
}

// a cell already found: its PSS / SSS are taken out before the search for weaker cells behind the same timing
struct KnownSync { int nid1 = 0, nid2 = 0, parity = 0, off = 0; std::vector<double> t; };

// +-6 subcarrier moving sum (the channel hardly changes over 90 kHz; noise and other cells' sequences average out)
inline void smooth62(const cf32* in, cf32* out) {
    for (int n = 0; n < 62; n++) {
        cf32 h(0, 0);
        for (int k = std::max(0, n - 6); k <= std::min(61, n + 6); k++) h += in[k];
        out[n] = h / (float)(std::min(61, n + 6) - std::max(0, n - 6) + 1);
    }
}

// the cell identity groups heard behind one PSS timing, the most significant first: time-synchronised cells of the same NID2 share the
// PSS. The known cells at this timing are cancelled first (their sequences and channels, estimated from the received symbols).
std::vector<SssResult> sssDetect(const std::vector<cf32>& ys, int nid2, const Track& tr, const std::vector<KnownSync>& known = {}) {
    std::vector<SssResult> all;
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
            cf32 Pv[62], Sv[62], tmp[62], hs[62], hSame[62];
            for (int n = 0; n < 62; n++) { const int b = lteBin(n + 5, 6, kNfft6); Pv[n] = P[(size_t)b]; Sv[n] = S[(size_t)b]; hSame[n] = cf32(0, 0); }
            for (const KnownSync& k : known) {
                if ((size_t)m >= k.t.size() || std::fabs(k.t[(size_t)m] - tr.t[(size_t)m]) > 2.5 || k.off != h.off) continue;
                const float* d = sssSeq(k.nid2, k.nid1, (m % 2) == k.parity ? 0 : 1);
                for (int n = 0; n < 62; n++) tmp[n] = Sv[n] * d[n];
                smooth62(tmp, hs);
                for (int n = 0; n < 62; n++) Sv[n] -= hs[n] * d[n];
                if (k.nid2 == nid2) {   // the same PSS: it carries both channels; take the known cell's (from its SSS) out of the estimate below
                    for (int n = 0; n < 62; n++) hSame[n] += hs[n];
                    continue;
                }
                cf32 kp[62];
                ltePss(k.nid2, kp);
                for (int n = 0; n < 62; n++) tmp[n] = Pv[n] * std::conj(kp[n]);
                smooth62(tmp, hs);
                for (int n = 0; n < 62; n++) Pv[n] -= hs[n] * kp[n];
            }
            for (int n = 0; n < 62; n++) tmp[n] = Pv[n] * std::conj(pss[n]);
            smooth62(tmp, hs);
            for (int n = 0; n < 62; n++) hs[n] -= hSame[n];
            float z[62];
            double e = 0;
            for (int n = 0; n < 62; n++) {
                z[n] = (Sv[n] * std::conj(hs[n])).real();
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
        // significance: the score against the spread of all 336 hypotheses (noise gives about 1, the largest of 1344 about 3.5; a strong
        // cell's own sequence cross-correlations raise the spread, so the cells under it stand out once it is cancelled)
        double ms = 0;
        for (double v : acc) ms += v * v;
        const double rms = std::sqrt(ms / (double)acc.size());
        for (int i = 0; i < 168 * 2; i++) {
            const float sc = (float)(acc[(size_t)i] / norm), sg = rms > 0 ? (float)(acc[(size_t)i] / rms) : 0.f;
            bool isKnown = false;
            for (const KnownSync& k : known) if (k.nid2 == nid2 && k.nid1 == i / 2) isKnown = true;
            if (!isKnown && (sc > 0.25f || sg >= kSssSigExtra)) { SssResult r; r.ok = true; r.score = sc; r.sig = sg; r.nid1 = i / 2; r.sf0Parity = i % 2; r.ext = h.ext; r.tdd = h.tdd; all.push_back(r); }
            if (sc > best.score) { best.score = sc; best.sig = sg; best.nid1 = i / 2; best.sf0Parity = i % 2; best.ext = h.ext; best.tdd = h.tdd; }
        }
    }
    std::sort(all.begin(), all.end(), [](const SssResult& a, const SssResult& b) { return a.sig > b.sig; });
    std::vector<SssResult> out;
    for (const auto& r : all) {
        if (out.size() >= 3 || (!out.empty() && r.sig < kSssSigExtra)) break;
        bool dup = false;
        for (const auto& o : out) if (o.nid1 == r.nid1) dup = true;
        if (!dup) out.push_back(r);
    }
    if (out.empty()) { best.ok = false; out.push_back(best); }
    return out;
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

// The MIB over every frame of the block at once: the 40 ms periods carry the same MIB but for the SFN, which advances by one each period.
// The code is linear, so period P's soft bits are turned into period 0's by flipping them where the code word of the SFN difference
// (s0 + P) xor s0 has a one; each start s0 (with the frame's place in its period) is a hypothesis, and only a decode whose own SFN gives the
// same differences counts (with the CRC, the spare bits at zero and a valid bandwidth: about 34 bits checked, noise does not pass).
const Bits& sfnDeltaWord(int d, int M) {
    static thread_local std::vector<Bits> cache[2];
    std::vector<Bits>& c = cache[M == 1920 ? 0 : 1];
    if (c.empty()) {
        c.resize(256);
        for (int v = 0; v < 256; v++) {
            Bits b(24, 0);
            for (int i = 0; i < 8; i++) b[(size_t)(6 + i)] = (uint8_t)((v >> (7 - i)) & 1);
            appendCrc(b, kCrc16, 16, 0);
            c[(size_t)v] = convRateMatch(convEncode(b), (size_t)M);
        }
    }
    return c[(size_t)(d & 255)];
}

MibTry mibCombine(const std::vector<std::array<std::vector<float>, 3>>& llr, int pci, bool ext) {
    MibTry out;
    const int M = ltePbchBits(ext), per = M / 4;
    const int nf = (int)llr.size();
    if (nf < 2) return out;
    const Bits cs = gold((uint32_t)pci, (size_t)M);
    static const int portsOf[3] = {1, 2, 4};
    std::vector<float> e((size_t)M);
    float bestAg = 0;
    for (int a = 0; a < 4; a++) {
        const int nP = (nf - 1 + a) / 4 + 1;
        // the hypotheses s0 grouped by the differences they give (decoded once per group)
        std::vector<std::vector<int>> keys;
        std::vector<std::vector<int>> members;
        for (int s0 = 0; s0 < 256; s0++) {
            std::vector<int> k((size_t)nP);
            for (int P = 0; P < nP; P++) k[(size_t)P] = ((s0 + P) & 255) ^ s0;
            size_t i = 0;
            while (i < keys.size() && keys[i] != k) i++;
            if (i == keys.size()) { keys.push_back(k); members.emplace_back(); }
            members[i].push_back(s0);
        }
        for (int pi = 0; pi < 3; pi++)
            for (size_t gi = 0; gi < keys.size(); gi++) {
                std::fill(e.begin(), e.end(), 0.f);
                int used = 0;
                for (int f = 0; f < nf; f++) {
                    const auto& L = llr[(size_t)f][(size_t)pi];
                    if (L.size() < (size_t)per) continue;
                    used++;
                    const int seg = (f + a) % 4, P = (f + a) / 4;
                    const Bits& D = sfnDeltaWord(keys[gi][(size_t)P], M);
                    for (int i = 0; i < per; i++) {
                        const size_t k = (size_t)(seg * per + i);
                        const float v = (cs[k] ^ D[k]) ? -L[(size_t)i] : L[(size_t)i];
                        e[k] += v;
                    }
                }
                if (used < 2) continue;
                const std::vector<float> d = convRateRecover(e.data(), (size_t)M, 40);
                const Bits b = viterbiTb(d.data(), 40);
                uint32_t par = 0;
                for (int i = 24; i < 40; i++) par = (par << 1) | b[(size_t)i];
                if ((crc(b.data(), 24, kCrc16, 16) ^ par) != mibMask(portsOf[pi])) continue;
                bool spare = true;
                for (int i = 14; i < 24; i++) if (b[(size_t)i]) spare = false;
                if (!spare) continue;
                int v = 0;
                for (int i = 0; i < 3; i++) v = (v << 1) | b[(size_t)i];
                if (v > 5) continue;
                int s8 = 0;
                for (int i = 6; i < 14; i++) s8 = (s8 << 1) | b[(size_t)i];
                if (std::find(members[gi].begin(), members[gi].end(), s8) == members[gi].end()) continue;
                const float ag = agreement(e.data(), convRateMatch(convEncode(b), (size_t)M));
                if (ag < 0.05f || ag <= bestAg) continue;
                bestAg = ag;
                out.ok = true;
                out.mib.ok = true;
                out.mib.nRb = lteBwIndexToRb(v);
                out.mib.phichDur = b[3];
                out.mib.phichRes = (b[4] << 1) | b[5];
                out.mib.sfn = (s8 * 4 + a) & 1023;
                out.mib.ports = portsOf[pi];
                out.frame = 0;
            }
    }
    return out;
}

struct DciFound { LteDciSi d; Bits payload; int cce = 0, L = 4; Bits coded; };

// a cell whose MIB decoded: a weaker cell at its timing borrows its set-up, and has it cancelled from under itself
struct Decoded {
    int pci = 0; bool ext = false;
    int nRb = 6, ports = 1, phichDur = 0, phichRes = 0;
    double fs0 = 0;        // first frame start in the block, sync samples
    double startIn = 0;    // the same, input samples
    double cfo = 0;
    int sfnAt0 = 0;        // SFN of that frame
    double slope = 9600;   // half frame length, sync samples
    int outIdx = -1;       // its result
    bool haveSib1 = false; LteDciSi sib1;   // the allocation of its SIB1
    Bits sib1Tb;                            // and its transport block, when read (rebuilt in every repetition)
    struct DciEv { int fr, sf, cfi, cce; Bits coded; };
    std::vector<DciEv> dcis;                // its SI-RNTI DCIs as decoded (frame of the block, subframe, the coded bits)
};

int pcfichDecode(const LteGrid& g, const LteChan& ch, const LteCellCfg& c, int sf) {
    const LteControl c1 = lteControl(c, 1, sf);
    std::vector<std::pair<int, int>> res;
    for (const auto& rg : c1.pcfich) for (int i = 0; i < 4; i++) res.push_back({rg.l, rg.k[i]});
    std::vector<float> L;
    txdRes(g, ch, res, c.ports, L);
    if (L.size() < 32) return 0;
    const Bits pcs = gold((uint32_t)((sf + 1) * (2 * c.pci + 1) * 512 + c.pci), 32);
    int cfi = 1; float bc = -1e30f;
    for (int k = 0; k < 3; k++) {
        float s = 0;
        for (int i = 0; i < 32; i++) s += (pcs[(size_t)i] ? -L[(size_t)i] : L[(size_t)i]) * (kCfiCode[k][i] ? -1.f : 1.f);
        if (s > bc) { bc = s; cfi = k + 1; }
    }
    return cfi;
}

// the known transmissions of a decoded cell in one subframe of the SI stage: its SIB1 PDSCH (subframe 5 of even frames) and its decoded DCIs
void knownParts(const Decoded& d, const LteCellCfg& c, int fr, int sf, int sfn, int cfi, std::vector<LteTxPart>& parts) {
    parts.clear();
    auto qp = [](const Bits& b) { std::vector<cf32> v; for (size_t i = 0; i + 1 < b.size(); i += 2) v.push_back(qpsk(b[i], b[i + 1])); return v; };
    if (sf == 5 && sfn % 2 == 0 && d.haveSib1 && !d.sib1Tb.empty() && cfi > 0) {
        std::vector<int> p0, p1;
        ltePrbs(d.sib1, c.nRb, p0, p1);
        bool ok = !p0.empty();
        for (int v : p0) if (v < 0 || v >= c.nRb) ok = false;
        for (int v : p1) if (v < 0 || v >= c.nRb) ok = false;
        Bits tb = d.sib1Tb;
        appendCrc(tb, kCrc24A, 24);
        const int B = (int)tb.size(), K = turboSizeFor(B), F = K - B;
        if (ok && K > 0) {
            LteTxPart pt;
            pt.res = ltePdschRes(c, sf, cfi, p0, p1);
            Bits cb((size_t)F, 0); cb.insert(cb.end(), tb.begin(), tb.end());
            const int NL = c.ports > 1 ? 2 : 1;
            const size_t G = pt.res.size() * 2, E = (size_t)(NL * 2) * (G / (size_t)(NL * 2));
            const int k = (sfn / 2) % 4, rv = ((3 * k + 1) / 2) % 4;
            Bits e = turboRateMatch(turboEncode(cb), K, F, E, rv);
            const Bits cs = gold(((uint32_t)kSiRnti << 14) + ((uint32_t)sf << 9) + (uint32_t)c.pci, e.size());
            for (size_t i = 0; i < e.size(); i++) e[i] ^= cs[i];
            pt.d = qp(e);
            parts.push_back(std::move(pt));
        }
    }
    for (const auto& ev : d.dcis) {
        if (ev.fr != fr || ev.sf != sf) continue;
        const LteControl ctl = lteControl(c, ev.cfi, sf);
        const size_t tot = ctl.pdcchQuad.size() * 8, off = (size_t)ev.cce * 72;
        if (off + ev.coded.size() > tot) continue;
        const Bits cs = gold((uint32_t)(sf * 512 + c.pci), tot);
        Bits e(ev.coded);
        for (size_t i = 0; i < e.size(); i++) e[i] ^= cs[off + i];
        const std::vector<cf32> sym = qp(e);
        for (size_t q = 0; q * 4 + 3 < sym.size(); q++) {
            const LteReg& rg = ctl.pdcchQuad[off / 8 + q];
            LteTxPart pt;
            for (int i = 0; i < 4; i++) pt.res.push_back({rg.l, rg.k[i]});
            pt.d.assign(sym.begin() + (long)(4 * q), sym.begin() + (long)(4 * q + 4));
            parts.push_back(std::move(pt));
        }
    }
}

LteCellCfg cfgOf(const Decoded& d, int nRbGrid) {
    LteCellCfg c;
    c.pci = d.pci; c.extCp = d.ext; c.tdd = false; c.nRb = nRbGrid; c.ports = d.ports; c.phichDur = d.phichDur; c.phichRes = d.phichRes;
    return c;
}

void addCrs(std::vector<LteGrid>& port, const LteCellCfg& c, int sf) {
    const int N = lteSymbPerSlot(c.extCp), nsym = 2 * N, nsc = 12 * c.nRb;
    cf32 r[2 * kMaxRb];
    for (int p = 0; p < c.ports; p++) for (int lsf = 0; lsf < nsym; lsf++) {
        const int l = lsf % N, ns = 2 * sf + lsf / N;
        const bool has = (p <= 1) ? (l == 0 || l == N - 3) : (l == 1);
        if (!has) continue;
        lteCrsSeq(c.pci, ns, l, c.extCp, r);
        for (int k = 0; k < nsc; k++) if (lteCrsAt(c, sf, lsf, k, p)) port[(size_t)p].at(lsf, k) = r[k / 6 + kMaxRb - c.nRb];
    }
}

} // namespace

void lteCancelKnown(LteGrid& y, const LteChan& ch, const LteCellCfg& c, int sf, int sfn, int mibNRb, float scale, const std::vector<LteTxPart>* extra) {
    const int N = lteSymbPerSlot(c.extCp), nsym = 2 * N, nsc = 12 * c.nRb;
    if (y.nSym != nsym || y.nSc != nsc || (int)ch.h.size() < c.ports) return;
    std::vector<LteGrid> port((size_t)c.ports);
    for (auto& p : port) p.init(nsym, nsc);
    addCrs(port, c, sf);
    if (sf == 0 && mibNRb > 0 && lteRbToBwIndex(mibNRb) >= 0) {
        Bits mib;
        auto put = [&](int v, int n) { for (int i = n - 1; i >= 0; i--) mib.push_back((uint8_t)((v >> i) & 1)); };
        put(lteRbToBwIndex(mibNRb), 3); put(c.phichDur, 1); put(c.phichRes, 2); put((sfn >> 2) & 0xff, 8); put(0, 10);
        appendCrc(mib, kCrc16, 16, mibMask(c.ports));
        const int M = ltePbchBits(c.extCp), per = M / 4, seg = (sfn & 1023) % 4;
        Bits e = convRateMatch(convEncode(mib), (size_t)M);
        const Bits cs = gold((uint32_t)c.pci, (size_t)M);
        std::vector<cf32> d;
        for (int i = seg * per; i + 1 < (seg + 1) * per; i += 2) d.push_back(qpsk(e[(size_t)i] ^ cs[(size_t)i], e[(size_t)i + 1] ^ cs[(size_t)i + 1]));
        std::vector<std::vector<cf32>> yv;
        lteTxdEncode(d, c.ports, yv);
        size_t j = 0;
        for (int l = N; l < N + 4; l++) for (int kk = 0; kk < 72; kk++) {
            const int k = 6 * c.nRb - 36 + kk;
            const bool rs = (l - N <= 1) || (c.extCp && l - N == 3);
            if (rs && (k % 3) == (c.pci % 6) % 3) continue;
            for (int p = 0; p < c.ports; p++) if (j < yv[(size_t)p].size()) port[(size_t)p].at(l, k) = yv[(size_t)p][j];
            j++;
        }
    }
    if (extra) for (const LteTxPart& pt : *extra) {
        std::vector<std::vector<cf32>> yv;
        lteTxdEncode(pt.d, c.ports, yv);
        for (int p = 0; p < c.ports; p++)
            for (size_t i = 0; i < pt.res.size() && i < yv[(size_t)p].size(); i++) {
                const auto& q = pt.res[i];
                if (q.first >= 0 && q.first < nsym && q.second >= 0 && q.second < nsc) port[(size_t)p].at(q.first, q.second) = yv[(size_t)p][i];
            }
    }
    for (int l = 0; l < nsym; l++) for (int k = 0; k < nsc; k++) {
        cf32 v(0, 0);
        for (int p = 0; p < c.ports; p++) v += ch.h[(size_t)p].at(l, k) * port[(size_t)p].at(l, k);
        y.at(l, k) += scale * v;
    }
}

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
    // cells found so far: cancelled from the SSS search of the others; a second pass looks behind every PSS timing again with all of them out
    std::vector<KnownSync> known;
    auto remember = [&](const SssResult& ss, int nid2, const Track& t) {
        KnownSync k; k.nid1 = ss.nid1; k.nid2 = nid2; k.parity = ss.sf0Parity; k.off = !ss.tdd ? (ss.ext ? 128 + 32 : 128 + 9) : (ss.ext ? 480 : 412); k.t = t.t;
        known.push_back(std::move(k));
    };
    std::vector<Decoded> decoded;
    decoded.reserve(64);
    const double frameLenSync = 2.0 * kHalf;
    // decoded cells at (about) the same frame timing and carrier: a synchronised network's sectors. k: frame index offset (theirs = ours + k)
    auto sameTiming = [&](double fs0, double cfo, bool ext, int pci, std::vector<std::pair<const Decoded*, int>>& res) {
        res.clear();
        for (const Decoded& d : decoded) {
            if (d.pci == pci || d.ext != ext || std::fabs(d.cfo - cfo) > 1500) continue;
            const double dt = fs0 - d.fs0;
            const int k = (int)std::lround(dt / frameLenSync);
            if (std::fabs(dt - k * frameLenSync) > 6.0) continue;
            res.push_back({&d, k});
        }
    };
    static const int portsOf[3] = {1, 2, 4};
    // take the decoded cells at this timing out of a subframe grid (nRbGrid wide): each one's channel from its reference signals, then its
    // known signals subtracted; a second round estimates each again with the others already out
    auto cancelUnder = [&](LteGrid& g, const std::vector<std::pair<const Decoded*, int>>& under, int sf, int frame, int nRbGrid) {
        std::vector<const Decoded*> ds;
        std::vector<int> sfns;
        std::vector<int> frs;
        for (const auto& u : under) if (nRbGrid == 6 || u.first->nRb == nRbGrid) { ds.push_back(u.first); sfns.push_back((u.first->sfnAt0 + frame + u.second) & 1023); frs.push_back(frame + u.second); }
        if (ds.empty()) return;
        std::vector<LteChan> chs(ds.size());
        std::vector<std::vector<LteTxPart>> ex(ds.size());
        for (int it = 0; it < 2; it++)
            for (size_t i = 0; i < ds.size(); i++) {
                const LteCellCfg cd = cfgOf(*ds[i], nRbGrid);
                if (it == 1) lteCancelKnown(g, chs[i], cd, sf, sfns[i], ds[i]->nRb, 1.f, nRbGrid > 6 ? &ex[i] : nullptr);   // put it back, estimate it again
                if (sf == 0 && nRbGrid == 6) lteChanEst(g, cd, sf, nRbGrid, 0, cd.ports, chs[i], 2, lteSymbPerSlot(cd.extCp), lteSymbPerSlot(cd.extCp) + 3);
                else lteChanEst(g, cd, sf, nRbGrid, 0, cd.ports, chs[i], it == 1 ? 2 : 0);
                double e0 = 0, e1 = 0;
                if (dbg && frame == 0 && sf == 0) for (int l = 7; l < 11; l++) for (int k = 0; k < 72; k++) e0 += std::norm(g.at(l, 6 * nRbGrid - 36 + k));
                if (nRbGrid > 6) knownParts(*ds[i], cd, frs[i], sf, sfns[i], pcfichDecode(g, chs[i], cd, sf), ex[i]);
                double q0 = 0, q1 = 0;
                const bool show = dbg && nRbGrid > 6 && sf == 5 && frame < 2 && !ex[i].empty();
                if (show) for (const auto& pt : ex[i]) for (const auto& q : pt.res) q0 += std::norm(g.at(q.first, q.second));
                lteCancelKnown(g, chs[i], cd, sf, sfns[i], ds[i]->nRb, -1.f, nRbGrid > 6 ? &ex[i] : nullptr);
                if (show) { for (const auto& pt : ex[i]) for (const auto& q : pt.res) q1 += std::norm(g.at(q.first, q.second)); fprintf(stderr, "[cell]     it %d PCI %d sfn %d: %zu parts, %.1f dB\n", it, ds[i]->pci, sfns[i], ex[i].size(), 10 * std::log10(q1 / std::max(1e-30, q0))); }
                if (dbg && frame == 0 && sf == 0) { for (int l = 7; l < 11; l++) for (int k = 0; k < 72; k++) e1 += std::norm(g.at(l, 6 * nRbGrid - 36 + k)); fprintf(stderr, "[cell]     it %d cancel PCI %d sfn %d ports %d: %.1f dB\n", it, ds[i]->pci, sfns[i], cd.ports, 10 * std::log10(e1 / std::max(1e-30, e0))); }
            }
    };

    // PBCH, from the central 6 resource blocks. hard: decoded cells at this timing are cancelled first, the channel is estimated over a wider
    // span, and the frames are combined across 40 ms periods (a weak cell, or one under a stronger one)
    auto pbchStage = [&](std::vector<cf32>& ys, Track& tr, double fs0, int pci, bool ext, bool fixCfo, bool hard, LteCellResult& R) -> MibTry {
        LteCellCfg c6;
        c6.pci = pci; c6.extCp = ext; c6.tdd = false; c6.nRb = 6; c6.ports = 4;
        const auto pres = pbchRes(pci, ext);
        std::vector<std::array<std::vector<float>, 3>> llr;
        double rsrp6 = 0, rssi6 = 0, nv6 = 0; int nm6 = 0;
        const double frameLen = 2 * tr.slope;
        std::vector<std::pair<const Decoded*, int>> under;
        if (hard) sameTiming(fs0, tr.cfo, ext, pci, under);
        if (!under.empty()) {   // one site's sectors share the oscillator: the strong cell's carrier offset is the better measure
            const double d = under[0].first->cfo - tr.cfo;
            mixDown(ys.data(), ys.size(), d, kSyncRate);
            tr.cfo += d;
            R.found.cfoHz = tr.cfo;
        }
        LteGrid g;
        LteChan ch;
        for (int pass = fixCfo ? 0 : 1; pass < 2; pass++) {
            cf32 rot(0, 0);
            for (int f = 0;; f++) {
                const long s = std::lround(fs0 + f * frameLen);
                if ((size_t)(s + lteSubframeLen(kNfft6) + 8) > ys.size()) break;
                if (pass == 0) {   // the residual carrier offset: the reference signals of ports 0 and 1, slot 0 against slot 1 of every subframe
                    const int N = lteSymbPerSlot(ext);   // (0.5 ms apart: +-1 kHz), the whole block
                    for (int sf = 0; sf < 10; sf++) {
                        const long s2 = s + (long)sf * lteSubframeLen(kNfft6);
                        if ((size_t)(s2 + lteSubframeLen(kNfft6) + 8) > ys.size()) break;
                        lteOfdmDemod(&ys[(size_t)s2], 6, ext, kNfft6, g);
                        cf32 ra[2 * kMaxRb], rb[2 * kMaxRb];
                        for (int l : {0, N - 3}) {
                            lteCrsSeq(pci, 2 * sf, l, ext, ra);
                            lteCrsSeq(pci, 2 * sf + 1, l, ext, rb);
                            for (int p = 0; p < 2; p++)
                                for (int k = 0; k < 72; k++) {
                                    if (!lteCrsAt(c6, sf, l, k, p)) continue;
                                    const int mm = k / 6 + kMaxRb - 6;
                                    rot += (g.at(N + l, k) * std::conj(rb[mm])) * std::conj(g.at(l, k) * std::conj(ra[mm]));
                                }
                        }
                    }
                    continue;
                }
                lteOfdmDemod(&ys[(size_t)s], 6, ext, kNfft6, g);
                if (!under.empty()) {
                    double e0 = 0, e1 = 0;
                    if (dbg && f == 0) for (const auto& q : pres) e0 += std::norm(g.at(q.first, q.second));
                    cancelUnder(g, under, 0, f, 6);
                    if (dbg && f == 0) { for (const auto& q : pres) e1 += std::norm(g.at(q.first, q.second)); fprintf(stderr, "[cell]   PCI %d: %zu cells cancelled, PBCH power %.1f dB\n", pci, under.size(), 10 * std::log10(std::max(1e-30, e1 / std::max(1e-30, e0)))); }
                }
                if (hard && !under.empty()) lteChanEst(g, c6, 0, 6, 0, 4, ch, 3, lteSymbPerSlot(ext), lteSymbPerSlot(ext) + 3);
                else lteChanEst(g, c6, 0, 6, 0, 4, ch, hard ? 3 : 0);
                rsrp6 += ch.rsrp; rssi6 += ch.rssi; nv6 += ch.noiseVar; nm6++;
                std::array<std::vector<float>, 3> L;
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
        if (nm6 && !hard) {
            R.meas.rsrpDb = (float)(10 * std::log10(std::max(1e-20, rsrp6 / nm6)));
            R.meas.rssiDb = (float)(10 * std::log10(std::max(1e-20, rssi6 / nm6)));
            R.meas.rsrqDb = (float)(10 * std::log10(std::max(1e-20, 6 * rsrp6 / std::max(1e-20, rssi6))));
            R.meas.sinrDb = (float)(10 * std::log10(std::max(1e-20, rsrp6 / std::max(1e-20, nv6))));
        }
        MibTry mt = mibDecode(llr, pci, ext);
        if (!mt.ok && hard) mt = mibCombine(llr, pci, ext);
        return mt;
    };

    // system information on SI-RNTI over the block. under: decoded cells at this timing (their reference signals and PBCH cancelled first);
    // wide: channel estimate span; hint: a SIB1 allocation to try when none of this cell's own SIB1 DCIs decoded (a sector of the same site)
    auto siStage = [&](LteCellResult& R, const LteCellCfg& c, double start, double cfo, double r, int sfnAt0, int wide,
                       const std::vector<std::pair<const Decoded*, int>>& under, const LteDciSi* hint, Decoded* rec) {
        const int nRb = c.nRb, pci = c.pci;
        const int nfft = lteNfft(nRb);
        const double rate = lteRate(nfft);
        const double halfBw = nRb * 90e3 + 30e3;
        const double frameIn = 0.01 * fs * r;
        const int nFrames = (int)std::floor(((double)n - start - 64) / frameIn);
        if (nFrames < 1) return;
        const size_t sfLen = (size_t)lteSubframeLen(nfft);
        const std::vector<cf32> z = resampleBlock(x, n, fs, rate, offsetHz + cfo, 2 * halfBw, start, (size_t)nFrames * 10 * sfLen + 16, r);
        double rsrp = 0, rssi = 0, nv = 0; int nm = 0;
        std::vector<Bits> seen;
        LteGrid g;
        LteChan ch;
        struct Rep { LteGrid g; LteChan ch; int sfn, cfi; };
        std::vector<Rep> reps;              // subframe 5 of the even frames: SIB1 repetitions, combined below if no single one decoded
        std::vector<LteDciSi> sib1Dcis;
        bool sib1Got = false;
        for (int fr = 0; fr < nFrames; fr++)
            for (int sf = 0; sf < 10; sf++) {
                const int sfn = (sfnAt0 + fr) & 1023;
                lteOfdmDemod(&z[((size_t)fr * 10 + (size_t)sf) * sfLen], nRb, c.extCp, nfft, g);
                cancelUnder(g, under, sf, fr, nRb);
                lteChanEst(g, c, sf, nRb, 0, c.ports, ch, wide);
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
                if (sf == 5 && sfn % 2 == 0) reps.push_back({g, ch, sfn, cfi});
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
                            df.cce = cce; df.L = L2;
                            df.coded = convRateMatch(convEncode(b), E);
                            if (rec) rec->dcis.push_back({fr, sf, cfi, cce, df.coded});
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
                    if (sf == 5 && sfn % 2 == 0) sib1Dcis.push_back(df.d);
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
                        if (sf == 5 && sfn % 2 == 0) {
                            sib1Got = true;
                            if (rec && rec->sib1Tb.empty()) { rec->haveSib1 = true; rec->sib1 = df.d; rec->sib1Tb = msg; }
                        }
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
        // SIB1 over its repetitions: the same transport block in subframe 5 of every even frame, the redundancy version following the SFN
        // (36.321 5.3.1: rv = ceil(3k/2) mod 4, k = (SFN/2) mod 4). The soft bits of each repetition go into one circular buffer.
        if (!sib1Got && reps.size() >= 2) {
            std::vector<LteDciSi> hyps = sib1Dcis;
            if (hint) hyps.push_back(*hint);
            if (dbg) fprintf(stderr, "[cell]   PCI %d SIB1 combining: %zu repetitions, %zu allocations, %zu cells cancelled, %d DCIs, SINR %.1f\n", pci, reps.size(), hyps.size(), under.size(), R.dciSeen, 10 * std::log10(std::max(1e-20, rsrp / std::max(1e-20, nv))));
            for (size_t h = 0; h < hyps.size() && h < 4 && !sib1Got; h++) {
                const LteDciSi& dd = hyps[h];
                bool dupH = false;
                for (size_t q = 0; q < h; q++) if (hyps[q].tbs == dd.tbs && hyps[q].rbStart == dd.rbStart && hyps[q].lCrb == dd.lCrb && hyps[q].distributed == dd.distributed) dupH = true;
                if (dupH) continue;
                std::vector<int> p0, p1;
                ltePrbs(dd, nRb, p0, p1);
                bool okPrb = !p0.empty();
                for (int v : p0) if (v < 0 || v >= nRb) okPrb = false;
                for (int v : p1) if (v < 0 || v >= nRb) okPrb = false;
                const int B = dd.tbs + 24, K = turboSizeFor(B), F = K - B;
                if (!okPrb || K == 0) continue;
                std::vector<float> acc;
                int nAdd = 0;
                for (const Rep& rp : reps) {
                    const auto pres2 = ltePdschRes(c, 5, rp.cfi, p0, p1);
                    std::vector<float> e;
                    txdRes(rp.g, rp.ch, pres2, c.ports, e);
                    const int NL = c.ports > 1 ? 2 : 1;
                    const size_t G = 2 * pres2.size(), E = (size_t)(NL * 2) * (G / (size_t)(NL * 2));
                    if (e.size() < E || E == 0) continue;
                    e.resize(E);
                    const Bits scs = gold(((uint32_t)kSiRnti << 14) + ((uint32_t)5 << 9) + (uint32_t)pci, E);
                    for (size_t i = 0; i < E; i++) if (scs[i]) e[i] = -e[i];
                    const int k = (rp.sfn / 2) % 4, rv = ((3 * k + 1) / 2) % 4;
                    turboRateRecover(e.data(), E, K, F, rv, acc);
                    nAdd++;
                }
                if (nAdd < 2) continue;
                Bits tb;
                struct Ck { int F; } ck{F};
                auto chk = [](const Bits& b, void* p) {
                    const int f = static_cast<Ck*>(p)->F;
                    Bits t(b.begin() + f, b.end());
                    return checkCrc(t, kCrc24A, 24);
                };
                if (!turboDecode(acc, K, F, 10, tb, chk, &ck)) continue;
                sib1Got = true;
                LteSiMsg sm;
                sm.sfn = reps[0].sfn; sm.subframe = 5; sm.bits.assign(tb.begin() + F, tb.begin() + F + dd.tbs); sm.format = dd.format;
                if (rec && rec->sib1Tb.empty()) { rec->haveSib1 = true; rec->sib1 = dd; rec->sib1Tb = sm.bits; }
                if (dbg) fprintf(stderr, "[cell]   PCI %d SIB1 from %d repetitions combined\n", pci, nAdd);
                R.si.push_back(std::move(sm));
            }
        }
        if (nm) {
            R.meas.rsrpDb = (float)(10 * std::log10(std::max(1e-20, rsrp / nm)));
            R.meas.rssiDb = (float)(10 * std::log10(std::max(1e-20, rssi / nm)));
            R.meas.rsrqDb = (float)(10 * std::log10(std::max(1e-20, nRb * rsrp / std::max(1e-20, rssi))));
            R.meas.sinrDb = (float)(10 * std::log10(std::max(1e-20, rsrp / std::max(1e-20, nv))));
        }
    };

    // cells whose PBCH did not decode: tried again at the end with the set-up of a decoded cell at their timing
    struct Pending { LteCellResult R; double fs0; Track tr; int outIdx; bool done = false; };
    std::vector<Pending> pending;
    // a cell whose MIB decoded: its system information, then it is one of the decoded cells (outIdx: its place in out if listed already)
    auto accept = [&](LteCellResult& R, const MibTry& mt, double fs0, const Track& tr, bool hardMib, int outIdx) {
        const int pci = R.found.pci;
        const bool ext = R.found.extCp;
        R.mib = mt.mib;
        R.mib.frameStart = R.found.frameStart;
        const int sfnAt0 = (mt.mib.sfn - mt.frame + 1024 * 4) & 1023;   // SFN of the first whole frame in the block
        R.mib.sfn = sfnAt0;
        Decoded D;
        D.pci = pci; D.ext = ext; D.nRb = mt.mib.nRb; D.ports = mt.mib.ports; D.phichDur = mt.mib.phichDur; D.phichRes = mt.mib.phichRes;
        D.fs0 = fs0; D.startIn = R.found.frameStart; D.cfo = tr.cfo; D.sfnAt0 = sfnAt0;
        // system information: needs the whole carrier inside the block
        const int nRb = mt.mib.nRb;
        const double halfBw = nRb * 90e3 + 30e3;
        if (o.wantSi && !R.found.tdd && std::fabs(offsetHz) + halfBw < fs / 2) {
            LteCellCfg c;
            c.pci = pci; c.extCp = ext; c.tdd = false; c.nRb = nRb; c.ports = mt.mib.ports; c.phichDur = mt.mib.phichDur; c.phichRes = mt.mib.phichRes;
            std::vector<std::pair<const Decoded*, int>> under;
            sameTiming(fs0, tr.cfo, ext, pci, under);
            const LteDciSi* hint = nullptr;
            for (const auto& u : under) if (u.first->haveSib1 && u.first->nRb == nRb) { hint = &u.first->sib1; break; }
            if (hardMib) siStage(R, c, R.found.frameStart, under.empty() ? tr.cfo : under[0].first->cfo, tr.slope / kHalf, sfnAt0, 2, under, hint, &D);
            else {
                siStage(R, c, R.found.frameStart, tr.cfo, tr.slope / kHalf, sfnAt0, 0, {}, nullptr, &D);
                if (R.si.empty() && !under.empty()) {   // nothing read: again with the cells at its timing cancelled, their carrier offset (one
                    R.si.clear();                       // site's oscillator) and the wider channel estimate
                    siStage(R, c, R.found.frameStart, under[0].first->cfo, tr.slope / kHalf, sfnAt0, 2, under, hint, &D);
                }
            }
        }
        D.slope = tr.slope;
        D.outIdx = outIdx >= 0 ? outIdx : (int)out.size();
        decoded.push_back(D);
        if (outIdx >= 0) out[(size_t)outIdx] = std::move(R);
        else out.push_back(std::move(R));
    };
    auto hasSib1 = [](const LteCellResult& r) { for (const auto& m : r.si) if (m.subframe == 5) return true; return false; };

    // passes until one finds no new cell (each cancels more from under the next)
    size_t knownBefore = 0;
    for (int pass = 0; pass < 4; pass++) {
    if (pass >= 1 && known.size() == knownBefore) break;
    knownBefore = known.size();
    for (const Cand& cd : cands) {
        if ((int)out.size() >= o.maxCells) break;
        Track tr;
        if (!pssTrack(y, cd, tr)) continue;
        std::vector<cf32> ys = y;
        mixDown(ys.data(), ys.size(), tr.cfo, kSyncRate);
        const Track tr0 = tr;
        const std::vector<cf32> ys0 = ys;
        const std::vector<SssResult> sss = sssDetect(ys, cd.nid2, tr, known);
        for (const SssResult& ss : sss) {
        if ((int)out.size() >= o.maxCells) break;
        tr = tr0; ys = ys0;
        if (dbg) fprintf(stderr, "[cell]   track cfo %.0f slope %.3f halves %d; SSS nid1 %d score %.2f sig %.1f ext %d tdd %d\n", tr.cfo, tr.slope, tr.nHalf, ss.nid1, ss.score, ss.sig, ss.ext, ss.tdd);
        if (!ss.ok) continue;
        const int pci = 3 * ss.nid1 + cd.nid2;
        bool dup = false;
        for (const auto& r : out) if (r.found.pci == pci) dup = true;
        for (const auto& p : pending) if (p.R.found.pci == pci) dup = true;
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
        R.found.sssScore = ss.score;
        R.found.sssSig = ss.sig;
        R.found.frameStart = fs0 / toSync;
        MibTry mt = pbchStage(ys, tr, fs0, pci, ss.ext, true, false, R);
        bool hardMib = false;
        if (!mt.ok && ss.sig >= 3.0f) { mt = pbchStage(ys, tr, fs0, pci, ss.ext, false, true, R); hardMib = mt.ok; }
        if (dbg) fprintf(stderr, "[cell]   PCI %d MIB %d%s: %d RB %d ports sfn %d, RSRP %.1f SINR %.1f fs0 %.1f cfo %.0f\n", pci, mt.ok, hardMib ? " (combined)" : "", mt.mib.nRb, mt.mib.ports, mt.mib.sfn, R.meas.rsrpDb, R.meas.sinrDb, fs0, tr.cfo);
        if (!mt.ok) {
            // the PBCH did not decode (too weak, or under a stronger cell): a clear SSS still lists the cell, without its MIB
            int idx = -1;
            if (ss.sig >= kSssListAlone) { remember(ss, cd.nid2, tr0); idx = (int)out.size(); out.push_back(R); }
            if (!ss.tdd && ss.sig >= 3.0f) pending.push_back({R, fs0, tr, idx});
            continue;
        }
        accept(R, mt, fs0, tr, hardMib, -1);
        remember(ss, cd.nid2, tr0);
        }
    }
    }
    // the end: the cells left over, with everything decoded meanwhile cancelled; again while that frees more
    int tries = 0;
    auto progress = [&]() { size_t v = decoded.size(); for (const auto& r : out) if (hasSib1(r)) v++; return v; };
    for (int outer = 0; outer < 3; outer++) {
        const size_t before = progress();
        // the cells whose MIB did not decode, again with every decoded cell at their timing cancelled (one decoded late may free another)
        for (int round = 0; round < 3; round++) {
            bool changed = false;
            for (Pending& p : pending) {
                if (p.done) continue;
                std::vector<std::pair<const Decoded*, int>> under;
                sameTiming(p.fs0, p.tr.cfo, p.R.found.extCp, p.R.found.pci, under);
                if (under.empty()) continue;
                std::vector<cf32> ys = y;
                mixDown(ys.data(), ys.size(), p.tr.cfo, kSyncRate);
                Track tr = p.tr;
                LteCellResult R = p.R;
                const MibTry mt = pbchStage(ys, tr, p.fs0, R.found.pci, R.found.extCp, false, true, R);
                if (dbg) fprintf(stderr, "[cell]   PCI %d MIB again under %zu cells: %d\n", R.found.pci, under.size(), mt.ok);
                if (!mt.ok) continue;
                p.done = changed = true;
                if (p.outIdx < 0 && (int)out.size() >= o.maxCells + 4) continue;
                accept(R, mt, p.fs0, tr, true, p.outIdx);
            }
            if (!changed) break;
        }
        // still without a MIB, under a decoded cell at the same frame timing (a synchronised network): the same carrier has the same bandwidth
        // and PHICH set-up, and the SFN follows from the timing. The antenna ports are tried (the decoded cell's count first); the CRCs decide.
        for (Pending& p : pending) {
            if (!o.wantSi || tries >= 6) break;
            if (p.done) continue;
            std::vector<std::pair<const Decoded*, int>> under;
            sameTiming(p.fs0, p.tr.cfo, p.R.found.extCp, p.R.found.pci, under);
            if (under.empty()) continue;
            const Decoded& d = *under[0].first;
            const double halfBw = d.nRb * 90e3 + 30e3;
            if (std::fabs(offsetHz) + halfBw >= fs / 2) continue;
            tries++;
            const int sfnAt0 = (d.sfnAt0 - under[0].second + 1024) & 1023;
            const double cfo = d.cfo;   // the site's oscillator
            const int order[3] = {d.ports, d.ports == 2 ? 4 : 2, d.ports == 1 ? 4 : 1};   // the site's own count first
            for (int ports : order) {
                LteCellResult R = p.R;
                LteCellCfg c;
                c.pci = R.found.pci; c.extCp = R.found.extCp; c.tdd = false; c.nRb = d.nRb; c.ports = ports; c.phichDur = d.phichDur; c.phichRes = d.phichRes;
                Decoded D2;
                siStage(R, c, R.found.frameStart, cfo, p.tr.slope / kHalf, sfnAt0, 2, under, d.haveSib1 ? &d.sib1 : nullptr, &D2);
                if (dbg) fprintf(stderr, "[cell]   PCI %d borrows the MIB of PCI %d, %d ports: %zu SI\n", c.pci, d.pci, ports, R.si.size());
                if (R.si.empty()) continue;
                R.found.cfoHz = cfo;
                R.mib.ok = true; R.mib.nRb = d.nRb; R.mib.ports = ports; R.mib.phichDur = d.phichDur; R.mib.phichRes = d.phichRes;
                R.mib.sfn = sfnAt0; R.mib.frameStart = R.found.frameStart;
                p.done = true;
                D2.pci = c.pci; D2.ext = c.extCp; D2.nRb = c.nRb; D2.ports = ports; D2.phichDur = c.phichDur; D2.phichRes = c.phichRes;
                D2.fs0 = p.fs0; D2.startIn = R.found.frameStart; D2.cfo = cfo; D2.sfnAt0 = sfnAt0; D2.slope = p.tr.slope;
                if (p.outIdx >= 0) { D2.outIdx = p.outIdx; out[(size_t)p.outIdx] = std::move(R); }
                else if ((int)out.size() < o.maxCells + 4) { D2.outIdx = (int)out.size(); out.push_back(std::move(R)); }
                if (D2.outIdx >= 0 && decoded.size() < decoded.capacity()) decoded.push_back(D2);
                break;
            }
        }
        // the decoded cells still without their SIB1, again with every cell decoded since cancelled (each SIB1 read takes its PDSCH out of the
        // others' way)
        for (int round = 0; round < 3 && o.wantSi; round++) {
            bool changed = false;
            for (size_t di = 0; di < decoded.size(); di++) {
                Decoded& d = decoded[di];
                if (d.outIdx < 0 || d.outIdx >= (int)out.size() || hasSib1(out[(size_t)d.outIdx])) continue;
                const double halfBw = d.nRb * 90e3 + 30e3;
                if (out[(size_t)d.outIdx].found.tdd || std::fabs(offsetHz) + halfBw >= fs / 2) continue;
                std::vector<std::pair<const Decoded*, int>> under;
                sameTiming(d.fs0, d.cfo, d.ext, d.pci, under);
                if (under.empty()) continue;
                LteCellResult R = out[(size_t)d.outIdx];
                R.si.clear();
                const LteCellCfg c = cfgOf(d, d.nRb);
                const LteDciSi* hint = nullptr;
                for (const auto& u : under) if (u.first->haveSib1 && u.first->nRb == d.nRb) { hint = &u.first->sib1; break; }
                siStage(R, c, d.startIn, under[0].first->cfo, d.slope / kHalf, d.sfnAt0, 2, under, hint, &d);
                if (dbg) fprintf(stderr, "[cell]   PCI %d SI again under %zu cells: %zu SI\n", d.pci, under.size(), R.si.size());
                if (!hasSib1(R)) continue;
                out[(size_t)d.outIdx] = std::move(R);
                changed = true;
            }
            if (!changed) break;
        }
        if (progress() == before) break;
    }
    return out;
}


std::vector<LtePssProbe> ltePssProbe(const cf32* x, size_t n, double fs, const std::vector<double>& offsetsHz, double maxCfoHz, float minDb) {
    std::vector<LtePssProbe> out;
    if (!x || fs < kSyncRate * 0.99 || offsetsHz.empty()) return out;
    const size_t nSync = (size_t)(4 * kHalf + kNfft6 + 16);
    const size_t len = (size_t)std::ceil((double)nSync * fs / kSyncRate) + 64;
    if (n < len) return out;
    // one FFT of the 20 ms; each offset takes its own slice of bins (a brick-wall filter of +-0.7 MHz), back to the time domain at a
    // low power-of-two fraction of the rate, then to 1.92 Msps with a short filter. Far cheaper than filtering at the full rate per offset.
    int N = 1; while ((size_t)N < len) N <<= 1;
    int M = 1; while (fs * M / N < 2.4e6) M <<= 1;
    if (M > N) M = N;
    const double fsM = fs * M / N, binHz = fs / N;
    Fft big(N), small(M);
    std::vector<cf32> X((size_t)N, cf32(0, 0));
    for (size_t i = 0; i < len; i++) X[i] = x[i];
    big.forward(X.data());
    const int half = (int)std::floor(0.7e6 / binHz);
    std::vector<cf32> Y((size_t)M);
    static const bool dbg = getenv("CELL_DEBUG") != nullptr;
    for (double off : offsetsHz) {
        const long c = std::lround(off / binHz);   // the slice centre; the rest of the offset is left as a carrier offset
        std::fill(Y.begin(), Y.end(), cf32(0, 0));
        for (int k = -half; k <= half && k < M / 2; k++) {
            const long src = ((c + k) % N + N) % N;
            Y[(size_t)((k + M) % M)] = X[(size_t)src] / (float)N;
        }
        small.inverse(Y.data());
        const double rest = off - c * binHz;
        const std::vector<cf32> y = resampleBlock(Y.data(), (size_t)M, fsM, kSyncRate, rest, 1.4e6, 0, nSync);
        const std::vector<Cand> cd = pssSearch(y, maxCfoHz, minDb, 4, 1);
        if (dbg && !cd.empty()) fprintf(stderr, "[cell] probe %.0f: %.1f dB\n", off, cd[0].db);
        if (!cd.empty()) out.push_back({off, cd[0].db});
    }
    return out;
}

} // namespace dect2::cell
