
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

## 2026-10-19 HFDL shared first-stage decimation and speed benchmark
Branch hfdl-fast-028 (from 0.2.8). HfdlReceiver::feed low-passes and decimates a wide capture once before the channels (F = floor(rate / (1.2 x (span + 8 kHz))), only for F >= 2, i.e. 64 MHz and up; 16 and 32 MHz unchanged). Output at 64 MHz is not bit-identical to before. New benchmark tests/hfdl/test_hfdl_speed.cpp (hfdl_speed): 2 s at 16/32/64 MHz, prints wall time per second of input, CI limit 1.0. Not built or run on this Mac; verified by reading only.

## 2026-10-19 HFDL shared FFT channeliser for 32 and 64 MHz captures
Branch hfdl-channeliser-028 (0.2.8 + hfdl-opt-028 + hfdl-fast-028). Above 16.5 MHz with at least 4 channels in band, HfdlReceiver runs one weighted-overlap-add filter bank (N bins of 32-64 kHz, 4N-tap Kaiser prototype, hop N/2, sub-band rate 2 rate/N) on the analysis thread and each channel takes the nearest bin plus a residual mixer at the sub-band rate; cost ~ input rate, not rate x channels. Up to 16 MHz the per-channel path is unchanged. Not run on this machine (CI): tests hfdl_speed (16/32/64 MHz, old vs new at 32 MHz) and hfdl_chan (3 bursts in a 32 MHz capture, same frame count as the old path).
