//! C ABI of `onair_drm` (core/include/dect2/rs/drm.h). Every export is `onair_drm_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_drm_version() -> u32 {
    1
}
