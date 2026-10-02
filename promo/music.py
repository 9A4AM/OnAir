#!/usr/bin/env python3
"""OnAir promo score, v3: a film score that keeps topping itself. Quiet intro build -> hit 1 (title), steady, build -> hit 2 (live TV),
build -> hit 3 (signal views), a breath, then the biggest build of the film -> the climax at "All of it. At once." and a warm resolve.
Composed as MIDI stems (piano, pad, strings, horn, bass, vibraphone, timpani, drums, glockenspiel cues), rendered with FluidSynth
(FluidR3 GM soundfont), mixed with a long hall reverb and light glue compression.
120 BPM grid (one bar = 2 s): every scene boundary, hit and on-screen cue (CUES in scenes.js) falls on a beat."""
import numpy as np, subprocess, os, random, re
from mido import MidiFile, MidiTrack, Message, MetaMessage, bpm2tempo
from scipy.io import wavfile
from scipy import signal

SR = 44100
OUT = os.environ.get('OUT', 'music_work'); os.makedirs(OUT, exist_ok=True)
SF2 = '/usr/share/sounds/sf2/FluidR3_GM.sf2'
BAR = 2.0; NBARS = 49; DUR = NBARS * BAR           # 98 s
rnd = random.Random(11)
HITS = [(4.0, 1), (24.0, 2), (46.0, 3), (80.0, 4)]   # time, size
BUILDS = [(0.0, 4.0), (20.0, 24.0), (42.0, 46.0), (72.0, 80.0)]

CH = [  # Am9 - Fmaj7 - Cmaj7 - G6
    dict(root=45, notes=[57, 60, 64, 67, 71]),
    dict(root=41, notes=[53, 57, 60, 64, 67]),
    dict(root=48, notes=[55, 60, 64, 67, 71]),
    dict(root=43, notes=[55, 59, 62, 64, 69]),
]
MOTIF = [[81, 79, 76, 72], [84, 81, 79, 77], [79, 76, 79, 83], [86, 83, 81, 79]]
def chord_at(t):
    for h, _ in HITS:
        if h - 4 <= t < h: return CH[3]            # every build hangs on G6
        if h <= t < h + 4: return CH[0]            # every hit lands on Am9
    if t >= 90: return CH[0]
    return CH[(int(t // BAR) // 2) % 4]

def energy(t):                                     # 0..10, the film's intensity
    if 76 <= t < 80: return 7                       # the finale's groove is already running under the last build
    for a, b in BUILDS:
        if a <= t < b: return None
    if t < 10: return 4
    if t < 20: return 4.5
    if t < 24: return 4.5
    if t < 40: return 6
    if t < 42: return 4.5
    if t < 54: return 7
    if t < 64: return 7.5
    if t < 70: return 5
    if t < 72: return 3
    if t < 84: return 8.5 if t >= 80 else 8
    if t < 90: return 5
    return 3

class Stem:
    def __init__(s, program=0, ch=0): s.ev = []; s.cc = []; s.program = program; s.ch = ch
    def add(s, t, dur, note, vel, jitter=0.0015):
        t = max(0, t + rnd.uniform(-jitter, jitter)); vel = int(max(1, min(127, vel + rnd.randint(-1, 1)))); s.ev.append((t, dur, note, vel))
    def ramp(s, t0, t1, v0, v1, ctrl=11, steps=24):
        for k in range(steps + 1): s.cc.append((t0 + (t1 - t0) * k / steps, ctrl, int(v0 + (v1 - v0) * (k / steps) ** 1.4)))
    def save(s, path):
        mid = MidiFile(ticks_per_beat=480); tr = MidiTrack(); mid.tracks.append(tr)
        tr.append(MetaMessage('set_tempo', tempo=bpm2tempo(120)))
        if s.ch != 9: tr.append(Message('program_change', program=s.program, channel=s.ch, time=0))
        msgs = []
        for t, d, n, v in s.ev:
            msgs.append((int(t * 960), 1, Message('note_on', note=n, velocity=v, channel=s.ch)))
            msgs.append((int((t + d) * 960), 0, Message('note_off', note=n, velocity=0, channel=s.ch)))
        for t, ctl, val in s.cc: msgs.append((int(t * 960), 0, Message('control_change', control=ctl, value=max(0, min(127, val)), channel=s.ch)))
        msgs.sort(key=lambda m: (m[0], m[1])); last = 0
        for tk, _, m in msgs: m.time = tk - last; last = tk; tr.append(m)
        mid.save(path)

piano = Stem(0); pad = Stem(89); strings = Stem(48); horn = Stem(60); bass = Stem(39); vibes = Stem(11); timp = Stem(47); drums = Stem(0, 9); bells = Stem(9)
kicks = []

def roll(stem, t_end, dur, note, v0, v1, drum=False, **kw):
    """roll on the 120 BPM grid that ends on t_end: quarters, eighths, sixteenths, thirty-seconds (seconds-before-end bands)"""
    offs = []
    for far, near, step in ((4.0, 3.0, .5), (3.0, 2.0, .25), (2.0, 1.0, .125), (1.0, 0.0, .0625)):
        x = far
        while x > near + 1e-6:
            if x <= dur + 1e-6: offs.append(x)
            x -= step
    n = len(offs)
    for i, x in enumerate(offs):
        v = int(v0 + (v1 - v0) * i / max(1, n - 1))
        (drums.add(t_end - x, .1, note, v, 0.001) if drum else timp.add(t_end - x, .4, note, v, 0.001))

# ---------------------------------------------------------------- arrangement
# strings / pad: one sustained, swelling chord for each build; otherwise a chord per bar
for a, b in BUILDS:
    ch = chord_at(a + .1)
    for n in (ch['notes'][0] - 12 if ch['notes'][0] > 55 else ch['notes'][0], ch['notes'][2], ch['notes'][3], ch['notes'][4]): strings.add(a, b - a + .6, n, 58, 0)
    for n in ch['notes']: pad.add(a, b - a + .6, n, 44, 0)
    strings.ramp(a, b, 22 if a == 0 else (30 if b - a <= 4 else 44), 118); pad.ramp(a, b, 20, 100, ctrl=11)
for b in range(NBARS):
    t0 = b * BAR; E = energy(t0)
    if E is None: continue
    c = chord_at(t0); T_ = c['notes']; first = (b % 2 == 0)
    for n in T_: pad.add(t0, BAR * 1.05, n, 38 + 3 * E, 0)
    for n in (T_[0] - 12 if T_[0] > 55 else T_[0], T_[2], T_[3]): strings.add(t0, BAR * 1.05, n, 26 + 6 * E, 0)
    # piano: slow rising quarter notes, then a flowing eighth-note ostinato, doubled in the octave at the climax
    if E < 5:
        pat = [0, 2, 3, 4] if first else [1, 3, 4, 3]
        for k, i in enumerate(pat): piano.add(t0 + k * 0.5, 1.6 - k * .1, T_[i], 46 - k * 2)
    else:
        pat = ([0, 2, 3, 4, 3, 2, 4, 3] if first else [1, 3, 4, 3, 2, 3, 4, 2])
        for k, i in enumerate(pat): piano.add(t0 + k * 0.25, .55, T_[i], 40 + 3 * E + (6 if k % 2 == 0 else 0))
        if E >= 8:
            for k, i in enumerate(pat): piano.add(t0 + k * 0.25, .5, T_[i] + 12, 36 + 2 * E)
    # bass
    if E >= 3:
        if E < 4.5: bass.add(t0, BAR * .98, c['root'], 50)
        elif E < 6:
            for k, o in enumerate([0, 0, 0, 12]): bass.add(t0 + k * .5, .48, c['root'] + o, 50)
        elif E < 8:
            for k, o in enumerate([0, 0, 12, 0]): bass.add(t0 + k * .5, .45, c['root'] + o, 58)
        else:
            for k in range(8): bass.add(t0 + k * .25, .22, c['root'] + (12 if k % 4 == 2 else 0), 60 + (8 if k % 2 == 0 else 0))
    # horn pads under the big sections
    if E >= 4.5:
        for n in (T_[0], T_[3]): horn.add(t0, BAR * 1.05, n, 32 + 4 * E, 0)
    # vibraphone melody
    if 3.5 <= E <= 8:
        mo = MOTIF[(int(t0 // BAR) // 2) % 4]; vibes.add(t0, .95, mo[(b % 2) * 2], 44); vibes.add(t0 + 1.0, .95, mo[(b % 2) * 2 + 1], 40)
        if E >= 7: vibes.add(t0 + 1.5, .45, mo[(b % 2) * 2 + 1] + 12, 36)
    if E >= 10:
        mo = MOTIF[(int(t0 // BAR) // 2) % 4]
        for k, n in enumerate(mo): vibes.add(t0 + k * .5, .45, n + 12, 56)
    # drums
    kb = [] if E < 3.5 else ([0] if E < 6 else ([0, 1.0] if E < 7 else [0, .5, 1.0, 1.5]))
    for tb in kb: drums.add(t0 + tb, 0.1, 36, 42 + 4 * E, 0.002); kicks.append(t0 + tb)
    if E >= 5:
        sixteenth = E >= 8
        for k in range(16 if sixteenth else 8):
            step = .125 if sixteenth else .25; drums.add(t0 + k * step, .05, 42, 16 + 2 * E + (6 if k % 2 == 1 else 0), 0.003)
    if E >= 7:
        for k in (.5, 1.5): drums.add(t0 + k, .1, 39, 40 + 4 * E, 0.003)
    if E >= 8:
        for k in (.5, 1.5): drums.add(t0 + k, .1, 38, 36 + 3 * E, 0.003)
# resolve: a long held chord under the last bars
timp.add(90.0, 3.0, 33, 40)
bass.add(90.0, 6.0, 33, 50)
# ---------------------------------------------------------------- builds and hits
for (a, b), (h, size) in zip(BUILDS, HITS):
    ch = chord_at(h + .1)
    # piano and hat energy inside the build (eighth notes that crescendo)
    if b - a > 4 or a > 0:
        k0 = a + (b - a) * .35; n_ = int((b - k0) / .25)
        for k in range(n_): piano.add(k0 + k * .25, .5, chord_at(k0 + .1)['notes'][[0, 2, 3, 4, 3, 2][k % 6]], 30 + int(40 * k / n_))
    # timpani / snare roll into the hit
    roll(timp, h, min(4.0, b - a), 33 if a > 0 else 33, 30, 100, start_gap=.5)
    if a > 0:
        roll(drums, h, min(3.0, b - a), 38, 25, 95, start_gap=.25, min_gap=.0625, drum=True)
    # the hit itself: timpani, kick, crash, horn + strings chord
    timp.add(h, 2.5, 33, 85 + 8 * size); timp.add(h, 2.5, 40, 70 + 8 * size)
    drums.add(h, 1.5, 49, min(127, 60 + 14 * size), 0.002); drums.add(h, .2, 36, 88, 0.002); kicks.append(h)
    for n in ch['notes'][:4]: horn.add(h, 4.0, n, 48 + 8 * size, 0)
    horn.ramp(h - .05, h + 4.0, 105, 60, steps=12)
# the title: quarter-note heartbeat of soft timpani, then a bar of eighths, with sparse piano on the beat
for t, v in [(0.0, 24), (1.0, 30), (2.0, 38), (2.5, 46), (3.0, 56), (3.25, 64), (3.5, 74), (3.75, 86)]: timp.add(t, .6, 33 if v < 60 else 40, v)
for t, n, v in [(1.0, 76, 34), (2.0, 79, 34), (3.0, 81, 40), (3.5, 84, 44)]: piano.add(t, 1.6, n, v)
# the finale stack: three octave hits right on the climax
for n in (45, 57, 69): piano.add(80.0, 3.0, n, 100)

# ---------------------------------------------------------------- on-screen cues: soft glockenspiel notes on the beat
_js = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scenes.js')).read()
_blk = re.search(r'const CUES=\[(.*?)\];', _js, re.S).group(1)
CUES = [float(x) for x in re.findall(r'\d+\.\d+', re.sub(r'//[^\n]*', '', _blk))]
scale = [76, 79, 81, 84, 88]
for i, t in enumerate(CUES): bells.add(t, 0.3, scale[i % 5], 38, 0.0)

stems = dict(piano=piano, pad=pad, strings=strings, horn=horn, bass=bass, vibes=vibes, timp=timp, drums=drums, bells=bells)
audio = {}
for name, st in stems.items():
    mid = f'{OUT}/{name}.mid'; wav = f'{OUT}/{name}.wav'; st.save(mid)
    subprocess.run(['fluidsynth', '-ni', '-q', '-g', '0.8', '-r', str(SR), '-F', wav, SF2, mid], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    sr, x = wavfile.read(wav); x = x.astype(np.float32) / 32768.0
    if x.ndim == 1: x = np.stack([x, x], 1)
    n = int(DUR * SR) + SR * 4
    y = np.zeros((n, 2), np.float32); y[:min(n, len(x))] = x[:n]; audio[name] = y

# ---------------------------------------------------------------- level the stems (measured over the steady middle of the film)
TARGET = dict(piano=.050, pad=.036, strings=.036, horn=.030, bass=.030, vibes=.022, timp=.030, drums=.026, bells=.012)
for n, tg in TARGET.items():
    seg = audio[n][26 * SR:60 * SR]; r = float(np.sqrt((seg ** 2).mean())) + 1e-9; audio[n] *= tg / r
# ---------------------------------------------------------------- mix
N = int(DUR * SR); T = np.arange(N + SR * 4) / SR
def reverb_ir(t60=2.4, pre=0.02, damp=6000, seed=1):
    r = np.random.default_rng(seed); n = int(t60 * 1.3 * SR); t = np.arange(n) / SR
    ir = r.standard_normal((n, 2)) * np.exp(-6.9 * t / t60)[:, None]
    sos = signal.butter(2, damp, fs=SR, output='sos'); ir = signal.sosfilt(sos, ir, axis=0)
    ir[:int(pre * SR)] = 0; ir[:int(.002 * SR)] = 0
    return (ir / np.sqrt((ir ** 2).sum() / 2)).astype(np.float32)
def rv(x, ir): return np.stack([signal.fftconvolve(x[:, c], ir[:, c])[:len(x)] for c in (0, 1)], 1)
IR_HALL = reverb_ir(3.6, 0.03, 4800, 1); IR_ROOM = reverb_ir(1.0, 0.008, 6000, 2)
def hp(x, f): return signal.sosfilt(signal.butter(2, f, 'hp', fs=SR, output='sos'), x, axis=0)
def lp(x, f): return signal.sosfilt(signal.butter(2, f, 'lp', fs=SR, output='sos'), x, axis=0)
def echo(x, d=0.5, fb=.3, mix=.25):
    n = int(d * SR); y = x.copy()
    for k in range(1, 4): y[k * n:] += x[:len(x) - k * n] * (fb ** k) * mix
    return y
duck = np.ones(len(T), np.float32)
for tk in kicks:
    i = int(tk * SR); seg = np.arange(len(T) - i)[:int(.5 * SR)] / SR
    duck[i:i + len(seg)] *= (1 - .16 * np.exp(-seg / .14)).astype(np.float32)

mix = np.zeros((len(T), 2), np.float32)
p = hp(audio['piano'], 90);                                  mix += (p * .80 + hp(p, 2800) * .5 + rv(p, IR_HALL) * .75)
pd = hp(lp(audio['pad'], 3600), 110) * duck[:, None];        mix += (pd * .55 + rv(pd, IR_HALL) * .40)
s = hp(lp(audio['strings'], 4200), 140) * duck[:, None];     mix += (s * .55 + rv(s, IR_HALL) * .50)
hn = hp(lp(audio['horn'], 3000), 120);                       mix += (hn * .55 + rv(hn, IR_HALL) * .5)
b_ = lp(audio['bass'], 600) * duck[:, None];                 mix += b_ * .85
v = audio['vibes'];                                          mix += (echo(v, .5, .3, .3) * .55 + hp(v, 2500) * .4 + rv(v, IR_HALL) * .6)
tp = lp(audio['timp'], 2500);                                mix += (tp * .9 + rv(tp, IR_HALL) * .6)
bl = audio['bells'];                                         mix += (echo(bl, .5, .3, .3) * .5 + rv(bl, IR_HALL) * .7)
d = lp(audio['drums'], 9000);                                mix += (d * .85 + hp(d, 5000) * .4 + rv(d, IR_ROOM) * .18)
rng = np.random.default_rng(3)
def riser(t_end, dur, g):
    """tonal riser: a bright saw sweeping an octave pair up to E (a chord tone of every hit chord) plus a soft filtered-noise bed"""
    n = int(dur * SR); x = np.arange(n) / n
    f = 164.81 * 2 ** (3 * x ** 1.6)                  # E3 -> E6
    ph = np.cumsum(2 * np.pi * f / SR)
    tone = sum(np.sin(k * ph) / k for k in range(1, 9)) * .5 + np.sin(ph * 2.0 * 1.003) * .3
    tone = tone * (1 - np.exp(-x * 8)) * (x ** 2.0)
    w = rng.standard_normal(n); sos = signal.butter(2, [400, 7000], 'bp', fs=SR, output='sos'); w = signal.sosfilt(sos, w) * (x ** 3.0) * .5
    sig = (tone + w)[:, None] * np.ones((1, 2)); sig /= np.abs(sig).max() + 1e-9
    i = max(0, int((t_end - dur) * SR)); y = np.zeros_like(mix); m = min(n, len(y) - i); y[i:i + m] = sig[:m] * g; return y
def impact(t, g, f0=70):
    n = int(4.0 * SR); x = np.arange(n) / SR; fr = f0 * np.exp(-x * 1.1) + 36
    s_ = np.sin(2 * np.pi * np.cumsum(fr) / SR) * (np.minimum(1, x / .02) * np.exp(-x * 1.5))
    nz = lp(rng.standard_normal((n, 1)), 3500)[:, 0] * np.exp(-x * 3.2) * .5
    y = np.zeros_like(mix); i = int(t * SR); sig = np.stack([s_ + nz, s_ + nz], 1) * g; y[i:i + n] = sig[:len(y) - i]; return y
fx = np.zeros_like(mix)
for (a, b), (h, size), g in zip(BUILDS, HITS, (.22, .26, .28, .30)): fx += riser(h - .04, b - a, g)
for (h, size), g in zip(HITS, (.12, .20, .24, .26)): fx += impact(h, g)
mix += fx + rv(fx, IR_HALL) * .35
# gain riding: lift the final build, trim the climax, settle for the outro
mix *= np.minimum(1, T / .8)[:, None]
tail = np.clip((DUR + 2.4 - T) / 3.4, 0, 1) ** 1.5; mix *= tail[:, None]
mix = hp(mix, 28); mix = lp(mix, 14500)
# loudness leveller: ride the gain so every section sits within ~2 dB of the target curve (no sudden jumps), hits keep their transient
blk = 2205                                             # 50 ms
nb = len(mix) // blk
rms = np.sqrt((mix[:nb * blk] ** 2).reshape(nb, blk, 2).mean((1, 2))) + 1e-5
win = 30                                               # 1.5 s centred window
sm = np.sqrt(np.convolve(rms ** 2, np.ones(win) / win, 'same'))
tt = (np.arange(nb) + .5) * blk / SR
want = np.interp(tt, [0, 4, 8, 24, 66, 72, 76, 80, 84, 90, 98], [.075, .110, .095, .105, .105, .100, .108, .118, .10, .07, .055])
g = np.clip(want / sm, .4, 4.5)
g = np.convolve(g, np.ones(20) / 20, 'same'); gain = np.repeat(g, blk); gain = np.concatenate([gain, np.full(len(mix) - len(gain), g[-1])])
mix *= gain[:, None]
mix = np.where(np.abs(mix) > .85, np.sign(mix) * (.85 + .15 * np.tanh((np.abs(mix) - .85) / .15)), mix)
mix = mix[:int((DUR + 2.4) * SR)]; mix /= np.abs(mix).max() * 1.05
wavfile.write('music.wav', SR, (mix * 32000).astype('<i2'))
print('ok', len(mix) / SR, 's')
