# Tone decoders (CTCSS, DCS, DTMF, selcall, SELCAL)

The mode **Tone decoders (CTCSS, DCS, DTMF, selcall)** (digital voice and two-way radio group) listens to the audio of a narrow-FM channel, or to a sound card, and
reports every signalling tone and code in it. It is receive only.

## What it decodes

| System | What is reported |
| --- | --- |
| CTCSS | the 50 tones of the EIA / TIA-603 list and its extension (67.0 to 254.1 Hz, "standard" or "extended"), with the measured frequency; the level of all 50 tones as bars |
| DCS | the 23 bit Golay (23,12) code word at 134.4 bit/s, as D023N or D023I (normal or inverted polarity), found at any start phase of the repeating word; whether the code is in the usual list of 83; the other code the same waveform reads as |
| DTMF | the 16 keys; keys pressed within 2.5 s are shown as one string |
| Five-tone sequential calls | ZVEI1 / PZVEI, ZVEI2 / ZVEI3, CCIR, EEA, EIA and DZVEI, with the repeat tone resolved to the repeated digit (the raw tones are in the details); 3 to 12 tones |
| SELCAL | the 16 tones of the aviation plan, two pairs (AB-CD) |
| Two-tone sequential | a generic page of a first tone (0.25 to 2 s) and a second one (0.8 s and longer): both frequencies are measured and shown |

The Live tab shows the sub-audible tone or code now, the strongest voice-band tone, the DTMF keys, the audio spectrum and the CTCSS bars. The History tab lists everything
heard with its length and details.

## The gate

Under Receiver the sound can be opened (a) always, (b) on any CTCSS tone or DCS code, (c) only on the chosen tone or code. It works like the squelch of a radio with CTCSS or
DCS. It acts on the **Listen** sound only (the sub-audible tones are filtered out of it); the decoders always run. The state shows in the status bar and in the telemetry.

## Input

- An FM channel from a radio: the same front end as APRS / packet (offset removal, 96 kHz resampling, AFC, discriminator). 5 kHz of deviation is 0 dB.
- A sound-card source (Source panel, sound card): the real audio is used as it is at any rate of 8 kHz and up. Full scale is 5 kHz deviation.

## How it works

All detectors work on the 8 kHz audio. CTCSS: a 4th-order 300 Hz low-pass, 1 kHz, a one-second Hann window, 50 Goertzel filters; a tone is announced when it holds the
strongest place, stands clear of every other tone (3 times the power) and has at least 12 % of the band energy for 0.4 s. DCS: the same band, adaptive threshold, 12 timing
phases, three consecutive words voted bit by bit, Golay check, marker bits 100, both polarities, all 23 rotations; the same code must come twice. DTMF: Goertzel filters over
205 samples with row/column dominance and twist tests. Five-tone: Goertzel filters on the tones of all plans over 16 ms, a two-step change tracker, the sequence closes after
160 ms of silence and is matched against every plan; the plan whose usual tone length is nearest is listed first (tone lengths are vendor specific, so several plans may fit
the same digits: the others are named in the details). SELCAL: 100 ms Hann windows, two tones in balance, each pair at least 0.55 s. Two-tone: a 64 ms FFT with a
parabolic frequency estimate.

## Limits and what is not verified

- Tested with synthetic tones in noise and speech-like interference (harmonic voice with gliding pitch, syllables and fricatives) in loopback, not with real radios.
- The DCS bit polarity of the air (which deviation sign is a 1) is not checked against a real transmitter: if a radio's codes show as the inverted code, the receiver's FM
  sign convention is the cause. The code word layout is checked against the published pattern of code 023 (0x763813).
- The 83-code list is the one commonly published; a valid code outside it is still decoded and marked as not in the usual list.
- Tone lengths of the five-tone plans are not specified here; CCIR (100 ms) and EEA (40 ms) share their tones and are told apart by length only.
- Tone frequency tolerance of the voice-band decoders is about +-1.5 % of the nominal tone; a radio with a larger error is not decoded.
- Encrypted or digital voice squelch systems other than DCS (NAC, colour code) are not part of this mode.

## Sources

Tone lists and layouts come from public descriptions: the EIA / TIA-603 CTCSS list, the DCS word (YO3IIU "Digital Coded Squelch codec", KB8ZQZ "DPL / DCS Information"),
the five-tone plans, the SELCAL plan (ARINC 714). No decoder code of other projects is used.
