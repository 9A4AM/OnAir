# Sweep scanner

A fast wideband sweep with a noise-floor measurement: View > Sweep scanner in the app, `dect2sweep` on the command line.

## What "Mykola" is (found vs assumed)

Found (rtl-sdr.com article "Mykola: A New Fast Multichannel Scanner Application for RTL-SDR, Airspy and HackRF" and its comments): a closed Windows/macOS
scanner program for RTL-SDR, Airspy R2 and HackRF. Advertised: very high scan speed (a commenter reports about 270 MHz/s on an Airspy Mini; an anecdote, not
a benchmark), an adaptive noise floor with automatic normalisation of it, and demodulating 3 channels at once (20 in a paid version). The free version
does not store frequencies.

Assumed: how it measures speed and the floor internally is not published in what I found, so this implementation follows the standard approach (rtl_power
/ rtl_power_fftw style FFT sweeps) rather than copying anything. Nothing here is taken from Mykola.

## How it works (core/src/sweep)

- The range is cut into hops of one sample-rate each. Per hop: retune, drop the settle time, capture `dwell` of samples, average Hann-windowed FFTs
  (50 % overlap, the averaging is split into 4 fixed parts run on the shared thread pool, so the result does not depend on the thread count).
- The FFT size is the power of two whose bin width is closest to the wanted bin width. 15 % of the rate is cropped at each edge of a hop (filter roll-off),
  hops are placed so the usable parts tile exactly on one bin grid: the stitched trace has no seams (test: below 0.5 dB steps).
- DC spike: the capture mean is removed and the five bins at DC are interpolated from their neighbours.
- Levels are dBFS per bin; a full-scale tone reads 0 dB on its peak bin.
- Noise floor per hop: a percentile of the bin power (50 = median), corrected for the bias of an average of K exponential values, then iteratively
  re-estimated after dropping bins above the 99.5 % point of the noise, so signals in the band do not lift it (test: 30 % of the bins occupied by +30 dB, still within 0.2 dB).
- Detection: runs of bins above floor + threshold (a gap of one bin is bridged); centre from a parabolic fit (narrow) or the power centroid (wide), bandwidth =
  run length, SNR = peak - local floor. The activity log merges detections by frequency: first/last seen, count, best SNR, peak, bandwidth.
- Settle time: `calibrateSettle` retunes between the two ends of the range several times and finds when the power stops changing; the sweep uses that
  (x1.25 + 1 ms). Switch it off in the window to enter a value.
- `minIntervalSec` (window: "every N s") makes the scan a schedule: a sweep is never started sooner, the thread sleeps in between.
- Files: rtl_power compatible CSV (one row per hop; also read from rtl_power / rtl_power_fftw), a signal log CSV, a baseline compare (noise floor change,
  largest rise/fall).
- Click a signal on the trace or in the table and press "Tune to selected" (or double-click the row): the sweep stops and the receiver starts on that
  frequency in the mode picked next to the button.

## Command line

    dect2sweep --from 87 --to 108 --bin 25 --dwell 10 --csv out.csv --signals log.csv [--count 5 --interval 60]
    dect2sweep --compare a.csv b.csv

## Speed

The ceiling is the radio: one hop covers about 70 % of the sample rate and costs settle + dwell, so with 10 Msps, 3 ms settle and 5 ms dwell the sweep
runs at 7 MHz / 8 ms, about 870 MHz/s on paper; USB retune latency is what usually decides. The processing (FFT, floor, detection) is not the limit: see
tests/sweep/test_sweep.cpp, which prints the figure with an instant fake radio.

## Band survey

The Sweep scanner window has a Band survey section that keeps every sweep of the range as one row of a time x frequency grid (level above the sweep's
noise floor, half-dB steps; core/src/sweep/survey.cpp). Set "every" to a few minutes and the survey runs for hours or days in the background at low CPU.

- Heatmap: time down, frequency across; the picture can be exported as a PNG, the per-bin occupancy (share of sweeps above the threshold), mean and
  peak level as CSV.
- Persistent signals: runs of bins above the threshold in at least the chosen share of the sweeps (always-on carriers, repeaters, spurs), with
  occupancy, mean and peak level above the floor.
- Quietest slots: the emptiest stretches of a chosen width (lowest occupancy, then lowest strongest-ever level): where to put a weak-signal receiver.
- What changed: load an older survey file as "previous survey" to list the persistent signals that appeared or vanished and the change of the noise
  floor and of the mean occupancy. Memory is capped (4000 rows): past that the older half is merged pairwise by maximum.

Survey files (`.oasurv`) are compact binary. On the command line: `dect2sweep --from 87.5 --to 108 --interval 300 --survey fm.oasurv` adds each sweep to
the file (and continues it if it exists); `dect2sweep --survey-report fm.oasurv [older.oasurv]` prints the persistent signals, the quiet slots and the diff.
