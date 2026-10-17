# Receiver frequency self-calibration

A cheap radio's oscillator is off by tens of ppm: at 1 GHz that is tens of kHz. The `selfcal` module (core/src/selfcal, `dect2/selfcal/selfcal.h`)
measures that error against a signal whose frequency is known and gives the number for the radio's `ppm` setting.

Sign: positive ppm = the radio's clock runs fast, a signal shows up below its real frequency (same as `radioPpm()` in source.h).
One oscillator sets the LO and the sample clock, so a tone of true frequency `fnom` is seen at `fnom / (1 + ppm 1e-6)`:
`ppm = (fnom / (centerHz + basebandHz) - 1) * 1e6`.

## References

| Reference | How | Typical accuracy |
|---|---|---|
| Steady carrier or pilot (ATSC pilot, a CW beacon, a test oscillator) | `ToneMeter` on the IQ | the station's own, ATSC <= 0.05 ppm in practice |
| FM stereo pilot, 19 kHz | `FmPilotMeter` (discriminator, then a tone meter in the MPX) | most stations lock it to a good reference, the standard only promises +-2 Hz (100 ppm), so use several stations |
| GSM / LTE / NR cell carrier | the cell decoder (core/src/cell, nr, nbiot) reports its measured carrier error, `fromFreqError()` converts it | about 0.05 ppm |
| GPS L1 C/A carrier | the gnss decoder's carrier after removing the Doppler of the tracked satellite, `fromFreqError()` | well under 0.1 ppm |
| DVB-T / DVB-T2 pilots, other digital TV | same, `fromFreqError()` | depends on the broadcaster |
| Time signals | not used |  |

`combine()` takes several estimates (for example three FM stations and a cell): it drops a reference that is more than 3 sigma (or 0.3 ppm) from
the median and weights the others by 1 / sigma^2. A single odd station therefore cannot move the result.

## Algorithm of ToneMeter

1. The IQ is mixed down by the tone's expected place and low-passed with a triangular (sinc^2) filter, hop = decimation, so that only the
   search window (+-100 ppm of the carrier by default) is left. A strong neighbour (FM audio around the pilot) is suppressed well.
2. The first 0.5 s are searched with a zero-padded Hann FFT; the peak, refined by a parabola on the log spectrum, gives the offset to about 1 Hz.
3. The signal is then summed in 50 ms blocks after a mixer at that offset. The phase of the block sums is unwrapped and a line is fitted;
   its slope is the remaining frequency error. The uncertainty comes from the residual of the fit (`sigma_f = sigma_phi / sqrt(Sxx) / (2 pi Tblock)`),
   so it falls with time^1.5, and no result is given when the phase scatter exceeds 1 rad (no steady tone).

## Using the result

* `Store` keeps the history per radio serial in a text file (utc, ppm, sigma, temperature if known, which reference). `drift()` gives the
  warm-up slope in ppm per minute; `value()` gives the newest measurement while the clock is still moving and the weighted mean of the last hour once
  it has settled (under 0.01 ppm per minute).
* `applyPpm(settings.radio, ppm)` writes the `ppm` key the radio panel and every driver read (and removes it for 0).
* `Wizard` is the state machine of the Calibrate dialog: Searching -> Integrating (progress from the current uncertainty against the 0.2 ppm target)
  -> Done, or Failed after 120 s without a usable reference. A result worse than the target but under 1 ppm is accepted at the time limit.
* A radio whose oscillator drifts with temperature should be calibrated after warm-up (5 to 10 minutes); the stored history shows how far it moves.

## Tests

`tests/selfcal/test_selfcal_core.cpp` (ctest name `selfcal_core`): synthetic carriers at -50 .. +50 ppm (100 MHz, 584 MHz ATSC pilot, 800 MHz at 0 dB
SNR) and a synthetic FM station with audio, pilot and a stereo subcarrier at 240 ksps all come back within 0.2 ppm; noise alone gives no result;
combine, store (save/load, drift, value) and the wizard are checked.

## Not done yet

The Calibrate dialog in the radio panel and the feed of live IQ to the meters (and the decoders' carrier errors to `fromFreqError()`) are not
connected in the app: the module is the measuring and bookkeeping core.
