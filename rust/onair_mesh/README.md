# onair_mesh

Will hold: LoRa-based mesh: Meshtastic, MeshCore, LoRaWAN and LoRa APRS frame parsing.
Status: skeleton (only `onair_mesh_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/mesh.h`, symbols prefixed `onair_mesh_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
