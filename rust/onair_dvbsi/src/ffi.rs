//! C ABI of `onair_dvbsi` (core/include/dect2/rs/dvbsi.h). Every export is `onair_dvbsi_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_dvbsi_version() -> u32 {
    1
}
