# onair_wisun

Will hold: Wi-SUN FAN / IEEE 802.15.4g frames.
Status: skeleton (only `onair_wisun_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/wisun.h`, symbols prefixed `onair_wisun_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
