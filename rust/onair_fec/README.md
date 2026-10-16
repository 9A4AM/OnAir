# onair_fec

Will hold: Forward error correction: LDPC, BCH, Reed-Solomon and Viterbi decoders.
Status: skeleton (only `onair_fec_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/fec.h`, symbols prefixed `onair_fec_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
