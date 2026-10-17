# core/src/engine: The receiver engine

Sources for this topic. engine.cpp (the thread that reads a radio and feeds the active mode), mode table and tuning (modes, mode_tuning, mode_synth), the scanner, quality score, signal identification, device claims, crash report, updater, and the wide-capture helpers (multitv, widespan, wide_bank, channelizer).

Mode folders (core/src/<mode>/) hold one decoder each; this folder holds code shared by all of them. See ARCHITECTURE.md.

## Pager and APRS channel width

The pager and APRS / packet receivers have a 12.5 / 25 kHz channel filter choice (`setNarrowChannel`, saved as `pagerNarrow` / `packetNarrow`).
Whole-span decoding (`WideModes`) follows it: every carrier receiver gets the same filter (`setNarrowChannels`). The carrier grid stays 25 kHz
(`stepHz` in `wide_modes.cpp`), so two 12.5 kHz channels between grid points are found as one carrier; use the single-channel receiver for those.
