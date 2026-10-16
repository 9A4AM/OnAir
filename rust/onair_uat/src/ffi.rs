//! C ABI of `onair_uat` (core/include/dect2/rs/uat.h). Every export is `onair_uat_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_uat_version() -> u32 {
    1
}
