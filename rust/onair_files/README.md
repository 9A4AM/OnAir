# onair_files

Will hold: File formats: SigMF and IQ headers, MBTiles, TLE, CSV and results export.
Status: skeleton (only `onair_files_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/files.h`, symbols prefixed `onair_files_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
