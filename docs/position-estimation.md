# Aircraft position estimation

One estimator serves ADS-B, UAT, ACARS, VDL2 and HFDL (`core/include/dect2/track/`, `core/src/track/`, test `tests/track/test_track.cpp`).
Pure C++, deterministic, no global state, no dependency on the receivers. Everything below was implemented from the literature listed at the end; no code of another project was used.

What it gives the maps:

* **between fixes**: an ADS-B aircraft moves smoothly instead of jumping once a second (or once in five),
* **when messages stop**: an ACARS / VDL2 / HFDL aircraft (a report every few minutes) is dead-reckoned along its track, with an uncertainty ellipse that grows,
* **with a route**: if a message named the destination (or an ETA), the prediction knows where the aircraft is going and when it arrives,
* **with a ground station only**: an aircraft that has no position at all is shown as the reach of the station it talks to.

## Structure

| File | Content |
| --- | --- |
| `track_geo.h` | local frame, great-circle helpers, small matrices |
| `track_imm.h/.cpp` | `Fix`, `PosEstimate`, `TrackEstimator` (the filter) |
| `track_route.h/.cpp`, `track_airports.cpp` | airport table, route / ETA / OOOI / position text, line-of-sight range, `WindField` |
| `track_hub.h/.cpp` | one estimator per aircraft, ground stations, wind field, adapters from the receivers' aircraft records |
| `app/map/pos_predict.h` | the "Predict positions" / "uncertainty" switches, ghost marker, dashed ring, ellipse, tooltip |

## The filter

### Frame

Each track has a local azimuthal-equidistant frame (x east, y north, metres) about a point near the aircraft, on a sphere of radius 6 371 008.8 m
(distances differ from WGS-84 by up to 0.3 %). Forward and inverse are exact on the sphere, longitudes are wrapped (date line), and the frame is regular everywhere except
at the antipode of its origin (poles are fine). When the aircraft is more than 100 km from the origin the frame moves to it: positions are mapped through the geodesy,
velocities and covariances through the 2x2 Jacobian of the map (finite differences), so the rotation between the two frames is carried along.

Tracks and velocities the receivers report are against the local meridian; the frame's north differs from it by the convergence angle `gamma`
(up to about 8 degrees 100 km from the origin at 84 degrees north). Every track, wind and route bearing is turned into frame azimuths with `gamma`, and the output covariance is turned back
(`convergence()` in `track_imm.cpp`). Without it the simulated polar flight had a several times larger error.

### Horizontal IMM (Bar-Shalom)

State `x = [e, n, ve, vn, w]` (w = turn rate, counter-clockwise positive in the frame), three models mixed by the standard IMM cycle (mixing, model-matched EKF, likelihood-weighted model probabilities, combination):

| Model | Motion | Process noise |
| --- | --- | --- |
| CV | constant velocity, `w = 0` | continuous white-noise acceleration `qCv` |
| CT | coordinated turn, `w` relaxes with time constant `omegaTau` | `qCt` plus a random walk `qOmega` on `w` |
| MAN | constant velocity with large acceleration noise (speed changes, the start of a turn, holds) | `qMan` |

Coordinated-turn step over `T` with `th = w T`:

```
e' = e + S ve - C vn        S = sin(th)/w,  C = (1 - cos(th))/w
n' = n + C ve + S vn        (series in th for |th| < 1e-3)
ve' = cos(th) ve - sin(th) vn
vn' = sin(th) ve + cos(th) vn
w'  = exp(-T/tau) w
```

with the analytic Jacobian (including `dS/dw`, `dC/dw`). Discrete process noise of a model with spectral density `q`: per axis `q [T^3/3, T^2/2; T^2/2, T]`.
Model switching is a continuous-time Markov chain with mean sojourn times `sojournSec`, discretised for the actual step (`Pi_ii = exp(-T/s_i)`), so irregular intervals are handled exactly.
Long gaps are predicted in sub-steps of at most 10 s with mixing at every step.

Updates are in **Joseph form** (`P = (I-KH) P (I-KH)^T + K R K^T`), symmetrised, diagonal floored; likelihoods are computed in the log domain; model probabilities are floored at 1e-4. No NaN reaches the output (tested with 20 000 random garbage fixes).

Measurements (a `Fix` may hold any of them):

* **position**: noise from the source, see below; 2-D, linear.
* **velocity**: ground speed and track become `(ve, vn)` with the covariance `J diag(sigma_s^2, sigma_track^2) J^T`. Air data (true airspeed and heading) add the track's wind estimate and its variance.
* **bank angle** (Comm-B 5,0): turn rate `w = -g tan(roll)/V` as a scalar measurement; it separates a turn from a speed change within one report.
* **altitude, vertical rate**: a separate two-state IMM `[h, vh]` with a level model (vertical speed relaxes in 15 s) and a climb/descent model (120 s).

### Measurement noise

| Source | Position sigma (1 sigma, each combined with the timing term below) |
| --- | --- |
| ADS-B / UAT | NACp: `EPU95 / 2.448` (table below); else NIC: `Rc / 5`; else 25 m; floor 6 m |
| ACARS / VDL2 text and ADS-C | 60 m (ADS-C uses 21-bit latitude/longitude, about 20 m) |
| HFDL | 150 m |
| any text report | plus the rounding step of the text: `step * 111 320 m / sqrt(12)` (tenths of a minute: 53 m, whole minutes: 535 m) |

NACp 95 % bounds (ICAO Doc 9871 / RTCA DO-260B): 11: 3 m, 10: 10 m, 9: 30 m, 8: 0.05 NM, 7: 0.1, 6: 0.3, 5: 0.5, 4: 1, 3: 2, 2: 4, 1: 10 NM. NIC containment radius `Rc`: 11: 7.5 m, 10: 25 m, 9: 75 m, 8: 0.1 NM, 7: 0.2, 6: 0.6, 5: 1, 4: 2, 3: 4, 2: 8, 1: 20 NM.
(`r95 = 2.448 sigma` for a 2-D Gaussian; `Rc` is a 1e-7 containment radius, so about 5 sigma.)

**Timing.** The report time is not the time of the position. `Fix::latencySec` is how much older the position is than `t`
(defaults per source in `TrackOptions::latencySec`: ADS-B 0.2 s GNSS latency, ACARS/HFDL 15 s, VDL2 5 s when `t` is the receive time; 0 when the report carries its own time stamp, as POS and ADS-C do),
and `timeSigmaSec` (ADS-B 0.1 s, VDL2 8 s, ACARS and HFDL 20 s) adds `speed * sigma_t` to the position noise. At 230 m/s a one-second timing error is 230 m, far more than any of the other terms for satellite-borne reports, so use the time stamp of the text whenever there is one.

### Outliers, restarts, late fixes

* **Gate**: Mahalanobis distance of the position against the mixed prediction, `chi2 > 21 (1 + (gap/30 s)^2)` (2 dof). The gate opens with the gap: after minutes the model, not the report, is the likely culprit.
* **Implied speed** above `maxSpeedMs` (450 m/s, change it for military traffic) is rejected too.
* Two mutually consistent rejected fixes restart the track at the new one (another aircraft with the same address, or a track that went wrong); a silence of 3 h also restarts it.
* **Adaptive process noise**: the smoothed normalised innovation squared (expected 1; rejected fixes count with a capped value) scales all process noise from 1 up to 400. A track the model describes badly (a hold, a steep arrival) widens its own covariance. The factor also grows when the vertical rate is large (arrivals slow down and turn).
* **Late fixes**: a snapshot of the filter is kept before each of the last 6 fixes. A fix older than the filter time but inside 120 s restores the snapshot before it and replays the fixes in time order. Older fixes are dropped.

### Output (`PosEstimate`)

`estimate(now)` predicts a copy of the state to `now` (the filter is not moved: display times can go back and forth).
Latitude, longitude, altitude, track (turned back from the frame), ground speed, vertical rate, turn rate, 1-sigma `sigmaH` / `sigmaV`, the horizontal covariance rotated to true east/north,
the 95 % ellipse (`sqrt(5.991 * eigenvalue)`, 2 dof), the shares of the sources in the recent fixes, model probabilities, `ageSec`, `predicted` (last position older than 6 s for ADS-B / UAT, 20 s for the others), `interpolated`,
`confidence = 1 - log10(r95/50 m)/log10(300 km/50 m)` clipped to 0..1, `staleness = age/1 h`.
The `quick` form skips covariance, ellipse and route (0.4 microseconds): it is what the map uses for ADS-B aircraft with a fresh fix when no ellipse is drawn.

## Route knowledge

`parseRouteText(label, text)` reads, where the format is defined: the Q-label OOOI and airport layouts, the ARINC 702A flight-plan line `FPN/RI:DA:KLAX:AA:KSFO`, ICAO flight-plan strings `(FPL-...-EDDF1030 -...-LTFM0310 ...)`, `ETA hhmm`, `DEST/DST/DES xxxx`, `NXT WPT name`.
`parseTextPosition(label, text)` reads `POSN43312W123174` (tenths of a minute), label-20 thousandths, `N 44.203,W 86.546`, `N 51 28.3 E 000 27.1`, and returns the rounding step (which becomes the position noise).
These formats are recognised from public descriptions and tested on synthetic strings only: see the limits below.

`TrackHub::feedText` sets a `RouteHint` (destination and origin from the airport table, ETA converted to the fixes' clock). The estimate then uses the route like this:

1. Only if the aircraft's track points at the destination (within 35 degrees) and its speed is above 40 m/s.
2. **Cross-track prior**: the great circle from the last fix to the destination. Cross-track distance and cross-track speed are measurements of zero with `sigma_xt = 4 km + 5 % of the distance flown since the fix + 12 % of the distance still to go`, and a heading sigma of 25 to 45 % of the speed. Far from the destination the prior is nearly off (airways wander); near it the line is a good description.
3. **ETA**: if the report gave an ETA, the speed along the route it implies is a measurement with 30 m/s sigma.
4. **Arrival**: once the dead-reckoned distance from the last fix reaches the distance to the destination, the aircraft is parked at the destination (`arrived`, sigma at most 2 km).

The airport table is 293 hubs (ICAO, latitude and longitude to 0.01 degree) generated from `web/map/public/airports.js`, which is the only airport data in the repository.
It is sorted by code and binary-searched; to extend it add rows there (or a public-domain OurAirports extract) and regenerate `core/src/track/track_airports.cpp`.
Unknown destinations simply give no route.

## Winds

`WindField` keeps up to 2048 wind samples (from ADS-B derived winds, Comm-B 4,4 reports, anything with `hasWind`); `at(t, lat, lon, alt)` returns an inverse-distance weighted mean of the samples within 300 km, 1500 m of altitude and 30 minutes, with a 6..16 m/s sigma. The hub gives that to a track that has no wind of its own (`setWindPrior`), used when a report carries air data (true airspeed and heading) instead of a ground velocity: `v_ground = v_air + wind`, wind variance added to the velocity noise.
This only matters for reports with air data, which the current ACARS / VDL2 decoders rarely deliver; the plumbing is there and tested through the hub, but no accuracy figure is claimed for it.

## Ground stations

`setStation` / `noteStation`: an aircraft with no position at all gets the station's position with a circular 95 % region of radius equal to the station's reach: VHF line of sight `1.23 (sqrt(h_aircraft ft) + sqrt(100 ft))` nautical miles (about 240 NM at FL350), HFDL 1500 NM. The maps draw it as a dashed ring. VDL2 stations come from their GSIF/XID location parameters, HFDL from the built-in station table.

## Accuracy

`./test_track` prints this table (12 simulated flights between real city pairs: EGLL-KJFK, OMDB-EGLL, YSSY-WSSS, KLAX-RJTT over the date line, KJFK-VHHH over 84 N, ...).
The flights have lateral waypoints up to 70 km from the great circle, fly-by turns limited to 25 degrees of bank, climb, cruise with a speed wander, a 3 degree descent and a landing.
Metres, median / 95th percentile of the horizontal error, measured between fixes. "linear" is the common baseline: the last fix advanced with the last reported velocity (ADS-B) or the speed and track of the last two fixes (ACARS, VDL2, as `vdl2AircraftPositionNow` does). "NEES" is the mean normalised estimation error squared (2 for a consistent filter; below 2 means the ellipse is too large, above it too small).

| Scenario | IMM | linear | last fix | NEES |
| --- | --- | --- | --- | --- |
| ADS-B, fix every 1 s | 4.3 / 10.2 | 49.7 / 75.3 | 183.6 / 279.6 | 0.81 |
| ADS-B, fix every 5 s | 9.3 / 22.9 | 50.6 / 76.4 | 717 / 1151 | 0.93 |
| ACARS text, 5 min gaps | 303 / 1452 | 452 / 1935 | 36 870 / 73 174 | 0.62 |
| ACARS text, 5 min gaps, route prior | 322 / 1478 | 452 / 1935 | | 0.62 |
| ACARS text, 15 min gaps | 1291 / 6977 | 2549 / 12 284 | 109 859 / 218 100 | 0.74 |
| ACARS text, 15 min gaps, route prior | 1401 / 6484 | 2549 / 12 284 | | 0.74 |
| ACARS, arrival (last 45 min), 5 min gaps | 536 / 9384 | 658 / 10 817 | 35 119 / 71 212 | 8.3 |
| VDL2, 2 min gaps | 166 / 503 | 172 / 534 | 14 725 / 29 396 | 0.59 |
| Hold (1 min legs, 3 deg/s), ADS-B every 4 s | 15 / 107 | | | 1440 of 1600 inside the 95 % ellipse |
| Hold, reports every 2 min | 9007 / 9915 | | | 0 of 53 inside |

Reading it:

* Between ADS-B fixes the filter is 5 to 10 times better than extrapolating the last fix with the reported velocity, mostly because it averages the position noise and the timing jitter over many fixes and uses the bank angle.
* For ACARS-style reports the gain over the two-point linear extrapolation is 30 to 50 % at 5 and 15 minutes (tests also assert it), and about equal at 2 minutes.
* The **route prior does not improve the median** in this simulation (it costs 6 to 8 % at the median) and shortens the 95th-percentile tail by 7 % at 15 minutes: the simulated flights, like real ones, do not fly direct. The value of the route is the arrival clamp and the ETA, not the cross-track prior. It is kept weak on purpose; turn it off with `routeXtBaseM = 1e9` if it does not help on real traffic.
* **Calibration**: the ellipses are conservative (NEES 0.6 to 0.9) for cruise, and over-confident in a steep arrival (8): speed is changing there in ways the model only follows through the adaptive process noise.
* **Holds with sparse reports cannot be tracked**: two positions two minutes apart say nothing about a race track, so the extrapolation wanders and the ellipse is wrong. With ADS-B (4 s) the turn model follows the pattern (median 15 m).

Tuning: `./test_track tune` runs a coordinate descent on the process-noise parameters against **different flights and different noise** than the tests use (seed offset 1000), and `./test_track cands` compares hand-picked sets. The shipped values are a sensible, non-degenerate set from that search (the unconstrained optimum shortened the turn model to one second, i.e. it switched it off).

Speed (Apple silicon, one thread): `update` 2 microseconds (position + velocity + altitude + vertical rate), `estimate(quick)` 0.4 microseconds, full `estimate` with ellipse 400 s ahead 16 microseconds. 5000 tracks updated once a second need about 1 % of one core.

## Derived speed, track and vertical rate

ACARS, VDL2 and HFDL position reports carry no ground speed (ADS-C earth reference excepted), but two fixes and the time between them give one (`core/include/dect2/geo/kinematics.h`, shown with a `~` on the screens, the map and the website card). The distance is the WGS-84 geodesic (Vincenty); **ground speed is the horizontal distance divided by the time and does not include the climb** (the slant speed including the height change is computed separately). The track is the mean bearing of the chord, labelled "average" over a gap of 20 minutes or more, when the real path may have been curved and longer. Time is the time inside the report when both fixes have one, else the reception time with a wider jitter. The uncertainty is `sqrt(2) * sigma_pos / dt` plus the timing term, with `sigma_pos` from the rounding of the coordinates (0.1 minute = 185 m, 1 minute = 1.85 km, found from the coordinates themselves), shown as `~452 kt +-12`. Pairs closer than 3 sigma, shorter than 60 s or longer than 90 min, faster than 1000 kt or with an inconsistent clock give no figure. Order of preference: the aircraft's own value (never replaced), then the estimator's filtered velocity (from the third fix on), then the two-point value. The map upload sends the derived figures in the usual `speed`/`heading`/`vrate` fields with `"dv":1` (the heading only when it is good enough to turn an icon); the CSV/JSON export of the aircraft tables has separate `gs_derived_kt`, `track_derived_deg` and `vrate_derived_fpm` columns.

## What is simulated, and what is not

The sensor models are mine: ADS-B position noise 8 m, latency 0.1 to 0.3 s, velocity noise 1 m/s and 0.8 degrees, 2 % dropouts, 0.3 % wild positions; text positions rounded to 0.1 minute and stamped to one second (`stamped`) or received 0 to 30 s late. **The simulator's latency has the same mean as the estimator's default**, so the ADS-B figures are best-case; real GNSS latencies differ per transponder.

Not verified on real traffic (no recordings were used): the latency and timing sigmas of every source; the NIC/NACp tables as transcribed here (check them against DO-260B before relying on a sigma); the real error of ADS-B positions against the NACp bound; the text formats of the route parser on real ACARS/VDL2 messages (the `FPN` and `DA:/AA:` layouts in particular); whether real aircraft fly direct enough for the route prior to help; ADS-C kinematics through `fixFromVdl2`; the HFDL position noise; the wind field (no real winds were fed); whether the adaptive process noise behaves on the real mix of dropouts and duplicates.

## References

* Y. Bar-Shalom, X. R. Li, T. Kirubarajan, *Estimation with Applications to Tracking and Navigation*, Wiley 2001 (IMM, EKF, NEES/NIS consistency tests, Joseph form).
* H. A. P. Blom and Y. Bar-Shalom, "The interacting multiple model algorithm for systems with Markovian switching coefficients", IEEE TAC 33(8), 1988.
* X. R. Li and V. P. Jilkov, "Survey of maneuvering target tracking", parts I (dynamic models, IEEE TAES 39(4), 2003) and V (multiple-model methods, 2005).
* ICAO Doc 9871, *Technical Provisions for Mode S Services and Extended Squitter*; RTCA DO-260B (NIC, NACp, NACv tables).
* ARINC 618, 620, 702A (ACARS air/ground, position and flight-plan message formats); ARINC 622 / FANS-1/A ADS-C basic report.
* J. P. Snyder, *Map Projections: A Working Manual* (azimuthal equidistant projection, convergence of the meridians); E. Williams, *Aviation Formulary* (great-circle bearing, cross-track and along-track distance).
* ICAO Doc 4444 (flight plan format, location indicators).
