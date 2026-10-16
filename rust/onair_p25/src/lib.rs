//! OnAir P25: APCO P25 phase 1/2: NID, TSBK, LDU link control, trunking messages.
//! Safe protocol code goes in modules next to `ffi`; only `ffi` may use `unsafe` (see rust/README.md).
#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

#[allow(unsafe_code)]
pub mod ffi;
