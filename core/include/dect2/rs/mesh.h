// C ABI of the Rust crate rust/onair_mesh (LoRa mesh). Written by hand; the Rust test rust/onair/tests/headers.rs checks it against ffi.rs.
// Only meaningful when DECT2_RUST_MESH is defined (see rust/README.md).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ABI version of this module (1 for the skeleton).
uint32_t onair_mesh_version(void);

#ifdef __cplusplus
}
#endif
