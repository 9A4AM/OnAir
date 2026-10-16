//! OnAir USB drivers: Android USB SDR drivers (RTL-SDR, Airspy, HackRF and others) behind a C ABI called from JNI.
//! Safe protocol code goes in modules next to `ffi`; only `ffi` may use `unsafe` (see rust/README.md).
#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

#[allow(unsafe_code)]
pub mod ffi;
