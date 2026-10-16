//! OnAir LoRa mesh: LoRa-based mesh: Meshtastic, MeshCore, LoRaWAN and LoRa APRS frame parsing.
//! Safe protocol code goes in modules next to `ffi`; only `ffi` may use `unsafe` (see rust/README.md).
#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

#[allow(unsafe_code)]
pub mod ffi;
