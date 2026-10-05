# Real app footage

The 0.1.0 promo shows the actual Linux build decoding a DVB-T broadcast made here:

1. `prog.html` / `prog.cjs` render the programme "Evening Landscapes" (`/tmp/progf`).
2. ffmpeg muxes it into a 24.13 Mbit/s TS with three services (Channel One HD, Channel One +1, City Radio).
3. `ts2iq.cpp` (links `libdect2core.a`) modulates the TS as DVB-T 8K 64-QAM 2/3 GI 1/8; a resample to 10 Msps cs8 gives the IQ file the app plays.
4. `cap.sh` runs the app under Xvfb and records the screen with ffmpeg x11grab. The capture build has local-only tweaks (window size, `--freq`, a slower file pace so the slow container keeps up, source label, dropped-sample counter hidden); these are not in the app.

Clips are extracted to `/tmp/clips/<name>/f_%05d.jpg`; the 3D living-room shot (`room/`) renders to `/tmp/roomf`.

## v0.1.4 footage
Captured from the current `main` build (`cap.sh` with `APP=/tmp/bmain/dect2`, `cap2.sh` for the first-start tour with Onny). Clips are extracted to `/tmp/clips2/<name>/f_%05d.jpg` (overview, Player, Guide, Outputs, echo, onny).
