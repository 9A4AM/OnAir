//! C ABI of `onair_ts` (core/include/dect2/rs/ts.h). Every export is `onair_ts_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_ts_version() -> u32 {
    1
}
