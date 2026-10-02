// PLP decoding: time/cell de-interleaving, demapping, LDPC, BCH, BB-frame header parsing.
// Runs on worker threads so that the receive path never stalls.
#pragma once
#include "t2fec.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace dect2 {

struct PlpJob {
    uint64_t frameNo = 0;
    int t2Frame = 0;        // L1-post FRAME_IDX
    double frameSec = 0;    // duration of the T2 frame
    int plpId = 0;
    PlpFec fec;
    int numBlocks = 0;
    int tiBlocks = 0;
    std::vector<cf32> cells;  // numBlocks * cellsPerBlock equalised cells (as received, still interleaved)
    std::vector<float> n0;    // noise variance per real dimension for every cell
};

struct BbFrame {
    int blockIndex = 0;
    BbHeader header;
    std::vector<uint8_t> bits; // descrambled BB frame, kBch bits (one per byte); empty if the block failed
};

struct PlpResult {
    uint64_t frameNo = 0;
    int t2Frame = 0;
    double frameSec = 0;
    int plpId = 0;
    PlpFec fec;
    int blocks = 0, blocksOk = 0, bchFailed = 0, headerOk = 0;
    int retryRecovered = 0;     // blocks that only decoded on the second-chance pass
    int bchCorrected = 0;       // total bit errors corrected by BCH
    double avgLdpcIters = 0;
    double preBer = 0;          // hard-decision bit error rate before LDPC (measured against the decoded codewords)
    double merDb = 0;           // modulation error ratio over correctly decoded blocks
    double decodeMs = 0;
    bool usedGpu = false;       // LDPC ran on the GPU
    std::vector<BbFrame> frames;
    std::vector<cf32> constellation; // equalised cells of decoded blocks (subsampled)
    std::vector<uint8_t> constErr;   // 1 where the nearest ideal point is not the transmitted one
};

class PlpDecoder {
public:
    explicit PlpDecoder(int threads = 0);
    ~PlpDecoder();
    // Returns false (and drops the job) if the decoder is still busy with earlier frames.
    bool submit(PlpJob&& job);
    bool poll(PlpResult& out); // oldest finished result
    int pending() const;
    uint64_t dropped() const { return dropped_; }
    static PlpResult decodeNow(const PlpJob& job, int threads, bool useGpu = false);
    // 0 = CPU, 1 = GPU, 2 = auto (CPU until it falls behind, then GPU)
    void setMode(int m) { mode_ = m; if (m != 2) autoGpu_ = false; }
    int mode() const { return mode_; }
    bool gpuAvailable() const { return gpuOk_; }
    bool autoOnGpu() const { return autoGpu_; }

private:
    void loop();
    int threads_;
    std::thread th_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<PlpJob> jobs_;
    std::deque<PlpResult> results_;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> dropped_{0};
    bool busy_ = false;
    std::atomic<int> mode_{2};
    std::atomic<bool> autoGpu_{false};
    bool gpuOk_ = false;
    uint64_t lastDropped_ = 0;
};

} // namespace dect2
