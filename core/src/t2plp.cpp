#include "dect2/t2plp.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace dect2 {

namespace {
template <class F>
void parallelFor(int n, int threads, F fn) {
    if (threads <= 1 || n <= 1) { for (int i = 0; i < n; i++) fn(i); return; }
    std::atomic<int> next{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < std::min(threads, n); t++)
        ts.emplace_back([&] { for (int i; (i = next++) < n;) fn(i); });
    for (auto& t : ts) t.join();
}
} // namespace

PlpResult PlpDecoder::decodeNow(const PlpJob& job, int threads) {
    auto t0 = std::chrono::steady_clock::now();
    PlpResult res;
    res.frameNo = job.frameNo; res.plpId = job.plpId; res.fec = job.fec;
    FecDims d = fecDims(job.fec);
    if (!d.ok || job.numBlocks <= 0 || (int)job.cells.size() < job.numBlocks * d.cellsPerBlock) return res;
    const int nb = job.numBlocks, cells = d.cellsPerBlock, bps = d.bitsPerCell;
    res.blocks = nb;
    std::vector<cf32> rx((size_t)nb * cells);
    std::vector<float> n0((size_t)nb * cells);
    cellDeinterleave(job.fec, nb, job.tiBlocks, job.cells.data(), rx.data());
    cellDeinterleaveF(job.fec, nb, job.tiBlocks, job.n0.data(), n0.data());
    const LdpcCode& ldpc = ldpcFor(job.fec);
    const BchCode& bch = bchFor(job.fec);
    const auto& map = bitInterleaverMap(job.fec);

    struct BlockOut {
        bool ok = false;
        int iters = 0, corrected = 0;
        long errBits = 0;
        double sigP = 0, errP = 0;
        BbFrame frame;
        std::vector<cf32> pts;
        std::vector<uint8_t> perr;
    };
    std::vector<BlockOut> outs(nb);
    parallelFor(nb, threads, [&](int b) {
        BlockOut& o = outs[b];
        const cf32* c = rx.data() + (size_t)b * cells;
        const float* nn = n0.data() + (size_t)b * cells;
        std::vector<float> llrLab((size_t)cells * bps), llr(d.nLdpc);
        qamDemapBlock(job.fec, c, nn, cells, llrLab.data());
        for (int p = 0; p < d.nLdpc; p++) llr[map[p]] = llrLab[p];
        std::vector<uint8_t> hard;
        ldpc.decode(llr, 40, hard, &o.iters);
        // hard decisions before LDPC, for the pre-FEC bit error rate
        long pre = 0;
        std::vector<uint8_t> bb(hard.begin(), hard.begin() + d.kLdpc);
        int corr = bch.decode(bb);
        o.frame.blockIndex = b;
        if (corr < 0) return;
        o.ok = true;
        o.corrected = corr;
        // reconstruct the codeword to measure the pre-LDPC error rate and the MER
        std::vector<uint8_t> cw(bb.begin(), bb.begin() + d.kBch);
        bch.encode(cw, d.kBch);
        ldpc.encode(cw);
        for (int p = 0; p < d.nLdpc; p++) pre += (llr[map[p]] < 0) != (cw[map[p]] != 0);
        o.errBits = pre;
        std::vector<uint16_t> lab(cells);
        for (int k = 0; k < cells; k++) { unsigned l = 0; for (int j = 0; j < bps; j++) l = (l << 1) | cw[map[k * bps + j]]; lab[k] = (uint16_t)l; }
        std::vector<cf32> ideal;
        qamMapBlock(job.fec, lab, ideal);
        const size_t step = std::max<size_t>(1, (size_t)cells / 600);
        for (int k = 0; k < cells; k++) {
            cf32 e = c[k] - ideal[k];
            o.sigP += std::norm(ideal[k]);
            o.errP += std::norm(e);
            if ((size_t)k % step == 0) { o.pts.push_back(c[k]); o.perr.push_back(std::abs(e) > 0.5f * 0.7f); }
        }
        // descramble the BB frame
        o.frame.bits.assign(bb.begin(), bb.begin() + d.kBch);
        const uint8_t* rnd = bbRandomiser();
        for (int i = 0; i < d.kBch; i++) o.frame.bits[i] ^= rnd[i];
        parseBbHeader(o.frame.bits.data(), o.frame.header);
    });
    double sig = 0, err = 0;
    long preErr = 0, preBits = 0;
    long itSum = 0;
    for (auto& o : outs) {
        itSum += o.iters;
        if (o.ok) {
            res.blocksOk++;
            res.bchCorrected += o.corrected;
            sig += o.sigP; err += o.errP;
            preErr += o.errBits; preBits += d.nLdpc;
            if (o.frame.header.crcOk) res.headerOk++;
            res.constellation.insert(res.constellation.end(), o.pts.begin(), o.pts.end());
            res.constErr.insert(res.constErr.end(), o.perr.begin(), o.perr.end());
        } else res.bchFailed++;
        res.frames.push_back(std::move(o.frame));
    }
    res.avgLdpcIters = (double)itSum / nb;
    res.preBer = preBits ? (double)preErr / preBits : 0;
    res.merDb = err > 0 ? 10 * std::log10(sig / err) : 99;
    // thin the constellation sample
    if (res.constellation.size() > 6000) {
        size_t st = res.constellation.size() / 6000 + 1;
        std::vector<cf32> c2; std::vector<uint8_t> e2;
        for (size_t i = 0; i < res.constellation.size(); i += st) { c2.push_back(res.constellation[i]); e2.push_back(res.constErr[i]); }
        res.constellation = std::move(c2); res.constErr = std::move(e2);
    }
    res.decodeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

PlpDecoder::PlpDecoder(int threads) {
    threads_ = threads > 0 ? threads : std::max(1u, std::thread::hardware_concurrency());
    th_ = std::thread([this] { loop(); });
}

PlpDecoder::~PlpDecoder() {
    stop_ = true;
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

bool PlpDecoder::submit(PlpJob&& job) {
    std::lock_guard<std::mutex> lk(mu_);
    if (jobs_.size() >= 2 || (busy_ && !jobs_.empty())) { dropped_++; return false; }
    jobs_.push_back(std::move(job));
    cv_.notify_one();
    return true;
}

bool PlpDecoder::poll(PlpResult& out) {
    std::lock_guard<std::mutex> lk(mu_);
    if (results_.empty()) return false;
    out = std::move(results_.front());
    results_.pop_front();
    return true;
}

int PlpDecoder::pending() const {
    std::lock_guard<std::mutex> lk(mu_);
    return (int)jobs_.size() + (busy_ ? 1 : 0);
}

void PlpDecoder::loop() {
    for (;;) {
        PlpJob job;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            busy_ = true;
        }
        PlpResult r = decodeNow(job, threads_);
        {
            std::lock_guard<std::mutex> lk(mu_);
            results_.push_back(std::move(r));
            if (results_.size() > 8) results_.pop_front();
            busy_ = false;
        }
    }
}

} // namespace dect2
