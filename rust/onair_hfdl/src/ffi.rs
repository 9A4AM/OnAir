//! C ABI of `onair_hfdl` (core/include/dect2/rs/hfdl.h). Every export is `onair_hfdl_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_hfdl_version() -> u32 {
    1
}
