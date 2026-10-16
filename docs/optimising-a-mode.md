# Optimising a mode

The rule: **no behaviour change.** Tests must pass, and the change comes with before/after numbers from `bench_modes`.

## Measure

`bench_modes` (tests/bench/bench_modes.cpp) pushes every mode's built-in test signal through the real receiver path (Engine, file source, no sound, no
real-time pacing) and reports **CPU seconds per second of signal** ("cost"), the real-time factor (1/cost: how many such streams one core could carry),
peak RSS, threads, the share of the busiest thread, and a count of decoded items as a sanity check. It never uses wall-clock speed: the machine may be busy.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target bench_modes
build/bench_modes --list                                     # ids and rates
build/bench_modes --mode dvbs                                # one mode, 10 s of signal, default rate and a second one
build/bench_modes --mode dvbs,dtmb --seconds 20 --rates default --out /tmp/before   # also writes /tmp/before.json and .md
build/bench_modes --rates 2,5 --mode wb                      # explicit rates in Msps
```

Run it twice and compare: CPU time is stable to a few percent even under load. Use `--seconds 20` or more for a cleaner steady-state figure (the
first quarter of the run is excluded from `cost`; plan creation and warm-up show as `startup`). A row marked `ok (nothing decoded)` means the mode
printed nothing for its own test signal: check that before trusting its cost. Each mode runs in a child process: a crash or a CPU-time overrun
(`--timeout`, default 240 CPU s) marks that row only. The full table of 0.2.7 is in docs/benchmark-modes-0.2.7.md.

## Profile

macOS (no root needed for your own process):

```
build/bench_modes --mode dvbs --seconds 60 --rates default &       # long enough to sample
sample $(pgrep -f 'bench_modes --run dvbs') 8 -file /tmp/dvbs.txt  # the child doing the receiving is "--run <id>"
```

The "Sort by top of stack" block at the end of the file lists the self-time per function. Instruments works too: `xctrace record --template 'Time Profiler'
--launch -- build/bench_modes --run dvbs 2000000 30 /tmp/sig.cs8` (generate the file first with `--gen <id> <rate> <secs> <file>`).

Linux:

```
perf record -g -- build/bench_modes --mode dvbs --seconds 30 --rates default
perf report --no-children --sort symbol | head -40        # self time; add --children for inclusive
```

Build with `-DCMAKE_BUILD_TYPE=RelWithDebInfo` if the symbols come out as addresses. Look at the self-time list first, then at who calls the top entry.

## Checklist

- No allocation and no lock in the feed path (the function called per block of samples): `reserve()` once, keep scratch buffers as members, no `std::vector` returned per block, no `std::string` building.
- Reuse FFT plans and buffers: create them in `configure()`, never per symbol or per block.
- Decimate early: filter and decimate to the channel's rate first (polyphase FIR or CIC plus a short clean-up filter), then do the expensive work at the low rate.
- Use the helpers of dsp compat (core/src/dsp/dsp_compat.cpp: vDSP on macOS, NEON on ARM, AVX on x86) instead of scalar loops for mixing, dot products, magnitude and similar.
- Keep arrays contiguous (struct of arrays for hot data, no `std::list` or `std::map` in an inner loop); avoid `std::complex` operator overloads that do NaN checks (build with the existing fast-math flags or use the real/imag forms).
- No `std::function` or virtual call per sample in an inner loop: template the loop or call per block.
- Do not recompute tables (sine/cosine, windows, interleaver and LDPC tables, CRC tables): build once, `static const` or a member.
- Replace `sin`/`cos`/`atan2`/`sqrt` per sample by a rotating phasor, a lookup table or an approximation, but only where the test tolerance allows and the decoded output stays the same.
- Skip work that nothing uses: a spectrum or a detector that runs while the mode is locked and idle, a decode attempt without a candidate.
- Move independent stages to their own thread only when the profile shows the feed thread is the bottleneck (the `busiest thread` column near 100% of one core) and the stages really are independent; more threads do not reduce the CPU used, only the latency.

## Before you hand in

1. `bench_modes --mode <id> --seconds 20 --out /tmp/before` on the unchanged code, `--out /tmp/after` on yours; paste both rows (cost, RT factor, RSS) in the commit message.
2. The mode's own tests (`ctest -R <mode>`) and `ctest -R bench_modes_smoke` pass; the decoded output (items, bytes) is the same.
3. Check the other modes that share the code you touched (driver and DSP helpers are shared: `docs/benchmark-modes-0.2.7.md` lists everything).
