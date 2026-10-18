# FSQ (Fast Simple QSO)

FSQ is the directed keyboard mode that is part of fldigi: every station has a callsign, a line says who it is from and who it is for, and the receiver shows the lines addressed to you. It sends one of 33 tones per symbol, slowly enough to be read under the noise with almost no bandwidth: 290 Hz at every speed.

## What the mode does

- Five speeds, found automatically: **1.5, 2, 3, 4.5 and 6** (the names fldigi gives them; symbols of 8192 / 6144 / 4096 / 3072 / 2048 samples at 12 kHz, so 1.46, 1.95, 2.93, 3.91 and 5.86 symbols a second). The receiver can also be forced to one.
- Upper sideband, the signal anywhere between dial + 100 Hz and dial + 3.9 kHz. The presets are starting points (80 to 10 m); look at what the stations in your area use.
- Text of 7-bit characters, with a few Latin-1 signs. Callsigns are highlighted.
- Messages: a line that starts with `CALL:` and two hex digits is shown with its sender; the digits are the CRC-8 of the callsign and tell a real sender from a garbled one. If the text after it begins with a callsign (or `allcall` / `cqcqcq`) followed by a command character, the table shows the addressee, the command and the text. Messages for the callsign typed in the **Callsign** box (or for everybody) are marked. Commands (`?` signal report, `*` acknowledgement, `$` heard list, `@` location, `&` message of the day, `^` software version, `;` relay, `!` and `~` repeat, `#` store, `+` file, `|` alert, `>` and `<` speed) are displayed, never executed: the receiver does not answer.
- Receive only. The built-in simulator sends an FSQ line at 3 baud.

## How it works

The 33 tones are 8.789 Hz apart (3 bins of a 4096-point spectrum at 12 kHz). The data is the step from the previous tone: tone = (previous + symbol + 1) mod 33, symbol 0 to 31, so a tuning error or an unknown start of the band does no harm. A character is one symbol (the 29 commonest: space, a-z, `.`, line feed) or two (a marker symbol 29, 30 or 31 after the first). A transmission starts with a space and a line feed, then `CALL:` and the CRC, and ends with a pause.

Receiver chain:
1. The channel is mixed to 0 Hz and brought to 8 kHz.
2. A 4096-point spectrum, averaged over a couple of seconds, looks for a box 33 tones wide that holds more power than the bands beside it.
3. The tone grid is found from where the spectral peaks of single frames fall modulo the tone spacing; 73 grid positions (the band with 20 on each side) are mixed down and integrated in 2 ms blocks, as running sums.
4. The speed and the symbol timing come from those sums: the right ones put one tone in each window and never the same tone twice in a row. The sums also let the receiver decode the last seven seconds at once, so the start of a message is not lost to the search.
5. Every symbol is the strongest of the positions; the step to the previous tone is decoded to characters. A symbol with too little of the energy in its strongest tone is not used. The window is nudged early/late by the energy of the winning tone, the grid by the phase turn of the tone between the two halves of the symbol (tuning tolerance: the whole passband, drift of 30 Hz a minute; clock errors of 300 ppm).

## Screens

Band presets, speed selector, callsign, Listen (the sideband on the sound card), the waterfall of the passband with the band found marked on it, how often each of the 73 tone positions was the strongest, the table of messages and the big text. The receiver tab shows the speed, tone spacing, the tones heard and a quality figure (about 0.07 for noise, 0.5 and more for a clean signal).

## What is verified and what is not

Verified against the generator written for this mode (`Kbd33Generator`, `tests/fsq`): all five speeds, noise down to about 0 dB in the 290 Hz band, tuning errors of 90 Hz, any position in the passband, symbol clock errors of 250 to 300 ppm, drift, fading, an echo, a signal that starts late, a forced speed, directed and undirected lines, noise only and an IFKP signal (not FSQ).

**Not verified against a real FSQ transmitter**: there is no recording of one at hand. The parameters, the varicode and the CRC come from fldigi's sources; what the sources do not fix (the exact shape of the tone changes, the transmit timing of the lead-in) was chosen to be self-consistent. If a real recording does not decode, compare those first.

## Credits

FSQ is the mode of the fldigi project (W1HKJ, Dave Freese and contributors, GPL-3.0-or-later): the tone spacing and symbol lengths, the keying rule, the varicode table, the CRC and the command characters are from its sources. The receiver, the synchroniser and the generator are new code. See THIRD_PARTY.md.
