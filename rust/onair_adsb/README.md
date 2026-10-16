# onair_adsb

Will hold: ADS-B / Mode S (1090 MHz): DF/TC decoding, CPR position, CRC-24, callsign and velocity messages.
Status: skeleton (only `onair_adsb_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/adsb.h`, symbols prefixed `onair_adsb_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
