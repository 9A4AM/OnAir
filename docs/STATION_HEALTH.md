# Station health

View > Station health. One button ("Check station") stops the receiver, retunes the radio over a few fixed bands, measures them, and lists plain advice.
Code: `core/src/sweep/station_health.cpp` (analysis, plain logic, tested on synthetic sweeps in `tests/sweep/test_station_health.cpp`),
`app/panels/station_health_ui.cpp` (window, worker thread, report text in the interface language).

## What it measures

| Item | How |
| --- | --- |
| Noise floor per band | the sweep scanner's floor (median of the hop floors) on FM 87.5-108, airband 118-137, VHF band III 174-240, UHF 470-694, 868 ISM and ADS-B 1080-1100 MHz; bands outside the radio's range are skipped |
| ADC level and clipping | rms, peak and the fraction of samples at full scale in a short capture at the middle of each band |
| ADC margin | the noise over the whole sample rate against the quantisation noise of an 8-bit ADC (-49.9 dBFS): small = the converter, not the antenna, limits sensitivity |
| Reference stations | FM carriers (60-450 kHz wide), DAB ensembles (1-1.9 MHz), DVB-T channels (5.5-8.8 MHz) seen above the floor; the strongest one is compared with what a healthy antenna shows (25 / 12 / 12 dB over the floor) |
| Frequency error (ppm) | optional: give the frequency of a carrier you trust; a 1 kHz-bin sweep +-100 kHz around it, parabolic peak |
| ADS-B range | from the range statistics the ADS-B mode keeps (10 degree sectors); needs the antenna position and 50 or more positions |

Everything is relative to the receiver itself: the radios are not calibrated, so there are no dBm values and no antenna gain figure.

## The advice (rules of thumb)

* ADC clips (more than 0.1 % of the samples, or a peak at full scale): lower the gain. If another band is ADC-limited at the same gain, the strong band needs a
  band-stop filter instead (an FM notch for the FM band).
* ADC level above -12 dBFS rms without clipping: little headroom, lower the gain a little.
* ADC margin under 6 dB: raise the gain; at the radio's maximum gain: add an LNA at the antenna.
* ADC margin over 26 dB with a high level: the gain could drop (margin minus 20 dB) with no loss of sensitivity.
* A band's noise density (per Hz) more than 12 dB over the quietest band, after allowing 15 dB per decade of galactic noise below 300 MHz: a local noise
  source (LED lights, switching supplies, computers); move the antenna or add a ferrite.
* No FM / DAB / DVB-T found, or the strongest one weak: check antenna and cable, move it higher or outdoors, add a filter and LNA.
* More than 5 ppm of clock error: enter the number as the frequency correction in the radio's settings.
* ADS-B farthest aircraft under 60 nm (under 25 nm: bad), or a run of empty 10 degree sectors between directions that reach far: raise the antenna, look for what blocks that direction.

## Export

"Save report..." writes the text (in the interface language), "Save as JSON..." the numbers and finding codes.

## Limits

The ADC statistics come from one capture at the middle of each band, so a band wider than the sample rate (FM at 10 Msps) is judged by its middle part.
Thresholds are heuristics, not standards. 8-bit converters are assumed (HackRF, RTL-SDR); a 12-bit radio reads a larger margin than it has.
