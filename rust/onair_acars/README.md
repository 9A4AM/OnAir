# onair_acars

Will hold: ACARS (VHF and satellite): message framing, parity, CRC-16, label and block decoding.
Status: skeleton (only `onair_acars_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/acars.h`, symbols prefixed `onair_acars_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
