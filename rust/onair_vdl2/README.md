# onair_vdl2

Will hold: VDL Mode 2: AVLC frames, CRC, ACARS-over-AVLC and X.25/ISO 8208 payloads.
Status: skeleton (only `onair_vdl2_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/vdl2.h`, symbols prefixed `onair_vdl2_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
