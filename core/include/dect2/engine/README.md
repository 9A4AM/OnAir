# core/include/dect2/engine: The receiver engine

Public headers (#include "dect2/engine/<name>.h") for this topic. engine.cpp (the thread that reads a radio and feeds the active mode), mode table and tuning (modes, mode_tuning, mode_synth), the scanner, quality score, signal identification, device claims, crash report, updater, and the wide-capture helpers (multitv, widespan, wide_bank, channelizer).

Mode folders (core/src/<mode>/) hold one decoder each; this folder holds code shared by all of them. See ARCHITECTURE.md.
