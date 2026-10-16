//! C ABI of `onair_p25` (core/include/dect2/rs/p25.h). Every export is `onair_p25_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_p25_version() -> u32 {
    1
}
