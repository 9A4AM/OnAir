// Metal compute LDPC decoder: layered normalised min-sum, one threadgroup (360 threads, one per check of a layer) per FEC block.
#pragma once
#include "ldpc.h"
#include "qc_ldpc.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace dect2 {

class GpuLdpc {
public:
    static GpuLdpc& instance();
    bool available() const;
    const char* deviceName() const;
#ifdef _WIN32
    size_t dedicatedMemoryMb() const;   // video memory of the chosen adapter that belongs to it (about 128 for integrated graphics)
#endif
    // Decodes `nb` blocks of n LLRs each (llr > 0 means bit 0). hard receives nb*n bits (one per byte),
    // ok[b] = all parity checks satisfied, iters[b] = iterations used. Blocks the caller until the GPU is done.
    bool decode(const LdpcCode& code, const float* llr, int nb, int maxIter, uint8_t* hard, uint8_t* ok, int* iters, float alpha = 0.78f);

    // The same for a table-driven quasi-cyclic code (layered normalised min-sum, the schedule and arithmetic of the DTMB CPU decoder, one thread group per
    // word). llr: nb * sentBits() values (> 0 means bit 0). info: nb * infoBits() bits (one per byte). ok[b]: all checks satisfied; iters[b]: iterations.
    // Returns false when the backend has no such kernel (everything except Metal for now) or the call failed: the caller decodes on the CPU.
    bool qcAvailable() const;   // decodeQc has a kernel on this backend and device (Metal only so far)
    bool decodeQc(const QcLdpc& code, const float* llr, int nb, int maxIter, uint8_t* info, uint8_t* ok, int* iters, float alpha);

private:
    bool selfTest();   // (Direct3D 11 build) decodes known blocks; the GPU decoder disables itself if the result is wrong
    GpuLdpc();
    ~GpuLdpc();
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace dect2
