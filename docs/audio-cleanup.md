# Audio cleanup

A chain that cleans the sound you hear. It sits in `AudioOut::write()` (`core/src/audio/audioout.cpp`), so every mode that plays sound gets it. The decoders, the radio clips and the live transcripts always receive the raw sound; only the speaker (and, if you ask, the audio recorder) gets the cleaned one.

Open it with the sliders button next to the volume. **Basic**: On/Off and one strength slider. **Advanced**: every stage. Each kind of mode keeps its own settings (saved in the preferences as `ac.<kind>.<field>`), started from the preset below. Everything is **off** until you switch it on.

## Stages (in this order)

| Stage | What it does |
|---|---|
| Auto notch | Finds steady narrow tones (whistles, carriers, heterodynes), up to 4 at once, in a 1024-point spectrum every 10 ms. A tone has to be 18 dB above its surroundings and steady in level and frequency for about 0.25 s before it is cut, so speech harmonics are not touched. The cut is a narrow band-pass subtracted from the sound (a trapezoidal state-variable filter that follows the tone as it drifts). It lets go again 0.35 s after the tone stops. |
| Manual notch | One notch at a frequency and width of your choice. |
| Noise reduction | Short-time spectrum (256 samples, 50 % overlap). The noise level per frequency is tracked as the minimum of the smoothed spectrum over the last 1.5 s (minimum statistics), the gain is the Wiener rule with a decision-directed signal-to-noise estimate, smoothed over frequency and limited in how fast it falls, which keeps "musical noise" away. Strength sets the largest cut (6 to 28 dB) and how much noise is assumed. |
| Voice emphasis | High-pass at 100 Hz, a small dip at 280 Hz and a lift of 5 dB at 2.8 kHz, blended in by the amount. |
| De-esser | Takes the 6.5 kHz band down while it dominates the sound. Off below 16 kHz sample rate. |
| Silence gate | Mutes when the level is not 9 dB (adjustable) above the noise floor (minimum over the last 4 s), with 150 ms hang, 2 ms attack, 30 ms release. Also the "squelch-aware" muting for recordings. |
| Leveller | Brings the level (50 ms RMS) towards a target (default -20 dBFS) by "balance" percent, up to +18 dB and down to -18 dB, slow up (0.6 s), fast down (8 ms), with a soft limiter at the end. "Do not boost the noise" freezes the boost while the level is within 8 dB of the noise floor. |

## Presets per kind of mode

| Kind | Modes | Preset (when switched on) |
|---|---|---|
| Data (off) | every data mode, TV, spectrum | nothing |
| Generic | General receiver | NR 50 %, auto notch, leveller 60 % |
| AM / airband | Airband | NR 50 %, auto notch, leveller 80 %, gate +10 dB, voice 30 % |
| NFM / VHF / UHF | pick it in the pop-up for NFM listening | NR 60 %, auto notch, leveller 70 %, gate +9 dB, voice 50 %, de-esser 40 % |
| WFM broadcast | FM, HD Radio, DAB | leveller 40 % at -18 dB without the noise hold, soft limiter; no NR, no gate |
| SSB / HF | pick it in the pop-up | NR 70 %, auto notch, leveller 80 %, voice 40 % |
| CW | CW | NR 30 %, leveller 50 %. The auto notch is off on purpose: it would cut the wanted tone. The mode's own audio filter comes first, so the NR only works on what is left in the filter's passband. |
| Digital voice | DMR, M17, D-STAR, NXDN, dPMR, TETRA, P25, Iridium | leveller 50 % only: the vocoder output is clean |

The kind is chosen from the mode automatically; "Settings for" in the pop-up overrides it per mode.

## Recording

"Record the cleaned sound" makes **Record audio** save the cleaned sound instead of the raw one; "Skip silence in the recording" additionally records nothing while the silence gate holds the sound off.

## Real time

Nothing allocates or locks in `process()`: all buffers are sized in `AudioCleanup::prepare()` when the output starts. The settings are atomics written by the UI thread. Every stage fades in and out (about 10 to 20 ms), so switching anything is click-free. Latency is 256 samples (5.3 ms at 48 kHz, 128 samples at 8 to 24 kHz, scaled up for higher rates) while the chain is on, and zero when it is off (the chain returns its input unchanged). A mono signal (left equal to right) is processed once.

## Measured (`tests/audio/test_audio_cleanup.cpp`, 48 kHz, release build)

- Noise reduction on a harmonic speech-like signal in white noise at 6.3 dB SNR: SNR improved by 8.7 / 9.3 / 8.8 dB at strength 30 / 60 / 100 %; noise between the bursts down 12.6 / 19 / 27 dB. On steady white noise alone: 27 dB at strength 100 %.
- Clean speech-like signal through NR plus auto notch: error 89 dB below the signal (transparent); the chain with all stages off is a pure delay (SNR > 60 dB).
- Auto notch on a 1.5 kHz whistle drifting 60 Hz/s plus 40 Hz wobble, 0.15 amplitude next to the speech: 24 dB removed after 5 s; speech SNR from -0.3 dB to 15.5 dB; speech alone is not damaged (> 30 dB); the notch lets go after the tone stops.
- Manual notch: 1 kHz tone down below 0.01 of 0.3, the 2.5 kHz tone untouched.
- Leveller: speech at -16 dB, 0 dB and +9.5 dB gain ends at -21.6 / -20.3 / -19.9 dBFS for a -20 dBFS target; peaks never above 1.0; hiss at -54 dBFS is not boosted; a 20 dB quiet/loud difference becomes 5 dB at 80 % balance.
- Silence gate: 24 dB down between bursts, speech level unchanged (0.00 dB).
- De-esser: 6.9 dB off a sibilant burst, 0.01 dB change to the speech around it.
- Switching stages at random every 70 ms on a 440 Hz sine: largest sample step equal to the sine's own (no clicks).
- Another thread rewriting the settings while the audio runs: finite, bounded output.
- CPU (one core, every stage on, dual-mono input): 0.8 to 1.4 % at 48 kHz; stereo with different channels (worst case) 1.2 to 2.5 %.

## Credits

Algorithms are our own implementation from the published papers (no code taken): R. Martin, minimum statistics noise tracking (2001); Y. Ephraim and D. Malah, decision-directed MMSE spectral gain (1984); A. Simper's trapezoidal state-variable filter; R. Bristow-Johnson's Audio EQ Cookbook. Listed in the in-app Credits and `tools/credits_data.tsv`.

## Not the same as

The neural noise remover (RNNoise) of the General receiver and Airband modes works earlier, inside the demodulator, and stays as it is. Use one or the other; both together is harmless but cleans no more.
