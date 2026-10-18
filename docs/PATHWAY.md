
## 2026-10-18 FPV video (analog)
New mode fpv (family 101, active 102, std 103): channel plans, scan, FM video picture via the analog TV decoder, 6.0/6.5 MHz sound, snapshot/recording. Synthetic tests only.

## 2026-10-18 GOES / Meteosat DCS
New mode goesdcs (family 107, active 108, std 109): whole 401.7 - 402.1 MHz uplink band scanned for platform carriers, PLL per carrier, 100/300/1200 bps NRZ and Manchester hypotheses with preamble + frame sync candidates, BCH (31,21) platform address (checked against the JMA annex example), ASCII and pseudo-binary B data, platform table, CSV log. Synthetic tests only; the 300/1200 bps spare bit and flag word are from summaries, not tried on a real platform.
