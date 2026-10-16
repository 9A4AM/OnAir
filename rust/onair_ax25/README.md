# onair_ax25

Will hold: AX.25 packet radio and APRS: HDLC frames, address fields, APRS position/message/telemetry parsing.
Status: skeleton (only `onair_ax25_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/ax25.h`, symbols prefixed `onair_ax25_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
