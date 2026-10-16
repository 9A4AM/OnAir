//! OnAir AX.25/APRS: AX.25 packet radio and APRS: HDLC frames, address fields, APRS position/message/telemetry parsing.
//! Safe protocol code goes in modules next to `ffi`; only `ffi` may use `unsafe` (see rust/README.md).
#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

#[allow(unsafe_code)]
pub mod ffi;
