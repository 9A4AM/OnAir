# ATSC 1.0 (8-VSB) receiver for OnAir: handoff document

Written 2026-10-02. Purpose: someone (or another agent) with no memory of the work so far can read this and finish the job.
Everything here was checked against the repository at commit `4699d7c` unless marked "unverified".

## 1. The task

OnAir (formerly DecT2; the source namespace, library and CLI tools still say `dect2`) is a native macOS digital-TV receiver for
HackRF One/Pro (GPL-3.0-or-later, C++17/Objective-C++, Dear ImGui + Metal). It already decodes DVB-T2 and DVB-T live, with a
scanner, player, EPG, teletext, multipath/quality meters, an antenna direction finder and automatic channel bandwidth.

The user asked for **ATSC 1.0 (8-VSB, 6 MHz, US/Canada/Mexico/South Korea) support**:
- a receiver that locks to an 8-VSB signal captured by the HackRF and outputs an MPEG transport stream into the existing
  TS / EPG / player pipeline;
- a **DVB <-> ATSC switch** in the UI (already built, see 6.2) that selects the standard family;
- the same quality as the rest of the app: fully automatic except device/frequency/gain, telemetry and lamps, scanner support,
  tests, honest documentation of limits.

Out of scope for now: ATSC 3.0 (different standard: OFDM + LDPC + IP), 16-VSB cable modes.

**There is no real ATSC signal available.** The user is in Dubai (DVB-T2) and the HackRF is currently unplugged. All validation
so far is against signals produced by our own generator, and the FEC chain is verified bit-exact against GNU Radio's `gr-dtv`.
A real ATSC capture (cs8/cu8, ~8-10 Msps, any US/Korean station) would be the best next validation; ask the user if someone can
record one.

## 2. State in one paragraph

The channel coding (randomiser, Reed-Solomon (207,187), convolutional byte interleaver, 12-way trellis code, field sync) and its
inverse (Viterbi, de-interleaver, RS, de-randomiser) are finished and verified. A test-signal generator exists. A first complete
receiver exists (resampler + matched filter, pilot loop, symbol clock, field sync, per-field least-squares equaliser, decoder)
and decodes clean and impaired generated signals, **but**: (a) it is about 2x too slow for real time, (b) a recent change made some
test cases flaky (see 5.2, the first thing to fix), (c) strong post-echoes (e.g. -6 dB at 5 us) are not handled (needs a decision
feedback equaliser), and (d) it is **not connected to the engine, UI, scanner or quality meter at all**.

## 3. Repository map (ATSC-relevant)

| File | What it is |
|---|---|
| `core/include/dect2/atsc.h`, `core/src/atsc.cpp` | Constants, PN sequences, `fieldSyncSymbols`, `Randomizer`, `rsEncode/rsDecode` (207,187), `ByteInterleaver`, trellis tables + `trellisMap()`, `FieldEncoder` (TX, 312 packets -> 313x832 symbols), `FieldDecoder` (RX, soft levels of one field -> packets) with the 8-state Viterbi |
| `core/include/dect2/atsc_gen.h`, `core/src/atsc_gen.cpp` | `Generator`: test signal (TS source -> fields -> VSB baseband centred on the channel, with pilot, echoes, CFO, SRO, noise, output at any sample rate). `rrcValue`, `rrcTaps` |
| `core/include/dect2/atsc_rx.h`, `core/src/atsc_rx.cpp` | `AtscReceiver` (pimpl) and `atsc::Core` (all the DSP: stages A-D below), telemetry publishing |
| `core/include/dect2/atsc_tel.h` | `AtscTelemetry` (plain data for the UI) |
| `tests/test_atsc_fec.cpp` | Round-trip of the coding chain, RS, interleaver, trellis map, noise down to 15.5 dB. **Passes.** |
| `tests/test_atsc_rx.cpp` | Generated signal through the receiver; table of impairments. **2 of 12 cases fail, see 5** |
| `tests/reference/cmp_atsc.cc` | Compares our TX chain with the gr-dtv blocks (randomiser, interleaver, trellis encoder, field sync mux). **Bit exact.** Not part of the CMake build, see 4.3 |
| `app/main.mm` | `standardSwitch()` (the DVB/ATSC toggle, `App::atscMode`), currently only shows a "not implemented" notice and disables Start |
| `TASKS.md` block C2 | task list entry for ATSC |

Existing infrastructure you will integrate with (not ATSC-specific): `core/src/engine.cpp` + `core/include/dect2/engine.h`
(`Engine`, `onTsPackets`, `autoSelect`, standard modes), `core/include/dect2/t2rx.h` (`RxTelemetry`), `core/src/ts.cpp` (TS demux, EPG),
`core/src/quality.cpp` (`QualityMeter`), `core/src/scanner.cpp`, `core/src/source.cpp` (HackRF/file/synthetic sources).
The DVB-T receiver (`dvbt_rx.h`) is the closest example of how a second standard was added: same `configure/feed/telemetry/
setPacketCallback/detectLevel` interface, selected by `Engine::activeStd_`, packets delivered through `Engine::onTsPackets`.

## 4. How to build, test, debug

### 4.1 Build and tests
```
cmake -S . -B build && cmake --build build -j8          # whole project (macOS; needs libhackrf, ffmpeg, glfw via Homebrew)
cmake --build build --target test_atsc_fec test_atsc_rx
build/test_atsc_fec            # passes
build/test_atsc_rx             # table of cases, prints FAIL lines
build/test_atsc_rx v 0         # verbose telemetry for case 0 (any argument turns verbosity on; the 2nd argument picks a case index)
```
`dect2core` is the static library; the new files are already in its source list in `CMakeLists.txt`.

### 4.2 Debug switches (environment variables, all read in `atsc_rx.cpp`)
`ATSC_DBG=1` print PLL state and one line per decoded field (SNR, RS clean/corrected/failed); `ATSC_PROF=1` stage timings;
`ATSC_RATE=<Hz>` input sample rate for the first test case; `ATSC_NOPLL`, `ATSC_LAMBDA`, `ATSC_KP`, `ATSC_KI`, `ATSC_FAST`,
`ATSC_NOWIN` experiment knobs (remove or turn into constants once settled).

### 4.3 Reference comparison (optional, needs GNU Radio sources)
The gr-dtv sources used were at `/private/tmp/gnuradio-source/gr-dtv` (a temporary location: re-clone `https://github.com/gnuradio/gnuradio`
if gone). The comparison compiles gr's `lib/atsc/atsc_randomize.cc, atsc_interleaver_impl.cc, atsc_trellis_encoder_impl.cc,
atsc_basic_trellis_encoder.cc, atsc_field_sync_mux_impl.cc, atsc_plinfo.cc` against tiny stub headers for `gnuradio/block.h,
sync_block.h, io_signature.h, dtv/api.h` (a stub `block` base class with `d_logger`, `set_output_multiple`, `consume_each`,
`make_block_sptr`) and `#define private public` to call the internals. Command used:
`clang++ -std=c++17 -O1 -w -Istubs -I$G/include -I$G/lib/atsc -Icore/include cmp_atsc.cc <those gr files> core/src/atsc.cpp -o cmp_atsc`.
The stubs are in the author's scratchpad, not in the repo; they are about 30 lines and easy to recreate.

### 4.4 Pitfalls met during the work
- macOS `sed -i` needs `sed -i ''`. A Python one-liner `open(p,'w').write(open(p).read())` truncated files twice earlier in the
  project: read into a variable first.
- `cmake --build ... | grep error` returns exit status 1 when there are no errors; that is fine.
- The 1 GB of generated samples are never written to disk; tests stream them.

## 5. Current test results and known problems

Last run (`build/test_atsc_rx`, 8 Msps unless noted, 1.6 s of signal per case; "good" = unflagged packets that are in the reference
stream; "CPU x" = processing time / signal time):

| Case | Result |
|---|---|
| clean | 99.6 % good (first fields after lock are flagged), lock 0.07 s |
| clean, 10 Msps | 100 % |
| carrier offset +18 kHz / -25 kHz | 100 % |
| clock offset +60 ppm | 100 % |
| clock offset -60 ppm | **1820 packets only** (should be ~19000): receiver stopped delivering after ~6 fields |
| SNR 19 dB / 16.5 dB | 98.9 % / 97.5 % (ATSC needs about 15.2 dB, so this is near ideal) |
| post echo -8 dB at 2 us | 99.8 % |
| pre echo -10 dB at 1.5 us | **86 %, only 4264 packets** (lost lock partway) |
| post echo -6 dB at 5 us, SNR 24 | **0 %** (equaliser too short, see 5.3) |
| three echoes + CFO 10 kHz + SRO 30 ppm, SNR 25 | 1820 packets only (stopped after ~6 fields) in the last run |

Run-to-run, the same cases passed 100 % in the run before the last two code changes (see 5.2). CPU is about 2.1x real time.

### 5.1 What was learned (keep these, they cost time)
1. **Signal model.** In channel-centred baseband the 8-VSB signal is `x(t) = sum_k (a_k + 1.25) (-j)^k u(t - kT)` with the real
   root-raised-cosine `u` (excess 0.1152, Nyquist frequency = symbol rate/4 = 2.69 MHz, i.e. the 6 MHz channel shape). The pilot sits
   at `-symbol_rate/4 = -2.690559 MHz` from the channel centre. After matched filtering with the same `u`, mixing the pilot to DC
   and taking the real part gives ISI-free symbols at the symbol rate (the quadrature neighbours cancel in the real part).
   gr-dtv's `atsc_rx_filter`/`atsc_fpll` do the same.
2. **Pilot loop.** gr's loop bandwidth (about 35 kHz) leaves ~0.15 rad phase jitter from the data energy near DC and caps SNR at
   ~19 dB. We use two modes: wide (acquisition) until the pilot has been on the real axis for ~11 ms, then narrow (`alpha 0.0035`,
   `tau 40 us`). Phase noise is then negligible.
3. **Symbol-timing loop.** The error is taken from the early/late difference of the four sync symbols; the random data symbols next to
   them corrupt each measurement, so a wide loop jitters the sampling instant and limited the SNR to ~24 dB. Fix in place: wide loop
   for the first 300 segments (`kp 0.12, ki 0.01`), then narrow (`kp 0.003, ki 1e-5`). Clean-signal SNR went 24 -> 41 dB.
4. **Equaliser regulariser** (`lambda`) is not the limit.
5. **Trellis encoders start in an unknown state** at each field: the Viterbi starts with all states equal, which causes a handful of
   byte errors per field that RS corrects.
6. gr-dtv's deinterleaver has an extra 156-byte alignment FIFO so interleaver + de-interleaver delay the stream by exactly 52 segments;
   our `ByteInterleaver(true)` reproduces it. The de-randomiser is reset at output segment 52 of each field.

### 5.2 FIRST THING TO FIX: lock loss / stalls introduced by the last edits
Between the run where all cases except the 5 us echo passed and the last run, the changes were: the NEON/split-array resampler
(`processInput`), profiling hooks, and a small reordering. Results are not bit-identical any more (different float summation order)
and several cases now stop delivering packets after ~6 fields or lose lock. Almost certainly this is not the NEON code being wrong
but **fragile lock/reacquisition logic** that a tiny numeric change tips over. Suggested approach:
1. Run `ATSC_DBG=1 build/test_atsc_rx v <case>` for the failing cases and watch `lockLosses`, field sync misses and the per-field SNR.
2. Reproduce with the old scalar loop (`#if 0` the NEON block) to confirm it is numerical sensitivity, not a bug.
3. Check `onSegment()`: loss of segment lock after 12 consecutive segments with correlation `q < 0.3`, `checkFieldSync()` with
   `err < 90` of 511 hard decisions and 3 tolerated misses, and the reacquisition path (`seg=false; accIdx=dEnd(); lastSearch=...;
   acc cleared`). After a loss the receiver does **not** seem to reacquire reliably (stuck with ~1820 packets): the accumulator is
   cleared but `dBase` trimming, `accIdx`, `lastSearch` and `tau` bookkeeping should be reviewed; also `dec.reset()` plus
   `pendingStart=-1` handling in `checkFieldSync`.
4. Make the loss criteria and the field-sync threshold less brittle (use soft correlation instead of hard-decision counts, and
   accept a field sync when two of three independent indicators agree).

### 5.3 Strong echoes need a decision feedback equaliser
The equaliser is a T-spaced feed-forward FIR of 128 taps (24 future, 103 past, plus a bias) solved per field by least squares on
the known symbols (field sync segment: first 728 symbols; plus the 4 sync symbols of every segment, 1976 equations). It copes with
echoes up to about -8 dB at 2 us and pre-echoes of -10 dB at 1.5 us, but not with -6 dB at 5 us or 0 dB echoes: inverting a strong
post-echo needs many taps (geometric series). Real ATSC receivers use a DFE. Plan:
- Add feedback taps (B about 128) on past decisions, FF about 64.
- Initial solution: LS on the field sync (past symbols known; for the first B symbols of the field sync use the previous field's
  decoded symbols) = 728 equations.
- Refinement: the **Viterbi path gives much better decisions than a slicer** (coding gain). After pass 1, take the survivor path's
  8-level symbols as targets, re-solve LS on a subset (about 2000 positions), re-equalise, re-decode. The Viterbi currently returns
  only dibits: extend `viterbiRun` to also output the path's symbol (`out_symbol[state*4+input]` is in `trellis().out`).
- Also keep an eye on error propagation in the feedback path; limit feedback to decisions with margin.
Budget: current LS costs 16 MMAC/field; DFE doubles filter cost, so the speed work (5.4) matters first.

### 5.4 Speed (about 2.1x real time on an M-series Mac, needs < 0.5x including the rest of the app)
Timings (ATSC_PROF, clean case, 8 Msps in): resampler+matched filter+pilot loop ~1.2 s, symbol clock + per-field decode ~2.0 s
(of which the field decode is ~1.9 s) for 1.6 s of signal.
- Resampler/matched filter: 44 taps x 2 per output at 21.5 Msps. NEON dot added; consider `J=14` instead of `ceil(2.4e-6*fs)+2`
  (the RRC needs about +-1.6 us), and moving the pilot mixing to the input rate.
- Field decode is the big one: LS accumulation (scalar double rank-1 updates: use `cblas_ssyrk` from Accelerate on a stacked
  matrix, or accumulate in float with NEON), the 128-tap FIR over 260k symbols (`vDSP_conv`), the 8-state Viterbi (12 encoders x 21.5k
  symbols, store only 5 bits/decision, consider the equivalent 4-state trellis with differential post-decoder as gr-dtv does),
  `rsDecode` (a syndrome pass of 20x207 multiplies per segment; skip the BM step when all syndromes are zero, already done) and the
  allocations (`std::vector` per field).
- Run field decode on a worker thread (fields are independent until the de-interleaver, which is sequential); the ATSC path in the
  engine must not stall the sample thread (see how `PlpDecoder` does it for DVB-T2).
- Provide `#ifdef __APPLE__` Accelerate paths with plain C++ fallbacks (the project will later be ported to Windows/Linux).

## 6. Design of the receiver (what the code does)

`AtscReceiver::feed(complex samples, centre = channel centre, rate >= 6.5 Msps)` -> `Core::processInput`:

**Stage A, resampler + matched filter.** Polyphase windowed RRC (128 phases, `J` taps either side, cos^2 taper) converts the capture
rate to exactly 2 samples/symbol (21.52 Msps). Output = complex stream `y`.

**Stage B, pilot loop and AGC.** `z = y * e^{-j theta}` with the NCO initialised at the nominal pilot frequency; one-pole filtered `z`
gives the pilot phasor `pf`; `atan2(Im pf, Re pf)` drives a 2nd-order loop (updated every 4 samples), acquisition/tracking
modes as in 5.1. Output `d = Re(z)` minus a slow DC estimate (the pilot) scaled to unit rms 4.58 (the rms of the 8 levels).
`pilotLock` = tracking mode and pilot steadily on the real axis. CFO telemetry = `(omega - omega0) * fs / 2 pi`.

**Stage C, symbol clock and segment sync.** Not locked: correlate `d[i] - d[i+2] - d[i+4] + d[i+6]` (the +,-,-,+ sync pattern at
2 samples/symbol) and average it over segments in a 1664-bin table; after the peak has been strong and stable for 6 segment
periods (`peakRun >= 6`) take the parabolic sub-sample peak as symbol timing and start sampling symbols with an 8-tap windowed-sinc
interpolator (64 phases). Locked: once per segment the early/late measurement on the four sync symbols nudges the time (`tau`) and the
rate (`rr`), see 5.1. `syncQ` = normalised sync correlation, lock is lost after 12 consecutive segments below 0.3.

**Stage D, fields.** Every completed 832-symbol segment is appended to `raw`. `fieldSyncAt` hard-slices the segment against PN511
(error limit 90 of 511) and reads the field polarity from the middle PN63. Two sync segments exactly 313 segments apart => `fieldLock`.
Thereafter the next sync is expected at `nextFs`, up to 3 misses are tolerated, parity alternates by assumption when missed.
When the segment that begins the next field completes, the previous field is decoded (`decodeField`): LS equaliser (training = known
sync symbols, with a bias term for the pilot DC), equalised levels for all data symbols, SNR from the training residual and from
the distance of data symbols to the nearest level, then `FieldDecoder::decode` (Viterbi, de-interleave, RS, de-randomise).
Fields whose training SNR is below 9 dB are not decoded (`dec.reset()`), so the first fields after lock are flagged/missing.
Packets go to the callback with `transport_error_indicator` set on RS failures.

`FieldDecoder` keeps the interleaver FIFOs between fields: it needs consecutive fields; `reset()` after any gap. The first 52 output
segments after a reset are discarded.

## 7. Work still to do, in order

1. **Fix the lock-loss/stall problem** (5.2) and make the test table green again. Add a stress test: lock, drop 0.3 s of signal,
   reacquire; random start offsets; very low SNR; no-signal noise (must not report lock; check false lock rate).
2. **Speed** (5.4) until the ATSC path needs < 50 % of one core at 8 Msps; threading for the field decode.
3. **DFE and decision-directed refinement** (5.3), tests with 0 dB, -3 dB echoes up to 10-15 us and the A/74-style ensembles.
4. **Engine integration** (follow what was done for DVB-T):
   - `RxTelemetry`: add `AtscTelemetry atsc;` and `standard == 2`; `Engine` holds an `AtscReceiver rxA_`, `Engine::setStandard(0/1/2/3)`
     or a separate "family" flag (UI switch) so that DVB auto-detection (T2 <-> T alternation) is not used in ATSC mode;
     `activeStd_`, `autoSelect`, `feedRx`, `applyReset`, `start/stop/configure` (rate >= 6.5 Msps; the HackRF Pro tunes exactly
     only at <= 10 Msps, use 8 or 10 Msps), `logAtscEvents` (lock messages), packet callback -> `onTsPackets` (it expects 188-byte
     packets and the signal duration in seconds).
   - The ATSC channel is 6 MHz wide: bandwidth fixed at 6 MHz in the app when ATSC mode is on (the automatic bandwidth detector is for DVB).
   - ATSC carries MPEG-2/H.264 video with **AC-3/E-AC-3 audio**: check that `Player` (FFmpeg) plays AC-3 and that `TsDemux`
     recognises the stream types (0x81 AC-3 private audio in ATSC PSIP) and parses PSIP (VCT/EIT) tables for channel names and the
     programme guide (ATSC does not use SDT/NIT/DVB EIT). PSIP parsing is a separate task: base PID 0x1FFB, tables TVCT (0xC8), MGT (0xC7),
     EIT (0xCB), STT (0xCD).
5. **UI.** Remove the "not implemented" notice in `standardSwitch()` and enable Start. Header lamps (pilot, segment sync, field
   sync, equaliser, TS), readouts (CFO, SRO, SNR, segment error rate), constellation panel replaced by an 8-level histogram/eye view
   (`AtscTelemetry::levels`), equaliser impulse response plot (`eqTaps`, cursor at `eqCursor`), history plots, Receiver tab contents.
   Quality bar: add an ATSC row to the `QualityMeter` table (threshold of visibility about 15.2 dB SNR).
6. **Scanner.** US channel plan: UHF channels 14-36 at 6 MHz steps (centre of channel 14 is 473 MHz; centre frequency of a channel
   is `470 + 6 (n - 14) + 3` MHz), VHF high 7-13 (177-213 MHz). South Korea uses the same raster. Presets in the Scan tab, ATSC mode
   in `Scanner::run` (lock criteria = field sync + RS success), names from PSIP.
7. **Synthetic source in the app** (Synthetic device -> ATSC variant using `atsc::Generator` for demonstrations without hardware).
8. **Real signal validation** (needs a capture), then tuning of thresholds (field sync error limit, lock criteria, SNR thresholds).
9. Documentation: README status table, `PROJECT_BRIEF.md` sections 11.x and 12, `TASKS.md` block C2 ticks.

## 8. Facts and conventions for whoever continues
- The user's preferences: dense dark instrument-style UI, everything automatic with manual override, honest reporting of limits
  (do not claim features that were not verified), ask before pushing to GitHub (the repo is `adswill/dvb-t2-decoder`; pushes only on request).
- Commit style: short imperative message, a body line when useful, and the attribution trailer given by the harness.
- HackRF Pro quirk: exact centre frequency only at <= 10 Msps and at 20 Msps.
- Verification method used throughout the project: build the reference code (GNU Radio blocks) against stub headers and compare bit
  for bit; generate signals ourselves and run them through the receiver with impairments; run ASan/UBSan (`build-asan` directory) and
  the fuzz test (`tests/fuzz_rx.cpp`) when a receiver changes; extend the fuzz test with the ATSC receiver once the code stabilises.
- Commits for this work: `5281aab` coding chain, `fc9838b` generator + first receiver, `4699d7c` timing/PLL fixes and test table
  (this document is committed with the next commit).
