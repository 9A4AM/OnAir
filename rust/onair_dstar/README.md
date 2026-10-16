# onair_dstar

Will hold: D-STAR: header, slow data, FEC and callsign fields.
Status: skeleton (only `onair_dstar_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/dstar.h`, symbols prefixed `onair_dstar_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
