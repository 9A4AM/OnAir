# Interference hunter (View > Interference hunter)

Watches a band for new interference, guesses what kind of source it is, and helps you walk to it with a loud / quiet meter. It needs only the radio you already have (a wideband tuner such as an RTL-SDR, Airspy, HackRF or SDRplay; HF below the tuner's range needs an upconverter). The sweeps come from the same sweep engine as the Sweep scanner. **Status: the analysis is tested on synthetic sweeps only. Nothing here has been checked against real interferers; the classes below are rules of thumb, and the window says "likely source" for that reason.**

## Monitor

1. The first sweeps (default 5) are the **baseline**: the median of every bin, the noise floor and the list of narrow signals that are normal in this band (broadcast carriers, known devices).
2. Every later sweep is compared with the baseline. A narrow spur that is not in the baseline (or is 8 dB stronger than its baseline entry), a wide raised region (broadband noise, 4 dB or more over the baseline, 400 kHz or wider), or a noise floor 3 dB above the baseline is a candidate. A candidate seen in 2 sweeps in a row (adjustable) raises an **alert**; gone for 3 sweeps, it is reported as cleared. The baseline floor follows slow drift (2 % of the difference per sweep, only while no floor alert is open), so temperature or gain drift does not alert.
3. **What is there now is normal** adds reported interference to the baseline. **Learn the baseline again** starts over (it also starts over by itself when the range or bin width changes).
4. Alerts and the noise floor history can be saved as CSV (a log of floor changes over days is the evidence to show a neighbour, a landlord or a regulator).

Use a long "every" interval (for example 30 to 300 s) to let it run for hours with little CPU.

## What the classes mean (rules of thumb)

| Likely source | What it looks like |
|---|---|
| Switching power supply | Four or more narrow spurs at equal spacing (a comb), 5 bins or more apart, not sitting on broadband noise. Chargers, LED drivers without noise, mains adapters, PC supplies. Confidence rises with the number of teeth. |
| LED lamp or driver | A smooth raised region (a hump) at least 400 kHz wide, above 30 MHz or on HF without amateur-band notches; a comb of 20 to 300 kHz spacing inside it raises the confidence. |
| Powerline (PLC) | On HF (1.6 to 30 MHz), a hump at least 1 MHz wide with notches that fall on amateur bands (adapters leave them out); or narrow carriers / blocks below 500 kHz (CENELEC / FCC powerline bands, if your tuner reaches them). |
| Local oscillator or clock leak | One narrow, strong (12 dB or more) spur that is not part of a comb. If it sits on a harmonic of a common clock (10, 12, 16, 19.2, 20, 24, 25, 26, 27, 30, 32, 38.4, 40, 48, 50, 100, 125 MHz) the confidence is higher and the clock is noted. Typical of USB devices, computers, SDRs, set-top boxes, cameras. |

A comb needs a spacing of at least 5 bins (use finer bins for closely spaced spurs). Very dense or very weak interference is not classified; it still raises an alert with "Unknown". The classifier never sees demodulated content: it only names the shape of the spectrum.

## Walk

Carry the radio (a laptop and a dongle, or a phone with an OTG dongle) and watch the meter. The **meter** is the mean power per bin of the sweep, with the signals you chose to leave out removed, smoothed (fast up, slow down), against the quietest level seen so far (or a level you set). It is full at 25 dB above the quiet level. The label says quiet, some interference or loud, and whether it is getting louder or quieter over the last few readings. Do **Leave out the signals seen now** at a quiet place first, so broadcast carriers do not drown the meter.

Every sweep is logged as a reading: time, position, power, level above the quiet level, noise floor, strongest spur and the likely source. **Save as CSV** or **Save as GPX** (one waypoint per reading with a position, the level in the name) to put them on a map. The loudest reading and its position are shown.

### Position

- **The receiver position** set for ADS-B (one position for the whole walk, useful for logging a fixed spot).
- **Type a position**.
- **GPS device or NMEA file**: a serial GPS puck (9600 baud, 8N1; for example `/dev/cu.usbmodem1101`, `COM3`) or a text file of NMEA 0183 sentences (GGA and RMC, any talker, checksum verified). A fix older than 10 s is not used. On Windows only a plain file or a device the C runtime can open as a file works; no baud rate is set there.

Without a position the readings are logged without coordinates; the meter does not need it.

## Demo

"Demo (simulated sweeps)" runs the whole window on synthetic sweeps: three broadcast carriers, then a switching supply, an LED lamp with a comb and an oscillator leak come and go. It is for trying the screen without a radio.

## Limits and what is unverified

- Only synthetic sweeps were used: a noise floor with random jitter, tones, combs, humps and notched blocks. Real interference drifts, hops, is modulated and overlaps broadcast signals; the thresholds will need tuning on real data.
- The mean-power meter is dominated by strong carriers unless they are left out; interference below the tuner's own noise or inside a strong broadcast signal is not seen.
- The position reader has been tested on the NMEA parser (sentences with and without checksums) only, not on a real GPS.
- Saving and reading of the alerts and the readings has been tested; nothing was run on a radio.

The code is original (core/src/rfi/rfi.cpp). No third-party code is used; the clock list is common knowledge and the classes are generic engineering rules of thumb.
