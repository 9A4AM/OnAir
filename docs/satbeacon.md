# Satellite beacon

The mode **Satellite beacon** (Weather & science group) monitors the narrow carrier of a geostationary satellite beacon, usually a Ku or Ka band beacon seen
through an LNB, and recovers the telemetry that some beacons carry on it. It is receive only and written clean-room from textbook methods and from our own
analysis of a recording of a beacon.

## What the recorded beacon is

The mode was built for this capture (2 Msps, 11705.250 MHz centre, 224 s, 8-bit IQ), measured with the tools of this mode:

- A **carrier** 236.1 kHz above the centre (11705.486 MHz), 58.7 dB-Hz at the time, drifting down by about 90 Hz over the 224 s (32 Hz per minute at the start,
  14 Hz per minute at the end): the warm-up drift of the LNB's oscillator.
- A second, unmodulated **reference carrier** 545.64 kHz above the first (11706.03 MHz), 10 dB weaker. The spacing is constant to a few Hz while both drift, so
  both come from the same transmitter chain and the same LNB.
- The main carrier is **phase modulated** (about 44 degrees rms, an index of about 1.2 rad) by a **48 kHz subcarrier** that carries **bi-phase (Manchester) data
  at 4800 bit/s**: PCM/PSK/PM in the terms of telemetry standards. A bi-phase stream of constant bits is a 4.8 kHz square wave, which is why the phase-modulation
  spectrum has lines at 48 kHz -/+ 4.8 kHz, -/+ 14.4 kHz, ... and nothing at 48 kHz itself, and why the capture shows lines at 96 kHz either side of the carrier.
- The bits form **frames of 2048 bits (256 bytes)**, 2.34 per second, each starting with the sync word **FAF320** (the IRIG 106 minor-frame sync word), followed by
  0xFE; **byte 4 is a counter that counts 0 to 31**, so 32 minor frames make a major frame of 13.65 s. About half of the bytes never change (00, FF and other
  constants); the rest changes from frame to frame or with the counter. All 457 frames of the recording are received without a bit error against the learned template.
- This is the same format that the DecSAT project (Arclamp) describes for the Ku-band beacon of Intelsat 19 (4800 bit/s bi-phase on 48 kHz, 2048-bit frames on FAF320,
  a counter 0 to 31 in byte 4); the beacon recorded here is on another frequency (11705.486 MHz), so it is probably a sister satellite or the same family of
  transmitters. Which satellite it is, and what the words mean, is **not known**: they are the spacecraft maker's and no public description was found, so the frames
  are recovered and shown as they are, not interpreted.

## How it works

- A 2^18-point Hann spectrum (7.6 Hz bins at 2 Msps) of the whole capture, averaged for one second, gives the narrow lines, their level over the noise and the noise
  density; the strongest line is the main carrier (C/N0 in dB-Hz = carrier level less the noise density per Hz). Lines closer than 120 kHz to the carrier are marked as
  lines of its modulation, the strongest line further away is reported as the reference carrier with its spacing.
- The carrier is mixed to zero, filtered and decimated to 200 to 400 kHz, and followed with a second-order phase-locked loop (15 Hz). Its integrator is the carrier
  frequency; a least-squares slope over the last 5 minutes is the drift. The phase error of the loop is the **phase modulation**; its spectrum (8192 points) is
  shown, and the strongest one or two humps above the floor are the subcarrier (its centre; a bi-phase subcarrier shows two humps either side of it, a plain BPSK one a single hump).
- The subcarrier is mixed down, filtered (about the hump spacing, else 8 kHz) and run through a Costas loop (BPSK). The symbol clock is the strongest line of the edge energy
  of the result (the chip rate of a bi-phase stream, the bit rate of an NRZ one; round rates are snapped to within 1.5 %) and a phase-locked loop on the phase of that line follows it
  (the radio's own clock is a few ppm off). Each symbol is the sign of its middle half.
- Two bit streams are made from the symbols: NRZ, and bi-phase (the sign of the difference of two successive chips). The pairing of the chips is the one in which the two chips
  of a pair are opposite; the stream whose chips are opposite in pairs (the sum of a pair near zero) is bi-phase and is the one shown.
- **Frames**: the known sync words (IRIG 106 FAF320, FE6B2840 and EB90, CCSDS 1ACFFC1D), as sent or inverted, are searched in the last 64 k bits; a word that repeats at a constant
  spacing gives the frame length, the frame start and the polarity. Without one the frame length is the lag with the best self-agreement of the stream (FFT autocorrelation,
  64 to 16384 bits, divisors of the best lag preferred), accepted only when well above chance, and the frame start is the beginning of the longest run of stable columns.
  The bits are folded on the frame length: the majority is the **template**, columns with at least 85 % agreement are the **fixed bits**. Each frame is listed with its errors
  against the template in the fixed columns (a frame that is the inverse of the template is inverted).
- The **counter**: the byte that goes up by one from frame to frame and wraps to 0 is the minor frame counter; the largest value seen plus one is the length of the major frame.

## What is shown

- **Carrier**: frequency (also in RF when the radio's tuning is known), level, C/N0, drift per minute, noise density, the spectrum of the whole capture, the list of narrow lines.
- **Telemetry**: subcarrier, number of humps, bit rate and coding, frame length and sync word, the counter byte, the template and the fixed bits as hex, the phase-modulation
  spectrum and the frames (newest first, hex, with their counter). Copy puts the frames on the clipboard as hex lines.
- Log to file (default on): `satellite_beacon.csv` in the data folder, one `carrier` line per second (time, frequency, level, C/N0, drift, bit rate, frame length) and a
  `frame` line for every frame with its bytes. From the command line: `dect2cli --file capture.cs8 --rate 2 --format cs8 --standard satbeacon --sb-log beacon.csv`.

## Tuning

Anything that puts the carrier inside the capture works: a dish with an LNB and a radio that takes the LNB's IF (the carrier may be anywhere in the capture; 2 Msps
or more shows 1.8 MHz around the centre, 250 ksps is the minimum). An LNB drifts for the first minutes after switch-on: the drift reading is meant to show that.
The test signal (`--synthetic --standard satbeacon`) is a carrier 236 kHz off the centre drifting 0.4 Hz/s, a second carrier 545 kHz above it and 4800 bit/s bi-phase
telemetry on a 48 kHz subcarrier (index 1.2 rad, data clock 7 ppm off) in frames of 256 bytes (FAF320, FE, a counter 0 to 31, fixed words and pseudo-random words; this layout
is made up for the tests); nothing is transmitted.

## What is not done

Only the formats above are looked for: a beacon whose telemetry is on the carrier's phase without a subcarrier (PCM/PM), a suppressed carrier, other sync words, CCSDS Reed-Solomon
and the Viterbi code of CCSDS frames are not handled, and a frame layout is not interpreted. DecSAT does more of this (see Credits).

## Credits

The format of this class of beacon (PCM/PSK/PM, bi-phase on a subcarrier, IRIG 106 and CCSDS sync words, the minor frame counter) was recognised thanks to the published
description in the README of DecSAT by Arclamp ([CasualArclamp](https://github.com/CasualArclamp/DecSAT), GPL-3.0-or-later). No code of DecSAT is in this mode.

## Tests

`tests/satbeacon`: the generator's golden frames, the frame length finder, the line finder's frequency, level and noise density, the whole chain on the test signal (carrier, drift,
C/N0, subcarrier, bit rate, bi-phase, sync word, frame length, counter, frames equal to the generator's), the reference carrier, an unmodulated carrier, and a sweep of impairments
(carrier at -300 kHz and +10 kHz, a carrier 24 dB over the noise in 1 kHz, a data clock 20 ppm off, 2400 bit/s on 24 kHz, 1200 bit/s NRZ on 12 kHz, a 250 ksps capture); the receiver as
the engine runs it (CSV log, reset, NaN and infinity in the input, noise only) and through the engine. The recording above is not in the repository (853 MB); on it the receiver finds
the carrier, the subcarrier, 4800 bit/s bi-phase and the FAF320 frames within about 15 s.
