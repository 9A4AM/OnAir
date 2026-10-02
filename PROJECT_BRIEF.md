# DVB-T2 Decoder for macOS — Project Brief

*Working name: **DecT2** (placeholder — rename freely)*
*Status: draft for review · 2026-10-01*

---

## 1. What we're building

A native macOS application that receives live DVB-T2 broadcasts through a **HackRF One** or **HackRF Pro**, demodulates and decodes them entirely in software, and delivers the resulting MPEG transport stream to:

1. **Live viewing** — video and audio played inside the app
2. **File** — `.ts` recording (full multiplex or selected services)
3. **UDP** — unicast or multicast stream for VLC, ffplay, tvheadend, etc.

The key requirement is **transparency**: every stage of the receiver is visible — spectrum, synchronisation, signalling, every constellation, channel estimate, error correction and transport stream statistics. The app looks and feels like the two reference screenshots (DecDRM and the V.32/fax modem tool): dark, dense, instrument-style, monospace readouts, small tab bars, coloured status lamps, a log panel at the bottom.

### Operating philosophy: fully automatic, manually overridable

| Always set by the user | Auto by default, override available | Always automatic |
|---|---|---|
| Device (HackRF One / Pro, serial) | Channel bandwidth (1.7 / 5 / 6 / 7 / 8 / 10 MHz) | P1 detection & coarse sync |
| Centre frequency | FFT size & guard interval | Fine timing / frequency / sampling-rate tracking |
| Gain (LNA, VGA, RF amp) — optional AGC toggle | Spectrum inversion | Reading L1-pre / L1-post signalling |
| | PLP selection (when there are several) | Pilot pattern, rotation, modulation, code rate, interleaving (all come from L1) |
| | Channel estimator / demapper / LDPC iteration settings | PSI/SI parsing, service discovery |
| | Service selection for viewing/output | |

Tune → the app finds the signal, locks, reads signalling, configures itself, and starts producing services — no further input needed.

---

## 2. Hardware

### HackRF One
- 1 MHz – 6 GHz, half-duplex, **8-bit I/Q**, 2–20 Msps.
- Gain: RF amp (0/14 dB), LNA 0–40 dB (8 dB steps), VGA 0–62 dB (2 dB steps).
- Selectable baseband filter (1.75 – 28 MHz).
- 8-bit ADC ≈ 45–48 dB usable dynamic range. Fine for 64-QAM and for strong-signal 256-QAM; **marginal for weak 256-QAM at high code rates** (UK/DE style multiplexes need ~20–23 dB SNR). The app will show clearly when the limit is the radio, not the signal.

### HackRF Pro
- Supported through the same `libhackrf` API (Homebrew libhackrf 2026.01.3 identifies it as board `BOARD_ID_PRALINE`, MAX2831 transceiver + FPGA). The device layer reads the board ID and queries capabilities instead of hard-coding them; final tuning of Pro-specific settings happens once the hardware is plugged in.

### Sample-rate plan
DVB-T2 elementary period T = 7/64 µs for 8 MHz (scaled for other bandwidths), i.e. a native rate of **9.142857 Msps** for an 8 MHz channel.

- Capture at **2× native** (18.2857 Msps for 8 MHz; HackRF's fractional clock can hit this closely) → half-band decimate to native rate. This gives clean anti-alias filtering and room for frequency offset.
- Residual sample-clock error is tracked and corrected with a fractional resampler (SRO shown in ppm, like DecDRM's "SRO").
- Baseband filter auto-chosen per bandwidth (e.g. 8.75 MHz for 8 MHz channels).
- Optional small LO offset (tune off-centre and shift digitally) to keep the HackRF's DC spike off the centre carriers.

---

## 3. Signal processing chain

Implements ETSI **EN 302 755 v1.4.1** (T2-Base and T2-Lite) and **TS 102 773 / EN 302 307 annexes** where shared.

```
HackRF ─► IQ ring buffer ─► DC removal / IQ imbalance ─► decimate ─► resample (SRO)
   │                                                                     │
   └──► (IQ recorder)                                                    ▼
                                                     P1 detection (1K, C-A-B correlation)
                                                     ├─ coarse time + fractional CFO
                                                     ├─ integer CFO
                                                     └─ S1/S2: T2-Base/Lite, SISO/MISO, FFT size, FEF
                                                                         ▼
                                                     Guard-interval detection (CP correlation)
                                                                         ▼
                                                     OFDM: FFT, fine CFO & timing tracking
                                                                         ▼
                                                     Channel estimation (scattered/continual/edge pilots,
                                                       time + frequency interpolation, MISO Alamouti)
                                                                         ▼
                                     ┌─────────── P2 symbols ───────────┐      Data symbols / FCS
                                     ▼                                  ▼              ▼
                           L1-pre (BPSK, BCH+LDPC 16K)       L1-post (config+dynamic)  Frequency de-interleave
                                     └──── configures everything below ─┘              ▼
                                                                         Cell → PLP mapping (Type 1/2, sub-slices)
                                                                                       ▼
                                                                         Time de-interleave (TI blocks, frames)
                                                                                       ▼
                                                                         Cell de-interleave
                                                                                       ▼
                                                                         Constellation de-rotation + soft demap (LLR)
                                                                                       ▼
                                                                         Bit de-interleave
                                                                                       ▼
                                                                         LDPC decode (16200 / 64800, all rates)
                                                                                       ▼
                                                                         BCH decode
                                                                                       ▼
                                                                         BB descramble, BBHeader parse
                                                                                       ▼
                                                                         Mode adaptation: sync, null-packet
                                                                         re-insertion, ISSY, common-PLP merge
                                                                                       ▼
                                                                         MPEG-TS (188-byte packets)
                                                                                       ▼
                                          ┌───────────────┬──────────────┬───────────────┐
                                     PSI/SI parse     Live player     File writer      UDP sender
```

### Must support (all auto-detected)
- FFT 1K, 2K, 4K, 8K, 16K, 32K, normal and extended carrier modes
- Guard intervals 1/128, 1/32, 1/16, 19/256, 1/8, 19/128, 1/4
- Pilot patterns PP1–PP8, PAPR reduction modes (tone reservation carriers ignored correctly)
- SISO and MISO
- QPSK, 16/64/256-QAM, rotated and non-rotated
- All code rates (1/2 … 5/6, plus T2-Lite 1/3, 2/5), short and normal FEC frames
- Multiple PLPs, common PLP, Type 1 and Type 2 data PLPs, FEF parts (skipped, or T2-Lite inside FEF)
- Input mode A (single TS) and B (multiple)

### Out of scope (initially)
- Encrypted (CA/scrambled) services — they'll be detected and labelled, not decrypted
- DVB-T (first generation) — **not planned**; this is a DVB-T2-only receiver
- Multiple simultaneous services on File/UDP output (for now: full mux or one service)
- Full EPG browser, band scan — possible later additions
- Transmitting

### Performance target
Real time for the hardest case (8 MHz, 32K, 256-QAM, CR 5/6, ~50 Mbit/s) on Apple Silicon. The heavy parts:
- **LDPC**: layered normalised min-sum, NEON SIMD, multithreaded across FEC blocks (Metal GPU path as a fallback if CPU isn't enough)
- **FFT**: Apple Accelerate (vDSP)
- **Demapping / interleavers**: SIMD + lookup tables

---

## 4. Video and audio

- Demux PAT/PMT/SDT/NIT/EIT → service list with names, provider, LCN, video/audio/subtitle PIDs, encryption flags, now/next EPG.
- Video: **H.264, HEVC (H.265), MPEG-2** decoded with **VideoToolbox** (hardware) through FFmpeg's libavcodec; rendered on a Metal layer. Deinterlacing for 1080i/576i.
- Audio: **AAC / HE-AAC, AC-3, E-AC-3, MPEG-1 Layer II** via libavcodec → CoreAudio, with output device and audio track selection.
- DVB subtitles and teletext (pages + subtitles) overlaid in the live player.
- Live A/V sync via PCR/PTS, with a small buffer shown on screen (like DecDRM's "Buffer 530 ms").

### Outputs (any combination, simultaneously)
| Output | Details |
|---|---|
| Live view | In-app video panel, can pop out to a separate window / full screen |
| File | `.ts`; full multiplex (all PLPs' TS) or filtered to chosen services (PAT/PMT rewritten) |
| UDP | Unicast or multicast, 7×188 bytes per datagram, optional RTP header, configurable TTL/interface |
| IQ recording | Raw HackRF samples (`.cs8` + JSON sidecar with freq/rate/gain) — replayable in the app |

---

## 5. User interface

Single window, dark "instrument" style matching the references (light theme optional via a "System theme" selector, as in DecDRM).

### Layout
```
┌─ Toolbar ────────────────────────────────────────────────────────────────────────────────┐
│ Source [HackRF One ▾ serial] Freq [ 690.000 MHz ] BW [Auto ▾] LNA ▮ VGA ▮ Amp ☐  ▶Start ■Stop │
│ ● IQ ● P1 ● GI ● L1-pre ● L1-post ● Frame ● LDPC ● TS ● Video ● Audio   Locked  32K 1/128 PP7 │
│ CFO +1234.5 Hz  SRO -2.3 ppm  SNR 24.1 dB  MER 23.8 dB  BER(pre) 3e-3  LDPC it 6.2  Level -18 dBFS │
├─ Tabs ─────────────────────────────────────────────┬─ Right panel ───────────────────────┤
│ Overview · Spectrum · Waterfall · Sync · Signalling │ Services (LCN, name, codec, bitrate) │
│ · Constellations · Channel · Impulse · SNR/carrier  │ Video preview + audio meters         │
│ · FEC · Frame map · TS · History · Outputs          │ Now / Next                           │
│                                                     │ Outputs: File ● UDP ● Live ●         │
├─ Log ───────────────────────────────────────────────┴─────────────────────────────────────┤
│ 0.12s P1 found: T2-Base SISO 32K · 0.20s GI 1/128 · 0.45s L1-pre OK: PP7, 256-QAM L1 ...   │
└───────────────────────────────────────────────────────────────────────────────────────────┘
```

### Status lamps (top bar)
Green / amber / red / grey for each stage: IQ, P1, GI, L1-pre, L1-post, Frame sync, LDPC, BCH, TS, Video, Audio. One glance tells you where the chain breaks.

### Tabs — "see everything"
| Tab | Shows |
|---|---|
| **Overview** | Spectrum with occupied-bandwidth overlay + the three most important constellations, like DecDRM's front page |
| **Spectrum / Waterfall** | Wideband FFT of the HackRF capture, averaging, peak hold, channel markers |
| **Sync** | P1 correlator output, guard-interval correlation, CFO/timing/SRO tracking plots |
| **Signalling** | Every decoded field of L1-pre, L1-post configurable and dynamic, per PLP, in readable tables with raw hex |
| **Constellations** | Grid of every constellation: P1 (DBPSK), L1-pre (BPSK), L1-post, each PLP before and after de-rotation, scattered/continual/edge pilots, frame-closing symbol. Density/heatmap mode, per-symbol scrub |
| **Channel** | Channel frequency response (magnitude + phase) across carriers, over time |
| **Impulse response** | Echo profile with guard-interval window overlay (SFN echoes visible) |
| **SNR per carrier** | MER/SNR per carrier, exposes narrowband interferers |
| **FEC** | Pre-LDPC BER, LDPC iteration histogram, failed blocks, BCH corrections, per PLP |
| **Frame map** | Visual T2 super-frame: P1 / P2 / data / FCS / FEF, which PLP occupies which cells |
| **TS** | PID table with bitrates, CC errors, PSI/SI tree, PCR jitter |
| **History** | Time plots of SNR, MER, BER, CFO, level, lock state |
| **Outputs** | File / UDP / IQ recording configuration and stats |

Every plot: pause, zoom, export PNG, copy values. Log panel with copy/clear, like both references.

### Manual override pattern
Each auto-detected parameter shows a small **Auto** toggle next to its value. Switching Auto off turns the value into an editable control. Overridden values are highlighted so you never forget something is pinned.

---

## 6. Architecture & technology

| Layer | Choice (proposed) | Why |
|---|---|---|
| DSP core | **C++20**, Accelerate (vDSP), NEON intrinsics | Speed, portability, testable in isolation |
| Device I/O | `libhackrf` (Homebrew) | Supports both HackRF models |
| A/V decode | FFmpeg libavcodec + VideoToolbox | Hardware HEVC/H.264 decode |
| UI | **Dear ImGui** (Metal backend), custom dark theme | Fast to build dense instrument panels like the references |
| Plots | Custom Metal renderers | Thousands of constellation points at 30–60 fps |
| Build | CMake (core + tests) + Xcode project (app) | |
| Tests | Unit tests per block, using reference vectors; IQ-file regression tests | |

### Threads
```
USB RX ─► ring buffer ─► sync/OFDM thread ─► demap thread ─► LDPC worker pool ─► TS thread ─► outputs/player
                                   │                │               │
                                   └─────── lock-free snapshot buffers ───────► UI (throttled 30 Hz)
```
Visualisation taps never block the signal path; they copy snapshots at a throttled rate.

### Repository layout (proposed)
```
core/          DSP & decoding library (C++)
  device/      HackRF + IQ-file sources
  sync/        P1, GI, CFO, timing
  ofdm/        FFT, pilots, channel estimation
  l1/          L1-pre/post decode
  fec/         LDPC, BCH
  frame/       PLP extraction, de-interleavers
  ts/          BB-frame → TS, PSI/SI
  output/      file, UDP
app/           macOS app (UI, plots, player)
tests/         unit tests, reference vectors
tools/         CLI decoder (iq file → ts), test-signal generator
docs/
```
A **headless CLI** (`dect2 --iq file.cs8 --out out.ts`) comes for free from this split and is very handy for testing.

---

## 7. Development plan

| Phase | Deliverable | How it's verified |
|---|---|---|
| 0 | Repo, build system, HackRF source, IQ record/playback, spectrum + waterfall UI shell | See live spectrum from your HackRF |
| 1 | P1 detection, GI detection, coarse/fine sync, FFT, constellation display of raw cells | P1 lamp green, CFO stable |
| 2 | Pilots, channel estimation, L1-pre/L1-post decode, Signalling tab | All L1 fields decoded match the broadcaster's known parameters |
| 3 | PLP extraction, de-interleavers, demapping, LDPC/BCH | BER drops to zero; FEC tab |
| 4 | BB-frame → TS, PSI/SI, service list, file + UDP output | `.ts` plays in VLC |
| 5 | Built-in live player (video/audio) | Watch TV in the app |
| 6 | Remaining views (frame map, impulse, history), polish, performance | Real-time on worst-case mux |
| 7 | MISO, T2-Lite, multi-PLP edge cases, robustness | Test vectors + recordings |

**Testing without the antenna:** a built-in **software test-signal generator** (offline, writes IQ files — never transmits) produces T2 signals with known parameters, plus channel impairments (noise, echoes, CFO). Combined with real recordings from your HackRF, this lets each stage be verified independently.

---

## 8. Risks

| Risk | Mitigation |
|---|---|
| 8-bit HackRF dynamic range limits 256-QAM | Warn only: ADC histogram, clipping indicator, and a clear "limited by receiver dynamic range" message when SNR is capped by the ADC |
| LDPC throughput at 50 Mbit/s | SIMD layered min-sum, multicore, early termination; Metal path if needed |
| HackRF DC spike / IQ imbalance | LO offset + digital shift; IQ imbalance estimation |
| Strong adjacent channels / overload | Channel filter, overload warnings, gain suggestions |
| HackRF Pro differences | Query capabilities at runtime; test on real hardware |

---

## 9. Decisions so far

- **UI:** Dear ImGui + Metal, styled after DecDRM / modem screenshots.
- **Gain:** manual LNA / VGA / amp, plus optional AGC and a gain-suggestion helper (ADC histogram, clipping indicator).
- **Extras in scope:** DVB subtitles and teletext.
- **Target signals:** Dubai first (thought to be 32K, 64-QAM, GI 1/8 — not confirmed, auto-detection will tell), then Lithuania.
- **Hardware host:** Apple Silicon Mac, macOS 27.
- **Primary test signal:** **522 MHz**, 8 MHz channel (Dubai). Real IQ recordings to follow once the HackRF Pro is connected.
- **8-bit limit:** the app warns; no external-hardware workflow.
- **Settings:** last device, frequency, gain and outputs are remembered; saved-frequency list.
- **Outputs:** full multiplex or a single service per output (no multi-service selection yet).
- **License:** open source, **GPL-3.0-or-later** (compatible with libhackrf BSD, Dear ImGui/ImPlot MIT, and Homebrew's GPL FFmpeg build).
- **Standard:** DVB-T2 only (T2-Base + T2-Lite). No DVB-T.

## 10. Open questions

- HackRF Pro specifics (sample-rate ceiling, ADC width, gain ranges) — confirm with `hackrf_info` when connected.

## 11. Requested features (not built yet)

Added 2026-10-02 at the user's request.

### 11.1 Antenna direction finder

An assistant that tells the user where to point the antenna, step by step:

1. The user points the antenna **north, east, south, west** in turn (the app says when to turn; each step it measures for a few seconds).
2. From those four readings it picks the best sector and asks for the **in-between directions** next (NE, SE, SW, NW), and then finer ones around the best one, until it can say "point it this way".
3. The measure is the decoder's own quality figure, not just signal level: data SNR / MER and the share of FEC blocks that decode, with ADC level as a fallback while there is no lock. Multipath and fades matter as much as strength, so the score also penalises block loss over the measuring window.
4. UI: a compass rose with the measured score per direction, the current instruction ("Turn to NE and hold still"), a progress bar for the measuring window, and the final recommendation. Remembered per frequency, so a saved location can be reused. It must work from recorded files for development (a recording is just one direction), so the scoring logic is unit-tested on synthetic per-direction data.

### 11.2 Constellation display like the reference UI

The constellation panels should look like the reference screenshot: clearly separated, tightly packed dots on the ideal QAM grid, not a fuzzy cloud. Direction of travel: plot the derotated symbols (done), with a density-based colour/brightness, small points, equal axes with the ideal grid drawn *above* the dots, symbols from correctly decoded blocks only, and optional averaging/persistence. Note that how tight the clusters look is limited by the real MER (about 15 dB on the 522 MHz signal); the reference picture comes from a much cleaner signal, so also consider showing decision-directed (decoded) symbols with their error vectors as an alternative view.

### 11.3 Native builds for every platform, and releases

The repository will become public. A release should offer a normal installer per platform:

| Platform | Artifact |
|---|---|
| macOS Apple Silicon (arm64) | `.dmg` |
| macOS Intel (x86_64) | `.dmg` (or one universal `.dmg`) |
| Windows (x64) | `.exe` installer (and a portable `.zip`) |
| Linux (x64, arm64) | `AppImage` plus `.deb`; a tarball for everything else |

Work this needs (the code today is macOS-only in a few places):

- **Build system:** CMake presets per platform; dependencies via vcpkg/Conan or system packages (libhackrf, FFmpeg, GLFW).
- **GPU/graphics:** the UI renders with Metal and the LDPC accelerator is a Metal kernel. Replace or add portable back ends: ImGui's OpenGL3/Vulkan back end for the UI, and a portable compute path (Vulkan compute or OpenCL, possibly via a thin abstraction) for LDPC. The CPU path always remains as the fallback, and the Auto mode stays.
- **SIMD:** the LDPC and DSP code uses ARM NEON and Apple's Accelerate/vDSP. Add SSE/AVX2 (x86) implementations, or portable fallbacks (e.g. PFFFT/KissFFT or FFTW instead of vDSP).
- **Audio and video:** CoreAudio and VideoToolbox become a cross-platform audio layer (miniaudio or SDL) and FFmpeg hardware decoding per platform (VideoToolbox on macOS, D3D11VA/DXVA on Windows, VAAPI on Linux).
- **Platform glue:** NSUserDefaults settings, file dialogs and QoS thread classes need equivalents (a settings file, native dialogs, thread priorities).
- **CI and release:** GitHub Actions matrix (macOS arm64 + Intel, Windows, Linux) that builds, runs the tests, packages (`create-dmg`, NSIS/WiX or Inno Setup, AppImage/`dpkg-deb`), signs/notarises where possible, and attaches the artifacts to a GitHub Release when a tag is pushed.
- **Docs:** per-platform install and HackRF driver notes (WinUSB/Zadig on Windows, udev rules on Linux).

### 11.4 Multipath / fading detector (first version built 2026-10-02)

The program tells the user when it thinks the signal suffers from multipath or fading, and why. Implemented in `core/src/channel.cpp` and shown as a "multipath: ..." badge in the status bar (hover for the reasons) and as a report at the top of the Channel tab. It combines three independent clues:

1. **Echoes** in the power-delay profile (level and delay relative to the strongest path, and whether an echo falls outside the guard interval, which causes inter-symbol interference).
2. **Frequency-selective fading**: dips in |H(f)|, counted and measured in depth.
3. **Time variation**: frame-to-frame SNR spread and how many recent frames lost a noticeable share of their FEC blocks.

The verdict is none / mild / likely / severe, with advice to re-aim the antenna for the worse levels. It is covered by `tests/test_channel.cpp` (clean channel reads "none", a synthetic echo is found at the right delay and level). On the 522 MHz recording it reports a likely multipath channel: echoes at -7 dB (+1.1 us) and weaker ones, several dips around 12 dB, and fades in about one frame in eight. Open points: thresholds need tuning against more real recordings, and the direction finder (11.1) should use this report as part of its per-direction score.

### 11.5 Signal-quality bar and channel selector (built 2026-10-02)

- **Signal-quality bar** on the Player tab: a red bar that fills green as reception improves. The value is the decoder's SNR margin over what the current modulation and code rate needs (0 dB margin = 25%, +6 dB or more = 100%), multiplied by the square of the share of FEC blocks that decoded over the last frames. Hover for the numbers. (`core/src/quality.cpp`; the required-SNR table is approximate AWGN values from EN 302 755 annex C.) On the 522 MHz recording it reads about 74% "good".
- **Channel selector** on the Player tab, above the video, next to the services list on the right: lists the DVB-T2 muxes the scanner has found (frequency, first service name and the number of further services, SNR). Picking one stops playback and tunes the HackRF there automatically (retune and flush; a restart if the bandwidth differs). The list is remembered between runs and merged from every scan.
