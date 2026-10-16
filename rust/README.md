# OnAir Rust workspace

Everything here builds with `cargo build --offline`: **zero external crates**, no rustup targets, no network. Homebrew cargo 1.98 is the
reference; `rust-version` is 1.70. Parsers of untrusted over-the-air bytes are the first candidates, then servers/clients, the engine
threading core, forward error correction and the Android USB drivers.

## Layout

```
rust/Cargo.toml        workspace (profiles live here: release = lto + panic=abort, dev = panic=abort)
rust/onair_common/     shared building blocks (CRC, bits, ...), no C ABI
rust/onair_<module>/   one crate per protocol/area: src/lib.rs (safe code), src/ffi.rs (the only place with unsafe), README.md
rust/onair/            the ONE static library libonair_rs.a: depends on every crate, owns the panic handler
core/include/dect2/rs/<module>.h   hand-written C header of a crate's ffi.rs
cmake/OnairRust.cmake  builds the workspace once (target onair_rs) and sets the per-module switches
tools/rust_check.sh    fmt + clippy -D warnings + tests (+ --sanitize)
```

Why one staticlib: several per-module staticlibs would each carry their own copy of Rust `core`/`compiler_builtins`, giving duplicate symbols at
link time. Member crates are `no_std` rlibs; `rust/onair` re-exports them and, with the `bare` feature (what CMake builds), is `no_std` too and
defines the panic handler (abort) and `rust_eh_personality`. Without `bare` (plain `cargo test`/clippy) it is ordinary std code.

## CMake switches

* `DECT2_RUST=AUTO|ON|OFF` (default AUTO): AUTO uses Rust when cargo, rustc >= 1.70 and the Rust target for this build exist, otherwise falls back to C++ with a status
  message. ON turns a missing toolchain into an error. OFF is a pure C++ build. Windows MinGW and Android cross builds fall back to C++ until their targets are installed.
* `DECT2_RUST_<MODULE>=AUTO|ON|OFF` (default AUTO, modules PROTO ADSB ACARS SAME PAGER AX25 DMR P25 NXDN DSTAR TETRA VDL2 HFDL MESH WISUN UAT TS DVBSI DAB DRM FILES NET ENGINE FEC USB):
  OFF keeps that module in C++ even when Rust is available (useful to bisect a difference).
* Results: `ONAIR_RUST_ACTIVE`, `ONAIR_RUST_<MODULE>`, `ONAIR_RUST_MODULES`, imported target `onair_rs`.
* `onair_rust_use(<target>)` adds `-DDECT2_RUST_<MODULE>` for every active module and links `onair_rs`. Call it on every target that compiles or links a converted C++ file.

## How to add a module (6 steps)

Worked example: the AIS pilot (`onair_proto`, `DECT2_RUST_PROTO`).

1. **Write the Rust** in safe modules of `rust/onair_<module>/src/` (AIS: `frame.rs`, `msg.rs`, `nmea.rs`, `mmsi.rs`). Take slices in, write into caller buffers, return `Option`/`Result`; no panics on any input.
2. **extern "C" in `ffi.rs`**: `#[no_mangle] pub unsafe extern "C" fn onair_<module>_<name>(ptr, len, ...)`, converting (pointer, length) to slices and calling the safe code (AIS: `onair_ais_get_u`, `onair_ais_hdlc_frames`, ...). Prefix every name `onair_<module>_` (proto also owns `onair_ais_`, `onair_nmea_`).
   Bump `onair_<module>_version` when a signature changes.
3. **Header** `core/include/dect2/rs/<module>.h`, by hand, every function documented (AIS: `rs/proto.h`). `rust/onair/tests/headers.rs` fails when the header and `ffi.rs` list different functions or break the prefix rule.
4. **CMake opt-in**: nothing to add for linking (`onair_rust_use`); make sure your C++ target calls it. `DECT2_RUST_<MODULE>` is defined automatically when the module is active.
5. **#ifdef in C++**: wrap the replaced C++ in `#ifndef DECT2_RUST_<MODULE>` and put the thin Rust-backed wrapper in a separate `.cpp` guarded by `#ifdef DECT2_RUST_<MODULE>`
   (AIS: `core/src/ais/ais_proto.cpp` keeps the C++ behind `#ifndef DECT2_RUST_PROTO`, `core/src/ais/ais_proto_rs.cpp` implements the same functions through `rs/proto.h` and checks struct layouts at start-up).
   Any target that compiles those files directly (tests, fuzzers) needs `COMPILE_DEFINITIONS DECT2_RUST_<MODULE>` and `onair_rs`.
6. **Differential + fuzz test**: keep a frozen copy of the C++ as reference (AIS: `cmake/gen_ais_ref.cmake` generates `dect2::ais_ref`; `tests/ais/test_ais_proto_rs.cpp` compares both on real and mutated input, `fuzz_ais_proto` runs mutations under the sanitizers). Register them in `CMakeLists.txt` under `if(ONAIR_RUST_<MODULE>)`.
   Add Rust unit tests with known vectors in the crate too.

Then update the status table below and run `tools/rust_check.sh`.

## Conventions

* No `unsafe` outside `ffi.rs` (`#![deny(unsafe_code)]` in each crate, allowed only on `mod ffi`; `#[forbid(unsafe_code)]` on modules where it is true today).
* Every public function has a doc comment; protocol rules are commented with the spec reference (e.g. ITU-R M.1371-5 section 3.3.7, ETSI TS 102 361-1 section 9.1).
* `no_std`, no allocation in protocol crates unless the module really needs it (then say why in its README). No external crates, no `std::process`, no panics reachable from the C ABI (the profile aborts).
* Reads past the end give 0 / writes past the end fail; never index unchecked. Return the written length or `usize::MAX` for "buffer too small" over the C ABI.
* Shared code goes into `onair_common` with a unit test and a known vector.
* Formatting: `rust/rustfmt.toml` (140 columns). `tools/rust_check.sh` must pass before a commit.

## Status

| Module | Crate | CMake switch | Header | Holds | Status |
|---|---|---|---|---|---|
| common | `onair_common` | - | - | shared CRC/bits/endian/hex/parity/gray/NRZI/Manchester/ring | done (no C ABI) |
| proto | `onair_proto` | `DECT2_RUST_PROTO` | `rs/proto.h` | AIS frame layer (HDLC, NMEA, messages, MMSI) | done |
| adsb | `onair_adsb` | `DECT2_RUST_ADSB` | `rs/adsb.h` | ADS-B / Mode S | not started |
| acars | `onair_acars` | `DECT2_RUST_ACARS` | `rs/acars.h` | ACARS | not started |
| same | `onair_same` | `DECT2_RUST_SAME` | `rs/same.h` | SAME / EAS | not started |
| pager | `onair_pager` | `DECT2_RUST_PAGER` | `rs/pager.h` | POCSAG / FLEX | not started |
| ax25 | `onair_ax25` | `DECT2_RUST_AX25` | `rs/ax25.h` | AX.25 / APRS | not started |
| dmr | `onair_dmr` | `DECT2_RUST_DMR` | `rs/dmr.h` | DMR | not started |
| p25 | `onair_p25` | `DECT2_RUST_P25` | `rs/p25.h` | P25 | not started |
| nxdn | `onair_nxdn` | `DECT2_RUST_NXDN` | `rs/nxdn.h` | NXDN | not started |
| dstar | `onair_dstar` | `DECT2_RUST_DSTAR` | `rs/dstar.h` | D-STAR | not started |
| tetra | `onair_tetra` | `DECT2_RUST_TETRA` | `rs/tetra.h` | TETRA | not started |
| vdl2 | `onair_vdl2` | `DECT2_RUST_VDL2` | `rs/vdl2.h` | VDL2 | not started |
| hfdl | `onair_hfdl` | `DECT2_RUST_HFDL` | `rs/hfdl.h` | HFDL | not started |
| mesh | `onair_mesh` | `DECT2_RUST_MESH` | `rs/mesh.h` | LoRa / mesh / LoRaWAN | not started |
| wisun | `onair_wisun` | `DECT2_RUST_WISUN` | `rs/wisun.h` | Wi-SUN | not started |
| uat | `onair_uat` | `DECT2_RUST_UAT` | `rs/uat.h` | UAT 978 | not started |
| ts | `onair_ts` | `DECT2_RUST_TS` | `rs/ts.h` | MPEG-TS, PSI, EIT | not started |
| dvbsi | `onair_dvbsi` | `DECT2_RUST_DVBSI` | `rs/dvbsi.h` | DVB service info, teletext, subtitles | not started |
| dab | `onair_dab` | `DECT2_RUST_DAB` | `rs/dab.h` | DAB FIG, MOT | not started |
| drm | `onair_drm` | `DECT2_RUST_DRM` | `rs/drm.h` | DRM data services | not started |
| files | `onair_files` | `DECT2_RUST_FILES` | `rs/files.h` | SigMF/IQ headers, MBTiles, TLE, CSV, results | not started |
| net | `onair_net` | `DECT2_RUST_NET` | `rs/net.h` | remote API, share server, SpyServer, rtl_tcp, map upload, tuner | not started |
| engine | `onair_engine` | `DECT2_RUST_ENGINE` | `rs/engine.h` | ring buffer, threading core | not started |
| fec | `onair_fec` | `DECT2_RUST_FEC` | `rs/fec.h` | LDPC, BCH, Reed-Solomon, Viterbi | not started |
| usb | `onair_usb` | `DECT2_RUST_USB` | `rs/usb.h` | Android USB SDR drivers | not started |
