//! C ABI of `onair_pager` (core/include/dect2/rs/pager.h). Every export is `onair_pager_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_pager_version() -> u32 {
    1
}
