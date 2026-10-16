# onair_dab

Will hold: DAB / DAB+: FIG decoding, MOT slideshow and directory objects.
Status: skeleton (only `onair_dab_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/dab.h`, symbols prefixed `onair_dab_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
