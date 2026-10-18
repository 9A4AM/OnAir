
## 2026-10-18 FPV video (analog)
New mode fpv (family 101, active 102, std 103): channel plans, scan, FM video picture via the analog TV decoder, 6.0/6.5 MHz sound, snapshot/recording. Synthetic tests only.

## 2026-10-18 QO-100 presets, auto decoder, conversation manager
Branch qo100-028. QO-100 NB presets for every mode (data/frequencies/qo100.tsv; LNB offset on tune; transponder is non-inverting, USB both ways). Narrowband tracker + classifier (core/src/qo100/qo100_track.cpp), auto decoders on the tracked signals through the ordinary FT8/FT4/SSTV/RTTY/JS8/PSK/CW receivers in shared channels with a CPU guard (qo100_auto.cpp), SSB conversation manager with Listen / auto listen / squelch / record / speech-to-text hook and callsign finder. UI: QO-100 Auto decoder and Conversations views, ADIF (PROP_MODE SAT), SSTV to the gallery. Synthetic tests only (tests/qo100/test_qo100_auto.cpp); docs/qo100-auto.md.
