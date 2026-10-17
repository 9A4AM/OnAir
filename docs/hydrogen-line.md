# Hydrogen line toolkit (Radio astronomy mode)

Everything here works on the integrated spectrum of the Radio astronomy mode (21 cm, 1420.405751 MHz). The maths is in
`core/include/dect2/astro/hline.h`; the screens are the tabs Spectrum, Waterfall, Velocity, Map, Pointing, Calibration / RFI and Log.
Nothing below was measured on a real antenna by the authors; the unit tests use synthetic skies and published worked examples.

## Hardware that people use

| Part | Typical choice | Notes |
|---|---|---|
| Receiver | RTL-SDR (2.4 Msps), Airspy, HackRF | 2.4 Msps gives a 1.7 MHz usable span, about 360 km/s, enough for the whole Galactic line |
| Antenna | Horn (about 20 dBi), 1 m offset dish (about 20 dB, 15 deg beam), 3 m dish (about 30 dB, 5 deg), or a "can" feed | A bigger dish gives a smaller beam, so a finer map in l |
| LNA | Noise figure under 0.6 dB at the feed | Powered through a bias-tee |
| Filter | 1420 MHz band-pass (SAWbird+ H1 or similar, which has an LNA and a filter in one box; a second LNA stage helps) | The RTL-SDR front end alone is too noisy and sees strong out-of-band signals |
| Typical Tsys | 60 to 120 K with a good LNA; 150 to 250 K with ground spill and a poor feed | Estimate it with the Y-factor in the Calibration tab |

## What results to expect

A hump or several humps a few percent above the baseline, 20 to 40 km/s wide, between about -100 and +100 km/s in the LSR frame.
With a small horn in the Galactic plane peaks are some 2 to 10 K above the baseline; away from the plane they are close to noise. In the first
quadrant the highest velocity grows with longitude up to the tangent point (the terminal velocity). A 10 minute integration shows the
line clearly; the radiometer noise is Tsys / sqrt(bandwidth x time).

## What each tab does

* **Spectrum / Waterfall**: the live and integrated spectrum, with an optional off-source reference or frequency switching, baseline, and the drift scan waterfall of finished integrations.
* **Velocity**: the target (Galactic l, b; the Sun, Moon, Cas A, Cyg A or Sgr A*; or RA / Dec), the observer's velocity towards it (Earth rotation, Earth orbit, the Sun's motion) and the spectrum on the LSR axis in kelvin (Tsys times the fractional excess). "Fit lines" does a multi-component Gaussian fit (Levenberg-Marquardt) and prints peak, centre, FWHM and area with one-sigma errors. Export: CSV, FITS (binary table with frequency, topocentric and LSR velocity and antenna temperature), SigMF annotation text with the fits.
* **Map**: add a spectrum at each longitude (b = 0) to build the l-v diagram; terminal velocities and the rotation curve use the tangent point method, R = R0 sin l and V = V_term + V0 sin l, with R0 and V0 you set (8.5 kpc and 220 km/s are the old IAU values). It ignores gas velocity dispersion, which makes the curve about 5 to 10 km/s low. Sessions (the map rows) can be saved and opened.
* **Pointing**: Az / El now, transit, rise and set times (UTC) and a 24 h elevation plot for the selected target, for the Galactic position, the Sun, Moon and the calibrators Cas A, Cyg A, Sgr A*.
* **Calibration / RFI**: Y-factor from a hot and a cold load, Tsys = receiver temperature + sky and spill, and RFI flagging (residual from a median baseline, 1.4826 x MAD) with interpolation over flagged bins. The library also has spectral kurtosis flagging for stacks of spectra and per-bin gain ripple correction.
* **Log**: events (fits, captures, map additions).

## Accuracy and references

* LSR correction: Earth orbit from Meeus' orbital elements (Sun's reflex motion neglected, under 0.02 km/s); Earth rotation 0.4651 km/s at the equator; the Sun's motion by the IAU standard solar motion, 20 km/s towards 18h 03m 50.29s +30d 00m 16.8s (J2000), or the dynamical LSR of Schoenrich, Binney and Dehnen (2010) as an option. Total error is below about 0.1 km/s.
* Sidereal time, Sun, precession and rise / transit / set follow Meeus, Astronomical Algorithms (2nd ed.); the Moon uses the Astronomical Almanac low-precision series (about 0.3 deg), so it is good for pointing a 5 deg beam, not for occultation work.
* Galactic coordinates: IAU 1958 system with the pole of Hipparcos (192.85948, +27.12825, node 122.93192).
* The unit tests (`tests/astro/test_astro_hline.cpp`) check sidereal time (Meeus ex. 12.a), Sun (25.a), Moon (47.a), the Venus transit / rise / set at Boston (15.a), the IAU solar apex at l = 56.1, b = +22.8, Earth's perihelion and aphelion speeds, line fit recovery with uncertainties, calibration recovery, RFI flagging, a synthetic flat rotation curve and the FITS round trip.
* FITS is written and read by our own code (FITS 4.0: 80-character cards, 2880-byte blocks, big-endian; primary, IMAGE and BINTABLE HDUs with D / E columns). Other FITS readers (astropy, CASA, CLASS via its FITS import) open the tables.
