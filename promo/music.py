#!/usr/bin/env python3
"""OnAir promo score, v2: a restrained cinematic cue to match the calm, slow visuals.
Composed as MIDI stems (piano, pad, strings, bass, vibraphone, soft drums, glockenspiel cues), rendered with FluidSynth
(FluidR3 GM soundfont), then levelled and mixed with a long hall reverb and gentle glue compression.
120 BPM grid (one bar = 2 s) so every scene boundary and on-screen cue (CUES in scenes.js) falls on a beat,
but the music plays half-time: slow sustained piano, pads, a soft pulse only from the middle of the film."""
import numpy as np, subprocess, os, random, re
from mido import MidiFile, MidiTrack, Message, MetaMessage, bpm2tempo
from scipy.io import wavfile
from scipy import signal

SR = 44100
OUT = os.environ.get('OUT', 'music_work'); os.makedirs(OUT, exist_ok=True)
SF2 = '/usr/share/sounds/sf2/FluidR3_GM.sf2'
BAR = 2.0; NBARS = 42; DUR = NBARS * BAR           # 84 s
rnd = random.Random(11)

CH = [  # a chord every two bars: bass root + open voicing (Am9 - Fmaj7 - Cmaj7 - G6)
    dict(root=45, notes=[57, 60, 64, 67, 71]),
    dict(root=41, notes=[53, 57, 60, 64, 67]),
    dict(root=48, notes=[55, 60, 64, 67, 71]),
    dict(root=43, notes=[55, 59, 62, 64, 69]),
]
MOTIF = [[81, 79, 76, 72], [84, 81, 79, 77], [79, 76, 79, 83], [86, 83, 81, 79]]   # vibraphone, one note per beat pair
def chord(b): return CH[(b // 2) % 4]

# energy per bar: 0 title ... 3 peak (reception) ... 1 stripped (output) ... resolve
def energy(b):
    if b < 4: return 0
    if b < 8: return 1
    if b < 12: return 1.5
    if b < 23: return 2
    if b < 27: return 2.5
    if b < 32: return 3
    if b < 35: return 2
    if b < 38: return 1
    if b < 40: return 2
    return 1

class Stem:
    def __init__(s, program=0, ch=0): s.ev = []; s.program = program; s.ch = ch
    def add(s, t, dur, note, vel, jitter=0.008):
        t = max(0, t + rnd.uniform(-jitter, jitter)); vel = int(max(1, min(127, vel + rnd.randint(-4, 4)))); s.ev.append((t, dur, note, vel))
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
    t0 = b * BAR; c = chord(b); T_ = c['notes']; E = energy(b); first = (b % 2 == 0)
    # pad: long chords from the first bar
    for n in T_: pad.add(t0, BAR * 1.05, n, 40 if E < 2.5 else 48, 0)
    # strings: enter with the second section, fuller towards the peak
    if E >= 1:
        for n in (T_[0] - 12 if T_[0] > 55 else T_[0], T_[2], T_[3]): strings.add(t0, BAR * 1.05, n, 34 if E < 2.5 else 46, 0)
    # piano: sparse in the title, then slow rising quarter notes with the pedal down
    if b < 4:
        for (tt, idx, v) in [(1.0, 4, 40), (0.5, 3, 38), (1.5, 4, 36), (0.0, 2, 38)][b:b + 1]: piano.add(t0 + tt, 1.8, T_[idx], v)
    else:
        pat = [0, 2, 3, 4] if first else [1, 3, 4, 3]
        for k, i in enumerate(pat): piano.add(t0 + k * 0.5, 1.6 - k * .1, T_[i], 50 - k * 2 + (4 if E >= 2.5 else 0))
    # bass: sustained roots, a second note at beat 3 near the peak
    if E >= 1 and not (35 <= b <= 37):
        if first or E >= 2: bass.add(t0, BAR * .98, c['root'], 52)
        if E >= 2.5: bass.add(t0 + 1.0, .9, c['root'] + 12, 40)
    if b == 41: bass.add(t0, 4.0, c['root'], 52)
    # vibraphone: a gentle two-note motif in the middle of the film, a third note near the peak
    if 12 <= b <= 34 or 38 <= b <= 39:
        mo = MOTIF[(b // 2) % 4]; vibes.add(t0, .95, mo[(b % 2) * 2], 46); vibes.add(t0 + 1.0, .95, mo[(b % 2) * 2 + 1], 42)
        if E >= 3: vibes.add(t0 + 1.5, .45, mo[(b % 2) * 2 + 1] + 12, 36)
    # soft pulse: kick on 1 and 3 from the second section, all four beats only at the peak, nothing in the stripped parts
    if 1 <= E and E != 1 or (E == 1 and 4 <= b < 8 and b >= 6):
        beats = [0, .5, 1.0, 1.5] if E >= 3 else [0, 1.0]
        for tb in beats: drums.add(t0 + tb, 0.1, 36, 40 if E < 3 else 48, 0.002); kicks.append(t0 + tb)
    if E >= 2 and not (35 <= b <= 37):
        for k in range(8): drums.add(t0 + k * 0.25, 0.05, 42, 18 + (8 if k % 2 == 1 else 0) + (4 if E >= 3 else 0), 0.003)

# ---------------------------------------------------------------- on-screen cues: soft glockenspiel notes on the beat
_js = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scenes.js')).read()
_blk = re.search(r'const CUES=\[(.*?)\];', _js, re.S).group(1)
CUES = [float(x) for x in re.findall(r'\d+\.\d+', re.sub(r'//[^\n]*', '', _blk))]
scale = [76, 79, 81, 84, 88]
for i, t in enumerate(CUES): bells.add(t, 0.3, scale[i % 5], 38, 0.0)

stems = dict(piano=piano, pad=pad, strings=strings, bass=bass, vibes=vibes, drums=drums, bells=bells)
audio = {}
for name, st in stems.items():
    mid = f'{OUT}/{name}.mid'; wav = f'{OUT}/{name}.wav'; st.save(mid)
    subprocess.run(['fluidsynth', '-ni', '-q', '-g', '0.8', '-r', str(SR), '-F', wav, SF2, mid], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    sr, x = wavfile.read(wav); x = x.astype(np.float32) / 32768.0
    if x.ndim == 1: x = np.stack([x, x], 1)
    n = int(DUR * SR) + SR * 4
    y = np.zeros((n, 2), np.float32); y[:min(n, len(x))] = x[:n]; audio[name] = y

# ---------------------------------------------------------------- level the stems
TARGET = dict(piano=.050, pad=.040, strings=.030, bass=.026, vibes=.022, drums=.020, bells=.012)
for n, tg in TARGET.items():
    seg = audio[n][14 * SR:40 * SR]; r = float(np.sqrt((seg ** 2).mean())) + 1e-9; audio[n] *= tg / r
# ---------------------------------------------------------------- mix
N = int(DUR * SR); T = np.arange(N + SR * 4) / SR
def reverb_ir(t60=2.4, pre=0.02, damp=6000, seed=1):
    r = np.random.default_rng(seed); n = int(t60 * 1.3 * SR); t = np.arange(n) / SR
    ir = r.standard_normal((n, 2)) * np.exp(-6.9 * t / t60)[:, None]
    sos = signal.butter(2, damp, fs=SR, output='sos'); ir = signal.sosfilt(sos, ir, axis=0)
    ir[:int(pre * SR)] = 0; ir[:int(.002 * SR)] = 0
    return (ir / np.sqrt((ir ** 2).sum() / 2)).astype(np.float32)
def rv(x, ir): return np.stack([signal.fftconvolve(x[:, c], ir[:, c])[:len(x)] for c in (0, 1)], 1)
IR_HALL = reverb_ir(3.4, 0.03, 4800, 1); IR_ROOM = reverb_ir(1.0, 0.008, 6000, 2)
def hp(x, f): return signal.sosfilt(signal.butter(2, f, 'hp', fs=SR, output='sos'), x, axis=0)
def lp(x, f): return signal.sosfilt(signal.butter(2, f, 'lp', fs=SR, output='sos'), x, axis=0)
def echo(x, d=0.5, fb=.3, mix=.25):
    n = int(d * SR); y = x.copy()
    for k in range(1, 4): y[k * n:] += x[:len(x) - k * n] * (fb ** k) * mix
    return y
duck = np.ones(len(T), np.float32)          # very gentle sidechain from the soft kick
for tk in kicks:
    i = int(tk * SR); seg = np.arange(len(T) - i)[:int(.5 * SR)] / SR
    duck[i:i + len(seg)] *= (1 - .18 * np.exp(-seg / .14)).astype(np.float32)

mix = np.zeros((len(T), 2), np.float32)
p = hp(audio['piano'], 90);                       mix += (p * .80 + hp(p, 2800) * .55 + rv(p, IR_HALL) * .75)
pd = hp(lp(audio['pad'], 3600), 110) * duck[:, None];  mix += (pd * .55 + rv(pd, IR_HALL) * .40)
s = hp(lp(audio['strings'], 4200), 140) * duck[:, None]; mix += (s * .45 + rv(s, IR_HALL) * .45)
b_ = lp(audio['bass'], 600) * duck[:, None];      mix += b_ * .85
v = audio['vibes'];                               mix += (echo(v, .5, .3, .3) * .55 + hp(v, 2500) * .4 + rv(v, IR_HALL) * .6)
bl = audio['bells'];                              mix += (echo(bl, .5, .3, .3) * .5 + rv(bl, IR_HALL) * .7)
d = lp(audio['drums'], 8000);                     mix += (d * .85 + rv(d, IR_ROOM) * .15)
rng = np.random.default_rng(3)
def swell_boom(t, g=.3, f0=62):                    # a soft low swell, not a hit
    n = int(4.0 * SR); x = np.arange(n) / SR; fr = f0 * np.exp(-x * .7) + 38
    s_ = np.sin(2 * np.pi * np.cumsum(fr) / SR) * (np.minimum(1, x / .6) * np.exp(-x * 1.1))
    y = np.zeros_like(mix); i = int(t * SR); sig = np.stack([s_, s_], 1) * g; y[i:i + n] = sig[:len(y) - i]; return y
fx = swell_boom(0.0, .10) + swell_boom(76.0, .16)
mix += fx + rv(fx, IR_HALL) * .4
mix *= np.minimum(1, T / 1.2)[:, None]
tail = np.clip((DUR + 2.2 - T) / 3.2, 0, 1) ** 1.5; mix *= tail[:, None]
mix = hp(mix, 28)
mix = lp(mix, 14000)
blk = 1024; nb = len(mix) // blk
rms = np.sqrt((mix[:nb * blk] ** 2).reshape(nb, blk, 2).mean((1, 2))) + 1e-6
g = np.where(rms > .14, (.14 / rms) ** (1 - 1 / 2.0), 1.0)
g = np.convolve(g, np.ones(12) / 12, 'same'); gain = np.repeat(g, blk); gain = np.concatenate([gain, np.full(len(mix) - len(gain), g[-1])])
mix *= gain[:, None] * 1.2
mix = np.where(np.abs(mix) > .8, np.sign(mix) * (.8 + .2 * np.tanh((np.abs(mix) - .8) / .2)), mix)
mix = mix[:int((DUR + 2.0) * SR)]; mix /= np.abs(mix).max() * 1.05
wavfile.write('music.wav', SR, (mix * 32000).astype('<i2'))
print('ok', len(mix) / SR, 's')
