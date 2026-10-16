# onair_dvbsi

Will hold: DVB service information: descriptors, teletext and DVB subtitle parsing.
Status: skeleton (only `onair_dvbsi_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/dvbsi.h`, symbols prefixed `onair_dvbsi_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
