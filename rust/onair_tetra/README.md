# onair_tetra

Will hold: TETRA (ETSI EN 300 392): MAC PDUs, burst layers, SDS and system information.
Status: skeleton (only `onair_tetra_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/tetra.h`, symbols prefixed `onair_tetra_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
