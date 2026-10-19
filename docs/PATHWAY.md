
## 2026-10-18 FPV video (analog)
New mode fpv (family 101, active 102, std 103): channel plans, scan, FM video picture via the analog TV decoder, 6.0/6.5 MHz sound, snapshot/recording. Synthetic tests only.

## 2026-10-18 QO-100 presets, auto decoder, conversation manager
Branch qo100-028. QO-100 NB presets for every mode (data/frequencies/qo100.tsv; LNB offset on tune; transponder is non-inverting, USB both ways). Narrowband tracker + classifier (core/src/qo100/qo100_track.cpp), auto decoders on the tracked signals through the ordinary FT8/FT4/SSTV/RTTY/JS8/PSK/CW receivers in shared channels with a CPU guard (qo100_auto.cpp), SSB conversation manager with Listen / auto listen / squelch / record / speech-to-text hook and callsign finder. UI: QO-100 Auto decoder and Conversations views, ADIF (PROP_MODE SAT), SSTV to the gallery. Synthetic tests only (tests/qo100/test_qo100_auto.cpp); docs/qo100-auto.md.
## 2026-10-18 FSQ and IFKP
Two separate new modes: FSQ (family 113, active 114, std 115; five speeds, directed messages with sender, addressee, command, CRC of the callsign) and IFKP (family 116, active 117, std 118; three speeds). One shared 33-tone receiver (core/src/fsq/kbd33.cpp: band search, tone grid, running sums of 73 positions, speed and timing from the sums, history decode on lock), a shared station and generator, two thin receivers, one UI template. Synthetic tests only; parameters and varicode from fldigi's sources, not checked against a real transmitter.
## 2026-10-18 Docs for 0.2.7
README: 120 modes, what is new in 0.2.7, Rust in the build steps, Android USB note; docs index (docs/README.md); new docs band-survey.md and pass-scheduler.md; Docker README version.

## 2026-10-19 DVB-T2 HEVC VideoToolbox fallback (#203)
Hardware start guard now begins on an HEVC IRAP NAL (CRA/IDR/BLA), not on the packet key flag, so RASL-led joins are held back. Give-up window is 3 s of input (was 4 s and 60 packets). Build and tests not run (owner on battery); verify on the Mac with the hw_guard test.
## 2026-10-19 Meteor LRPT capture (#199)
LRPT (std 61) runs a 2 Msps capture instead of 1 Msps when the radio allows it, so the 150 kHz channel stays inside the window while the pass tracker moves the centre (app/state/app_state.cpp applyBandwidth). Not built or run here; the centre-follow path (app/panels/passes_ui.cpp retuneTo) was already in place.
## 2026-10-19 Recording library
Library button in the diagnostics row (app/panels/recording_library_ui.cpp): lists the recordings folder with date, length, rate, centre, the mode they were recorded in (the sidecar description) and a mode to re-analyse in; Open makes one the IQ file source, Re-analyse also opens the chosen mode. listRecordingFiles() in core/src/source/iq_file.cpp reads only sidecars and file headers. New SigMF sidecars now carry the mode id as their description. Nothing deleted or moved. Not built or run on this machine.
## 2026-10-19 Codec loader for user-installed speech codecs
Branch codec-loader-213. Generic loader for speech codecs the user installs (TETRA voice now, Iridium later): plug-in onair-codec-<id> in the user codecs folder under plat::dataDir() (never bundled), "Codec not installed" text with a docs link, one-time legal notice stored in prefs before loading. C plug-in ABI and SpeechCodec interface in core/include/dect2/codec/speech_codec.h; loader core/src/codec/speech_codec.cpp; app dialog app/codec/speech_codec_ui.cpp; unit test tests/codec/test_speech_codec.cpp with a silence fake. No codec in the repo. Not built or run on this Mac.

## 2026-10-19 DGPS beacon mode (WIP) wired into the app
DGPS beacon (family 119, active 120, engine std 121, "DGPS beacon (WIP)" in the Satellite and navigation group). core/src/dgps/dgps_engine.cpp mixes the beacon to 0 Hz by the signal offset, decimates to about 4.8 kHz and feeds dgps::Receiver a second at a time; panel in app/dgps/dgps_ui.cpp (station ID, carrier offset, level, frames, message types). Not built or run here (owner rule: no builds on the Mac); test tests/app/test_dgps_mode.cpp reads the source for the row, the family count and the panel strings. Real beacons not checked; RTCM tables still from memory per the decoder commit.
