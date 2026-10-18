#!/usr/bin/env python3
"""Beat map and chapter plan for the 0.2.7 film on a song you supply (never committed): paranoid_map_027.py <song> <outdir>.
The song is used as it is (no edit). Beats are tracked from the spectral flux (dynamic programming around the tempo found by autocorrelation),
the downbeat phase is chosen so that the band's entry is a downbeat, and the chapters are planned backwards from the last strong hit:
cold open, title, stats, count, 14 groups, the new modes, the features, a wall, a montage, the closing hit. Writes beatmap027.js."""
import sys, json, subprocess, numpy as np
from scipy.ndimage import gaussian_filter1d
song, out = sys.argv[1], sys.argv[2]; FS = 22050; HOP = 512; dt = HOP / FS
x = np.frombuffer(subprocess.run(['ffmpeg', '-loglevel', 'error', '-i', song, '-ac', '1', '-ar', str(FS), '-f', 's16le', '-'], capture_output=True).stdout, np.int16) / 32768
win = np.hanning(1024); S = np.array([np.abs(np.fft.rfft(x[i:i + 1024] * win)) for i in range(0, len(x) - 1024, HOP)])
flux = np.maximum(0, np.diff(np.log1p(S * 10), axis=0)).sum(1); flux = gaussian_filter1d((flux - flux.mean()) / flux.std(), 1.0)
ac = np.correlate(flux - flux.mean(), flux - flux.mean(), 'full')[len(flux) - 1:]; lags = np.arange(len(ac)) * dt; m = (lags > .3) & (lags < .6)
period = lags[m][np.argmax(ac[m])]; P = period / dt; n = len(flux); score = flux.copy(); back = np.full(n, -1)
for i in range(n):
    lo, hi = int(i - P * 1.3), int(i - P * .75)
    if lo < 0: continue
    w = np.arange(lo, hi); pen = -2.0 * np.log((i - w) / P) ** 2; j = np.argmax(score[w] + pen); score[i] = flux[i] + score[w][j] + pen[j]; back[i] = w[j]
i = np.argmax(score[-int(P * 2):]) + n - int(P * 2); bt = []
while i >= 0: bt.append(i); i = back[i]
b = np.array(bt[::-1]) * dt
def onsets(t0, t1, th): return [i * dt for i in range(max(3, int(t0 / dt)), min(len(flux) - 4, int(t1 / dt))) if flux[i] == flux[i - 3:i + 4].max() and flux[i] > th]
rms = lambda a, c: 20 * np.log10(np.sqrt((x[int(a * FS):int(c * FS)] ** 2).mean()) + 1e-6)
quiet = np.median([rms(t, t + .5) for t in np.arange(1, 4, .5)]); band = next(t for t in np.arange(1, 20, .1) if rms(t, t + .4) > quiet + 7)
band = min(onsets(band - .6, band + .6, 1.0) or [band], key=lambda t: abs(t - band)); final = max(onsets(len(x) / FS - 12, len(x) / FS, 2.0))
kb = int(np.argmin(abs(b - band))); ph = kb % 4; down = [float(t) for t in b[ph::4]]
bar = lambda t: int(np.argmin(abs(np.array(down) - t))); B, F = bar(band), bar(final)
plan = dict(band=B, stats=[[0, 0], [1, 1]], statDur=1, stats2=[[B + 3, 3], [B + 4, 4]], title=B + 3)
plan['count'] = B + 5; plan['groups'] = plan['count'] + 2; plan['newStart'] = plan['groups'] + 14; plan['newSlow'] = 18; plan['newFast'] = 2; plan['feat'] = plan['newStart'] + 20
plan['run'] = F - 18; plan['wall'] = F - 16; plan['fin1'] = F - 2; plan['fin2'] = F - 1
tot = plan['run'] - plan['feat']; base, extra = tot // 16, tot % 16; plan['featList'] = [base + 1 if k < extra else base for k in range(16)]
slam = down[plan['wall']]; join = down[F - 8]
bm = dict(beats=[round(float(t), 4) for t in b], down=[round(t, 4) for t in down], band=float(band), stop=float(slam), slam=float(slam), join=float(join), final=float(final), len=len(x) / FS + 1.4, plan=plan)
open(f'{out}/beatmap027.js', 'w').write('window.BM=' + json.dumps(bm) + ';\n'); print('tempo', round(60 / period, 1), 'bpm; band', round(band, 2), 'final', round(final, 2), 'bars to final', F, 'plan', {k: v for k, v in plan.items() if k not in ('stats', 'stats2')})
