# onair_vdl2

VDL Mode 2 (VHF data link, 136.975 MHz CSC and the other channels), the layers above the modem (ICAO Annex 10 Vol III Part I section 6.3, ICAO Doc 9776 as dumpvdl2 implements them):

* `bits`: the x^15 + x + 1 scrambler (register 0x6959 at the first header bit), the (25,20) burst header code with single-error correction, the burst layout (data octets, Reed-Solomon blocks,
  parity of the short last block).
* `rs`: Reed-Solomon (255,249) over GF(2^8), polynomial 0x187, first root alpha^120: encoder and the errors-and-erasures decoder (Berlekamp-Massey seeded with the erasure locator, Chien search, Forney).
  The field tables are built at compile time.
* `burst`: gathering a burst's octets column by column into blocks, correcting them and returning the data bits; the HDLC deframer (flags, zero-bit deletion, abort on seven ones) and the FCS
  (CRC-16/X.25, residue 0xF0B8).
* `avlc`: the 4-octet addresses (A/G and C/R bits, address types), the control octet and the I / S / U frames. `proto`: ISO 8208 / X.25 packets (data, call set-up, clear, reset, restart, flow control),
  CLNP headers with the NSAP addresses and the COTP TPDU around ATN application data (ASN.1 PER, not decoded). `xid`: XID link management (GSIF, link establishment, handoff; locations, airports,
  parameter set, h / r bits).

Results are flat `repr(C)` records; text is length plus bytes (the detail text of a frame holds 16384 bytes, enough for frames of about 700 octets; larger ones are cut). The Reed-Solomon decoder clamps the
parity count to 0..6 (the reference is undefined outside), the burst gatherer handles up to 70 blocks (the 17 bit header length allows 66). `unsafe` only in `ffi.rs`, which also holds `cfn` (C library
`snprintf("%.1f")` for the CLNP lifetime and XID locations).

Stays in C++ (`core/src/vdl2/`): the demodulator, receiver and tracker, the test signal builders (`vdl2BurstBits`, `vdl2HdlcBits`, `vdl2Frame`, the ACARS / X.25 / XID information builders), and the ACARS reader
`vdl2ReadAcars` (ARINC 618 block, ADS-C, position text, FANS-1/A CPDLC IMIs: it uses the ACARS and aero code); the Rust frame decoder reports where an ACARS block starts (`acars_at`) and the C++ continues
there. `vdl2_proto.cpp` keeps the C++ under `#ifndef DECT2_RUST_VDL2`; `vdl2_proto_rs.cpp` is the Rust-backed twin (layout check at start-up).

C ABI: `src/ffi.rs`, header `core/include/dect2/rs/vdl2.h`, symbols prefixed `onair_vdl2_`. Differential test `tests/vdl2/test_vdl2_proto_rs.cpp` against a frozen copy of `vdl2_proto.cpp` (all 2^25 header words,
every burst length, 200000 Reed-Solomon decodes with errors and erasures, 50000 burst bodies, 200000 HDLC streams, 200000 frames: 36 M comparisons), fuzz target `fuzz_vdl2_proto`. Status and conventions: ../README.md.
