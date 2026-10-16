# onair_hfdl

Will hold: HFDL (HF data link): LPDU/MPDU parsing and ground station tables.
Status: skeleton (only `onair_hfdl_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/hfdl.h`, symbols prefixed `onair_hfdl_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
