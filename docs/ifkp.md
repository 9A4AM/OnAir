# IFKP (Incremental Frequency Keying Plus)

IFKP is the keyboard mode of the fldigi family that uses 33 tones with the same "step from the previous tone" keying as FSQ, at three speeds and with a wider band of 387 Hz. It is a separate mode here: the tone spacing, the symbol rates and the text conventions differ from FSQ.

## What the mode does

- Three speeds, found automatically: **0.5, 1.0 and 2.0** (1.95, 3.9 and 7.8 baud: symbols of 8192 / 4096 / 2048 samples at 16 kHz). The receiver can also be forced to one.
- Upper sideband, the signal anywhere between dial + 100 Hz and dial + 3.9 kHz. The presets (80 to 10 m) are starting points, not an official band plan.
- Text of 7-bit characters. Callsigns are highlighted; the log shows one row per line.
- Receive only. The built-in simulator sends a CQ call at speed 1.0.

## How it works

The 33 tones are 11.719 Hz apart (3 bins of a 4096-point spectrum at 16 kHz). The data is the step from the previous tone: tone = (previous + symbol + 1) mod 33. A character is one symbol (space, a-z and `.`) or two (a marker symbol 29, 30 or 31 after the first); symbol 0 alone is the idle character. The line feed is the pair 28, 30. The receiver chain is the one of FSQ (see [fsq.md](fsq.md)): spectrum search of a 33-tone box, tone grid from the spectral peaks, 73 positions integrated in blocks, speed and timing from the sums, early/late and phase loops.

## Screens

Band presets, speed selector, Listen, the waterfall of the passband with the band found marked on it, how often each of the 73 tone positions was the strongest, the big text and the chat log.

## What is verified and what is not

Verified against the generator written for this mode (`Kbd33Generator`, `tests/ifkp`): the three speeds, noise, tuning errors of 90 Hz, any position in the passband, symbol clock errors of 250 to 300 ppm, drift, fading, an echo, a late start, a forced speed, noise only and an FSQ signal (not IFKP).

**Not verified against a real IFKP transmitter**: no recording at hand. The parameters and the varicode come from fldigi's sources; the image and avatar transfers of the mode are not decoded.

## Credits

IFKP is a mode of the fldigi project (W1HKJ, Dave Freese and contributors, GPL-3.0-or-later): the tone spacing, symbol lengths, keying rule and varicode table are from its sources. The receiver and the generator are new code. See THIRD_PARTY.md.
