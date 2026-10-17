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
| adsb | `onair_adsb` | `DECT2_RUST_ADSB` | `rs/adsb.h` | ADS-B / Mode S message layer (CRC-24, altitude/identity codes, CPR, Comm-B inference, field extraction, geometry) | done (generator, tracker and register in C++) |
| acars | `onair_acars` | `DECT2_RUST_ACARS` | `rs/acars.h` | ACARS block layer (CRC-16, parity repair, block parsing, label names, OOOI / media advisory text) | done (frame builder in C++) |
| same | `onair_same` | `DECT2_RUST_SAME` | `rs/same.h` | SAME / EAS header parser, tables, location text, time text, county CSV reader | done (county table state in C++) |
| pager | `onair_pager` | `DECT2_RUST_PAGER` | `rs/pager.h` | POCSAG / FLEX code word (BCH, repair), word classification, text readers and looks-like-text heuristic, FLEX speed table, FIW, phase decoding | done (generators, POCSAG message assembler and receiver in C++) |
| ax25 | `onair_ax25` | `DECT2_RUST_AX25` | `rs/ax25.h` | AX.25 frame parser, FCS, address / path / TNC-2 text, HDLC receiver, APRS parser (positions, Mic-E, objects, items, messages, weather, telemetry...) | done (frame builder, HDLC / NRZI / G3RUH transmit helpers and receiver in C++) |
| dmr | `onair_dmr` | `DECT2_RUST_DMR` | `rs/dmr.h` | DMR frame layer: Golay / QR / Hamming / RS (12,9), CRCs, BPTC (196,96), embedded LC, short LC, CACH, trellis 3/4, rate 1, sync patterns, bursts, slot type / EMB, link control, CSBK, data headers and blocks, text formats, talker alias | done (link / receiver, generator, bit helpers and alias PDU builder in C++) |
| p25 | `onair_p25` | `DECT2_RUST_P25` | `rs/p25.h` | P25 Phase 1 frame layer: NID (BCH), Reed-Solomon GF(64), Hamming / Golay, CRC-CCITT, trellis 1/2, TSBK, link control, encryption sync, header word, IMBE interleave | done (receiver, generator and vocoder glue in C++) |
| nxdn | `onair_nxdn` | `DECT2_RUST_NXDN` | `rs/nxdn.h` | NXDN frame layer: scrambler, LICH, CRC-6/12/16, SACCH / FACCH1 / CAC coding (convolutional code, interleaver, soft Viterbi), layer 3 messages, AMBE+2 bit order | done (receiver, generator and vocoder glue in C++) |
| dstar | `onair_dstar` | `DECT2_RUST_DSTAR` | `rs/dstar.h` | D-STAR DV frame layer: CRC, radio header, flag text, header FEC (convolutional code, interleaver, scrambler, soft Viterbi), AMBE bit order, NMEA / D-PRS slow data GPS text | done (receiver, generator, sync tables and vocoder glue in C++) |
| tetra | `onair_tetra` | `DECT2_RUST_TETRA` | `rs/tetra.h` | TETRA | not started |
| vdl2 | `onair_vdl2` | `DECT2_RUST_VDL2` | `rs/vdl2.h` | VDL2 | not started |
| hfdl | `onair_hfdl` | `DECT2_RUST_HFDL` | `rs/hfdl.h` | HFDL | not started |
| mesh | `onair_mesh` | `DECT2_RUST_MESH` | `rs/mesh.h` | LoRa mesh packet layers: protobuf reader, Meshtastic header / Data / port payloads, MeshCore header and key-free payloads, LoRaWAN MAC frames (MHDR, FHDR, MAC commands, join / rejoin), LoRa APRS and MeshCom frames, name and key tables | done (AES / HMAC / Ed25519 and channel tables, LoRa PHY, builders and settings tables in C++) |
| wisun | `onair_wisun` | `DECT2_RUST_WISUN` | `rs/wisun.h` | Wi-SUN | not started |
| uat | `onair_uat` | `DECT2_RUST_UAT` | `rs/uat.h` | UAT 978 message layer: downlink (header, state vector, mode status, aux SV), uplink frame, information frames, FIS-B APDU headers | done (FEC, demodulator, tracker, display text and UI mapping in C / C++) |
| ts | `onair_ts` | `DECT2_RUST_TS` | `rs/ts.h` | MPEG-TS, PSI, EIT | not started |
| dvbsi | `onair_dvbsi` | `DECT2_RUST_DVBSI` | `rs/dvbsi.h` | DVB service info, teletext, subtitles | not started |
| dab | `onair_dab` | `DECT2_RUST_DAB` | `rs/dab.h` | DAB FIG, MOT | not started |
| drm | `onair_drm` | `DECT2_RUST_DRM` | `rs/drm.h` | DRM data services | not started |
| files | `onair_files` | `DECT2_RUST_FILES` | `rs/files.h` | SigMF/IQ headers, MBTiles, TLE, CSV, results | not started |
| net | `onair_net` | `DECT2_RUST_NET` | `rs/net.h` | remote API, share server, SpyServer, rtl_tcp, map upload, tuner | not started |
| engine | `onair_engine` | `DECT2_RUST_ENGINE` | `rs/engine.h` | ring buffer, threading core | not started |
| fec | `onair_fec` | `DECT2_RUST_FEC` | `rs/fec.h` | LDPC, BCH, Reed-Solomon, Viterbi | not started |
| usb | `onair_usb` | `DECT2_RUST_USB` | `rs/usb.h` | Android USB SDR drivers | not started |

## The C++ originals are still there

For every converted module the replaced C++ stays in its file under `#ifndef DECT2_RUST_<MODULE>` (AIS: `ais_proto.cpp`; ADS-B: `adsb_msg.cpp`; ACARS:
`acars_proto.cpp`; SAME: `same_proto.cpp`; pager: `pager_proto.cpp`; AX.25 / APRS: `packet_ax25.cpp`, `packet_aprs.cpp`; NXDN: `nxdn_proto.cpp`; DMR: `dmr_fec.cpp`, `dmr_proto.cpp`; P25: `p25_proto.cpp`; D-STAR: `dstar_proto.cpp`; UAT: `uat_decode.c`; mesh: `mesh_proto*.cpp`, `mesh_pb.cpp`), because `DECT2_RUST=OFF` builds and cross builds without a Rust target must keep working. The Rust-backed
twin is `<file>_rs.cpp` (guarded by `#ifdef DECT2_RUST_<MODULE>`). A later step deletes the `#ifndef` blocks when Rust becomes mandatory; the differential
tests then keep a frozen copy as their reference (`cmake/gen_rs_ref.cmake` writes it from the C++ file with the switch undefined).

What deliberately stays C++ in the three first converted protocol modules: ADS-B `getBits` / `setBits` / `toHex` (helpers without a length, called in loops by the
generator and trackers), the encoder and generator (`adsb_encode`, `adsb_gen`), the tracker and register, the demodulator; ACARS `acarsBuildFrame` (transmitter of the
test signal) and the receiver; SAME's county table (user state with a mutex) and file loader, and the receiver. Pager: `pocsagBits` / `flexPhaseBits` (generators) and the
`PocsagParser` message assembler (unbounded bit list). AX.25: `buildFrame`, `appendFcs`, `hdlcBits`, `nrziEncode`, `scramble`. `HdlcRx` and the pager `PocsagParser` are thin C++ classes
over Rust steps (`hdlcStep`, `pocsagClassify`).

## Differential tests and floating point

`tests/<mode>/test_<mode>_proto_rs.cpp` runs the same inputs through the Rust-backed namespace and the frozen C++ copy and requires identical results (doubles bit for
bit), over the real vectors and 100000+ seeded random / mutated / truncated inputs per function family; `fuzz_<mode>_proto` is the seeded mutation run for the
sanitizer build (also a libFuzzer target with `-DONAIR_LIBFUZZER`). Both are registered by `onair_rust_diff_tests()` in `CMakeLists.txt`.

Floating point: Rust never fuses `a * b + c` and calls the C math library for `sin`, `cos`, `floor`, `fmod`... (`no_std` has no methods for them; they are declared in `ffi.rs`,
`math`). clang and gcc fuse multiply-adds on arm64 by default and rewrite `sin(x)` + `cos(x)` into `sincos` on Apple platforms, which changes the last bit. `adsb_msg.cpp`, `packet_aprs.cpp` (pow), `dmr_fec.cpp` and `p25_proto.cpp` (trellis metrics add squares) are
therefore compiled with `-ffp-contract=off -fno-builtin-sin -fno-builtin-cos` in every build (also `DECT2_RUST=OFF`), so ON and OFF give the same numbers bit for bit. Use
the same flags for any future module that mixes floating point between Rust and C++.
