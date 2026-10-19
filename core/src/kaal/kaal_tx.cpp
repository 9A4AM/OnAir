// KAAL voice transmitter, streaming port of kaal_tx.py.
#include "dect2/kaal/kaal_tx.h"
#include "dect2/kaal/kaal_codec.h"
#include "dect2/kaal/kaal_fec.h"
#include "dect2/kaal/kaal_phy.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dect2 {
namespace kaal {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr size_t kChunk = 320;                 // codec samples per frame
constexpr int kHistSyms = kRrcSpan + 1;        // symbols that overlap one output sample (49 taps / 6)

// Streaming windowed-sinc resampler for complex samples (48 kHz in, outRate out).
class Resampler {
public:
    void configure(double outRate) {
        ratio_ = 48000.0 / outRate;
        fc_ = 0.97 * std::min(1.0, outRate / 48000.0);
        reset();
    }
    void reset() { buf_.assign(kH, cf(0, 0)); base_ = -kH; pos_ = 0; }
    void process(const cf* in, size_t n, std::vector<cf>& out) {
        buf_.insert(buf_.end(), in, in + n);
        while (true) {
            const int64_t i0 = (int64_t)std::floor(pos_);
            if (i0 + kH >= base_ + (int64_t)buf_.size()) break;
            double ar = 0, ai = 0;
            for (int64_t i = i0 - kH + 1; i <= i0 + kH; i++) {
                const double x = pos_ - (double)i;
                const double a = fc_ * x;
                const double sinc = std::fabs(a) < 1e-9 ? 1.0 : std::sin(kPi * a) / (kPi * a);
                const double w = 0.5 + 0.5 * std::cos(kPi * x / kH);
                const double k = fc_ * sinc * w;
                const cf& v = buf_[(size_t)(i - base_)];
                ar += k * v.real();
                ai += k * v.imag();
            }
            out.emplace_back((float)ar, (float)ai);
            pos_ += ratio_;
        }
        const int64_t keep = (int64_t)std::floor(pos_) - kH;
        if (keep - base_ > 4096) {
            buf_.erase(buf_.begin(), buf_.begin() + (keep - base_));
            base_ = keep;
        }
    }
    void finish(std::vector<cf>& out) {
        const std::vector<cf> z(2 * kH, cf(0, 0));
        process(z.data(), z.size(), out);
        reset();
    }
private:
    static constexpr int kH = 16;   // kernel half width in input samples
    double ratio_ = 1, fc_ = 0.97, pos_ = 0;
    int64_t base_ = -kH;
    std::vector<cf> buf_;
};
}  // namespace

struct VoiceTx::Impl {
    double outRate = 48000;
    bool resample = false;
    Resampler rs;
    KaalCodec codec;
    std::vector<double> rrc;

    // audio -> frames
    std::vector<int16_t> pcmQ;              // not yet encoded, < 320 after every push
    std::vector<int16_t> prev;              // the previous 320 samples (zeros at the start)
    std::vector<std::vector<uint8_t>> frames;   // info frames of the current 4-frame block
    bool started = false;                   // at least one codec frame was made in this stream

    // symbols -> samples
    cf hist[kHistSyms];                     // hist[0] newest
    uint64_t sampleIdx = 0;                 // output sample index at 48 kHz within the stream
    double sweepAcc = 0;

    // beacon
    bool beaconOn = false;
    BeaconFields bf;
    std::string btext;
    std::vector<cf> sliceFull, sliceHdr, cur;
    size_t curPos = 0;

    std::vector<cf> out;                    // ready samples (at the output rate)
    size_t head = 0;

    Impl() { rrc = rrcTaps(); reset(); makeSlices(); }

    void reset() {
        pcmQ.clear();
        prev.assign(kChunk, 0);
        frames.clear();
        started = false;
        for (cf& h : hist) h = cf(0, 0);
        sampleIdx = 0;
        sweepAcc = 0;
        cur.clear();
        curPos = 0;
    }

    void makeSlices() {
        sliceFull = beaconSliceModulate(bf, btext, kFs);
        sliceHdr = beaconSliceModulate(bf, std::string(), kFs);
        if (btext.empty()) sliceFull = sliceHdr;
    }

    void encodeWindow(const int16_t* cur320) {   // frame for samples [prev, cur320)
        int16_t w[2 * kChunk];
        std::memcpy(w, prev.data(), kChunk * sizeof(int16_t));
        std::memcpy(w + kChunk, cur320, kChunk * sizeof(int16_t));
        const std::vector<uint8_t> bits = codec.encode(w, 2 * kChunk);   // 3 frames; the middle one covers exactly [prev, cur320)
        frames.emplace_back(bits.begin() + KaalCodec::kBits, bits.begin() + 2 * KaalCodec::kBits);
        std::memcpy(prev.data(), cur320, kChunk * sizeof(int16_t));
        started = true;
        if (frames.size() == (size_t)4) encodeBlock();
    }

    void encodeBlock() {
        std::vector<uint8_t> coded(4 * kCodedBits);
        for (int f = 0; f < 4; f++) ldpcEncode(frames[f].data(), &coded[f * kCodedBits]);
        frames.clear();
        const std::vector<uint8_t> il = interleave(coded);
        const cf pil = pilotSymbol();
        const std::vector<cf> pre = preamble();
        for (int f = 0; f < 4; f++) {
            const std::vector<cf> data = qam16Map(&il[f * kCodedBits], kCodedBits);
            for (const cf& s : pre) emitSymbol(s);
            for (size_t j = 0; j < data.size(); j += kPilotEvery - 1) {
                const size_t m = std::min<size_t>(kPilotEvery - 1, data.size() - j);
                for (size_t k = 0; k < m; k++) emitSymbol(data[j + k]);
                if (m == (size_t)(kPilotEvery - 1)) emitSymbol(pil);
            }
        }
    }

    void startBeaconIfDue() {
        if (!beaconOn || curPos < cur.size()) return;
        const uint64_t full = (uint64_t)(kBeaconFullPeriodS * kFs), hdr = (uint64_t)(kBeaconHeaderPeriodS * kFs);
        if (sampleIdx % full == 0) { cur = sliceFull; curPos = 0; }
        else if (sampleIdx % hdr == 0) { cur = sliceHdr; curPos = 0; }
    }

    void emitSymbol(const cf& s) {
        for (int m = kHistSyms - 1; m > 0; m--) hist[m] = hist[m - 1];
        hist[0] = s;
        const float beaconAmp = (float)std::sqrt(kTxDataPower * std::pow(10.0, kTxBeaconDb / 10.0));
        const float sweepAmp = (float)std::sqrt(kTxDataPower * std::pow(10.0, kSweepPowerDb / 10.0));
        cf blk[kSps];
        for (int q = 0; q < kSps; q++) {
            double ar = 0, ai = 0;
            for (int m = 0; m < kHistSyms; m++) {
                const int ti = m * kSps + q;
                if (ti >= (int)rrc.size()) break;
                ar += hist[m].real() * rrc[ti];
                ai += hist[m].imag() * rrc[ti];
            }
            // sweep signature: phase-continuous chirp from the start of the stream, as kaal_phy.sweep_template
            const double t = (double)sampleIdx / kFs;
            const double ph = std::fmod(t, kSweepPeriodS);
            sweepAcc += kSweepLowHz + (kSweepHighHz - kSweepLowHz) * (ph / kSweepPeriodS);
            if (sweepAcc > 1e12) sweepAcc = std::fmod(sweepAcc, (double)kFs);
            const double a = 2 * kPi * sweepAcc / kFs;
            double sr = ar + sweepAmp * std::cos(a), si = ai + sweepAmp * std::sin(a);
            startBeaconIfDue();
            if (curPos < cur.size()) {
                sr += beaconAmp * cur[curPos].real();
                si += beaconAmp * cur[curPos].imag();
                curPos++;
            }
            blk[q] = cf((float)(sr * kTxGain), (float)(si * kTxGain));
            sampleIdx++;
        }
        if (resample) rs.process(blk, kSps, out);
        else out.insert(out.end(), blk, blk + kSps);
    }

    void pushAudio(const int16_t* pcm, size_t n) {
        pcmQ.insert(pcmQ.end(), pcm, pcm + n);
        size_t used = 0;
        while (pcmQ.size() - used >= kChunk) { encodeWindow(&pcmQ[used]); used += kChunk; }
        pcmQ.erase(pcmQ.begin(), pcmQ.begin() + used);
    }

    void flush() {
        if (!pcmQ.empty()) {
            pcmQ.resize(kChunk, 0);
            encodeWindow(pcmQ.data());
            pcmQ.clear();
        }
        if (started) {
            const std::vector<int16_t> z(kChunk, 0);
            encodeWindow(z.data());   // the last half-overlapped frame, as the whole-signal encoder adds
            while (!frames.empty()) {   // fill the 4-frame block with all-zero info frames
                frames.emplace_back(KaalCodec::kBits, (uint8_t)0);
                if (frames.size() == (size_t)4) encodeBlock();
            }
            for (int i = 0; i < kRrcSpan; i++) emitSymbol(cf(0, 0));   // the filter tail
            if (resample) rs.finish(out);
        }
        reset();
    }
};

VoiceTx::VoiceTx() : p_(new Impl) {}
VoiceTx::~VoiceTx() { delete p_; }

void VoiceTx::configure(double sampleRate) {
    p_->outRate = sampleRate > 0 ? sampleRate : 48000.0;
    p_->resample = std::fabs(p_->outRate - kFs) > 1e-6;
    if (p_->resample) p_->rs.configure(p_->outRate);
    p_->reset();
    p_->out.clear();
    p_->head = 0;
}
void VoiceTx::pushAudio(const int16_t* pcm, size_t n) { p_->pushAudio(pcm, n); }
void VoiceTx::flush() { p_->flush(); }
size_t VoiceTx::available() const { return p_->out.size() - p_->head; }
size_t VoiceTx::popIq(std::complex<float>* out, size_t maxSamples) {
    const size_t n = std::min(maxSamples, p_->out.size() - p_->head);
    if (n) std::memcpy(out, &p_->out[p_->head], n * sizeof(cf));
    p_->head += n;
    if (p_->head == p_->out.size()) { p_->out.clear(); p_->head = 0; }
    else if (p_->head > 65536) { p_->out.erase(p_->out.begin(), p_->out.begin() + p_->head); p_->head = 0; }
    return n;
}
void VoiceTx::setBeaconMix(bool on) { p_->beaconOn = on; }
void VoiceTx::setBeacon(const BeaconFields& f, const std::string& text) {
    p_->bf = f;
    p_->btext = text;
    p_->makeSlices();
}

}  // namespace kaal
}  // namespace dect2
