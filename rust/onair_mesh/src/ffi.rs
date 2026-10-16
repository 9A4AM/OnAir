//! C ABI of `onair_mesh` (core/include/dect2/rs/mesh.h). Every export is `onair_mesh_*`; pointers come as (pointer, length) pairs and the caller owns all buffers.

/// ABI version of this module's C interface; bump it when an exported signature changes.
#[no_mangle]
pub extern "C" fn onair_mesh_version() -> u32 {
    1
}
