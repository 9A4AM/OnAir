# Porting audit: what ties OnAir to macOS

Date: 2026-10-02. Scope: everything in `core/`, `app/`, `tools/`, `tests/`, `CMakeLists.txt` (third_party only where noted).
Goal (from the project brief 11.3): macOS arm64 + Intel, Windows x64, Linux x64/arm64, each with a normal installer. The CPU path
stays the baseline everywhere; GPU decoding is an optional extra.

## 1. Verdict

The code is in better shape than expected. All the decoding algorithms (DVB-T2, DVB-T, ATSC, TS parsing, EPG, teletext, scanner,
quality/multipath/direction/bandwidth logic) are plain C++20 with `std::thread`/`std::mutex`. Apple-specific code is concentrated in
a few well-defined places, and the three big hardware/OS boundaries already sit behind small interfaces (`AudioOut`, `GpuLdpc`,
`Player`'s NV12 output). What blocks a Linux/Windows build today:

1. **Apple's Accelerate framework** is used for FFTs, convolution, resampling and (ATSC) BLAS in 9 files, **without fallbacks** except
   in `atsc_rx.cpp`. `fftutil.h` and `t2ofdm.h` even leak `Accelerate.h` into public headers. This alone stops `dect2core` building elsewhere.
2. **The build system** hard-codes `OBJCXX`, Apple frameworks and pkg-config for everything.
3. **The GUI** is one 2,900-line Objective-C++ file mixing portable ImGui code with Metal, Cocoa dialogs, NSUserDefaults and
   a Metal video shader. Only about 100 lines are really Apple-specific, but they are spread through the file.
4. A handful of small POSIX/GCC-isms (sockets, thread QoS, `__builtin_*`, `M_PI`, iconv).

Nothing here needs an algorithm change. Estimated effort: about 3-4 weeks of focused work for the full set (core on Linux first: a few
days). The SIMD kernels for x86 are the only part that needs careful measurement.

## 2. Inventory

Effort: S = hours, M = a day or two, L = several days. Risk = chance of surprises.

### 2.1 Core library (`dect2core`)

| Area | Files | Apple API used | Portable replacement | Effort / risk |
|---|---|---|---|---|
| FFT | `fftutil.h` (users: `dvbt_gen.cpp`, `dvbt_rx.cpp`); own `FFTSetup` code in `t2rx.cpp`, `t2ofdm.cpp`, `t2gen.cpp`, `spectrum.cpp` | `vDSP_create_fftsetup`, `vDSP_fft_zip`, `vDSP_ctoz/ztoc`, split-complex layout | One `Fft` class in a portable header with a vendored FFT (PFFFT, BSD-like, has NEON/SSE/AVX; or KissFFT) and the Accelerate version kept under `__APPLE__`. All current uses are power-of-two complex FFTs (sizes 1K-32K, plus 4096/8192 for spectrum) | M / low |
| Convolution / filters | `t2ofdm.cpp` (5 calls), `t2ofdm.h`, `atsc_rx.cpp` | `vDSP_conv` | Short FIR loops (auto-vectorised), optional SIMD | S / low |
| Resampler | `resampler.cpp` | `vDSP_desamp` (polyphase decimation) | Plain loop / SIMD dot products | S / low |
| Window | `spectrum.cpp` | `vDSP_hann_window` | 3 lines | S |
| BLAS (ATSC equaliser fit) | `atsc_rx.cpp` (has `#else` scalar paths) | `cblas_ssyrk`, `cblas_sgemv` | The scalar fallback exists; add a blocked/vectorised version or link OpenBLAS/Eigen if found (n = 194, 5,000 rows: about 190 MMAC per field) | S-M / medium (speed) |
| Synthetic source | `source.cpp` | includes `Accelerate.h` only | remove include if unused (verify) | S |
| LDPC CPU decoder | `ldpc.cpp` | ARM NEON (`#if __ARM_NEON` with scalar `#else`) | scalar path exists; **add SSE4.1/AVX2** versions of the check-node update for acceptable x86 speed | L / medium: it is the hottest code |
| DVB-T Viterbi | `dvbt.cpp` | NEON int16 butterflies (`#if` with scalar `#else`) | add SSE/AVX2 (`_mm_adds_epi16`, `_mm_min_epi16`) | M / medium |
| ATSC PFB/dot products | `atsc_rx.cpp` | NEON (`#if`, scalar `#else`) | add SSE/AVX; compilers auto-vectorise the scalar loops reasonably | S |
| Metal LDPC | `gpu_ldpc.mm`, `gpu_ldpc.h` (`GpuLdpc`) | Metal compute | Interface already returns `available() == false` when absent and the engine's Auto mode falls back to CPU. Non-Apple build: a stub implementation. Later: Vulkan compute / OpenCL | S (stub) / L (real GPU path, optional) |
| Audio output | `audioout.mm`, `audioout.h` (`AudioOut`) | CoreAudio / AudioToolbox (AudioQueue) | miniaudio (public domain, one header) behind the same class; keep the lock-free ring and frame counters | M / low |
| Video decode | `player.cpp` | `AV_HWDEVICE_TYPE_VIDEOTOOLBOX`, `AV_PIX_FMT_VIDEOTOOLBOX`; software path already outputs NV12 | Select per platform: D3D11VA (Windows), VAAPI (Linux), VideoToolbox (macOS), always fall back to software. Output stays NV12 | M / medium (FFmpeg hw-frame details) |
| Thread priority | `engine.cpp:327`, `player.cpp:179`, `t2plp.cpp:21,224` | `pthread_set_qos_class_self_np` | Small `setThreadPriority(kind)` wrapper: macOS QoS, Windows `SetThreadPriority`, Linux nice/`sched_setscheduler` (best effort) | S |
| UDP output | `tsout.cpp` | BSD sockets (`socket`, `sendto`, `inet_pton`, `close`) | Works on Linux as is; on Windows add Winsock (`WSAStartup`, `closesocket`, `ws2_32`) behind a tiny wrapper | S |
| Text conversion | `ts.cpp` (5 uses) | `iconv` (DVB text tables) | Own tables for ISO 8859-x/6937/UTF-16 (small), or vcpkg libiconv on Windows | M / low |
| GCC builtins | `dvbt.cpp` (`__builtin_parity`), `teletext.cpp` (`__builtin_popcount`), 6 uses | GCC/Clang only | `std::popcount` (C++20 `<bit>`) | S |
| `M_PI` | 10 files | not defined by MSVC without `_USE_MATH_DEFINES` | add the define in CMake or use a constant | S |
| HackRF | `source.cpp` | libhackrf (portable) | vcpkg/apt/Homebrew package; Windows needs the WinUSB driver (Zadig), Linux needs udev rules; documentation only | S |
| FFmpeg | `player.cpp`, `audio` | libav* (portable) | vcpkg/apt | S |

### 2.2 Application (`app/main.mm`, 2,933 lines)

| Piece | Apple API | Replacement | Effort / risk |
|---|---|---|---|
| Render backend | `imgui_impl_metal.mm`, `MTLDevice`, `CAMetalLayer`, `glfwGetCocoaWindow` | `imgui_impl_opengl3` + GLFW (needs the backend files added to `third_party/imgui`, which currently has only the GLFW and Metal ones, plus a GL loader such as glad) | M |
| Video texture | `VideoTex`: Metal compute pipeline converting NV12 -> RGBA with an adaptive comb deinterlace (embedded MSL shader `kYuvShader`) | GLSL fragment shader doing the same conversion and deinterlace from two textures (Y, UV); upload with `glTexSubImage2D` | M-L / medium (shader port, performance of uploads) |
| File dialogs | `NSOpenPanel`, `NSSavePanel` | portable-file-dialogs (header-only) or tinyfiledialogs | S |
| Settings | `NSUserDefaults` (about 20 keys, including the channel list) | one JSON/INI file in the OS config directory (`~/Library/Application Support/OnAir`, `%APPDATA%\OnAir`, `~/.config/onair`), with one-time migration on macOS | S-M |
| Screenshots (dev flag `--shot`) | Metal readback + `NSBitmapImageRep` PNG | `glReadPixels` + stb_image_write (used only for development) | S |
| Window/DPI/fullscreen | GLFW (portable) | check high-DPI handling on Windows/Linux | S |
| Structure | one file | split into per-tab portable `.cpp` files and a `platform_*` layer (dialogs, settings, GPU video, window) | M (also listed in TASKS block E) |

Only about 41 lines mention Metal and 18 mention Cocoa/NS types, so the split is mostly mechanical.

### 2.3 Tests and tools
- `tests/` and `tools/` only link `dect2core`, so they build wherever the core does. Exception: `test_gpu_ldpc.cpp` (Metal): compile only on Apple.
- `tests/reference/cmp_atsc.cc` is a manual tool, not part of the build.
- The 60 s recording (`recordings/`, git-ignored) is needed only by manual runs, not by the test suite (tests generate their own signals).
  CI needs the 10 s excerpt as a release asset or a generated signal.

### 2.4 Build system (`CMakeLists.txt`)
- `project(... OBJCXX)` and `CMAKE_OBJCXX_*`: must be conditional on `APPLE`.
- `-framework Accelerate/AudioToolbox/CoreAudio/CoreFoundation/Metal/VideoToolbox/CoreMedia/CoreVideo/MetalKit/QuartzCore/Cocoa`: guard.
- `pkg_check_modules` for HackRF/GLFW/FFmpeg: fine on Linux and macOS; on Windows use vcpkg (`find_package`, config packages).
- `-O3`: use generator-neutral flags (`/O2` on MSVC). Add `-march` handling per architecture (only on the SIMD kernels, via function multiversioning or per-file flags: do not compile everything with `-mavx2`).

## 3. What already works across platforms
All DSP/decoder source, tests and tools; GLFW, ImGui and ImPlot core; libhackrf; FFmpeg; `std::thread` use; the engine/source/receiver interfaces; the `AudioOut`/`GpuLdpc`/`Player` boundaries.

## 4. Recommended order (each step leaves the macOS build working)

1. **CMake restructure** (S): options `DECT2_BUILD_APP`, `DECT2_ENABLE_METAL`, conditional languages and frameworks, presets for macOS/Linux/Windows. Result: `dect2core` + tests + tools build with no Objective-C.
2. **Portable DSP layer** (M): `fft.h` (+ vendored PFFFT or KissFFT), `fir.h` (conv/desamp/window), `blas_lite.h`; replace all Accelerate uses; keep Accelerate under `#ifdef __APPLE__` where it is faster. Gate: all existing tests give the same results (compare error vector magnitudes, bit-exact for the integer/FEC parts).
3. **Small portability fixes** (S): builtins, `M_PI`, thread-priority wrapper, socket wrapper, iconv replacement, `-Wall` clean on GCC and clang.
4. **First Linux CI job** (S): build core + tests on Ubuntu x64 and arm64; run `test_*` and `fuzz_rx`. This is the cheapest way to find the next problems.
5. **Stubs and portable backends** (M): `GpuLdpc` stub, `AudioOut` on miniaudio, `Player` hardware-decode selection.
6. **x86 SIMD kernels** (L): LDPC check-node update, DVB-T Viterbi, ATSC dot products; measure real-time factors on a mid-range x86 laptop against the 64K-LDPC worst case (the repo has `tests/bench_fec.cpp`). If the CPU path cannot keep up, plan the Vulkan/OpenCL LDPC path (optional, after release).
7. **App port** (M-L): split `main.mm`; `platform_*` layer; OpenGL3 backend and GLSL video shader; settings file; dialogs.
8. **Windows build** (M): vcpkg manifest, MSVC fixes (found by the Linux/clang warnings first), WinUSB notes.
9. **Packaging and release** (M): `.dmg` (arm64/x86_64/universal), NSIS or Inno Setup installer + zip, AppImage + `.deb`, code signing/notarisation where certificates exist, GitHub Actions matrix, per-platform install docs.

## 5. Risks and open questions
- **LDPC speed on x86** is the main technical risk (DVB-T2 64-QAM 2/3 with 64K frames is the heaviest case). The layered min-sum kernel is straightforward to vectorise with AVX2 (8 floats) but needs tuning; AVX-512 is not needed.
- **FFmpeg hardware decoding** differs per platform; software decode of 1080i H.264 is about one core, acceptable as a fallback.
- **Video upload cost** with OpenGL: NV12 planes of 1080p are 3 MB per frame; fine with `glTexSubImage2D` (PBOs if needed).
- **Windows USB**: HackRF needs the WinUSB driver; document Zadig. Not testable without a Windows machine and the device.
- **Licensing**: PFFFT (BSD-like), KissFFT (BSD), miniaudio (public domain/MIT-0), portable-file-dialogs (WTFPL), stb (public domain) are all compatible with GPL-3.0-or-later.
- **Testing without hardware** is already possible everywhere (synthetic sources, generated signals); real HackRF checks per OS remain manual.
