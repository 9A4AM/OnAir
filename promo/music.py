#!/usr/bin/env python3
"""OnAir promo score: composed as MIDI stems (piano, pads, bass, vibraphone, drums, glockenspiel ticks), rendered with
FluidSynth (FluidR3 GM), then mixed with reverb / sidechain / master compression in numpy+scipy.
120 BPM, one bar = 2 s, so every scene boundary of the video falls on a bar line."""
import numpy as np, subprocess, os, random
from mido import MidiFile, MidiTrack, Message, MetaMessage, bpm2tempo
from scipy.io import wavfile
from scipy import signal

SR = 44100
OUT = os.environ.get('OUT', 'music_work'); os.makedirs(OUT, exist_ok=True)
SF2 = '/usr/share/sounds/sf2/FluidR3_GM.sf2'
BAR = 2.0; NBARS = 41; DUR = NBARS * BAR           # 82 s
rnd = random.Random(11)

CH = [  # chord per bar (cycle of 4): bass root, voicing for pad/piano
    dict(root=45, notes=[57, 60, 64, 67, 71]),   # Am9
    dict(root=41, notes=[53, 57, 60, 64, 67]),   # Fmaj7
    dict(root=48, notes=[55, 60, 64, 67, 71]),   # Cmaj7
    dict(root=43, notes=[55, 59, 62, 64, 69]),   # G6
]
MOTIF = [  # vibraphone melody per chord: (beat, length, note)
    [(0, 1.5, 81), (1.5, .5, 79), (2, 1, 76), (3, 1, 72)],
    [(0, 1, 84), (1, 1, 81), (2, 1.5, 79), (3.5, .5, 77)],
    [(0, 1.5, 79), (1.5, .5, 76), (2, 1, 79), (3, 1, 83)],
    [(0, 1, 86), (1, 1, 83), (2, 1, 81), (3, 1, 79)],
]
def chord(b): return CH[b % 4]

class Stem:
    def __init__(s, program=0, ch=0): s.ev = []; s.program = program; s.ch = ch
    def add(s, t, dur, note, vel, jitter=0.006):
        t = max(0, t + rnd.uniform(-jitter, jitter)); vel = int(max(1, min(127, vel + rnd.randint(-6, 6)))); s.ev.append((t, dur, note, vel))
    def save(s, path):
        mid = MidiFile(ticks_per_beat=480); tr = MidiTrack(); mid.tracks.append(tr)
        tr.append(MetaMessage('set_tempo', tempo=bpm2tempo(120)))
        if s.ch != 9: tr.append(Message('program_change', program=s.program, channel=s.ch, time=0))
        msgs = []
        for t, d, n, v in s.ev:
            msgs.append((int(t * 960), 1, Message('note_on', note=n, velocity=v, channel=s.ch)))
            msgs.append((int((t + d) * 960), 0, Message('note_off', note=n, velocity=0, channel=s.ch)))
        msgs.sort(key=lambda m: (m[0], m[1])); last = 0
        for tk, _, m in msgs: m.time = tk - last; last = tk; tr.append(m)
        mid.save(path)

piano = Stem(0); pad = Stem(89); strings = Stem(48); bass = Stem(39); vibes = Stem(11); drums = Stem(0, 9); bells = Stem(9)
kicks = []

# ---------------------------------------------------------------- arrangement
for b in range(NBARS):
    t0 = b * BAR; c = chord(b); T = c['notes']; ext = T + [n + 12 for n in T]
    # pads: whole bars, from the first bar (swell comes from the mix)
    for n in T: pad.add(t0, BAR * 1.02, n, 52 if b < 3 else 60, 0)
    if b >= 2 and not (34 <= b <= 36):
        for n in (T[0] - 12 if T[0] > 55 else T[0], T[2], T[3]): strings.add(t0, BAR * 1.02, n, 54 if b < 10 else 64, 0)
    # piano
    if b == 0:
        for k, n in enumerate([81, 76, 72]): piano.add(t0 + 0.5 + k * 0.5, 1.2, n, 62)
    elif b == 1:
        for k in range(4): piano.add(t0 + k * 0.5, 0.9, ext[[0, 2, 4, 6][k]], 66)
    else:
        pat = [0, 2, 4, 6, 4, 2, 5, 3] if b % 2 == 0 else [1, 3, 5, 7, 5, 3, 6, 4]
        vel = 60 if b < 6 else 68
        if 34 <= b <= 36: vel = 58
        for k, i in enumerate(pat): piano.add(t0 + k * 0.25 + 0.0, 0.5, ext[i], vel + (8 if k % 4 == 0 else 0) + (4 if k % 2 == 0 else 0))
    # bass
    if b >= 2 and not (34 <= b <= 35):
        r = c['root']
        if b < 6 or b == 36: bass.add(t0, BAR * .98, r, 80)
        else:
            for k, (o, v) in enumerate([(0, 98), (0, 70), (12, 82), (0, 70), (0, 92), (0, 70), (12, 80), (0, 72)]): bass.add(t0 + k * 0.25, 0.23, r + o, v)
    if b == 40: bass.add(t0, 4.0, c['root'], 80)
    # vibraphone melody
    if 14 <= b <= 33 or 38 <= b <= 39:
        for (bt, ln, n) in MOTIF[b % 4]: vibes.add(t0 + bt * .5, ln * .5 * 0.95, n + (12 if 25 <= b <= 28 and bt >= 2 else 0) - (12 if n > 84 else 0), 70)
    # bells (glockenspiel) sparkle on the last beat of every second bar from the logo
    if b >= 3 and b % 2 == 1 and not 34 <= b <= 36: bells.add(t0 + 1.5, 0.4, [84, 88, 91, 96][(b // 2) % 4], 48)
    # drums
    kickOn = b >= 1 and not (b == 35)
    if kickOn:
        for k in range(4):
            tk = t0 + k * 0.5; v = 66 if b < 6 else (92 if b < 34 else 78)
            if b == 36: v = 70
            drums.add(tk, 0.1, 36, v, 0.002); kicks.append(tk)
    if b >= 6 and not 34 <= b <= 35:
        for k in (1, 3): drums.add(t0 + k * 0.5, 0.1, 39, 82, 0.003)
    if b >= 3 and not 34 <= b <= 35:
        for k in range(8):
            v = 34 + (16 if k % 2 == 1 else 0) + (6 if b >= 10 else 0)
            drums.add(t0 + k * 0.25 + 0.0, 0.05, 42, v, 0.003)
        if b >= 10 and b % 2 == 1: drums.add(t0 + 1.75, 0.2, 46, 54, 0.003)
    # crashes / fills
    if b in (2, 6, 10, 14, 18, 22, 25, 29, 34, 37): drums.add(t0, 1.5, 49, 90, 0.002)
    if b in (5, 9, 13, 17, 21, 24, 28, 33, 36):          # snare build in the last half bar
        for k in range(8): drums.add(t0 + 1.0 + k * 0.125, 0.1, 38, 50 + k * 8, 0.002)
# the very first bar: soft tom heartbeat so the first cut has weight
for tt in (0.0,): drums.add(tt, .5, 41, 70, 0)

# UI ticks: glockenspiel notes (A minor pentatonic) at animation cues
CUES = [2.5, 2.75, 3.0, 3.25,                                # lock lamps (hook)
        13.0, 13.5, 14.0, 14.5, 15.0, 15.5, 16.0, 16.5,          # radio cards
        22.5, 23.0, 23.5, 25.0, 25.5, 26.0, 26.5,                # standards / scanner
        29.5, 30.0, 30.5, 31.0,                                  # live chips
        37.5, 38.5, 39.5, 40.0, 40.5, 41.0,                      # standards columns, then chips
        45.0, 46.0, 47.0,                                        # "built to be exact" cards
        51.5, 52.5, 53.5,                                        # data panels
        59.5, 60.0, 60.5, 61.0, 61.5,                            # multipath echoes
        63.0, 63.5, 64.0, 64.5, 65.0, 65.5, 66.0, 66.5,          # antenna directions
        69.5, 70.0, 70.5,                                        # outputs
        76.0, 76.5, 77.0, 77.5, 78.0]                            # outro chips + platforms
scale = [69, 72, 76, 79, 81, 84, 88]
for i, t in enumerate(CUES): bells.add(t, 0.25, scale[i % 7] + 12 if i % 7 < 3 else scale[i % 7], 62, 0.0)

stems = dict(piano=piano, pad=pad, strings=strings, bass=bass, vibes=vibes, drums=drums, bells=bells)
audio = {}
for name, st in stems.items():
    mid = f'{OUT}/{name}.mid'; wav = f'{OUT}/{name}.wav'; st.save(mid)
    subprocess.run(['fluidsynth', '-ni', '-q', '-g', '0.8', '-r', str(SR), '-F', wav, SF2, mid], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    sr, x = wavfile.read(wav); x = x.astype(np.float32) / 32768.0
    if x.ndim == 1: x = np.stack([x, x], 1)
    n = int(DUR * SR) + SR * 4
    y = np.zeros((n, 2), np.float32); y[:min(n, len(x))] = x[:n]; audio[name] = y

# ---------------------------------------------------------------- level the stems (FluidSynth renders them at wildly different loudness)
TARGET=dict(piano=.060,pad=.032,strings=.026,bass=.034,vibes=.040,drums=.040,bells=.020)
for n,tg in TARGET.items():
    seg=audio[n][14*SR:40*SR]; r=float(np.sqrt((seg**2).mean()))+1e-9; audio[n]*=tg/r
# ---------------------------------------------------------------- mix
N = int(DUR * SR); T = np.arange(N + SR * 4) / SR
def reverb_ir(t60=2.4, pre=0.02, damp=6000, seed=1):
    r = np.random.default_rng(seed); n = int(t60 * 1.3 * SR); t = np.arange(n) / SR
    ir = r.standard_normal((n, 2)) * np.exp(-6.9 * t / t60)[:, None]
    sos = signal.butter(2, damp, fs=SR, output='sos'); ir = signal.sosfilt(sos, ir, axis=0)
    ir[:int(pre * SR)] = 0; ir *= np.linspace(0.2, 1, n)[:, None] ** 0 ; ir[:int(.002*SR)] = 0
    return (ir / np.sqrt((ir ** 2).sum() / 2)).astype(np.float32)
def rv(x, ir): return np.stack([signal.fftconvolve(x[:, c], ir[:, c])[:len(x)] for c in (0, 1)], 1)
IR_HALL = reverb_ir(2.8, 0.025, 5200, 1); IR_ROOM = reverb_ir(0.9, 0.008, 7000, 2)
def hp(x, f): return signal.sosfilt(signal.butter(2, f, 'hp', fs=SR, output='sos'), x, axis=0)
def lp(x, f): return signal.sosfilt(signal.butter(2, f, 'lp', fs=SR, output='sos'), x, axis=0)
def echo(x, d=0.375, fb=.35, mix=.3):
    n = int(d * SR); y = x.copy()
    for k in range(1, 5): y[k * n:] += x[:len(x) - k * n] * (fb ** k) * mix
    return y
# sidechain envelope from the kick pattern
duck = np.ones(len(T), np.float32)
for tk in kicks:
    i = int(tk * SR); seg = np.arange(len(T) - i)[:int(.45 * SR)] / SR
    duck[i:i + len(seg)] *= (1 - .45 * np.exp(-seg / .11)).astype(np.float32)
def swell(x, a, b, t0=0.0, t1=6.0): return x  # placeholder

mix = np.zeros((len(T), 2), np.float32)
p = hp(audio['piano'], 90); mix += (p * .85 + rv(p, IR_HALL) * .55)
pd = audio['pad'];    pd = hp(lp(pd, 3800), 110) * duck[:, None]; mix += (pd * .50 + rv(pd, IR_HALL) * .28)
s = audio['strings']; s = hp(lp(s, 4500), 140) * duck[:, None]; mix += (s * .42 + rv(s, IR_HALL) * .35)
b_ = audio['bass'];   b_ = lp(b_, 700) * duck[:, None]; mix += b_ * .90
v = audio['vibes'];   mix += (echo(v) * .55 + rv(v, IR_HALL) * .45)
bl = audio['bells'];  mix += (echo(bl, .25, .3, .35) * .55 + rv(bl, IR_HALL) * .55)
d = audio['drums'];   d = lp(d, 12000); mix += (d * .9 + hp(d, 5000) * .9 + rv(d, IR_ROOM) * .18)
# intro swell (the first bar fades the pads up) and noise risers / impacts
rng = np.random.default_rng(3)
def riser(t_end, dur=1.6, g=.35):
    n = int(dur * SR); w = rng.standard_normal((n, 2)); out = np.zeros((n, 2)); x = np.arange(n) / n
    for k in range(0, n, 2048):
        c = 400 + (x[k] ** 2.2) * 9000; seg = w[k:k + 2048] * np.hanning(min(2048, n - k))[:, None]
        S = np.fft.rfft(seg, axis=0); f = np.fft.rfftfreq(len(seg), 1 / SR); S *= np.exp(-((np.log(f + 1) - np.log(c)) ** 2) / .9)[:, None]
        out[k:k + 2048] += np.fft.irfft(S, len(seg), axis=0)
    out *= (x ** 2.0)[:, None]; out /= np.abs(out).max() + 1e-9
    i = int((t_end - dur) * SR); y = np.zeros_like(mix); y[i:i + n] = out * g; return y
def impact(t, g=.9, f0=80):
    n = int(2.8 * SR); x = np.arange(n) / SR; fr = f0 * np.exp(-x * 1.5) + 34
    s_ = np.sin(2 * np.pi * np.cumsum(fr) / SR) * np.exp(-x * 2.6)
    nz = lp(rng.standard_normal((n, 1)), 2500)[:, 0] * np.exp(-x * 4) * .5
    y = np.zeros_like(mix); i = int(t * SR); sig = np.stack([s_ + nz, s_ + nz], 1) * g; y[i:i + n] = sig[:len(y) - i]; return y
fx = np.zeros_like(mix)
for tb in (6, 12, 20, 28, 36, 44, 50, 58, 68, 74): fx += riser(tb, 1.4, .30)
fx += riser(2, 1.8, .25) + riser(4, 1.8, .30)
for ti, g in ((0.0, .20), (2.0, .28), (4.0, .32), (6.1, .28), (74.0, .38)): fx += impact(ti, g)
mix += fx * .8 + rv(fx, IR_HALL) * .35
# fades: gentle fade-in on the first second, long tail on the end
mix *= np.minimum(1, T / 0.6)[:, None] ** 1.0
tail = np.clip((DUR + 1.8 - T) / 2.4, 0, 1) ** 1.5; mix *= tail[:, None]
mix = hp(mix, 28)
# master: gentle glue compression (block rms, 2.5:1) + tone shaping, no hard saturation
mix = lp(mix, 15000)
blk = 1024; nb = len(mix) // blk
rms = np.sqrt((mix[:nb * blk] ** 2).reshape(nb, blk, 2).mean((1, 2))) + 1e-6
g = np.where(rms > .16, (.16 / rms) ** (1 - 1 / 2.5), 1.0)
g = np.convolve(g, np.ones(8) / 8, 'same'); g = np.minimum(g, np.concatenate([[1], g[:-1]]) * 1.02)
gain = np.repeat(g, blk); gain = np.concatenate([gain, np.full(len(mix) - len(gain), g[-1])])
mix *= gain[:, None] * 1.25
pk = np.abs(mix).max(); mix = np.where(np.abs(mix) > .8, np.sign(mix) * (.8 + .2 * np.tanh((np.abs(mix) - .8) / .2)), mix)
mix = mix[:int((DUR + 1.5) * SR)]; mix /= np.abs(mix).max() * 1.05
wavfile.write('music.wav', SR, (mix * 32000).astype('<i2'))
print('ok', len(mix) / SR, 's')
