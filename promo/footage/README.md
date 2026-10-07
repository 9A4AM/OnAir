# Real app footage

The 0.1.0 promo shows the actual Linux build decoding a DVB-T broadcast made here:

1. `prog.html` / `prog.cjs` render the programme "Evening Landscapes" (`/tmp/progf`).
2. ffmpeg muxes it into a 24.13 Mbit/s TS with three services (Channel One HD, Channel One +1, City Radio).
3. `ts2iq.cpp` (links `libdect2core.a`) modulates the TS as DVB-T 8K 64-QAM 2/3 GI 1/8; a resample to 10 Msps cs8 gives the IQ file the app plays.
4. `cap.sh` runs the app under Xvfb and records the screen with ffmpeg x11grab. The capture build has local-only tweaks (window size, `--freq`, a slower file pace so the slow container keeps up, source label, dropped-sample counter hidden); these are not in the app.

Clips are extracted to `/tmp/clips/<name>/f_%05d.jpg`; the 3D living-room shot (`room/`) renders to `/tmp/roomf`.

## v0.1.4 footage
Captured from the current `main` build (`cap.sh` with `APP=/tmp/bmain/dect2`, `cap2.sh` for the first-start tour with Onny). Clips are extracted to `/tmp/clips2/<name>/f_%05d.jpg` (overview, Player, Guide, Outputs, echo, onny).

## v0.2.0 footage
Captured from the 0.2.0 build with `cap3.sh` (Xvfb, mouse parked off-screen; `CLICKS` drives click-to-tune; capture-only `ONAIR_PACE`/`ONAIR_WFAUTO` slow the file and auto-range the waterfall). The new modes are shown from their built-in test signals (`--mode dvbs|dtmb|atv|drm|adsb|dmr|gnss`, `--dab`, `--atsc3`, `--isdbt`, `--fm`). Clips are extracted to `/tmp/clips3/<name>/f_%05d.jpg`; the 0.1.4 interface for the before/after wipe comes from `/tmp/clips2`.

The speed scene names no figure. Measured with `dect2cli` on the same 30 s DVB-T 8K 64-QAM file, the receiver costs about 310 % of real time in 0.1.4 and about 100 % in both 0.1.5 and 0.2.0: the DVB-T speed-up shipped in 0.1.5. 0.2.0's speed work is in the other receivers and in the screen pacing, so the video says that and shows no 0.1.x-vs-0.2.0 number. `cpu.sh` measures the whole app under Xvfb.
