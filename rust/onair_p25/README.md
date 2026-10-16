# onair_p25

Will hold: APCO P25 phase 1/2: NID, TSBK, LDU link control, trunking messages.
Status: skeleton (only `onair_p25_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/p25.h`, symbols prefixed `onair_p25_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
