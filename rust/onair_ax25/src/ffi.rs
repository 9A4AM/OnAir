//! C ABI of `onair_ax25` (core/include/dect2/rs/ax25.h). Every export is `onair_ax25_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_ax25_version() -> u32 {
    1
}
