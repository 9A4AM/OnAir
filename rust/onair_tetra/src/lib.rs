//! OnAir TETRA: TETRA (ETSI EN 300 392): MAC PDUs, burst layers, SDS and system information.
//! Safe protocol code goes in modules next to `ffi`; only `ffi` may use `unsafe` (see rust/README.md).
#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

#[allow(unsafe_code)]
pub mod ffi;
