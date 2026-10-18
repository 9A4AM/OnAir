
## 2026-10-18 FPV video (analog)
New mode fpv (family 101, active 102, std 103): channel plans, scan, FM video picture via the analog TV decoder, 6.0/6.5 MHz sound, snapshot/recording. Synthetic tests only.

## 2026-10-18 FSQ and IFKP
Two separate new modes: FSQ (family 113, active 114, std 115; five speeds, directed messages with sender, addressee, command, CRC of the callsign) and IFKP (family 116, active 117, std 118; three speeds). One shared 33-tone receiver (core/src/fsq/kbd33.cpp: band search, tone grid, running sums of 73 positions, speed and timing from the sums, history decode on lock), a shared station and generator, two thin receivers, one UI template. Synthetic tests only; parameters and varicode from fldigi's sources, not checked against a real transmitter.
