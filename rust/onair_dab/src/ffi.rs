//! C ABI of `onair_dab` (core/include/dect2/rs/dab.h). Every export is `onair_dab_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_dab_version() -> u32 {
    1
}
