//! C ABI of `onair_net` (core/include/dect2/rs/net.h). Every export is `onair_net_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_net_version() -> u32 {
    1
}
