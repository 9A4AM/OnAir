//! C ABI of `onair_usb` (core/include/dect2/rs/usb.h). Every export is `onair_usb_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_usb_version() -> u32 {
    1
}
