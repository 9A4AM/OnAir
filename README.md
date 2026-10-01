# DecT2

Open-source DVB-T2 receiver for macOS (HackRF One / HackRF Pro). See [PROJECT_BRIEF.md](PROJECT_BRIEF.md).

**Status: phase 1** — P1 detection and S1/S2 decoding, guard-interval detection, CFO/timing tracking, raw OFDM cells; synthetic DVB-T2-style signal generator with impairments. Tests: `build/test_sync`.

## Build
```
brew install cmake glfw hackrf pkg-config
cmake -S . -B build && cmake --build build -j
build/dect2
```
Dev flags: `--autostart` (start the synthetic source), `--shot out.png` (save a screenshot after ~400 frames and quit).

License: GPL-3.0-or-later. Bundled: Dear ImGui and ImPlot (MIT).
