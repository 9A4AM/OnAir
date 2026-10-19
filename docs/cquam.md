# AM stereo (C-QUAM)

C-QUAM (Motorola) is the AM stereo system used by most AM stereo stations: medium wave in Canada, the USA, Mexico, Brazil, Japan and Australia. A C-QUAM station is still a normal AM station: a mono receiver hears left plus right.

## What the mode does

- Medium wave (and the shortwave broadcast bands) in the AM broadcast style: dial frequency with presets, 9 or 10 kHz channel spacing, audio bandwidth 4.5 / 5 / 7.5 / 10 kHz either side of the carrier. Needs a radio that reaches medium wave (HF / direct sampling), like the AM broadcast entry.
- Finds the **25 Hz pilot** and switches to stereo by itself (**Auto**), or you force **Mono** or **Stereo**. A big STEREO / MONO / NO CARRIER indicator, the carrier-to-noise ratio, the pilot level (about 5 percent of the carrier on a real station), the modulation depth, left and right level bars with a 30 second history, and the spectrum of the quadrature channel from 0 to 60 Hz with the pilot marked.
- Sound: left and right on the sound card with the shared volume and mute. A squelch mutes the noise under a carrier-to-noise ratio of 4 dB. A plain AM station gives the same audio on both sides.
- The older Kahn-Hazeltine, Harris and Magnavox systems (incompatible with C-QUAM) are **not** decoded; their stations play as mono AM.

## How it works

C-QUAM sends the vector (1 + L + R) + j (L - R) on the carrier, with the envelope stretched by 1 / cos(phi) (phi = the stereo phase) so that an envelope detector still hears L + R. The receiver:

1. mixes the channel to 0 Hz and brings it to 48 kHz exactly (the same front end as the AM broadcast entry), then a channel filter of the chosen width;
2. estimates the carrier frequency from the mean phase step and pulls a phase-locked loop on it (natural frequency 30 Hz, narrowed to 4 Hz once locked: well under the pilot), so that the carrier sits on the real axis: tuning errors of several hundred Hz are followed;
3. divides by the carrier amplitude: the real part minus one is L + R, the imaginary part is L - R; L = (m + q) / 2, R = (m - q) / 2;
4. looks for the pilot in the imaginary part every half second (Hann window of one second, Goertzel at 25 Hz against the four bins beside it, at least 12 dB over them and 1 percent of the carrier); two hits switch to stereo, three misses back to mono, with a 40 ms crossfade;
5. high-passes the audio at 50 Hz (pilot and DC out).

## Tests

`tests/cquam`: stereo separation (over 25 dB between the channels), plain AM stays mono (L equals R), noise alone gives silence, tuning error of +-400 Hz, a station off the dial, sample rates from 250 kHz to 2 Msps, carrier-to-noise ratios down to the weakest that still shows stereo (printed), no false stereo on noisy mono stations, flat fading, a retune in the middle, and the engine.

## Not verified

Everything is checked against the synthetic C-QUAM signal of this project (`core/src/cquam/cquam_gen.cpp`, built from the published system description), not against a real station. Real transmitters add selective fading, pre-emphasis, audio processing and adjacent channels; the pilot depth and polarity and the audio low-pass of real stations may differ from the assumptions here.
