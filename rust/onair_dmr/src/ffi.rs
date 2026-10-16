//! C ABI of `onair_dmr` (core/include/dect2/rs/dmr.h). Every export is `onair_dmr_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_dmr_version() -> u32 {
    1
}
