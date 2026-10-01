// DVB-T2 / DVB-S2 style IRA LDPC codes defined by 360-bit-group address tables.
#pragma once
#include <cstdint>
#include <vector>

namespace dect2 {

class LdpcCode {
public:
    // `rows[r]` lists the parity-check addresses of the 360 information bits of group r (value 0 terminated by `len`).
    LdpcCode(int k, int n, const std::vector<std::vector<int>>& rows);
    int k() const { return k_; }
    int n() const { return n_; }

    // Systematic encode: bits[0..k) in, bits[k..n) out.
    void encode(std::vector<uint8_t>& bits) const;

    // Normalised min-sum decoding. llr > 0 means bit 0. Returns true if all parity checks are satisfied.
    // hard[0..n) receives the decided bits. `iters` gets the iteration count used.
    bool decode(const std::vector<float>& llr, int maxIter, std::vector<uint8_t>& hard, int* iters = nullptr) const;

private:
    int k_, n_, m_;
    // check -> variable adjacency (CSR)
    std::vector<int> chkStart_, chkVar_;
    // information-bit edges for encoding: parity index p, info index d
    std::vector<int> encP_, encD_;
};

} // namespace dect2
