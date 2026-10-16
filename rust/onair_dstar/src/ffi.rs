//! C ABI of `onair_dstar` (core/include/dect2/rs/dstar.h). Every export is `onair_dstar_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_dstar_version() -> u32 {
    1
}
