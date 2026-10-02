# OnAir task list

Order matters: each block depends on the ones above it. `[ ]` open, `[x]` done. Items marked **(needs signal)** cannot be finished without a real capture or a reference vector.

## A. Cross-platform builds and releases (brief 11.3)

Goal: normal installers for macOS arm64, macOS Intel, Windows x64, Linux x64/arm64. The CPU path stays the baseline everywhere; GPU acceleration is an optional extra per platform.

**A1. Find what is macOS-only**
- [ ] List every use of Metal, Accelerate/vDSP, NEON intrinsics, CoreAudio, VideoToolbox, Cocoa (NSUserDefaults, NSOpenPanel), `pthread_set_qos_class_self_np`, `.mm` files. Write the list into this file.

**A2. Build system and dependencies**
- [ ] CMake presets for macOS, Windows (MSVC) and Linux; `vcpkg.json` manifest (libhackrf, FFmpeg, GLFW, libusb).
- [ ] Build options: `DECT2_GPU_METAL`, `DECT2_GPU_PORTABLE`, `DECT2_UI_BACKEND` (Metal / OpenGL3); the Metal parts compile only on macOS.
- [ ] Make `dect2core` (the decoder, with no UI and no GPU) build and pass its tests with a plain C++17 compiler on Linux first. This is the fastest way to find problems.

**A3. Portable core (no UI yet)**
- [ ] FFT: replace vDSP with a portable library (PFFFT or KissFFT) behind the existing `fftutil.h`; keep vDSP on macOS if it is faster.
- [ ] SIMD: scalar reference versions of the NEON kernels (Viterbi butterflies, LDPC check nodes, resampler), then SSE4.1/AVX2 versions. Tests compare scalar against SIMD bit for bit.
- [ ] Thread priority and QoS wrapper (macOS QoS, Windows `SetThreadPriority`, Linux `sched`/nice).
- [ ] Settings in a JSON file under the user's config directory instead of NSUserDefaults; migrate the existing macOS values.
- [ ] HackRF source: check libhackrf on Windows/Linux (device open, callbacks, USB errors); udev rules and WinUSB notes.
- [ ] Core tests, the fuzz test and the CLI tools run on Linux and Windows.

**A4. Portable app**
- [ ] ImGui OpenGL3 + GLFW back end next to the Metal one (choose at build time).
- [ ] Video: NV12/YUV to RGB in an OpenGL shader (today a Metal compute kernel); deinterlace filter ported to the same shader.
- [ ] Audio: miniaudio (or SDL) replaces CoreAudio.
- [ ] FFmpeg hardware decoding per platform (VideoToolbox, D3D11VA, VAAPI) with software fallback.
- [ ] File dialogs (native or a small portable one), fullscreen, high-DPI scaling.
- [ ] Portable GPU LDPC (Vulkan compute or OpenCL) behind the existing `Auto/CPU/GPU` switch; skip it at first if the CPU path keeps up (check CPU load on a mid-range x86 laptop).

**A5. CI, packaging, release**
- [ ] GitHub Actions matrix: macOS arm64, macOS x86_64, Windows, Linux x64 and arm64; build, run tests, upload artifacts.
- [ ] Packaging: `.dmg` (create-dmg), Windows installer (Inno Setup or NSIS) plus portable `.zip`, AppImage and `.deb`.
- [ ] Code signing and notarisation where certificates exist; otherwise document the Gatekeeper/SmartScreen warnings.
- [ ] Release workflow on tag: attach artifacts, release notes; put the 60 s test recording in a release asset for the tests.
- [ ] Docs: install guide per platform, driver notes, README badges.

## B. Constellation polish (brief 11.2)

- [x] Decision-directed data: the decoder now returns the transmitted point label for every displayed cell of a correctly decoded block.
- [x] Density view (heat map with persistence): shows the 64 / 256 clusters as blobs on the ideal grid.
- [x] Cluster view: one ring per transmitted point (centre = average received position, radius = 1 sigma), coloured relative to the average; tooltip with point bits, sigma, offset and EVM.
- [ ] Per-carrier and per-symbol MER plots from the same data (shows which carriers or symbols are bad).
- [ ] Optional cleaner display: 2x2 / 4x4 zoom on one quadrant of the constellation.
- [ ] DVB-T: the same views for its data cells (needs decoded-block labels from the DVB-T chain).
- [ ] Update the screenshots in the README.

## C. Decoder completeness

- [ ] Merge the common PLP into the data PLP's transport stream (multi-PLP muxes).
- [ ] DVB-T hierarchical modulation: decode the low-priority stream too (stream selector).
- [ ] Time interleaving type 1 (used in the UK and others) **(needs signal or reference vector)**.
- [ ] Sub-slicing and type-2 PLPs **(needs signal)**.
- [ ] MISO (Alamouti, two channel estimates) **(needs signal)**.
- [ ] FEF frames and GSE: pass through or report in more detail.
- [ ] T2-Lite on a real signal **(needs signal)**.

## C2. ATSC 1.0 support (8VSB, 6 MHz)

Details and the full status are in `docs/ATSC_HANDOFF.md`.
- [x] Channel coding and decoder, bit-exact against `gr-dtv` (`test_atsc_fec`, `tests/reference/cmp_atsc.cc`).
- [x] Test signal generator and receiver (resampler/matched filter, pilot loop, symbol clock, field sync, per-field equaliser), worker thread, about 0.5x real time on the sample thread.
- [x] Engine integration, DVB/ATSC switch, lamps/readouts/panels, quality meter row, synthetic ATSC source, `dect2cli --synthetic --atsc`.
- [x] Scanner support and US presets (compiles; not exercised on hardware).
- [x] Tests: clean, carrier/clock offsets, SNR 16.5 dB, moderate echoes, noise only, dropout recovery.
- [x] Decision feedback equaliser (trained on decoded symbols): -6 dB at 5 us, -3 dB at 5 us, -1.5 dB at 8 us decode. Limit: a 0 dB echo cannot be acquired.
- [x] PSIP channel table (names, numbers) and AC-3/E-AC-3 stream recognition (the player uses FFmpeg, which decodes AC-3; not tried on a real stream).
- [ ] PSIP guide (EIT/ETT).
- [ ] Real-signal validation **(needs a capture from an ATSC country)**; scan on hardware.
- [ ] Fuzz/ASan runs, README and brief updates.

## D. Real-world validation (when the HackRF is available)

- [ ] Direction finder with a real antenna: tune the 14 s window, score weights and the flat-signal rules.
- [ ] DVB-T on a live signal: check acquisition, TPS and the Auto standard switch.
- [ ] Teletext on real data; EPG on a mux that sends EIT.
- [ ] Multipath detector thresholds against more recordings; use its report in the quality score.
- [ ] Record a small library of captures (different muxes, weak signal, strong signal, DVB-T) for regression tests.

## E. Quality and maintenance

- [ ] Run the sanitizer builds and the fuzz tests in CI.
- [ ] Performance regression test (real-time factor on the 522 MHz recording) in CI.
- [ ] Split `app/main.mm` (about 2,700 lines) into per-tab files while porting the UI.
- [ ] Update PROJECT_BRIEF.md and README when each block is finished.
