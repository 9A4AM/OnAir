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

The speed scene shows receiver CPU from `dect2cli`'s own load meter (% of real time = CPU cores busy), each build made from its release tag, the same recording, the same machine, 30 s per run, two runs each:

| mode | 0.1.0 | 0.1.5 | 0.2.0 |
|---|---|---|---|
| DVB-T (8K 64-QAM file) | 305–307 % | 100–103 % | 102–104 % |
| ISDB-T (`isdbtgen` 13-segment test recording) | – | 172 % | 138–141 % |
| ATSC (`--synthetic --atsc`) | 231–241 % | 255–256 % | 242–245 % |
| DAB (`dabtool gen` recording) | – | 9 % | 8–9 % |

The video shows DVB-T against 0.1.0 (3x less; the gain shipped in 0.1.5) and ISDB-T against 0.1.5 (19 % less), each bar labelled with its version. Whole-app CPU under Xvfb (software rendering) was the same for 0.1.5 and 0.2.0, so the screen pacing is described, not quantified.

## v0.2.4 footage and the rock cut
The 0.2.4 film (`v024.html` / `v024.js`) describes what changed since the 0.2.0 video, cut to a rock song you supply (the song and the finished video are never committed: `OnAir_promo_v0.2.4*.mp4` is in `.gitignore`).
- Footage: the unmodified `main` build (`DECT2_WINSIZE=1920x1080`), every mode on its built-in test signal, captured with `cap4.sh` (Xvfb + x11grab); `clicktab.py` finds the active tab's top line in a screenshot and opens the mode's own tab; `capture_024.sh` is the full list; `extract.sh` writes `/tmp/clips4/<name>/` and `c4n.js`.
- Mode groups and counts: `groups024.js`, generated from `app/toolbar.cpp` on main (76 mode-list entries in 14 groups; with DVB-T2 and DVB-T counted apart, as the 0.2.0 film did, 14 modes then and 77 now).
- Soundtrack: `song_edit_024.py <song> <dir>` tracks the beats, joins three parts of the song on downbeats and writes `song_edit.wav` and `beatmap024.js`; the film's cuts follow that beat map.
- Render: `PAGE=v024.html FDIR=<dir> node render.cjs render 3`, then mux with the edited song (`loudnorm=I=-14`).

## v0.2.7 footage and the film
The 0.2.7 film (`v027.html` / `v027.js`) shows what changed since 0.2.4, cut to a rock song you supply (neither the song nor the finished video is ever committed: `OnAir_promo_v0.2.*.mp4` is in `.gitignore`).
- **App:** the head of the `0.2.7` branch, built with `cmake -DDECT2_BUILD_APP=ON` and `ninja dect2 dect2cli`; every mode runs on its built-in test signal under Xvfb at 1920x1080 (`cap7.sh`). `init_prefs.sh` answers the "share positions with map.on-air.dev" question once with "No, do not ask again" into a settings folder every capture reuses; `dlgscan.py` checks every clip for that dialog.
- **Clips:** `capture_027_modes.sh` (all 76 older modes, 3 s each) and `capture_027_features.sh` (the new screens, opened with the app's dev flags: `--touchsheet`, `--txdry`, `--images`, `--passive-radar`, `--df`, `--packs`, `--rfi`, `--passes`, `--map`...); the 44 new modes are `--mode <id>` for the ids in `groups027.js`. `extract7.sh` writes `/tmp/clips7/<name>/` and `c7n.js`.
- **Modes and groups:** `groups027.js` is generated from `app/ui/mode_table.cpp`: 120 modes in 14 groups, 44 of them new since the 0.2.4 film.
- **Numbers on screen:** 652 non-merge commits and 16 authors between the `v0.2.4` tag and the `0.2.7` branch (86 + 202 + 364 per release); CPU from `speed_027.sh` (dect2cli's load meter, 0.2.4 and 0.2.7 built from source, same recordings, two runs): ISDB-T 140 and 146 % to 76 and 80 %, DVB-T 100 and 109 % to 91 and 98 %, ATSC and DAB unchanged.
- **Soundtrack and sync:** `audio_features_028.py <song> audio028.js` analyses the song you supply (never committed): 64-band spectrum at 60 fps, kick / snare / hat events with their strength at 6 ms resolution, the song's own section boundaries (a beat-synchronous self-similarity novelty curve, each snapped to the nearest hit), the band's entry and the last full-strength hit. `v027.js` starts every chapter on a section boundary, fires its cuts, flashes and shakes on the real kick and snare events, and draws the song's own spectrum and waterfall along the bottom as if OnAir were receiving it. Render at 60 fps: `PAGE=v027.html FDIR=<dir> node render.cjs render 3`, then mux with the song (`-framerate 60`, `loudnorm=I=-14`).
