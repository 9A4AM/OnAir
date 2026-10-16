//! OnAir ADS-B: ADS-B / Mode S (1090 MHz): DF/TC decoding, CPR position, CRC-24, callsign and velocity messages.
//! Safe protocol code goes in modules next to `ffi`; only `ffi` may use `unsafe` (see rust/README.md).
#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

#[allow(unsafe_code)]
pub mod ffi;
