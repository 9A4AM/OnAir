# onair_net

Will hold: Network code: remote API, share server, SpyServer, rtl_tcp, map upload and the tuner protocol.
Status: skeleton (only `onair_net_version`). Plan and conventions: ../README.md.
C ABI: `src/ffi.rs`, header `core/include/dect2/rs/net.h`, symbols prefixed `onair_net_`.
Shared CRC/bit/hex helpers come from `onair_common`; keep protocol logic in safe modules and `unsafe` in `ffi.rs` only.
