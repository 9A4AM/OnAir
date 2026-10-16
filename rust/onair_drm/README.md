# onair_drm

Will hold: DRM data services: SDC/FAC fields, text messages and journaline.
Status: skeleton (only `onair_drm_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/drm.h`, symbols prefixed `onair_drm_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
