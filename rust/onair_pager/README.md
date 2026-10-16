# onair_pager

Will hold: POCSAG and FLEX pager protocols: BCH(31,21) codeword layer, batches, address/message decoding.
Status: skeleton (only `onair_pager_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/pager.h`, symbols prefixed `onair_pager_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
