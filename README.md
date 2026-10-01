# DecT2

Open-source DVB-T2 receiver for macOS (HackRF One / HackRF Pro). See [PROJECT_BRIEF.md](PROJECT_BRIEF.md).

**Status: phase 0** — IQ sources (HackRF, recorded file, synthetic signal), spectrum, waterfall, status lamps, log.

## Build
```
brew install cmake glfw hackrf pkg-config
cmake -S . -B build && cmake --build build -j
build/dect2
```
Dev flags: `--autostart` (start the synthetic source), `--shot out.png` (save a screenshot after ~400 frames and quit).

License: GPL-3.0-or-later. Bundled: Dear ImGui and ImPlot (MIT).
