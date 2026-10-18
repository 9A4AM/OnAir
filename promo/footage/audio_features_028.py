#!/usr/bin/env python3
"""Audio features for the 0.2.7 film, from a song you supply (never committed): audio_features_028.py <song> <out audio028.js> [fps].
Writes window.AUD = { fps, nb, bands (base64 uint8, frames x bands, log-spaced 50 Hz - 14 kHz, per-band percentile-normalised, fast attack / slow release),
loud (per frame 0-255), kick / snare / hat ([time, strength 0-1] from 5.8 ms band onsets), beats, sections (the song's own boundaries from a beat-synchronous
self-similarity novelty curve, each snapped to the nearest kick or snare within 0.3 s), band (the first full-band entry), final (the last strong hit), len }.
The film draws the song's own spectrum and waterfall from this, and fires its cuts, flashes and shakes on the real kick and snare events."""
import sys, json, base64, subprocess, numpy as np
from scipy.ndimage import gaussian_filter1d
song, out = sys.argv[1], sys.argv[2]; FPS = int(sys.argv[3]) if len(sys.argv) > 3 else 60; SR = 44100; NB = 64
x = np.frombuffer(subprocess.run(['ffmpeg', '-loglevel', 'error', '-i', song, '-ac', '1', '-ar', str(SR), '-f', 's16le', '-'], capture_output=True).stdout, np.int16).astype(np.float32) / 32768
dur = len(x) / SR
# ---- onsets per band at 5.8 ms
N, H = 2048, 256; w = np.hanning(N); fr = np.lib.stride_tricks.sliding_window_view(x, N)[::H]; S = np.abs(np.fft.rfft(fr * w, axis=1)).astype(np.float32); f = np.fft.rfftfreq(N, 1 / SR); dt = H / SR
def onset(a, b):
    e = S[:, (f >= a) & (f < b)].sum(1); l = np.log1p(e * 30); return gaussian_filter1d(np.maximum(0, np.diff(l, prepend=l[0])), 1.0)
def events(o, th, mind):
    z = (o - np.median(o)) / (o.std() + 1e-9); ev = []; last = -9
    for i in range(3, len(z) - 3):
        if z[i] == z[i - 3:i + 4].max() and z[i] > th and i * dt - last > mind: ev.append([round(i * dt, 4), round(float(min(1, .25 + (z[i] - th) / 6)), 3)]); last = i * dt
    return ev
kick, snare, hat = events(onset(40, 140), 2.2, .08), events(onset(1500, 6000), 2.2, .08), events(onset(6000, 14000), 2.6, .08)
# ---- the song's spectrum at the film's frame rate
HF = int(SR / FPS); NF = int(dur * FPS) + 1; Nw = 4096; ww = np.hanning(Nw); fb = np.fft.rfftfreq(Nw, 1 / SR); edges = np.geomspace(50, 14000, NB + 1)
pad = np.concatenate([np.zeros(Nw // 2, np.float32), x, np.zeros(Nw, np.float32)]); B = np.zeros((NF, NB), np.float32)
for i in range(NF):
    seg = pad[i * HF:i * HF + Nw]; sp = np.abs(np.fft.rfft(seg * ww))
    B[i] = [sp[(fb >= edges[k]) & (fb < edges[k + 1])].max() if ((fb >= edges[k]) & (fb < edges[k + 1])).any() else 0 for k in range(NB)]
B = 20 * np.log10(B + 1e-6); lo = np.percentile(B, 8, axis=0); hi = np.percentile(B, 99.7, axis=0); B = np.clip((B - lo) / (hi - lo + 1e-6), 0, 1)
for i in range(1, NF):   # fast attack, slow release
    B[i] = np.maximum(B[i], B[i - 1] * .86)
loud = np.array([np.sqrt((x[int(i / FPS * SR):int((i + 1) / FPS * SR)] ** 2).mean() + 1e-12) for i in range(NF)]); loud = np.clip(loud / np.percentile(loud, 99.5), 0, 1)
# ---- beats (tracked on the flux), the first full-band entry, the last hit
flux = gaussian_filter1d(onset(60, 9000), 1.0); ac = np.correlate(flux - flux.mean(), flux - flux.mean(), 'full')[len(flux) - 1:]; lags = np.arange(len(ac)) * dt; m = (lags > .3) & (lags < .6)
per = lags[m][np.argmax(ac[m])]; P = per / dt; sc = flux.copy(); bk = np.full(len(flux), -1)
for i in range(len(flux)):
    lo_, hi_ = int(i - P * 1.3), int(i - P * .75)
    if lo_ < 0: continue
    ww_ = np.arange(lo_, hi_); pen = -2.0 * np.log((i - ww_) / P) ** 2; j = np.argmax(sc[ww_] + pen); sc[i] = flux[i] + sc[ww_][j] + pen[j]; bk[i] = ww_[j]
i = int(np.argmax(sc[-int(P * 2):])) + len(flux) - int(P * 2); bt = []
while i >= 0: bt.append(i); i = bk[i]
beats = [round(float(t * dt), 4) for t in bt[::-1]]
rms = lambda a, c: 20 * np.log10(np.sqrt((x[int(a * SR):int(c * SR)] ** 2).mean()) + 1e-6)
qv = np.median(loud[int(1 * FPS):int(4 * FPS)]); t0 = next(k for k in range(int(2 * FPS), int(12 * FPS)) if loud[k] > max(3 * qv, .3)) / FPS   # the first frame well above the quiet intro: the full band comes in
allev = sorted(kick + snare + hat); band = min([e for e in allev if abs(e[0] - t0) < .15 and e[1] >= .5], key=lambda e: e[0])[0];   # the earliest strong event of that first hit
final = max(e[0] for e in sorted(kick + snare) if e[0] > dur - 12 and e[1] >= .95)   # the last full-strength hit final = max(e[0] for e in sorted(kick + snare) if e[0] > dur - 12 and e[1] >= .95)
# ---- sections: novelty of a beat-synchronous timbre self-similarity
xs = x[::2]; S2 = np.abs(np.fft.rfft(np.lib.stride_tricks.sliding_window_view(xs, 2048)[::1024] * np.hanning(2048), axis=1)); f2 = np.fft.rfftfreq(2048, 1 / (SR / 2)); dt2 = 1024 / (SR / 2)
ed = np.geomspace(60, 9000, 33); M = np.log1p(np.stack([S2[:, (f2 >= ed[k]) & (f2 < ed[k + 1])].sum(1) for k in range(32)], 1) * 20); M = (M - M.mean(0)) / (M.std(0) + 1e-6)
Mb = np.array([M[i:i + 8].mean(0) for i in range(0, len(M) - 8, 8)]); nb = len(Mb); bdt = 8 * dt2; D = np.linalg.norm(Mb[:, None] - Mb[None], axis=2); Ss = np.exp(-D / D.mean())
K = 16; g = np.outer(np.r_[-np.ones(K), np.ones(K)], np.r_[-np.ones(K), np.ones(K)]); nov = np.zeros(nb)
for i in range(K, nb - K): nov[i] = (Ss[i - K:i + K, i - K:i + K] * g).sum()
nov = gaussian_filter1d(nov, 2); z = (nov - nov[K:nb - K].mean()) / nov[K:nb - K].std()
cand = [i * bdt for i in range(K, nb - K) if z[i] == z[max(K, i - 4):min(nb - K, i + 5)].max() and z[i] > .8]
secs = [band] + [min(allev, key=lambda e: abs(e[0] - t))[0] for t in cand if t > band + 8 and t < final - 6]
AUD = dict(fps=FPS, nb=NB, nf=NF, bands=base64.b64encode((B * 255).astype(np.uint8).tobytes()).decode(), loud=base64.b64encode((loud * 255).astype(np.uint8).tobytes()).decode(),
           kick=kick, snare=snare, hat=hat, beats=beats, sections=[round(s, 3) for s in secs], band=round(band, 3), final=round(final, 3), len=round(dur, 3), bpm=round(60 / per, 1))
open(out, 'w').write('window.AUD=' + json.dumps(AUD) + ';\n')
print('kick', len(kick), 'snare', len(snare), 'hat', len(hat), 'beats', len(beats), 'band', band, 'final', final, 'sections', AUD['sections'], 'bpm', AUD['bpm'], 'size', len(open(out).read()) // 1024, 'KB')
