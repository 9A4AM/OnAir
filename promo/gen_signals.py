#!/usr/bin/env python3
"""Synthesises real baseband captures (DVB-T 8K 64-QAM OFDM through a slowly moving multipath channel; two DAB ensembles) plus receiver
noise, runs them through Welch-averaged FFTs exactly as a spectrum display would, and writes signals.js (uint8 dB tables, one row per
video frame). The video's spectra and waterfalls are plotted from these tables, not drawn from a formula."""
import numpy as np, base64, json
from scipy import signal
rng = np.random.default_rng(5)
NF = 360; NB = 1024; M = 6; FPS = 30
DB0, DB1 = -120.0, -20.0

def qam64(n):
    a = rng.integers(0, 8, n) * 2 - 7; b = rng.integers(0, 8, n) * 2 - 7
    return (a + 1j * b) / np.sqrt(42)

def ofdm(nsym, nfft, ncar, guard, boost_every=None):
    """nsym OFDM symbols; ncar occupied carriers centred on DC"""
    out = []
    k = np.arange(-(ncar // 2), ncar // 2 + 1)
    for s in range(nsym):
        X = np.zeros(nfft, complex); X[k % nfft] = qam64(len(k))
        if boost_every:
            pil = ((k + s * 3) % boost_every) == 0
            X[(k[pil]) % nfft] = (4 / 3) * np.sign(rng.standard_normal(pil.sum()))
        x = np.fft.ifft(X) * nfft
        out.append(np.concatenate([x[-guard:], x]))
    return np.concatenate(out)

def multipath(x, fs, echoes, t_hz):
    """slowly moving echoes: (delay_us, amp, doppler_hz, phase0)"""
    n = np.arange(len(x)); y = x.copy()
    for d, a, f, p0 in echoes:
        sh = int(round(d * 1e-6 * fs)); z = np.zeros_like(x); z[sh:] = x[:len(x) - sh]
        y += a * z * np.exp(1j * (2 * np.pi * f * n / fs + p0))
    return y

def welch_frames(x, fs, nfr, span_hz=None):
    """nfr frames of M-averaged 1024-pt Hann periodograms, consecutive in time. returns linear power, shape (nfr, NB), fftshifted"""
    w = np.hanning(NB); seg = NB; hop = len(x) // (nfr * M + 1)
    hop = min(hop, NB * 4)
    out = np.zeros((nfr, NB))
    pos = 0
    for f in range(nfr):
        acc = np.zeros(NB)
        for m in range(M):
            s = x[pos:pos + seg] * w; acc += np.abs(np.fft.fftshift(np.fft.fft(s))) ** 2; pos += hop
        out[f] = acc / M
        pos = (f + 1) * (len(x) - seg - 1) // nfr
    return out / (w ** 2).sum()

def tab(P, ref_db):
    d = 10 * np.log10(P + 1e-30) + ref_db
    return np.clip(np.round((d - DB0) / (DB1 - DB0) * 255), 0, 255).astype(np.uint8)

# ---------------- DVB-T: 8K, 64-QAM, 1/8 guard; native fs 64/7 MHz resampled to 10 Msps
dur = NF / FPS
n_sym = 320
print('dvb symbols', n_sym)
x = ofdm(n_sym, 8192, 6816, 1024, boost_every=12)
x = signal.resample_poly(x, 35, 32)                    # -> 10 Msps
# PA compression gives the characteristic -40 dBc shoulders, then the output filter trims them
x = x / np.sqrt((np.abs(x) ** 2).mean()); x = x - 0.075 * np.abs(x) ** 2 * x * 0.5
sos = signal.butter(8, 4.0e6, 'lp', fs=10e6, output='sos'); x = signal.sosfilt(sos, x)
x = multipath(x, 10e6, [(1.1, .22, 1.1 * 40, .4), (3.3, .10, -.7 * 40, 1.7), (.45, .12, .35 * 40, 2.5)], 0)
# receiver: slight passband tilt + droop at the edges
f = np.fft.fftfreq(len(x), 1 / 10e6); H = 1 - .12 * (f / 5e6) - .10 * (f / 5e6) ** 2
x = np.fft.ifft(np.fft.fft(x) * H)
Sx = welch_frames(x, 10e6, NF)
plateau = Sx[:, 300:720].mean()                         # inside the 7.6 MHz signal (bins ~ 120..904 -> centre part)
Sx *= 10 ** ((-52) / 10) / plateau

# ---------------- noise (thermal + LNA) with two stationary spurs and a gentle bandpass tilt
nn = (rng.standard_normal(len(x)) + 1j * rng.standard_normal(len(x)))
for fh, a in ((-4.1e6, .55), (3.55e6, .35)):
    nn += a * 6 * np.exp(2j * np.pi * fh * np.arange(len(x)) / 10e6)
f = np.fft.fftfreq(len(nn), 1 / 10e6); nn = np.fft.ifft(np.fft.fft(nn) * (1 + .10 * (f / 5e6) - .22 * (f / 5e6) ** 2 + .05 * np.abs(np.sin(f / 2.2e5 * 0))))
Nx = welch_frames(nn, 10e6, NF)
Nx *= 10 ** ((-84) / 10) / np.median(Nx[:, 20:60])

# ---------------- DAB: two ensembles at fs 6.4 Msps (2048-pt OFDM native 2.048 Msps -> x3.125)
n_d = 130
def dab(nsym):
    out = []
    k = np.concatenate([np.arange(-768, 0), np.arange(1, 769)])
    for s in range(nsym):
        X = np.zeros(2048, complex); X[k % 2048] = np.exp(1j * np.pi / 2 * rng.integers(0, 4, len(k)))
        x = np.fft.ifft(X) * 2048; out.append(np.concatenate([x[-504:], x]))
    return np.concatenate(out)
fs2 = 6.4e6
def ensemble(shift_hz, pdb):
    e = signal.resample_poly(dab(n_d), 25, 8); e /= np.sqrt((np.abs(e) ** 2).mean())
    e = e * np.exp(2j * np.pi * shift_hz * np.arange(len(e)) / fs2); return e * 10 ** (pdb / 20)
a = ensemble(-1.47e6, 0); b = ensemble(+1.41e6, -5.5)
L = min(len(a), len(b)); d = multipath(a[:L] + b[:L], fs2, [(.8, .15, .6 * 120, 1.1), (2.2, .07, -.4 * 120, .3)], 0)
Sd = welch_frames(d, fs2, NF)
Sd *= 10 ** ((-52) / 10) / Sd[:, 250:270].mean() if False else 1
pl = Sd[:, int((.27 - .5) * 0 + 1024 * .27 - 60):int(1024 * .27 + 60)].mean(); Sd *= 10 ** ((-52) / 10) / pl
nd = rng.standard_normal(L) + 1j * rng.standard_normal(L)
for fh, am in ((-2.65e6, .45),): nd += am * 6 * np.exp(2j * np.pi * fh * np.arange(L) / fs2)
Nd = welch_frames(nd, fs2, NF); Nd *= 10 ** ((-84) / 10) / np.median(Nd[:, 20:60])

def b64(a): return base64.b64encode(a.tobytes()).decode()
js = 'window.SIGDATA=' + json.dumps(dict(nf=NF, nb=NB, db0=DB0, db1=DB1,
    T_s=b64(tab(Sx, 0)), T_n=b64(tab(Nx, 0)), D_s=b64(tab(Sd, 0)), D_n=b64(tab(Nd, 0)))) + ';\n'
open('signals.js', 'w').write(js); print('signals.js', len(js) // 1024, 'KiB')
# diagnostics: mean spectrum
import sys
m = 10 * np.log10((Sx + Nx).mean(0)); print('T plateau', m[300:700].mean(), 'floor', m[10:60].mean(), 'shoulder', m[60:100].mean())
m = 10 * np.log10((Sd + Nd).mean(0)); print('D plateau', m[250:300].mean(), 'floor', m[10:60].mean())
np.save('/tmp/sx.npy', Sx + Nx); np.save('/tmp/sd.npy', Sd + Nd)
