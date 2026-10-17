// Shared by hfdl_proto.cpp and hfdl_proto_rs.cpp (DECT2_RUST_HFDL): the ACARS reader that both parsers call.
#pragma once
#include "dect2/hfdl/hfdl_tel.h"
#include <cstddef>
#include <cstdint>

namespace dect2 {
// The ACARS block of an HFNPDU (n octets at p, after the HFNPDU's FF FF): sets the ACARS fields, summary and position of m and appends to m.detail.
void hfdlReadAcars(const uint8_t* p, size_t n, HfdlMessage& m);
} // namespace dect2
