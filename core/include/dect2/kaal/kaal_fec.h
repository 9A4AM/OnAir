// KAAL FEC (port of kaal_fec.py): systematic rate-1/2 quasi-cyclic LDPC, n=640 k=320 (Z=32, 10x20 base matrix),
// normalised min-sum decoder, plus the old K=7 convolutional encoder for reference.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dect2 {
namespace kaal {

constexpr int kLdpcZ = 32, kLdpcMb = 10, kLdpcNb = 20;
constexpr int kLdpcN = kLdpcZ * kLdpcNb;   // 640
constexpr int kLdpcK = kLdpcZ * (kLdpcNb - kLdpcMb);   // 320
constexpr int kLdpcM = kLdpcZ * kLdpcMb;   // 320
constexpr float kLdpcAlpha = 0.8f;
constexpr int kLdpcIters = 40;

// info: 320 bits (one per byte, 0/1) -> out: 640 bits [info | parity]
void ldpcEncode(const uint8_t* info, uint8_t* out);
// llr: 640 values, positive = bit 0 more likely. info: 320 decoded bits. Returns true when all parity checks are satisfied.
// iterationsUsed (optional) gets the number of message-passing iterations run.
bool ldpcDecode(const float* llr, uint8_t* info, int maxIter = kLdpcIters, float alpha = kLdpcAlpha, int* iterationsUsed = nullptr);
// Fraction of satisfied parity checks on the hard decisions of llr (alignment metric).
float ldpcSyndromeScore(const float* llr);

// Rate 1/2, K=7, generators 0o171 and 0o133: 2 output bits per input bit.
std::vector<uint8_t> convEncode(const uint8_t* bits, size_t n);

}  // namespace kaal
}  // namespace dect2
