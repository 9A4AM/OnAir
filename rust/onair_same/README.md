# onair_same

Will hold: SAME / NOAA weather radio alert headers and EAS event/area codes.
Status: skeleton (only `onair_same_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/same.h`, symbols prefixed `onair_same_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
