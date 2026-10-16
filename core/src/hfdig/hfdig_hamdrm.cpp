// HamDRM / EasyPal decoder (hfdig_hamdrm.h). The 8 kHz audio goes to 12 kHz and to complex baseband around 1600 Hz; the guard interval
// correlation finds the mode, the symbol timing and the fine frequency; the reference symbol finds the frame and the coarse frequency.
// Each frame: the channel from the reference symbol, corrected per symbol by the scattered pilots, the frame information, then the data
// cells through the DRM multilevel decoder, and the data group (CRC-16) into the file it belongs to.
#include "dect2/hfdig/hfdig_hamdrm.h"
#include "dect2/drm/drm_fft.h"
#include "hfdig_hamdrm_int.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>

namespace dect2 {

using namespace hamdrm;

const char* hamdrmModeName(int m) { return m == 0 ? "A" : m == 1 ? "B" : m == 2 ? "E" : "?"; }
double hamdrmOccupancyKhz(int o) { return o == 0 ? 2.3 : 2.5; }
int hamdrmQam(int q) { return q == 0 ? 4 : q == 1 ? 16 : 64; }

namespace {

constexpr int kSearchSymbols = 8;        // symbols the guard interval correlation adds up
constexpr float kSearchMin = 0.45f;      // correlation that counts as a signal (noise: about 0.15)
constexpr float kRefMin = 0.6f;          // the reference symbol found (data symbols: about 0.15)
constexpr float kRefKeep = 0.35f;        // ... while locked
constexpr int kLpTaps = 127;

struct FileRx {
    uint16_t tid = 0;
    bool haveHeader = false;
    Header hdr;
    int last = -1;
    std::vector<std::vector<uint8_t>> segs;
    std::vector<bool> have;
    int haveN = 0;
    int transmissions = 0;
    uint64_t heardIn = 0;                // the lock it was last heard in
    bool done = false;
    uint64_t order = 0;
};

} // namespace

struct HfdigHamdrm::Impl {
    std::function<void(const std::string&)> log;
    HfdigHamdrmTelemetry tel;
    // front: 8 -> 12 kHz, down to complex baseband, low-pass
    Resampler up{3, 2, 8000.0, 3500.0};
    std::vector<float> a12;
    std::vector<float> lp;
    std::vector<cf32> lpHist;
    int lpPos = 0;
    int64_t nMix = 0;
    // baseband
    std::vector<cf32> bb;
    int64_t bbBase = 0;                  // absolute index of bb[0]
    // the signal
    int state = 0;                       // 0 searching, 1 frame sync, 2 locked
    int mode = -1;
    Geometry g;                          // of the mode, 2.5 kHz carriers (the widest)
    drm::DrmFft fft[kModes] = {drm::DrmFft(geometry(0, 1).tu), drm::DrmFft(geometry(1, 1).tu), drm::DrmFft(geometry(2, 1).tu)};
    int64_t symStart = 0;                // absolute index of the next symbol (start of its guard interval)
    double cfo = 0;                      // of the signal centre against 1600 Hz
    int symIdx = 0, syncTries = 0, misses = 0;
    std::vector<std::vector<cf32>> frame;
    cf32 cpAcc[5];                       // guard interval correlation at -2 .. +2 samples over the frame
    uint64_t lockNo = 0;
    bool heardThisLock = false;
    std::map<int, std::shared_ptr<const Layout>> layouts;
    float snrAcc = 0;
    // files
    std::map<uint16_t, FileRx> files;
    uint16_t cur = 0;
    bool haveCur = false;
    uint64_t order = 0, nextFileId = 1;

    Impl() {
        const auto h = hfdig::lowpass(kLpTaps, 1450.0 / kRate, 7.0);
        lp.assign(h.begin(), h.end());
        resetSignal();
    }

    void resetSignal() {
        up.reset();
        a12.clear();
        lpHist.assign(2 * kLpTaps, cf32(0, 0));
        lpPos = 0; nMix = 0;
        bb.clear(); bbBase = 0;
        state = 0; mode = -1; misses = 0;
    }

    void say(const std::string& s) { if (log) log("HamDRM: " + s); }

    const Layout& layoutOf(int m, int occ, int qam, int prot) {
        const int key = ((m * 2 + occ) * 3 + qam) * 2 + prot;
        auto it = layouts.find(key);
        if (it == layouts.end()) it = layouts.emplace(key, layout(m, occ, qam, prot)).first;
        return *it->second;
    }

    void feed(const float* x, size_t n) {
        tel.audioSamples += n;
        a12.clear();
        up.process(x, n, a12);
        const double w = 2 * hfdig::kPi * kCentreHz / kRate;
        for (float v : a12) {
            const double ph = std::fmod(w * (double)(nMix++), 2 * hfdig::kPi);
            const cf32 m((float)(v * std::cos(ph)), (float)(-v * std::sin(ph)));
            lpHist[(size_t)lpPos] = m; lpHist[(size_t)(lpPos + kLpTaps)] = m;
            if (++lpPos >= kLpTaps) lpPos = 0;
            const cf32* hv = &lpHist[(size_t)lpPos];
            cf32 s(0, 0);
            for (int k = 0; k < kLpTaps; k++) s += hv[k] * lp[(size_t)(kLpTaps - 1 - k)];
            bb.push_back(s * 2.f);
        }
        run();
        // keep the buffer short
        const int64_t keep = state == 0 ? 0 : symStart - bbBase - 400;
        if (keep > 8192) { bb.erase(bb.begin(), bb.begin() + (long)keep); bbBase += keep; }
    }

    cf32 at(int64_t a) const { return bb[(size_t)(a - bbBase)]; }
    int64_t bbEnd() const { return bbBase + (int64_t)bb.size(); }

    // the guard interval correlation of the symbol starting at a
    cf32 cpCorr(int64_t a, const Geometry& gg, float* energy = nullptr) const {
        cf32 s(0, 0);
        float e = 0;
        for (int i = 0; i < gg.tg; i++) {
            const cf32 p = at(a + i), q = at(a + i + gg.tu);
            s += p * std::conj(q);
            e += 0.5f * (std::norm(p) + std::norm(q));
        }
        if (energy) *energy = e;
        return s;
    }

    // false when there is not enough signal buffered to look
    bool search() {
        // need kSearchSymbols + 1 of the longest symbol
        int maxL = 0;
        for (int m = 0; m < kModes; m++) maxL = std::max(maxL, geometry(m, 1).len());
        const int64_t need = (int64_t)(kSearchSymbols + 1) * maxL + 8;
        if ((int64_t)bb.size() < need) return false;
        float best = 0;
        int bm = -1, bd = 0;
        cf32 bs(0, 0);
        std::vector<cf32> p;
        std::vector<float> e;
        for (int m = 0; m < kModes; m++) {
            const Geometry gg = geometry(m, 1);
            const int Lm = gg.len(), span = kSearchSymbols * Lm + gg.tg;
            p.assign((size_t)span + 1, cf32(0, 0)); e.assign((size_t)span + 1, 0.f);
            for (int i = 0; i < span; i++) {   // prefix sums
                const cf32 a = bb[(size_t)i], b = bb[(size_t)(i + gg.tu)];
                p[(size_t)i + 1] = p[(size_t)i] + a * std::conj(b);
                e[(size_t)i + 1] = e[(size_t)i] + 0.5f * (std::norm(a) + std::norm(b));
            }
            for (int d = 0; d < Lm; d++) {
                cf32 s(0, 0);
                float en = 0;
                for (int k = 0; k + 1 < kSearchSymbols; k++) {
                    const int a = d + k * Lm;
                    s += p[(size_t)(a + gg.tg)] - p[(size_t)a];
                    en += e[(size_t)(a + gg.tg)] - e[(size_t)a];
                }
                if (en <= 1e-12f) continue;
                const float c = std::abs(s) / en;
                if (c > best) { best = c; bm = m; bd = d; bs = s; }
            }
        }
        if (bm >= 0 && best > kSearchMin) {
            mode = bm;
            g = geometry(bm, 1);
            symStart = bbBase + bd;
            cfo = -std::arg(bs) * kRate / (2 * hfdig::kPi * g.tu);
            state = 1; syncTries = 0;
            frame.assign((size_t)g.ns, std::vector<cf32>());
            return true;
        }
        const int64_t drop = 4 * maxL;
        bb.erase(bb.begin(), bb.begin() + (long)drop);
        bbBase += drop;
        return true;
    }

    // the carriers -h .. h (2.5 kHz set) of the symbol starting at a, the frequency error taken out
    void symbol(int64_t a, std::vector<cf32>& Y) {
        const int back = g.tg / 4;
        const int64_t w0 = a + g.tg - back;
        std::vector<cf32> x((size_t)g.tu);
        const double dw = -2 * hfdig::kPi * cfo / kRate;
        double ph = std::fmod(dw * (double)w0, 2 * hfdig::kPi);
        cf32 rot((float)std::cos(ph), (float)std::sin(ph));
        const cf32 step((float)std::cos(dw), (float)std::sin(dw));
        for (int i = 0; i < g.tu; i++) { x[(size_t)i] = at(w0 + i) * rot; rot *= step; }
        fft[mode].forward(x.data());
        const int W = g.kmax - g.kmin + 1;
        Y.resize((size_t)W);
        for (int k = g.kmin; k <= g.kmax; k++) {
            const double sp = 2 * hfdig::kPi * k * back / g.tu;   // the window starts `back` samples early: undo its phase slope
            Y[(size_t)(k - g.kmin)] = x[(size_t)((k + g.tu) % g.tu)] * cf32((float)std::cos(sp), (float)std::sin(sp));
        }
    }

    // how well the symbol matches the reference symbol moved by q carriers (differential: a timing slope does not matter)
    float refCorr(const std::vector<cf32>& Y, int q) const {
        cf32 s(0, 0);
        float e = 0;
        for (int k = g.fmin; k < g.fmax; k++) {
            const int i0 = k + q - g.kmin, i1 = i0 + 1;
            if (i0 < 0 || i1 >= (int)Y.size()) continue;
            const cf32 d = Y[(size_t)i0] * std::conj(Y[(size_t)i1]);
            s += d * std::conj(refCell(k) * std::conj(refCell(k + 1)));
            e += std::abs(d);
        }
        return e > 1e-20f ? std::abs(s) / (e * 1.0f) : 0.f;
    }

    void lost() {
        if (heardThisLock && haveCur) {
            auto it = files.find(cur);
            if (it != files.end() && !it->second.done) {
                FileRx& f = it->second;
                if (f.last >= 0 && f.haveN == f.last + 1 && !f.haveHeader) finish(f);   // the header never came: the body alone
                else {
                    std::string miss;
                    int listed = 0;
                    for (int i = 0; i <= f.last; i++) if (!f.have[(size_t)i]) { if (listed++ < 24) miss += (miss.empty() ? "" : ", ") + std::to_string(i); }
                    if (listed > 24) miss += ", ...";
                    char b[200];
                    snprintf(b, sizeof b, "transmission ended, %s: %d of %s segments", f.haveHeader && !f.hdr.name.empty() ? f.hdr.name.c_str() : "file",
                             f.haveN, f.last >= 0 ? std::to_string(f.last + 1).c_str() : "?");
                    say(std::string(b) + (miss.empty() ? "" : ", missing " + miss) + " (a repeat fills them in)");
                }
            }
        }
        state = 0; mode = -1;
        tel.state = heardThisLock ? 2 : tel.state == 2 ? 2 : 0;
        heardThisLock = false;
    }

    void run() {
        for (;;) {
            if (state == 0) {
                if (!search()) return;
                if (state == 0) continue;
            }
            const int L = g.len();
            if (symStart + L + 4 > bbEnd() || symStart - 4 < bbBase) {
                if (symStart - 4 < bbBase) symStart += L;   // only at the very start of the buffer
                else return;
                continue;
            }
            std::vector<cf32> Y;
            if (state == 1) {
                symbol(symStart, Y);
                float best = 0;
                int bq = 0;
                for (int q = -3; q <= 3; q++) { const float c = refCorr(Y, q); if (c > best) { best = c; bq = q; } }
                if (best > kRefMin) {
                    cfo += bq * g.spacing;
                    symbol(symStart, Y);
                    state = 2; symIdx = 0; misses = 0;
                    frame[0] = Y;
                    for (cf32& c : cpAcc) c = cf32(0, 0);
                    lockNo++;
                    tel.state = 1;
                    tel.mode = mode;
                } else if (++syncTries > 2 * g.ns + 4) {
                    state = 0;   // the search starts again from here
                    const int64_t drop = std::min<int64_t>((int64_t)bb.size(), std::max<int64_t>(0, symStart - bbBase));
                    bb.erase(bb.begin(), bb.begin() + (long)drop); bbBase += drop;
                    continue;
                }
                symStart += L;
                continue;
            }
            // locked
            if (symIdx + 1 == g.ns) {
                // the next symbol should be the reference symbol: first the timing and the frequency from the frame just done
                int bd = 2;
                for (int d = 0; d < 5; d++) if (std::abs(cpAcc[d]) > std::abs(cpAcc[bd]) * 1.05f) bd = d;
                const double meas = -std::arg(cpAcc[2]) * kRate / (2 * hfdig::kPi * g.tu);
                double r = meas - cfo;
                const double sp = g.spacing;
                r -= sp * std::round(r / sp);
                if (std::abs(cpAcc[2]) > 0) cfo += 0.5 * r;
                if (bd != 2) symStart += bd < 2 ? -1 : 1;
                for (cf32& c : cpAcc) c = cf32(0, 0);
                if (symStart + L + 4 > bbEnd()) return;
                symbol(symStart, Y);
                const float c = refCorr(Y, 0);
                if (c < kRefKeep) {
                    if (++misses >= 2) { lost(); continue; }
                } else misses = 0;
                symIdx = 0;
                frame[0] = Y;
            } else {
                symbol(symStart, Y);
                symIdx++;
                frame[(size_t)symIdx] = Y;
                for (int d = 0; d < 5; d++) cpAcc[d] += cpCorr(symStart + d - 2, g);
                if (symIdx + 1 == g.ns) decodeFrame();
            }
            symStart += L;
        }
    }

    // the channel of symbol s on carriers lo .. hi: the reference symbol's estimate times the pilot correction
    void channel(const std::vector<cf32>& H, int s, int lo, int hi, std::vector<cf32>& out) {
        std::vector<int> pk;
        std::vector<cf32> pg;
        const std::vector<cf32>& Y = frame[(size_t)s];
        for (int k = lo; k <= hi; k++) {
            if (!isPilot(s, k)) continue;
            const cf32 ref = H[(size_t)(k - g.kmin)] * pilotValue(s, k);
            const float n = std::norm(ref);
            pk.push_back(k);
            pg.push_back(n > 1e-20f ? Y[(size_t)(k - g.kmin)] * std::conj(ref) / n : cf32(1, 0));
        }
        out.assign((size_t)(g.kmax - g.kmin + 1), cf32(0, 0));
        for (int k = lo; k <= hi; k++) {
            cf32 c(1, 0);
            if (!pk.empty()) {
                size_t j = 0;
                while (j < pk.size() && pk[j] < k) j++;
                if (j == 0) c = pg[0];
                else if (j == pk.size()) c = pg.back();
                else { const float t = (float)(k - pk[j - 1]) / (float)(pk[j] - pk[j - 1]); c = pg[j - 1] * (1 - t) + pg[j] * t; }
            }
            out[(size_t)(k - g.kmin)] = H[(size_t)(k - g.kmin)] * c;
        }
    }

    void decodeFrame() {
        const int W = g.kmax - g.kmin + 1;
        // the reference symbol's channel, smoothed over three carriers
        std::vector<cf32> H0((size_t)W), H((size_t)W);
        for (int k = g.kmin; k <= g.kmax; k++) H0[(size_t)(k - g.kmin)] = frame[0][(size_t)(k - g.kmin)] * std::conj(refCell(k));
        for (int i = 0; i < W; i++) {
            cf32 s = H0[(size_t)i] * 2.f;
            float w = 2;
            if (i > 0) { s += H0[(size_t)i - 1]; w += 1; }
            if (i + 1 < W) { s += H0[(size_t)i + 1]; w += 1; }
            H[(size_t)i] = s / w;
        }
        // the frame information (in the 2.3 kHz carriers)
        const Layout& L0 = layoutOf(mode, 0, 0, 0);
        std::vector<cf32> he;
        std::vector<cf32> fz(kFacCells);
        std::vector<float> fw(kFacCells);
        int lastS = -1;
        std::vector<cf32> hs;
        for (int i = 0; i < kFacCells; i++) {
            const int s = L0.fac[(size_t)i].first, k = L0.fac[(size_t)i].second;
            if (s != lastS) { channel(H, s, g.fmin, g.fmax, hs); lastS = s; }
            const cf32 h = hs[(size_t)(k - g.kmin)];
            const float n = std::norm(h);
            fz[(size_t)i] = n > 1e-20f ? frame[(size_t)s][(size_t)(k - g.kmin)] * std::conj(h) / n : cf32(0, 0);
            fw[(size_t)i] = n;
        }
        normalise(fw);
        uint8_t fb[32];
        facCode().decode(fz.data(), fw.data(), fb, 1);
        Fac f;
        if (!facParse(fb, f) || f.mode != mode) { tel.framesBad++; return; }
        // the data cells
        const Layout& Lc = layoutOf(f.mode, f.occ, f.qam, f.prot);
        const Geometry& gc = Lc.g;
        const int N = (int)Lc.msc.size();
        std::vector<cf32> z((size_t)N);
        std::vector<float> w((size_t)N);
        const auto& perm = cellPerm(N);
        lastS = -1;
        for (int i = 0; i < N; i++) {
            const int s = Lc.msc[(size_t)i].first, k = Lc.msc[(size_t)i].second;
            if (s != lastS) { channel(H, s, gc.kmin, gc.kmax, hs); lastS = s; }
            const cf32 h = hs[(size_t)(k - g.kmin)];
            const float n = std::norm(h);
            z[(size_t)perm[(size_t)i]] = n > 1e-20f ? frame[(size_t)s][(size_t)(k - g.kmin)] * std::conj(h) / n : cf32(0, 0);
            w[(size_t)perm[(size_t)i]] = n;
        }
        normalise(w);
        // signal to noise from the distance to the nearest point
        {
            const int lv = qamLevels(f.qam);
            double e = 0;
            uint8_t bits[6];
            for (int i = 0; i < N; i++) { drm::qamDemapHard(lv, z[(size_t)i], bits); e += std::norm(z[(size_t)i] - drm::qamMap(lv, bits)); }
            e /= std::max(1, N);
            const float snr = (float)(10 * std::log10(1.0 / std::max(e, 1e-6)));
            snrAcc = tel.framesOk + tel.framesBad == 0 ? snr : 0.7f * snrAcc + 0.3f * snr;
            tel.snrDb = snrAcc;
        }
        std::vector<uint8_t> u((size_t)Lc.code->infoBits());
        Lc.code->decode(z.data(), w.data(), u.data(), f.qam > 0 ? 2 : 1);
        scramble(u.data(), u.size());
        std::vector<uint8_t> bytes((size_t)Lc.payload, 0);
        for (size_t i = 0; i < bytes.size() * 8; i++) bytes[i / 8] |= (uint8_t)(u[i] << (7 - i % 8));
        tel.mode = f.mode; tel.occ = f.occ; tel.qam = f.qam; tel.prot = f.prot;
        tel.offsetHz = cfo;
        Group grp;
        if (!parseGroup(bytes, grp)) { tel.framesBad++; return; }
        tel.framesOk++;
        handle(grp);
    }

    static void normalise(std::vector<float>& w) {
        double m = 0;
        for (float v : w) m += v;
        m /= std::max<size_t>(1, w.size());
        if (m > 0) for (float& v : w) v = (float)(v / m);
    }

    void handle(const Group& grp) {
        if (grp.type != kGroupHeaderSeg && grp.type != kGroupBody) return;
        auto it = files.find(grp.tid);
        if (it == files.end()) {
            if (files.size() >= 8) {   // the oldest goes
                auto old = files.begin();
                for (auto j = files.begin(); j != files.end(); ++j) if (j->second.order < old->second.order) old = j;
                files.erase(old);
            }
            it = files.emplace(grp.tid, FileRx()).first;
            it->second.tid = grp.tid;
        }
        FileRx& f = it->second;
        f.order = ++order;
        cur = grp.tid; haveCur = true;
        heardThisLock = true;
        if (f.heardIn != lockNo) { f.heardIn = lockNo; f.transmissions++; }
        if (f.done) return;
        if (grp.type == kGroupHeaderSeg) {
            Header h;
            if (parseHeader(grp.data, h)) {
                if (!f.haveHeader) say("receiving " + (h.name.empty() ? std::string("a file") : h.name) + (h.callsign.empty() ? "" : " from " + h.callsign) +
                                       ", " + std::to_string(h.bodySize) + " bytes");
                f.hdr = h; f.haveHeader = true;
            }
        } else {
            if (f.last < 0) { f.last = grp.last; f.segs.assign((size_t)f.last + 1, {}); f.have.assign((size_t)f.last + 1, false); }
            if (grp.last == f.last && grp.seg <= f.last && !f.have[(size_t)grp.seg]) {
                f.segs[(size_t)grp.seg] = grp.data;
                f.have[(size_t)grp.seg] = true;
                f.haveN++;
            }
        }
        if (f.haveHeader && f.last >= 0 && f.haveN == f.last + 1) finish(f);
    }

    void finish(FileRx& f) {
        auto out = std::make_shared<HamdrmFile>();
        for (const auto& s : f.segs) out->data.insert(out->data.end(), s.begin(), s.end());
        if (f.haveHeader && f.hdr.bodySize > 0 && f.hdr.bodySize < out->data.size()) out->data.resize(f.hdr.bodySize);
        out->id = nextFileId++;
        out->name = f.hdr.name;
        out->callsign = f.hdr.callsign;
        out->mime = f.haveHeader ? f.hdr.mime : (out->data.size() > 3 && out->data[0] == 0x89 ? "image/png" : "image/jpeg");
        out->unixTime = (int64_t)time(nullptr);
        out->transmissions = f.transmissions;
        f.done = true;
        tel.history.insert(tel.history.begin(), out);
        if (tel.history.size() > (size_t)kHamdrmHistory) tel.history.resize((size_t)kHamdrmHistory);
        tel.filesDone++;
        char b[160];
        snprintf(b, sizeof b, "received %s%s%s, %zu bytes, after %d transmission%s", out->name.empty() ? "a file" : out->name.c_str(),
                 out->callsign.empty() ? "" : " from ", out->callsign.c_str(), out->data.size(), out->transmissions, out->transmissions == 1 ? "" : "s");
        say(b);
    }

    void fill(HfdigHamdrmTelemetry& o) const {
        o = tel;
        o.haveFile = false;
        o.missing.clear();
        if (!haveCur) return;
        auto it = files.find(cur);
        if (it == files.end()) return;
        const FileRx& f = it->second;
        o.haveFile = true;
        o.transportId = f.tid;
        o.name = f.hdr.name; o.callsign = f.hdr.callsign;
        o.bodySize = (int64_t)f.hdr.bodySize;
        o.segTotal = f.last + 1;
        o.segHave = f.haveN;
        o.transmissions = f.transmissions;
        o.fileDone = f.done;
        for (int i = 0; i <= f.last && (int)o.missing.size() < kHamdrmMaxMissing; i++) if (!f.have[(size_t)i]) o.missing.push_back(i);
    }
};

HfdigHamdrm::HfdigHamdrm() : p_(std::make_unique<Impl>()) {}
HfdigHamdrm::~HfdigHamdrm() = default;
void HfdigHamdrm::reset() {
    p_->resetSignal();
    p_->tel.audioSamples = 0;
    p_->tel.state = 0;
    p_->heardThisLock = false;
}
void HfdigHamdrm::feedAudio(const float* x, size_t n) { p_->feed(x, n); }
void HfdigHamdrm::telemetry(HfdigHamdrmTelemetry& out) const { p_->fill(out); }
void HfdigHamdrm::setLogCallback(std::function<void(const std::string&)> cb) { p_->log = std::move(cb); }
void HfdigHamdrm::clearFiles() { p_->files.clear(); p_->haveCur = false; }
void HfdigHamdrm::setHistory(std::vector<std::shared_ptr<const HamdrmFile>> h) {
    for (const auto& f : h) if (f) p_->nextFileId = std::max(p_->nextFileId, f->id + 1);
    if (h.size() > (size_t)kHamdrmHistory) h.resize((size_t)kHamdrmHistory);
    p_->tel.history = std::move(h);
}

std::unique_ptr<HfdigHamdrm> makeHamdrmDecoder() { return std::make_unique<HfdigHamdrm>(); }

} // namespace dect2
