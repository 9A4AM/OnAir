# onair_usb

Will hold: Android USB SDR drivers (RTL-SDR, Airspy, HackRF and others) behind a C ABI called from JNI.
Status: skeleton (only `onair_usb_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/usb.h`, symbols prefixed `onair_usb_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
