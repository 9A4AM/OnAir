# Meteor scatter

The mode **Meteor scatter** (Weather & science group) counts meteors by radio. A fixed transmitter far beyond the horizon, such as the French GRAVES space
radar (a continuous carrier on **143.050 MHz**) or any other CW beacon, is normally not heard. When a meteor burns up it leaves an ionised trail that reflects
the beacon for a fraction of a second to many seconds: a **ping**. The mode finds the beacon line, watches the spectrum around it and counts the echoes.
It is receive only and written clean-room from the general idea of forward-scatter detection.

## How it works

- The capture is filtered (third-order CIC) to about 12 kHz and analysed with a 2048-point Hann FFT every 21 ms (5.9 Hz bins).
- The beacon is the strongest steady line within 4 kHz of the radio's centre. A radio's frequency error is therefore harmless; the line is followed when it drifts.
- Every bin has its own slow background level (about 8 s). A bin that rises above it by the **threshold** (default 13 dB) in two frames in a row starts a ping,
  which is followed (also when its Doppler drifts) until it falls below threshold minus 6 dB for a quarter of a second.
- Echoes closer than 3 bins (about 18 Hz) to the line are masked, and echoes further than 500 Hz are not looked for. Steady lines and spurs are ignored because they are in the background.
- A frame with many bins up at once (lightning, a spark, a switching supply) is interference and is counted as rejected, not as a ping.
- Each ping is kept with its start (to within the 170 ms window), duration, peak level over the background, Doppler at the peak and the slope of the Doppler.
  **Underdense** pings rise sharply and decay exponentially; **overdense** ones last more than 1.5 s with a flat top.

## What is shown

- **Pings**: the list with a level profile of the selected one; Clear, Copy (CSV), Log to file, threshold.
- **Rates**: pings per UTC hour (24 h) and per UTC day (30 days), with the annual meteor showers marked at their peak and a table of the showers now and near
  (peak date and typical hourly rate; dates are rounded from the IMO working list, daytime showers are invisible to the eye but strong for radio).
- **Echoes**: a waterfall of the +-500 Hz around the beacon for the last 30 s and the long-term spectrum.

## Unattended logging

With **Log to file** on (the default) every ping is appended to `meteor_scatter.csv` in the data folder, also with the window hidden. Columns:
`id,utc,duration_s,peak_snr_db,doppler_hz,drift_hz_per_s,type,beacon_hz`. From the command line:
`dect2cli --standard mscatter --freq 143.05 --ms-log pings.csv --secs 86400`.

## Tuning

Tune to the beacon (GRAVES: 143.050 MHz) with 1 Msps or more (2 Msps default); the line may be anywhere within 4 kHz of the centre. A VHF antenna pointing
away from the beacon's direction (towards the sky above the midpoint of the path) and a quiet place help most. A file is read as a capture of the beacon.
The test signal (`--synthetic --standard mscatter`) is a beacon 420 Hz off the centre with simulated pings; nothing is transmitted.
