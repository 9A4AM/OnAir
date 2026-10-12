#!/usr/bin/env python3
"""The 0.2.4 cut's soundtrack, built from a song file you supply (it is never in the repository): song_edit_024.py <song> <outdir>.
Beats are tracked from the spectral flux (dynamic programming around 0.44 s), then three parts of the song are joined on downbeats with 30 ms
equal-power crossfades: the intro and band entry (0 to beat 225), four bars before the full stop to eight bars after the slam (beats 341-389),
and sixteen bars before the end to the ring-out (beat 578 to the end). Writes song_edit.wav and the beat map for v024.js (beatmap024.js)."""
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
A, B, C = (0.0, b[225]), (b[341], b[389]), (b[578], len(x) / SR - .2)
pre, xf = int(.012 * SR), int(.03 * SR); cut = lambda a, e: x[max(0, int(a * SR) - pre if a > 0 else 0):int(e * SR) - pre].copy()
res = cut(*A)
for p in (cut(*B), cut(*C)):
    w = np.linspace(0, np.pi / 2, xf)[:, None]; res = np.concatenate([res[:-xf], res[-xf:] * np.cos(w) + p[:xf] * np.sin(w), p[xf:]])
res[-int(.6 * SR):] *= np.linspace(1, 0, int(.6 * SR))[:, None]
wf.write(f'{out}/song_edit.wav', SR, (np.clip(res, -1, 1) * 32767).astype(np.int16))
LA, LB = A[1], B[1] - B[0]; iB = int((b < A[1]).sum()); iC = iB + int(((b >= B[0]) & (b < B[1])).sum())
grid = [t for t in b if t < A[1]] + [LA + t - B[0] for t in b if B[0] <= t < B[1]] + [LA + LB + t - C[0] for t in b if C[0] <= t < C[1]]
down = [round(float(t), 4) for k, t in enumerate(grid) if (k % 4 == 1 if k < iB else (k - iB) % 4 == 0 if k < iC else (k - iC) % 4 == 0)]
bm = dict(beats=[round(float(t), 4) for t in grid], down=down, band=float(b[65]), joinAB=float(LA), joinBC=float(LA + LB), stop=float(LA + 161.216 - B[0]),
          slam=float(LA + onset(162.0, 162.5) - B[0]), final=float(LA + LB + onset(289.3, 289.8) - C[0]), len=len(res) / SR)
open(f'{out}/beatmap024.js', 'w').write('window.BM=' + json.dumps(bm) + ';\n'); print({k: v for k, v in bm.items() if not isinstance(v, list)})
