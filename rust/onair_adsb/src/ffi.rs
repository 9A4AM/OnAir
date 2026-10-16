//! C ABI of `onair_adsb` (core/include/dect2/rs/adsb.h). Every export is `onair_adsb_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_adsb_version() -> u32 {
    1
}
