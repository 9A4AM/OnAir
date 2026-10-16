//! OnAir VDL2: VDL Mode 2: AVLC frames, CRC, ACARS-over-AVLC and X.25/ISO 8208 payloads.
//! Safe protocol code goes in modules next to `ffi`; only `ffi` may use `unsafe` (see rust/README.md).
#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

#[allow(unsafe_code)]
pub mod ffi;
