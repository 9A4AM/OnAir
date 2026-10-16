//! C ABI of `onair_files` (core/include/dect2/rs/files.h). Every export is `onair_files_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_files_version() -> u32 {
    1
}
