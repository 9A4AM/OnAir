# onair_ts

Will hold: MPEG-2 transport stream: packet sync, PSI sections, PAT/PMT/SDT, EIT and CRC-32 checks.
Status: skeleton (only `onair_ts_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/ts.h`, symbols prefixed `onair_ts_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
