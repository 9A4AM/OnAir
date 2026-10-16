//! C ABI of `onair_same` (core/include/dect2/rs/same.h). Every export is `onair_same_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_same_version() -> u32 {
    1
}
