# Mode A/C

The mode **Mode A/C** (Aircraft group, 1090 MHz) reads the old secondary-radar replies that most transponders still send beside Mode S: the **squawk**
(Mode A, four octal digits) and the **pressure altitude** (Mode C, 100 ft steps, Gillham code), with the **ident (SPI) pulse**. It needs the same radio as
ADS-B (2 Msps or more, 4 Msps recommended). Receive only; written clean-room from the public description of the reply code.

## How it works

- A reply is 15 pulse positions 1.45 us apart, each 0.45 us wide. The first (F1) and the last (F2, 20.3 us later) frame it; between them sit C1 A1 C2 A2 C4 A4 X B1 D1 B2 D2 B4 D4.
  The SPI pulse follows F2 by 4.35 us.
- The detector works on the power of the samples, as the ADS-B demodulator does (fractional windows, any rate from 2 Msps). It looks for two pulses 20.3 us apart, refines the timing,
  measures the 13 positions against the weaker framing pulse, and checks that the reply is clean where it should be: X, the two positions after F2 and the one before F1 are empty
  and there is little energy between the pulses (a Mode S message, with pulses on a 1 us grid, fills the gaps and is rejected).
- The ADS-B receiver runs on the same samples. The Mode S messages it decodes mark where a message was on the air, and candidates there are dropped; its aircraft are on the map.
- A code goes into the table after two replies with the same pattern (the *Show after* setting: 1 to 3). A code that is silent for a minute leaves it.

## Mode A or Mode C

The reply does not say which of the two it answers. The table shows **A** (squawk) when the pattern cannot be an altitude (the C bits 000 or 111, or X set; the C digit of 1200, 7000, 7500, 7600 and 7700 is 0, so they are never altitudes), **A** when the code equals the squawk a Mode S aircraft reported (DF5 / DF21 replies), **C** when the altitude agrees (within 200 ft) with
exactly one Mode S aircraft in the table, and **A/C** otherwise. A matched code shows the aircraft's address and callsign, and selecting it centres the ADS-B map on the aircraft.
7500, 7600 and 7700 are in red and logged.

## Screens

- **Replies** tab: *Map* is the ADS-B map of the Mode S aircraft in the capture; *Codes* is the table (squawk, altitude, type, ident, replies, level, age, aircraft) with a CSV/JSON save button.
- The list on the right has the codes; the Receiver tab the counters (replies per second, candidates, the number left out because a Mode S message was there, noise).
- The antenna position is the ADS-B screen's.

## Test signal

The synthetic source flies the ADS-B airspace and lets every aircraft answer interrogations about twice a second, alternating A and C, at a level that falls with distance, with replies and Mode S messages
overlapping now and then. Options: aircraft, reply rate, Mode S traffic on or off, one aircraft on 7700, one with its ident pulse (18 s of 60).

## Limits

- Not checked against a real transponder or a real capture; the pulse tolerances come from the description. At 2 Msps the pulses are smeared and fewer replies are found (about 6 dB less sensitivity, as for ADS-B).
- Garbled replies (two aircraft answering the same interrogator within 20 us, FRUIT) are not decoded.
- Mode C is read from -1200 to 126,700 ft; a pattern with D1 set is read as a squawk only.
