//! OnAir POCSAG/FLEX: POCSAG and FLEX pager protocols: BCH(31,21) codeword layer, batches, address/message decoding.
//! Safe protocol code goes in modules next to `ffi`; only `ffi` may use `unsafe` (see rust/README.md).
#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

#[allow(unsafe_code)]
pub mod ffi;
