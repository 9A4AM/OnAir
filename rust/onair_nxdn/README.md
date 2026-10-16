# onair_nxdn

Will hold: NXDN: LICH, SACCH/FACCH, CAC message decoding.
Status: skeleton (only `onair_nxdn_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/nxdn.h`, symbols prefixed `onair_nxdn_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
