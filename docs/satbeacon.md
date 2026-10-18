# TV sat beacon decoder

The mode **TV sat beacon decoder** (TV group, next to DVB-S/S2) monitors the narrow carrier of a geostationary satellite beacon, usually a Ku or Ka band beacon seen
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

## The Decoded view

The Decoded view (and the summary at the top of every view of the mode) shows everything the frames carry as far as it can be told from the frames themselves, each line
with how sure it is: **decoded** (checked against the data), **estimated** (inferred; hover for the reason) or **unknown meaning** (shown as it is; what a word means is the spacecraft
maker's, and is never guessed). All frames seen are analysed, word by word and by the position of the frame in the major frame (the counter). On the recorded beacon:

- **Decoded**: the sync word FAF320; byte 3 (0xFE, constant); the minor frame counter in byte 4 (0 to 31, a major frame of 13.65 s); a **checksum in the last byte**, which is the
  sum of the other 255 bytes modulo 256 (true in all 512 frames of the recording, which is also how a good frame is told from a damaged one); a copy of the counter in byte 122.
- **Estimated**: a **clock** in byte 15 that goes up by one at 1.000 per second and never otherwise (a seconds counter, 8 bit, it carries no date); the satellite and orbital
  slot from the measured frequency against a table of 87 published Ku-band beacons (110 to 180 E): the nearest is BSAT-4b, 110 E, 11705.7 MHz, 214 kHz from the measured
  11705.486 MHz, which is a guess (the radio's and the LNB's frequency errors can be that large, the list is a community list and unconfirmed).
- **Unknown meaning**, shown raw and grouped: constant words (bytes 5-9 are 00 00 1E A3 2B; DecSAT's Intelsat 19 frames have 00 01 9E A3 2B there), idle fill (FB/FC), about
  twenty words that depend only on the position in the major frame (a table of 32 values each), and about sixty words that change from frame to frame (range and mean, as raw
  counts; some are noisy like sensor readings, some drift slowly).
- **Not found** (listed as such): a date or time of day, an orbit or ephemeris, a transponder or channel table, text. No run of printable characters and no word counting like a calendar
  was found in the 512 frames.

The CSV and JSON exports (Save CSV, Save JSON, Copy in the Decoded view; `tv_sat_beacon_decoded.csv` and `.json` in the data folder) hold the summary lines, every field with its raw hex, byte
position, confidence and the reason, and the frames.

## CCSDS frames

When the frames start with the attached sync marker 1ACFFC1D (found by the sync search like FAF320), the codeblock layout is worked out from the first blocks (the pseudo-randomiser
on or off, Reed-Solomon (255,223) or (255,239) in the dual or the conventional basis, interleave depth 1 to 8, or no Reed-Solomon with the frame CRC) and every block is decoded: the
transfer frame header (TM or AOS: spacecraft, virtual channel, master and virtual channel counts, idle data), the frame CRC, symbols corrected, lost frames by the counters; the
decoded frames are analysed like any frames (constants, clock, counters, samples) and the Decoded view shows them. The Reed-Solomon, randomiser and header code is adapted from DecSAT
(`core/src/ccsds/ccsds_tm.cpp`, from `decsat-modem/ccsds.rs`). Not done: the CCSDS convolutional code (K = 7) under the symbols, and the identification of the spacecraft from its identifier.

## What is not done

Only the formats above are looked for: a beacon whose telemetry is on the carrier's phase without a subcarrier (PCM/PM) or a suppressed carrier, other sync words, and a frame layout is
not interpreted (a spacecraft maker's words stay raw).

## Credits

The format of this class of beacon (PCM/PSK/PM, bi-phase on a subcarrier, IRIG 106 and CCSDS sync words, the minor frame counter) was recognised thanks to the published
description in the README of DecSAT by Arclamp ([CasualArclamp](https://github.com/CasualArclamp/DecSAT), GPL-3.0-or-later). No code of DecSAT is in this mode; the table of published Ku-band beacons (`core/src/satbeacon/satbeacon_ku.cpp`) is DecSAT's `ku_beacons.csv` (the author's research of October 2026, GPL-3.0-or-later) turned into a C++ table, with its notice kept.

## Tests

`tests/satbeacon`: the generator's golden frames, the frame length finder, the line finder's frequency, level and noise density, the whole chain on the test signal (carrier, drift,
C/N0, subcarrier, bit rate, bi-phase, sync word, frame length, counter, frames equal to the generator's), the reference carrier, an unmodulated carrier, and a sweep of impairments
(carrier at -300 kHz and +10 kHz, a carrier 24 dB over the noise in 1 kHz, a data clock 20 ppm off, 2400 bit/s on 24 kHz, 1200 bit/s NRZ on 12 kHz, a 250 ksps capture); the receiver as
the engine runs it (CSV log, reset, NaN and infinity in the input, noise only) and through the engine. The recording above is not in the repository (853 MB); on it the receiver finds
the carrier, the subcarrier, 4800 bit/s bi-phase and the FAF320 frames within about 15 s.
