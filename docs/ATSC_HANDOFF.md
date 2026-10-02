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

## 2. State in one paragraph (updated later the same day, after commit `4699d7c`)

The coding chain, generator and receiver exist and are **integrated**: the engine has an ATSC mode (`Engine::setStandard(3)`, the
DVB/ATSC switch in the app), the app shows ATSC lamps, readouts, an 8-level histogram, equaliser response, level strip and a
Receiver-tab page, the quality meter has an ATSC row (15.2 dB), the scanner and the Scan tab have ATSC presets, the synthetic
device can generate ATSC, and `dect2cli --synthetic --atsc` runs the whole path (verified: locks in 0.2 s, TS flows, no continuity
errors, service list shows). The stall/flakiness (old section 5.2) was an out-of-range read in the timing measurement and is fixed;
the receiver also recovers quickly after a signal dropout and does not lock on noise. The sample thread needs about 0.5x real time;
the per-field work runs on a worker thread. **All 14 test cases now pass** (the equaliser is a plain 128-tap feed-forward one plus a decision feedback equaliser trained on the sync symbols and on trellis-decoded symbols, see 5.3). **Known limit: a 0 dB echo (equal to the main signal) cannot be acquired; -1.5 dB at 8 us and -3 dB at 5 us work (about 95 % of packets, the rest are lost in the first few fields).** What has *never* been done: running on a real ATSC signal or the HackRF,
AC-3 audio and PSIP (ATSC channel names / guide), and an actual scan.

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

### 5.2 RESOLVED: lock loss / stalls (kept for the record)
Cause found with AddressSanitizer: the symbol-timing measurement read samples that had already been discarded (random leftover memory),
so results varied with tiny numeric changes. Fixed by checking that all needed samples are still buffered and keeping one segment of
history. The original analysis follows.
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

### 5.3 Equaliser (implemented): plain feed-forward + decision feedback
`Core::runJob` (worker thread) chooses per field:
- Normal start / mild channels: plain T-spaced feed-forward FIR of 128 taps (24 future, 103 past, plus a bias) solved by least squares
  on the field sync segment and the 4 sync symbols of each segment (`trainFF`).
- If that fits badly (training SNR < 9 dB): a decision feedback equaliser (65 feed-forward + 128 feedback taps) trained on the field
  sync alone with a strong ridge (`trainDFE`, no data rows).
- After two consecutive clean fields: the decision feedback equaliser is trained on the *previous* field's trellis-decoded symbols
  (rows where decision and reception agree within 1.0) plus the field sync; the Viterbi path symbols come from
  `FieldDecoder::decode(..., symbolsOut)`.
- A field that decodes badly (>= 20 % failed segments) gets one more try with the other kind of equaliser (the feedback one can run
  away after a wrong decision); the one with fewer failed segments is kept (the de-interleaver state is snapshotted and restored).
Acquisition was made robust to echoes: the segment-sync peak test uses the median magnitude, the field-sync match tolerates up to
175 of 511 hard-decision errors (random data gives about 255).
Possible improvements: soft/trellis-aided feedback decisions, fractional (T/2) spacing, averaging the normal equations across
fields, equalising before the field sync search so that 0 dB echoes can be acquired.

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

Done since the first version of this list: lock-loss fix, speed + worker thread, engine integration (7.4 first bullet), app UI
(7.5 except quality details), ATSC row in the quality meter, scanner + presets (untested without a HackRF), synthetic ATSC source,
noise and dropout tests.

1. **Acquisition with a 0 dB echo** and longer echoes (>15 us); more equaliser tests (A/74-style ensembles, Doppler).
2. **Real signal validation** (needs a capture, or an ATSC modulator): check field sync thresholds, SNR estimate, pilot level, then tune.
3. **PSIP guide.** The virtual channel table (TVCT/CVCT, PID 0x1FFB) is parsed (`TsDemux::parseTvct`: name, major.minor shown in the provider line,
   program number, service type; test in `tests/test_ts.cpp`). Still missing: MGT (0xC7), EIT (0xCB) and ETT for the programme guide, and
   STT (0xCD) for the clock. AC-3/E-AC-3 are recognised in the PMT (stream types 0x81/0x87); playback relies on FFmpeg and is untested.
4. **Scanner check on hardware**: US channel centre frequencies are `473 + 6 (n - 14)` MHz for UHF (n = 14..36), `177 + 6 (n - 7)` for VHF high
   (n = 7..13); run a scan with the HackRF in the US/Korea (or with a recording of several channels).
5. **Speed polish**: sample thread 0.5x real time; field worker about 0.55x. Smaller resampler (`J` about 14 instead of 22), a 4-state
   trellis decoder, and skipping RS work on clean segments would give margin for slower machines. Portable (non-Accelerate) fallbacks
   exist for the filters but are unoptimised.
6. **Robustness**: add the ATSC receiver to `tests/fuzz_rx.cpp` and run the ASan/UBSan build (`build-asan`) on `test_atsc_rx`
   (slow under ASan: use a short case) before releasing.
7. **UI polish**: the standard combo is disabled in ATSC mode; the TV/Guide tabs show DVB-style data; "Stream" tab outputs work unchanged.
8. **Documentation**: README status table, `PROJECT_BRIEF.md` sections 11.x and 12, `TASKS.md` block C2 ticks.

## 8. Facts and conventions for whoever continues
- The user's preferences: dense dark instrument-style UI, everything automatic with manual override, honest reporting of limits
  (do not claim features that were not verified), ask before pushing to GitHub (the repo is `adswill/dvb-t2-decoder`; pushes only on request).
- Commit style: short imperative message, a body line when useful, and the attribution trailer given by the harness.
- HackRF Pro quirk: exact centre frequency only at <= 10 Msps and at 20 Msps.
- Verification method used throughout the project: build the reference code (GNU Radio blocks) against stub headers and compare bit
  for bit; generate signals ourselves and run them through the receiver with impairments; run ASan/UBSan (`build-asan` directory) and
  the fuzz test (`tests/fuzz_rx.cpp`) when a receiver changes; extend the fuzz test with the ATSC receiver once the code stabilises.
- Commits for this work: `5281aab` coding chain, `fc9838b` generator + first receiver, `4699d7c` timing/PLL fixes and test table, then later commits for the timing-read fix, speed/worker thread, engine integration, UI,
  scanner, tests (see `git log`).
