//! C ABI of `onair_wisun` (core/include/dect2/rs/wisun.h). Every export is `onair_wisun_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_wisun_version() -> u32 {
    1
}
