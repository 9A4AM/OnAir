// Analog TV picture decoder: sync detection, the line phase-locked loop and the per-line processing.
#include "atv_video_impl.h"

namespace dect2 {

void AtvVideo::Impl::process(const float* v, const float* i, const float* q, size_t n) {
    const double inv1 = 1.0 / L1, inv2 = 1.0 / L2;
    constexpr size_t kPiece = 1024;                        // samples a piece: the rings run at most this far ahead of the pulse search
    for (size_t k0 = 0; k0 < n; k0 += kPiece) {
        const size_t m = std::min(kPiece, n - k0);
        const uint64_t n0 = nAbs;
        // the rings
        {
            size_t done = 0;
            while (done < m) {
                const size_t idx = (size_t)((n0 + done) & kMask), c = std::min(m - done, kRing - idx);
                if (pol > 0) {
                    std::memcpy(&rv[idx], v + k0 + done, c * sizeof(float));
                    std::memcpy(&ri[idx], i + k0 + done, c * sizeof(float));
                    std::memcpy(&rq[idx], q + k0 + done, c * sizeof(float));
                } else {
                    // positive modulation: the decoder works on the negated signal, so that the sync tip is the largest value here too
                    for (size_t t = 0; t < c; t++) { rv[idx + t] = -v[k0 + done + t]; ri[idx + t] = -i[k0 + done + t]; rq[idx + t] = -q[k0 + done + t]; }
                }
                done += c;
            }
        }
        // two running means; what leaves the sum is read back from the rings
        {
            double s1 = sum1, s2 = sum2;
            const uint64_t o1 = (uint64_t)L1, o2 = (uint64_t)L2;
            for (size_t j = 0; j < m; j++) {
                const size_t idx = (size_t)((n0 + j) & kMask);
                s1 += rv[idx] - rv[(size_t)((n0 + j - o1) & kMask)];
                const float m1 = (float)(s1 * inv1);
                m1r[idx] = m1;
                s2 += m1 - m1r[(size_t)((n0 + j - o2) & kMask)];
                vsr[idx] = (float)(s2 * inv2);
            }
            sum1 = s1; sum2 = s2;
        }
        // the pulse search. Between pulses the signal is below the threshold and nothing happens until the next line is overdue: such a
        // stretch is skipped at once, with the line-overdue time kept as a limit on the sample count.
        float thrF = (float)thr;
        const uint64_t warm = (uint64_t)(L1 + L2 + 4);
        for (size_t j = 0; j < m;) {
            if (levelsValid && loopRunning && pState == 0 && smoothCount >= warm) {
                // checkMiss does nothing until (nAbs - 1 - dVs) passes tNext + gate + 0.3 spu
                const double lim = tNext + gate() + 0.3 * spu + dVs + 1.0;
                const uint64_t limN = lim < 0 ? 0 : lim >= 1.8e19 ? ~0ull : (uint64_t)std::floor(lim);       // nAbs > lim  <=>  nAbs > limN
                size_t jj = j;
                uint64_t na = nAbs;
                for (; jj < m; jj++) {
                    if (vsr[(size_t)((n0 + jj) & kMask)] >= thrF || na + 1 > limN) break;
                    na++;
                }
                if (jj > j) {
                    prevVs = vsr[(size_t)((n0 + jj - 1) & kMask)];
                    smoothCount += (uint64_t)(jj - j);
                    nAbs = na;
                    j = jj;
                    if (j >= m) break;
                }
            }
            const float vs = vsr[(size_t)((n0 + j) & kMask)];
            j++;
            nAbs++;
            smoothCount++;
            if (smoothCount < warm) { prevVs = vs; continue; }
            if (!levelsValid) {
                if (acq.empty()) acqStart = nAbs - 1;
                acq.push_back(vs);
                if (acq.size() >= acqNeed) { if (!acquire()) { acq.erase(acq.begin(), acq.begin() + (long)(acq.size() / 2)); acqStart += (uint64_t)acqNeed / 2; } }
                prevVs = vs;
                thrF = (float)thr;
                continue;
            }
            if (pState != 0 || vs >= thrF) detect(vs);          // between pulses the signal is below the threshold: nothing to do
            prevVs = vs;
            if (loopRunning) checkMiss();
        }
    }
    processLines();
}

void AtvVideo::Impl::detect(float vs) {
    const float up = (float)(thr + hyst), dn = (float)(thr - hyst);
    const float th = (float)thr;
    switch (pState) {
    case 0:
        if (prevVs < th && vs >= th) { pT0 = crossT(prevVs, vs, th); pState = 1; }
        break;
    case 1:
        if (vs >= up) pState = 2;
        else if (vs < dn) pState = 0;
        else if (prevVs < th && vs >= th) pT0 = crossT(prevVs, vs, th);
        break;
    case 2:
        if (prevVs >= th && vs < th) { pT1 = crossT(prevVs, vs, th); pState = 3; }
        break;
    case 3:
        if (vs <= dn) { pState = 0; onPulse(pT0, pT1); }
        else if (vs >= up) pState = 2;
        else if (prevVs >= th && vs < th) pT1 = crossT(prevVs, vs, th);
        break;
    }
}

void AtvVideo::Impl::normalPulse(double t0, double t1) {
    if (!loopRunning) return;
    const double e = t0 - tNext;
    if (std::fabs(e) <= gate()) {
        const double alpha = goodRun < 8 ? 0.4 : 0.12, beta = goodRun < 8 ? 0.02 : 0.004;
        const double tg = tNext + alpha * e;
        P += beta * e;
        P = std::min(std::max(P, raster(rast).pMin * 1e-6 * fv), raster(rast).pMax * 1e-6 * fv);
        Pslow += (P - Pslow) * (1.0 / 2048);
        lineQ.push_back({tg, true, ++lineNo});
        tNext = tg + P;
        goodRun++; missRun = 0; oogRun = 0;
        syncQ += (1.f - syncQ) / 32.f;
        lastGoodT0 = t0; (void)t1;
    } else {
        // a pulse where none was expected: after a longer stretch of those the line timing has jumped
        if (++oogRun >= 10) {
            tNext = t0 + P; oogRun = 0; goodRun = 0;
        }
    }
}

void AtvVideo::Impl::vsync(double t_v) {
    // where does it fall on the line grid: on a line sync (first field) or half-way (second field)?
    const double d = (t_v - tNext) / P;
    const double fr = d - std::floor(d + 0.5);
    const int field = std::fabs(fr) < 0.25 ? 0 : 1;
    // plausibility: about half a frame since the last one, and the other field
    bool ok = true;
    if (haveVsync) {
        const double dt = (t_v - vsyncT) / P;                 // in lines
        const double expect = fmt.lines / 2.0;
        if (std::fabs(dt - expect) > 3.0 && std::fabs(dt - 2 * expect) > 3.0 && std::fabs(dt - expect * 3) > 3.0) ok = vsyncMiss > 6;
    }
    if (!ok) return;
    if (haveVsync) finishField();
    haveVsync = true; vsyncT = t_v; vsyncField = field; vsyncMiss = 0; vsyncCount++;
    curField = field; fieldLines = 0;
    vbiPending = true; vbiT = t_v;
}

bool AtvVideo::Impl::analyse(const std::vector<float>& a, Acq& r) const {
    const size_t n = a.size();
    std::vector<float> sub;
    for (size_t k = 0; k < n; k += 5) sub.push_back(a[k]);
    std::sort(sub.begin(), sub.end());
    const double lo = sub[(size_t)(sub.size() * 0.02)], hi = sub[(size_t)(sub.size() * 0.997)];
    if (hi - lo < 1e-9) return false;
    // the rasters to look for: all of them, or the one of the system the user chose
    int g0 = 0, g1 = 2;
    if (prm.forceSys >= 0) g0 = g1 = rasterOf(atvSysLines(prm.forceSys));
    // the threshold that gives the most pulses of the width of a line sync
    int bestCount[3] = {0, 0, 0};
    std::vector<std::pair<double, int>> counts[3];
    std::vector<Cr> pl;
    // the search over thresholds runs on a copy with every 4th sample (means of four): widths of 4 us stay clear of the limits, and a failed
    // attempt, which is what a weak or no signal gives every 25 ms, costs a quarter
    std::vector<float> dec(n / 4);
    for (size_t k = 0; k < dec.size(); k++) dec[k] = 0.25f * (a[4 * k] + a[4 * k + 1] + a[4 * k + 2] + a[4 * k + 3]);
    for (int s = 1; s <= 28; s++) {
        const double f = 0.03 + 0.02 * s;
        const double th = hi - f * (hi - lo);
        scanArr(dec.data(), dec.size(), th, 0.03 * (hi - lo), pl);
        for (int g = g0; g <= g1; g++) {
            const Raster& R = raster(g);
            int c = 0;
            for (const auto& p : pl) { const double w = 4 * (p.t1 - p.t0) / spu; if (w >= R.wMin && w <= R.wMax) c++; }
            counts[g].push_back({th, c});
            bestCount[g] = std::max(bestCount[g], c);
        }
    }
    const double secs = (double)n / fv;
    // the raster with the largest share of its lines found; more pulses than it has lines means the pulses are something else (the blanking
    // of a faster raster, cut at a low threshold, is as wide as the sync pulse of a slower one)
    int grp = -1;
    double bestFrac = 0;
    for (int g = g0; g <= g1; g++) {
        const double fr = bestCount[g] * raster(g).lineUs * 1e-6 / secs;
        if (fr > bestFrac && fr < 1.06) { bestFrac = fr; grp = g; }
    }
    if (grp < 0) return false;
    const Raster& R = raster(grp);
    const double lineRate = 1e6 / R.lineUs;
    if (bestCount[grp] < 0.4 * secs * lineRate * 0.9) return false;
    // the middle of the thresholds that are nearly as good
    std::vector<double> good;
    for (auto& c : counts[grp]) if (c.second >= 0.92 * bestCount[grp]) good.push_back(c.first);
    const double th = good[good.size() / 2];
    scanArr(a.data(), a.size(), th, 0.03 * (hi - lo), pl);
    std::vector<Cr> np;
    for (const auto& p : pl) { const double w = (p.t1 - p.t0) / spu; if (w >= R.wMin && w <= R.wMax) np.push_back(p); }
    // The line period: the neighbouring pulses give it roughly, then every pulse gets the number of its line (from the spacing to the one before)
    // and a straight line through (number, time) is fitted, throwing away the pulses that are not on it. With noise the edges jitter, but
    // hundreds of pulses fix the period to a few ppm.
    std::vector<double> sp;
    for (size_t k = 1; k < np.size(); k++) { const double d = (np[k].t0 - np[k - 1].t0) / spu; if (d > R.spMin && d < R.spMax) sp.push_back(d); }
    if (sp.size() < 40) return false;
    std::sort(sp.begin(), sp.end());
    const double med = sp[sp.size() / 2];
    if (med < R.medMin || med > R.medMax) return false;
    std::vector<double> ks(np.size(), 0.0);
    for (size_t k = 1; k < np.size(); k++) ks[k] = ks[k - 1] + std::round((np[k].t0 - np[k - 1].t0) / (med * spu));
    std::vector<char> in(np.size(), 1);
    double Pm = med * spu, T0 = np[0].t0;
    size_t nin = np.size();
    for (int pass = 0; pass < 5; pass++) {
        double sk = 0, st = 0, skk = 0, skt = 0;
        size_t c = 0;
        for (size_t k = 0; k < np.size(); k++) if (in[k]) { sk += ks[k]; st += np[k].t0; skk += ks[k] * ks[k]; skt += ks[k] * np[k].t0; c++; }
        if (c < 30) return false;
        const double den = (double)c * skk - sk * sk;
        if (den < 1e-9) return false;
        Pm = ((double)c * skt - sk * st) / den;
        T0 = (st - Pm * sk) / (double)c;
        const double tol = (pass < 2 ? 2.5 : 1.2) * spu;
        nin = 0;
        for (size_t k = 0; k < np.size(); k++) { in[k] = std::fabs(np[k].t0 - (T0 + ks[k] * Pm)) < tol; if (in[k]) nin++; }
    }
    if ((double)nin < 0.4 * secs * lineRate || Pm / spu < R.medMin || Pm / spu > R.medMax) return false;
    size_t lastIn = 0;
    for (size_t k = 0; k < np.size(); k++) if (in[k]) lastIn = k;
    r.group = grp;
    r.lines = grp == 1 ? 405 : grp == 2 ? 819 : (std::fabs(Pm / spu - 64.0) < std::fabs(Pm / spu - 63.5556)) ? 625 : 525;
    // levels
    double tipS = 0;
    int tn = 0;
    for (size_t k = 0; k < np.size(); k++) { if (!in[k]) continue; const size_t c = (size_t)(0.5 * (np[k].t0 + np[k].t1)); if (c < a.size()) { tipS += a[c]; tn++; } }
    r.T = tipS / std::max(1, tn); r.B = 2 * th - r.T;
    if (!(r.T > r.B)) return false;
    r.th = th; r.hy = 0.18 * (r.T - r.B);
    r.Pm = Pm; r.lastT = T0 + ks[lastIn] * Pm;
    r.nin = (double)nin; r.expect = secs * lineRate;
    return true;
}

void AtvVideo::Impl::commit(const Acq& r) {
    Tr = r.T; Br = r.B; thr = r.th; hyst = r.hy;
    TrMeas = Tr;
    rast = r.group;
    P = Pslow = r.Pm;
    // the next line start after the data we hold
    const double last = (double)acqStart + r.lastT - dVs;
    double tn0 = last + P;
    while (tn0 < (double)nAbs - 3 * P) tn0 += P;
    while (tn0 - P > (double)nAbs - 3 * P) tn0 -= P;
    tNext = tn0;
    loopRunning = true; levelsValid = true; goodRun = 0; missRun = 0; syncQ = 0.6f;
    lineQ.clear();
    acq.clear();
    pState = 0;
    makeFormat(r.lines);
    if (pol < 0) positiveLevels();
    {
        char b[200];
        snprintf(b, sizeof b, "analog TV: line sync found, %d lines, line period %.3f us, %s modulation", r.lines, P / spu, pol < 0 ? "positive" : "negative");
        doLog(b);
    }
}

// The line sync in the stretch held: with the polarity in use, and when that finds no clear line structure (and the system is not forced),
// with the other one. The polarity that finds more lines wins: a negatively modulated signal turned over has no sync pulses, and the other way.
bool AtvVideo::Impl::acquire() {
    const int fp = forcedPol();
    if (fp != 0 && fp != pol) flipPolarity();
    Acq r1;
    const bool ok1 = analyse(acq, r1);
    if (fp == 0 && (!ok1 || r1.nin < 0.8 * r1.expect)) {
        std::vector<float> neg(acq.size());
        for (size_t k = 0; k < acq.size(); k++) neg[k] = -acq[k];
        Acq r2;
        const bool ok2 = analyse(neg, r2);
        if (ok2 && (!ok1 || r2.nin > 1.15 * r1.nin)) {
            flipPolarity();
            commit(r2);
            return true;
        }
    }
    if (!ok1) return false;
    commit(r1);
    return true;
}

// ------------------------------------------------------------------------------ lines
void AtvVideo::Impl::processLines() {
    // the SECAM decoder looks 5 us past the end of the line (the bell rings)
    const double after = 20 + (colourKind == kAtvSecam ? 5.5 * spu : 0.0);
    while (!lineQ.empty() && lineQ.front().tL + P + after <= (double)nAbs) {
        const LineRec r = lineQ.front();
        lineQ.pop_front();
        if (!fmtKnown) continue;
        processLine(r);
    }
    if (vbiPending && vbiT + 14 * P + 30 <= (double)nAbs && lineGain() > 0) { captureVbi(); vbiPending = false; }
    // a field sync that did not come: after about a field and a half without one the vertical position is lost
    if (haveVsync && loopRunning && fmtKnown && (double)nAbs - vsyncT > (fmt.lines * 0.5 * 1.6) * P) {
        vsyncMiss++; vsyncT += fmt.lines * 0.5 * P; vsyncField ^= 1;     // keep counting: the next field is due
        if (vsyncMiss > 12) haveVsync = false;
    }
}

// FM video (analog FPV): the transmitter is AC coupled, so the long broad pulses of the field sync shift the whole baseline and it creeps back
// over the next lines. The threshold of the slicer follows the levels of the lines whose pulse was found; once the baseline moved by half the
// sync height no pulse is found any more and the levels would stay where they were: the decoder gave up after 40 lines. The levels of a missed
// line are measured where its pulse is due instead, and used when they look like a sync pulse (about the height known).
void AtvVideo::Impl::trackLevelsOnMiss(double tL) {
    const double d0 = Tr - Br;
    if (d0 <= 0) return;
    double tA, tB, bpA, bpB, fpA, fpB;
    porches(tA, tB, bpA, bpB, fpA, fpB);
    const double tip = meanRaw(tL + tA * spu, tL + tB * spu);
    const double bl = blankingAt(tL, bpA, bpB, fpA, fpB);
    const double depth = tip - bl;
    if (depth < 0.6 * d0 || depth > 1.5 * d0) return;
    const double a = 1.0 / 8;
    Tr += (tip - Tr) * a; Br += (bl - Br) * a;
    hyst = std::max(0.1 * (Tr - Br), 1e-9);
    thr = 0.5 * (Tr + Br);
}

void AtvVideo::Impl::processLine(const LineRec& r) {
    const double tL = r.tL;
    curNo = r.no;
    lineCount++;
    const AtvFormat& F = fmt;
    if (!r.hadPulse && fmVideo && levelsValid && loopRunning) trackLevelsOnMiss(tL);
    // ---- levels from this line's sync pulse and porches
    double tip = 0, bl = 0;
    if (r.hadPulse) {
        double tA, tB, bpA, bpB, fpA, fpB;
        porches(tA, tB, bpA, bpB, fpA, fpB);
        tip = meanRaw(tL + tA * spu, tL + tB * spu);
        const double b1m = meanRaw(tL + bpA * spu, tL + bpB * spu);
        bl = blankingAt(tL, bpA, bpB, fpA, fpB);
        // noise on the porches (variance about the mean)
        // (SECAM has the subcarrier on the back porch: the sync tip serves there)
        if (colourKind == kAtvSecam) { bpA = tA; bpB = tB; }
        const double nm = colourKind == kAtvSecam ? tip : b1m;
        double v1 = 0; int c1 = 0;
        for (uint64_t n = (uint64_t)std::ceil(tL + bpA * spu); n <= (uint64_t)std::floor(tL + bpB * spu); n++) { const double d = rvAt(n) - nm; v1 += d * d; c1++; }
        if (c1 > 3) { const double var = v1 / (c1 - 1); noiseVar += (var - noiseVar) * 0.03; if (noiseVar <= 0) noiseVar = var; }
        const double a = 1.0 / 16;
        const bool posMod = pol < 0;
        double& tipEma = posMod ? TrMeas : Tr;     // positive modulation: the measured tip is only reported, the gain comes from blanking
        if (levelsRestart > 0) { tipEma = tip; Br = bl; levelsRestart--; }
        else { tipEma += (tip - tipEma) * a; Br += (bl - Br) * a; }
        if (posMod) positiveLevels();
        else {
            hyst = std::max(0.1 * (Tr - Br), 1e-9);
            thr = 0.5 * (Tr + Br);
        }
        if (Bline == 0) Bline = bl; else Bline += (bl - Bline) * 0.4;
        gainValid = Tr > Br;
        // The carrier phase for the radio side: at the sync tip (negative modulation, the carrier is largest there), or on blanking (positive
        // modulation: there is no carrier at the sync tip). The rings hold the negated signal then: the phase is measured on it turned back.
        double si = 0, sq = 0, vi = 0, vq = 0; int cn = 0;
        auto acc = [&](double ua, double ub) {
            const uint64_t ta = (uint64_t)std::ceil(tL + ua * spu), tb = (uint64_t)std::floor(tL + ub * spu);
            for (uint64_t n = ta; n <= tb; n++) { const float a1 = ri[n & kMask], b1v = rq[n & kMask]; si += a1; sq += b1v; vi += (double)a1 * a1; vq += (double)b1v * b1v; cn++; }
        };
        if (!posMod) acc(tA, tB);
        else {
            // the porches before the colour (burst or SECAM lead-in from 5.6 us) in 625 and 525 lines, all of the back porch in 405 and 819
            const double ea = F.syncUs + 0.3, eb = (F.lines == 625 || F.lines == 525) ? 5.45 : bpB;
            if (eb > ea) acc(ea, eb);
            if (fpB > fpA) acc(fpA, fpB);
        }
        if (cn > 3 && haveCarrierErr()) {
            si /= cn; sq /= cn;
            if (posMod) { si = -si; sq = -sq; }
            const double amp = std::hypot(si, sq);
            const double var = std::max(1e-12, (vi + vq) / cn - si * si - sq * sq);
            const double snr = amp * amp / var;
            const double ph = std::atan2(sq, si);
            // the peak carrier: the sync tip, or (positive) white, from the blanking level
            const double peak = posMod && F.blankPct > 0 ? amp * 100.0 / F.blankPct : amp;
            carrierAmp += (peak - carrierAmp) * 0.05;
            cnVar = cnVar > 0 ? cnVar + (var - cnVar) * 0.03 : var;
            if (syncQ > 0.5f) doCarrierErr(ph, std::min(1.0, snr / 12.0));
            phErr2 += (ph * ph - phErr2) * 0.03;
            if (phErr2 < 0.12) { if (phGood < 1000) phGood++; } else phGood = 0;
            phaseLockedFlag = phGood > 60;
        }
    }
    // ---- which picture line is this?
    int row = -1, field = 0;
    bool visible = false;
    if (haveVsync) {
        const double h = (tL - vsyncT) * 2.0 / P;
        const long hr = std::lround(h);
        const int off = F.visibleStartH(vsyncField) - F.broadStart[vsyncField];
        if (hr >= off && ((hr - off) & 1) == 0) {
            const long k = (hr - off) / 2;
            if (k < F.fieldRows) { visible = true; row = (int)(2 * k + vsyncField); field = vsyncField; }
        }
    }
    const bool havePicture = visible && r.hadPulse && gainValid;
    // ---- colour: burst and chroma
    const bool wantColour = colourCapable && prm.colourOn;
    bool inVbi = false;                                      // the burst is blanked around the field sync
    if (haveVsync) {
        const double half = F.lines / 2.0;
        double x = std::fmod((tL - vsyncT) / P, half);
        if (x < 0) x += half;
        inVbi = x < 10.5 || x > half - 5.0;
    }
    secamLineOk = false;
    if ((F.lines == 625 || F.lines == 525) && wantColour && r.hadPulse && gainValid && haveVsync && (colourKind < 0 || colourKind == kAtvSecam)) secamLine(tL, havePicture);
    if (wantColour && r.hadPulse && gainValid && !inVbi && (F.lines == 625 || F.lines == 525)) burstAndColour(tL, havePicture);
    if (havePicture) {
        extractPicture(tL, row, field);
        fieldLines++;
    }
    // ---- scope
    if (r.hadPulse && gainValid && (++scopeSkip >= 50) && haveVsync && visible) { scopeSkip = 0; captureLine(tL); }
    // ---- publish counters
    lineHzMeas = fv / Pslow;
}

} // namespace dect2
