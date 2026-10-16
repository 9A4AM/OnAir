# onair_uat

Will hold: UAT (978 MHz) ADS-B: FEC-framed uplink/downlink message parsing.
Status: skeleton (only `onair_uat_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/uat.h`, symbols prefixed `onair_uat_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
