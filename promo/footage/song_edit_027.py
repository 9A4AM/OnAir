#!/usr/bin/env python3
"""Soundtrack for the 0.2.7 film, built from a song file you supply (never committed): song_edit_027.py <song> <outdir>.
Beats are tracked from the spectral flux (dynamic programming around 0.44 s). The song is used continuously from the start to beat 389 (the
downbeat just after the full stop and the slam), then joined on a downbeat to its ending, beat 610 (eight bars before the last hit), with a 30 ms
equal-power crossfade. Writes song_edit.wav and beatmap027.js (beats, downbeats and the key times) for v027.js."""
import sys, json, subprocess, numpy as np, scipy.io.wavfile as wf
from scipy.ndimage import gaussian_filter1d
song, out = sys.argv[1], sys.argv[2]; SR = 44100; HOP = 512; FS = 22050
mono = np.frombuffer(subprocess.run(['ffmpeg', '-loglevel', 'error', '-i', song, '-ac', '1', '-ar', str(FS), '-f', 's16le', '-'], capture_output=True).stdout, np.int16) / 32768
win = np.hanning(1024); S = np.array([np.abs(np.fft.rfft(mono[i:i + 1024] * win)) for i in range(0, len(mono) - 1024, HOP)])
flux = np.maximum(0, np.diff(np.log1p(S * 10), axis=0)).sum(1); flux = gaussian_filter1d((flux - flux.mean()) / flux.std(), 1.0)
dt = HOP / FS; P = 0.4505 / dt; n = len(flux); score = flux.copy(); back = np.full(n, -1)
for i in range(n):
    lo, hi = int(i - P * 1.3), int(i - P * .75)
    if lo < 0: continue
    w = np.arange(lo, hi); pen = -2.0 * np.log((i - w) / P) ** 2; j = np.argmax(score[w] + pen); score[i] = flux[i] + score[w][j] + pen[j]; back[i] = w[j]
i = np.argmax(score[-int(P * 2):]) + n - int(P * 2); bt = []
while i >= 0: bt.append(i); i = back[i]
b = np.array(bt[::-1]) * dt
x = np.frombuffer(subprocess.run(['ffmpeg', '-loglevel', 'error', '-i', song, '-ac', '2', '-ar', str(SR), '-f', 'f32le', '-'], capture_output=True).stdout, np.float32).reshape(-1, 2)
def onset(t0, t1):
    s = x[int(t0 * SR):int(t1 * SR)].mean(1); e = np.sqrt(np.convolve(s ** 2, np.ones(441) / 441, 'same')); return t0 + np.argmax(np.diff(20 * np.log10(e + 1e-6))) / SR
JA, JC = 389, 610; endC = len(x) / SR - .2
pre, xf = int(.012 * SR), int(.03 * SR)
A = x[:int(b[JA] * SR) - pre].copy(); C = x[int(b[JC] * SR) - pre:int(endC * SR)].copy()
w = np.linspace(0, np.pi / 2, xf)[:, None]; res = np.concatenate([A[:-xf], A[-xf:] * np.cos(w) + C[:xf] * np.sin(w), C[xf:]])
res[-int(.6 * SR):] *= np.linspace(1, 0, int(.6 * SR))[:, None]
wf.write(f'{out}/song_edit.wav', SR, (np.clip(res, -1, 1) * 32767).astype(np.int16))
LA = float(b[JA])
grid = [float(t) for t in b[:JA]] + [LA + float(t - b[JC]) for t in b[JC:] if t < endC]
down = [round(float(b[k]), 4) for k in range(1, JA, 4)] + [round(LA + float(b[k] - b[JC]), 4) for k in range(JC, len(b), 4) if b[k] < endC]
bm = dict(beats=[round(t, 4) for t in grid], down=down, band=float(b[65]), join=LA, stop=float(b[357]),
          slam=float(onset(162.0, 162.5)), final=float(LA + onset(289.3, 289.8) - b[JC]), len=len(res) / SR)
open(f'{out}/beatmap027.js', 'w').write('window.BM=' + json.dumps(bm) + ';\n'); print({k: round(v, 3) for k, v in bm.items() if not isinstance(v, list)})
