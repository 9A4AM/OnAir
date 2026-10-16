//! C ABI of `onair_vdl2` (core/include/dect2/rs/vdl2.h). Every export is `onair_vdl2_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_vdl2_version() -> u32 {
    1
}
