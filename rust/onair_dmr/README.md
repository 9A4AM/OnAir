# onair_dmr

Will hold: DMR (ETSI TS 102 361): burst layer, slot types, link control, embedded signalling.
Status: skeleton (only `onair_dmr_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/dmr.h`, symbols prefixed `onair_dmr_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
