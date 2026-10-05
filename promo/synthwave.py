#!/usr/bin/env python3
"""OnAir promo score, synthwave edition: an 80s-style track synthesised from scratch (no samples, no soundfont).
Detuned-saw pads, a pulsing octave bass, a 16th-note saw arpeggio, a vibrato saw lead, LinnDrum-style kick, hats and a big
gated-reverb snare, tom fills. 120 BPM (one bar = 2 s); hits, builds and the picture-freeze windows follow scenes.js.
Writes music.wav."""
import numpy as np, os, re
from scipy import signal
from scipy.io import wavfile

SR = 44100
DUR = 116.0
N = int((DUR + 3) * SR)
T = np.arange(N) / SR
rng = np.random.default_rng(7)
BEAT = .5; BAR = 2.0

HITS = [4.0, 18.0, 52.0, 68.0, 96.0, 110.0]
BUILDS = [(0.0, 4.0), (14.0, 18.0), (48.0, 52.0), (64.0, 68.0), (88.0, 96.0)]
_js = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scenes.js')).read()
STALLS = [(44 + float(a), 44 + float(b)) for a, b in re.findall(r'\[([\d.]+),([\d.]+)\]', re.search(r'const STALLS=\[(.*?)\];', _js).group(1))]

def energy(t):
    for a, b in BUILDS:
        if a <= t < b: return -1                      # build
    for lim, e in ((10, 5), (14, 6), (30, 8), (44, 8.5), (48, 7), (64, 9.5), (72, 10), (78, 9.5), (86, 8), (88, 9),
                   (104, 10.5), (110, 10), (112, 9), (116, 3)):
        if t < lim: return e
    return 0

# D minor, i - VI - VII - v  (Dm - Bb - C - Am), one chord per bar; builds hang on C, hits land on Dm
CHORDS = [(50, [62, 65, 69]), (46, [58, 62, 65]), (48, [60, 64, 67]), (45, [57, 60, 64])]
def chord_at(t):
    for h in HITS:
        if h - 2 <= t < h: return CHORDS[2]
    if t >= 110: return CHORDS[0]
    return CHORDS[int(t // BAR) % 4]

mf = lambda m: 440 * 2 ** ((m - 69) / 12)
def lp(x, f, o=2): return signal.sosfilt(signal.butter(o, min(f, SR / 2 - 100), 'lp', fs=SR, output='sos'), x, axis=0)
def hp(x, f, o=2): return signal.sosfilt(signal.butter(o, f, 'hp', fs=SR, output='sos'), x, axis=0)
def bp(x, a, b): return signal.sosfilt(signal.butter(2, [a, b], 'bp', fs=SR, output='sos'), x, axis=0)
def saw(f, n, ph=0.0):
    t = np.arange(n) / SR; return signal.sawtooth(2 * np.pi * f * t + ph)
def adsr(n, a, d, s, r, hold):
    t = np.arange(n) / SR; e = np.where(t < a, t / max(a, 1e-4), np.where(t < a + d, 1 - (1 - s) * (t - a) / max(d, 1e-4), s))
    rel = np.clip((t - hold) / max(r, 1e-4), 0, 1); return e * (1 - rel)
def put(buf, t, x, pan=0.0):
    i = int(t * SR); m = min(len(x), N - i)
    if m <= 0: return
    l, r = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
    buf[i:i + m, 0] += x[:m] * l * 1.41; buf[i:i + m, 1] += x[:m] * r * 1.41
def reverb_ir(t60, damp, seed):
    r = np.random.default_rng(seed); n = int(t60 * 1.2 * SR); t = np.arange(n) / SR
    ir = r.standard_normal((n, 2)) * np.exp(-6.9 * t / t60)[:, None]; ir = lp(ir, damp); ir[:int(.012 * SR)] = 0
    return ir / np.sqrt((ir ** 2).sum() / 2)
def rv(x, ir): return np.stack([signal.fftconvolve(x[:, c], ir[:, c])[:len(x)] for c in (0, 1)], 1)
IR_HALL = reverb_ir(3.2, 5000, 1); IR_PLATE = reverb_ir(1.6, 7000, 2)

pad = np.zeros((N, 2)); bass = np.zeros((N, 2)); arp = np.zeros((N, 2)); lead = np.zeros((N, 2)); drums = np.zeros((N, 2)); snare = np.zeros((N, 2)); fxb = np.zeros((N, 2))
kicks = []; snares = []

# ---------------------------------------------------------------- instruments
def pad_note(t, dur, m, vel, bright=2400):
    n = int((dur + .8) * SR); env = adsr(n, .35, .4, .85, .7, dur)
    for k, (det, pan) in enumerate(((-.12, -.7), (0, 0), (.12, .7))):
        x = saw(mf(m) * 2 ** (det / 12), n, rng.uniform(0, 6)) * env * vel * .16
        put(pad, t, lp(x, bright), pan)
def bass_note(t, dur, m, vel):
    n = int((dur + .05) * SR); tt = np.arange(n) / SR
    x = saw(mf(m), n) + .5 * signal.square(2 * np.pi * mf(m) / 2 * tt)
    cut = 300 + 1600 * np.exp(-tt / .07); y = lp(x, 900) * .6 + lp(x, 2400) * .4 * np.exp(-tt / .07)
    put(bass, t, y * adsr(n, .003, .1, .7, .04, dur) * vel * .5, 0)
def arp_note(t, m, vel, bright):
    n = int(.22 * SR); tt = np.arange(n) / SR; x = saw(mf(m), n) + saw(mf(m) * 1.004, n)
    y = lp(x, 600 + bright) * np.exp(-tt / .09)
    put(arp, t, y * vel * .22, .25 * np.sin(t * 3))
def lead_note(t, dur, m, vel):
    n = int((dur + .4) * SR); tt = np.arange(n) / SR
    vib = 1 + .006 * np.sin(2 * np.pi * 5.6 * tt) * np.clip((tt - .18) / .3, 0, 1)
    ph = 2 * np.pi * np.cumsum(mf(m) * vib) / SR
    x = signal.sawtooth(ph) + signal.sawtooth(ph * 1.007) + .5 * signal.square(ph / 2)
    put(lead, t, lp(x, 3800) * adsr(n, .02, .2, .8, .35, dur) * vel * .16, 0)
def kick(t, vel=1.0):
    n = int(.45 * SR); tt = np.arange(n) / SR; f = 45 + 110 * np.exp(-tt / .035)
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-tt / .18) + .3 * np.exp(-tt / .004) * rng.standard_normal(n) * .2
    put(drums, t, x * vel * .9, 0); kicks.append(t)
def snare_hit(t, vel=1.0):     # big gated-reverb snare: dry body + plate tail cut off sharply at ~260 ms
    n = int(.6 * SR); tt = np.arange(n) / SR
    body = np.sin(2 * np.pi * 190 * tt) * np.exp(-tt / .05) * .7 + bp(rng.standard_normal(n), 1200, 8000) * np.exp(-tt / .09)
    put(snare, t, body * vel * .55, 0); snares.append(t)
def hat(t, vel=1.0, open_=False):
    n = int((.25 if open_ else .05) * SR); tt = np.arange(n) / SR
    put(drums, t, hp(rng.standard_normal(n), 7500) * np.exp(-tt / (.08 if open_ else .015)) * vel * .18, .3)
def tom(t, m, vel=1.0):
    n = int(.5 * SR); tt = np.arange(n) / SR; f = mf(m) * (1 + .6 * np.exp(-tt / .05))
    put(drums, t, np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-tt / .22) * vel * .6, -.3 + .2 * (m % 5))
def crash(t, vel=1.0):
    n = int(2.4 * SR); tt = np.arange(n) / SR
    put(drums, t, hp(rng.standard_normal(n), 3500) * np.exp(-tt / .8) * vel * .2, .2)
def boom(t, vel=1.0):
    n = int(2.0 * SR); tt = np.arange(n) / SR; f = 32 + 60 * np.exp(-tt * 1.2)
    put(fxb, t, np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-tt * 1.6) * vel * .55, 0)

# ---------------------------------------------------------------- the track
HOOK = [[(0, 1, 74), (1, .5, 72), (1.5, .5, 74), (2, 1, 77), (3, 1, 76)],            # beats within a bar
        [(0, 1.5, 74), (1.5, .5, 72), (2, 2, 69)],
        [(0, 1, 72), (1, .5, 74), (1.5, .5, 76), (2, 1, 79), (3, 1, 77)],
        [(0, 1.5, 76), (1.5, .5, 74), (2, 2, 72)]]
for b in range(int(DUR // BAR)):
    t0 = b * BAR; E = energy(t0 + .01); root, ch = chord_at(t0 + .01)
    inbuild = E < 0
    # pad: every bar (a swell in builds)
    if t0 < 112 or b == 56: pad_note(t0, BAR, ch[0], .7 if not inbuild else .85, 1800 if E < 8 else 2800); [pad_note(t0, BAR, m, .55, 1800 if E < 8 else 2800) for m in ch[1:]]
    if E >= 5:   # octave bass, 8ths (16ths at the top)
        step = .125 if E >= 10 else .25
        for k in range(int(BAR / step)): bass_note(t0 + k * step, step * .9, root - 12 + (12 if k % 2 else 0), .8 if E >= 8 else .55)
    if E >= 6 or inbuild:   # saw arpeggio; in builds its filter opens toward the hit
        seq = [ch[0], ch[1], ch[2], ch[0] + 12, ch[2], ch[1], ch[0] + 12, ch[1] + 12]
        bright = 1200 if E < 8 else 2600
        for k in range(16):
            tk = t0 + k * .125
            if inbuild:
                a, bb = next((a, bb) for a, bb in BUILDS if a <= t0 < bb); bright = 300 + 4200 * ((tk - a) / (bb - a)) ** 1.6
            arp_note(tk, seq[k % 8] + 12, .9 if k % 4 == 0 else .65, bright)
    if E >= 7:   # drums: four on the floor, gated snare on 2 and 4, hats
        for k in range(4): kick(t0 + k * BEAT, .95)
        snare_hit(t0 + .5, .9); snare_hit(t0 + 1.5, .9)
        hstep = .125 if E >= 9.5 else .25
        for k in range(int(BAR / hstep)): hat(t0 + k * hstep, .9 if k % 2 == 0 else .55, open_=(E >= 9.5 and k % 4 == 2))
        if b % 4 == 3: [tom(t0 + 1.5 + k * .125, [50, 47, 45, 41][k], .9) for k in range(4)]
    if E >= 9.5:   # the lead hook
        for (bo, du, m) in HOOK[b % 4]: lead_note(t0 + bo * BEAT, du * BEAT * .95, m, 1.0)
        if E >= 10.5: [lead_note(t0 + bo * BEAT, du * BEAT * .95, m + 12, .45) for (bo, du, m) in HOOK[b % 4]]
# builds: gated snare roll accelerating into the hit, toms on the last beat, a kick-less last bar
for a, b2 in BUILDS:
    d = b2 - a; x = d
    while x > 1e-6:
        st = .5 if x > 3 else .25 if x > 1.5 else .125 if x > .5 else .0625
        if x <= 4: snare_hit(b2 - x, .35 + .6 * (1 - x / min(4, d)))
        x -= st
    for k, m in enumerate((52, 50, 47, 45, 43, 41, 40, 38)): tom(b2 - .5 + k * .0625, m, .8)
# hits: crash, kick, a fat gated snare, sub boom, and a pad chord stab
for i, h in enumerate(HITS):
    g = .7 + .1 * i
    crash(h, g); kick(h, 1.0); snare_hit(h, 1.0); boom(h, .6 + .08 * i)
    for m in (62, 65, 69, 74): pad_note(h, 1.6, m, .7, 3600)
# sound design: the click on "smooth picture gaps", every screen switching on
n = int(.05 * SR); put(fxb, 51.95, bp(rng.standard_normal(n), 2000, 7000) * np.exp(-np.arange(n) / SR / .006) * .5)
n = int(.5 * SR); tt = np.arange(n) / SR; put(fxb, 68.0, np.sin(2 * np.pi * (48 + 30 * np.exp(-tt * 12)) * tt) * np.exp(-tt * 7) * .3)
# outro: the last chord rings out
for m in (50, 62, 65, 69): pad_note(112.0, 3.5, m, .6, 2000)

# ---------------------------------------------------------------- mix
duck = np.ones(N)
for tk in kicks:
    i = int(tk * SR); s_ = np.arange(min(int(.4 * SR), N - i)) / SR; duck[i:i + len(s_)] *= 1 - .45 * np.exp(-s_ / .12)
D = duck[:, None]
gate = np.zeros(N)                                           # the 80s gate: open for 260 ms after each snare, then slammed shut
for t_ in snares:
    i = int(t_ * SR); n = min(int(.30 * SR), N - i); g_ = np.ones(n); g_[int(.26 * SR):] = np.linspace(1, 0, n - int(.26 * SR)); gate[i:i + n] = np.maximum(gate[i:i + n], g_)
snr = snare + rv(snare, IR_PLATE) * 1.4 * gate[:, None]
mix = (pad * D * .55 + rv(pad, IR_HALL) * .35 * D
       + bass * D * .7
       + arp * D * .45 + rv(arp, IR_PLATE) * .18
       + lead * .55 + rv(lead, IR_HALL) * .35
       + drums * .8 + rv(drums, IR_PLATE) * .08
       + snr * .6 + fxb * .8 + rv(fxb, IR_HALL) * .2)
# echo on the lead and arp (dotted eighth ping-pong)
dl = int(.375 * SR); e = np.zeros_like(mix); src = lead * .55 + arp * .2
for k in range(1, 4): e[k * dl:, k % 2] += src[:-k * dl, 0] * (.35 ** k)
mix += e * D
# while the picture freezes in the gaps demo, the music dips and darkens (never stops)
for a, b in STALLS:
    i0, i1 = int(a * SR), int(b * SR); n = i1 - i0; r = int(.04 * SR)
    w = np.minimum(1, np.minimum(np.arange(n), n - 1 - np.arange(n)) / r)
    seg_ = mix[i0:i1]; mix[i0:i1] = (seg_ * (1 - w[:, None]) + lp(seg_, 1400) * w[:, None]) * (1 - .3 * w)[:, None]
mix = hp(mix, 30)
mid = (mix[:, 0] + mix[:, 1]) / 2; side = (mix[:, 0] - mix[:, 1]) / 2; mix = np.stack([mid + side * 1.2, mid - side * 1.2], 1)
# leveller toward a rising target: every hit and section a little bigger than the last
blk = 2205; nb = len(mix) // blk; rms = np.sqrt((mix[:nb * blk] ** 2).reshape(nb, blk, 2).mean((1, 2))) + 1e-5
sm = np.sqrt(np.convolve(rms ** 2, np.ones(80) / 80, 'same')); tt = (np.arange(nb) + .5) * blk / SR
want = np.interp(tt, [0, 4, 13.9, 18, 44, 47.9, 52, 63.9, 68, 87.9, 95.9, 96, 104, 109.9, 110, 113, 116, 120],
                 [.070, .095, .090, .102, .102, .096, .110, .104, .120, .110, .118, .152, .135, .128, .146, .100, .060, .045])
g = np.convolve(np.clip(want / sm, .5, 3.5), np.ones(60) / 60, 'same'); gain = np.concatenate([np.repeat(g, blk), np.full(len(mix) - nb * blk, g[-1])])
mix *= gain[:, None]
mix *= np.minimum(1, T / .3)[:, None] * (np.clip((DUR + 2.0 - T) / 3.0, 0, 1) ** 1.5)[:, None]
mix = np.where(np.abs(mix) > .85, np.sign(mix) * (.85 + .15 * np.tanh((np.abs(mix) - .85) / .15)), mix)
mix = mix[:int((DUR + 2.4) * SR)]; mix /= np.abs(mix).max() * 1.05
wavfile.write(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'music.wav'), SR, (mix * 32000).astype('<i2'))
print('ok', len(mix) / SR, 's')
