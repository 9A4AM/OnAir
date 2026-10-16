//! OnAir MPEG-TS: MPEG-2 transport stream: packet sync, PSI sections, PAT/PMT/SDT, EIT and CRC-32 checks.
//! Safe protocol code goes in modules next to `ffi`; only `ffi` may use `unsafe` (see rust/README.md).
#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

#[allow(unsafe_code)]
pub mod ffi;
