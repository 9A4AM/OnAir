// Olivia and Contestia (hfdig_kbd.h): the modem of Pawel Jalocha as fldigi uses it (src/include/jalocha/pj_mfsk.h, GPL), written again here.
//   symbol    2^n tones two FFT bins of Ns = 2^(n + 7 - log2(BW / 125)) points apart (8 kHz), a Hann shaped symbol of Ns samples every Ns / 2
//   block     n characters of 7 bits (Olivia: Walsh-Hadamard word of 64 symbols; Contestia: 6 bits, 32 symbols), the word of each character
//             scrambled with a 64 bit code and rotated into the n bit planes of the Gray coded tone numbers
//   receiver  an FFT every quarter symbol pair (two slices per symbol), a decoder for each slice and each of 2 * margin + 1 frequency
//             offsets decoding the last block with the fast Hadamard transform; the time phase and offset with the strongest
//             integrated signal win, and the block that was decoded four blocks ago at that phase is the output (Jalocha's trick: the
//             decision waits until the phase has settled)
#include "hfdig_kbd_int.h"
#include <cstring>
#include <cstdlib>

namespace dect2 {
namespace kbd {
namespace {

constexpr uint64_t kScrOlivia = 0xE257E6D0291574ECULL;
constexpr uint64_t kScrContestia = 0xEDB88320ULL;

inline int log2i(unsigned x) { int y = 0; for (; x > 1; x >>= 1) y++; return y; }

struct Params {
    bool contestia;
    int tones, bw, bits, symLen, sep, bpc, spb;   // bpc: bits per character, spb: symbols per block
    int firstCarrier;
    double centre;
    Params(const KbdConfig& c, double centreHz) {
        contestia = c.family == KbdContestia;
        tones = 2; while (tones < c.tones && tones < 256) tones *= 2;
        bits = log2i(tones);
        int b = std::clamp(c.bandwidth, 125, 2000);
        b = 125 << log2i((unsigned)(b / 125));
        bw = b;
        symLen = 1 << std::max(5, bits + 7 - log2i((unsigned)(bw / 125)));
        sep = symLen / 2;
        bpc = contestia ? 6 : 7;
        spb = 1 << (bpc - 1);
        centre = centreHz;
        const double fcOffset = contestia ? bw / 2.0 : bw * (1.0 - 0.5 / tones) / 2.0;
        const double mult = (centreHz - fcOffset) / 500.0;
        firstCarrier = (int)((symLen / 16) * mult) + 1;
        if (firstCarrier < 1) firstCarrier = 1;
    }
};

// The character -> Hadamard row mapping of the transmitter and the decoder's inverse
inline int contestiaCode(int ch) {
    if (ch >= 'a' && ch <= 'z') ch += 'A' - 'a';
    if (ch == ' ') return 59;
    if (ch == '\r') return 60;
    if (ch == '\n') return 0;
    if (ch >= 33 && ch <= 90) return ch - 32;
    if (ch == 8) return 61;
    if (ch == 0) return 0;
    return '?' - 32;
}

// ------------------------------------------------------------------ transmitter
class OliviaMod : public Modulator {
public:
    OliviaMod(const KbdConfig& c, double centre) : p_(c, centre), cosT_(p_.symLen), tap_(p_.symLen, 0.f), blk_(p_.spb), fhtBuf_(p_.spb) {
        for (int i = 0; i < p_.symLen; i++) cosT_[i] = (float)std::cos(2 * kPi * i / p_.symLen);
        wrap_ = p_.symLen - 1;
    }
    void next(std::vector<float>& out, TextSrc& src) override {
        if (sym_ == 0) {
            uint8_t in[8] = {0};
            for (int i = 0; i < p_.bits; i++) in[i] = first_ && i == 0 ? 0 : (uint8_t)src.next();
            first_ = false;
            encodeBlock(in);
        }
        send(blk_[sym_]);
        if (++sym_ >= p_.spb) sym_ = 0;
        for (int i = 0; i < p_.sep; i++) { out.push_back(tap_[tp_] * 0.5f); tap_[tp_] = 0; tp_ = (tp_ + 1) & wrap_; }
    }
private:
    void encodeBlock(const uint8_t* in) {
        std::fill(blk_.begin(), blk_.end(), (uint8_t)0);
        const int nShift = p_.contestia ? 5 : 13;
        const uint64_t code = p_.contestia ? kScrContestia : kScrOlivia;
        for (int fb = 0; fb < p_.bits; fb++) {
            int ch = in[fb];
            ch = p_.contestia ? contestiaCode(ch) : (ch & ((p_.spb << 1) - 1));
            std::fill(fhtBuf_.begin(), fhtBuf_.end(), (int8_t)0);
            if (ch < p_.spb) fhtBuf_[ch] = 1; else fhtBuf_[ch - p_.spb] = -1;
            ifht(fhtBuf_.data(), p_.spb);
            int cb = (fb * nShift) & (p_.spb - 1);
            for (int t = 0; t < p_.spb; t++) {
                if (code >> cb & 1) fhtBuf_[t] = (int8_t)-fhtBuf_[t];
                cb = (cb + 1) & (p_.spb - 1);
            }
            int rot = 0;
            for (int t = 0; t < p_.spb; t++) {
                if (fhtBuf_[t] < 0) { int bit = fb + rot; if (bit >= p_.bits) bit -= p_.bits; blk_[t] |= (uint8_t)(1u << bit); }
                if (++rot >= p_.bits) rot -= p_.bits;
            }
        }
    }
    void send(unsigned sym) {
        sym = grayCode(sym);
        const int freq = p_.firstCarrier + 2 * (int)sym;
        int timeShift = p_.sep / 2 - p_.symLen / 2;
        phase_ = (phase_ + freq * timeShift) & wrap_;
        addSymbol(freq, phase_);
        timeShift = p_.sep / 2 + p_.symLen / 2;
        phase_ = (phase_ + freq * timeShift) & wrap_;
        int diff = p_.symLen / 4;
        rnd_ = rnd_ * 1103515245u + 12345u;
        if (rnd_ >> 16 & 1) diff = -diff;
        phase_ = (phase_ + diff) & wrap_;
    }
    void addSymbol(int freq, int phase) {
        int tp = tp_;
        for (int t = 0; t < p_.symLen; t++) {
            tap_[tp] += cosT_[phase & wrap_] * (1.0f - cosT_[t]);
            phase = (phase + freq) & wrap_;
            tp = (tp + 1) & wrap_;
        }
    }
    Params p_;
    std::vector<float> cosT_, tap_;
    std::vector<uint8_t> blk_;
    std::vector<int8_t> fhtBuf_;
    int sym_ = 0, tp_ = 0, wrap_ = 0, phase_ = 0;
    bool first_ = true;
    uint32_t rnd_ = 12345;
};

// ------------------------------------------------------------------ receiver
class OliviaEngine : public Engine {
public:
    OliviaEngine(const KbdConfig& c) : cfg_(c) { setCentre(c.centreHz); }
    double bandwidth() const override { return p_->bw; }
    void setCentre(double hz) override { build(hz); }
    void reset() override { build(centre_); }

    void feed(const float* x, size_t n) override {
        for (size_t i = 0; i < n; i++) {
            in_.push_back(x[i]);
            if ((int)in_.size() >= P().sep) { symbol(in_.data()); in_.clear(); }
        }
    }

private:
    const Params& P() const { return *p_; }
    void build(double centre) {
        centre_ = centre;
        p_ = std::make_unique<Params>(cfg_, centre);
        const Params& p = *p_;
        in_.clear(); in_.reserve(p.sep);
        const int N = p.symLen;
        fft_.init(N);
        tap_.assign(N, 0.f); tapPtr_ = 0;
        shape_.resize(N);
        for (int t = 0; t < N; t++) shape_[t] = (float)((1.0 - std::cos(2 * kPi * t / N)) / N);
        buf_.assign(N, cf());
        margin_ = std::min(8, p.firstCarrier);
        width_ = p.tones * 2 - 1 + 2 * margin_;
        nOff_ = 2 * margin_ + 1;
        for (auto& e : en_) e.assign(width_, 0.f);
        nPhases_ = 2 * p.spb;
        decIn_.assign((size_t)2 * nOff_ * p.spb * p.bits, 0.f);
        decPtr_.assign((size_t)2 * nOff_, 0);
        pipe_.assign((size_t)nPhases_ * kInteg * nOff_, 0);
        pipePtr_.assign(nPhases_, 0);
        sig_.assign((size_t)nPhases_ * nOff_, 0.f);
        noi_.assign((size_t)nPhases_ * nOff_, 0.f);
        phase_ = 0; bestSig_ = 0; bestPhase_ = 0; bestOff_ = 0; snr_ = 0;
        fhtBuf_.assign(p.spb, 0.f);
        locked = false; snrDb = 0; offsetHz = 0; bad_ = 0;
        tf_ = TextFilter();
    }

    void symbol(const float* x) {
        const Params& p = *p_;
        const int N = p.symLen, q = N / 4, wrap = N - 1;
        int ip = 0;
        for (int i = 0; i < q; i++) { tap_[tapPtr_] = x[ip++]; tapPtr_ = (tapPtr_ + 1) & wrap; }
        for (int t = 0; t < N; t++) { buf_[t] = cf(tap_[tapPtr_] * shape_[t], 0); tapPtr_ = (tapPtr_ + 1) & wrap; }
        for (int i = 0; i < q; i++) { tap_[tapPtr_] = x[ip++]; tapPtr_ = (tapPtr_ + 1) & wrap; }
        for (int t = 0; t < N; t++) { buf_[t] = cf(buf_[t].real(), tap_[tapPtr_] * shape_[t]); tapPtr_ = (tapPtr_ + 1) & wrap; }
        fft_.run(buf_.data());
        // two real transforms in one: slice 0 in the real part, slice 1 in the imaginary part
        for (int i = 0; i < width_; i++) {
            const int k = p.firstCarrier - margin_ + i;
            if (k <= 0 || k >= N / 2) { en_[0][i] = en_[1][i] = 0; continue; }
            const cf a = buf_[k], b = std::conj(buf_[N - k]);
            const cf x0 = (a + b) * 0.5f, x1 = (a - b) * cf(0, -0.5f);
            en_[0][i] = std::norm(x0);
            en_[1][i] = std::norm(x1);
        }
        const int bits = p.bits, spb = p.spb;
        for (int sl = 0; sl < 2; sl++) {
            float* noiseP = &noi_[(size_t)phase_ * nOff_];
            float* sigP = &sig_[(size_t)phase_ * nOff_];
            uint64_t* blockP = &pipe_[((size_t)phase_ * kInteg + pipePtr_[phase_]) * nOff_];
            int bestSliceOff = 0;
            float bestSliceSig = 0;
            for (int off = 0; off < nOff_; off++) {
                const int di = sl * nOff_ + off;
                float symb[8];
                softDecode(symb, sl, off - nOff_ / 2);
                float* inb = &decIn_[(size_t)di * spb * bits];
                int& ptr = decPtr_[di];
                for (int b = 0; b < bits; b++) inb[ptr + b] = symb[b];
                ptr += bits;
                if (ptr >= spb * bits) ptr -= spb * bits;
                float signal, noise;
                uint64_t packed = decodeBlock(inb, ptr, signal, noise);
                blockP[off] = packed;
                noiseP[off] += (noise - noiseP[off]) * (1.0f / kInteg);
                sigP[off] += (signal - sigP[off]) * (1.0f / kInteg);
                if (sigP[off] > bestSliceSig) { bestSliceSig = sigP[off]; bestSliceOff = off; }
            }
            pipePtr_[phase_] = (pipePtr_[phase_] + 1) % kInteg;
            if (phase_ == bestPhase_) { bestSig_ = bestSliceSig; bestOff_ = bestSliceOff; }
            else if (bestSliceSig > bestSig_) { bestSig_ = bestSliceSig; bestPhase_ = phase_; bestOff_ = bestSliceOff; }
            int dist = phase_ - bestPhase_;
            if (dist < 0) dist += nPhases_;
            if (dist == nPhases_ / 2) {
                const float bn = std::sqrt(std::max(0.f, noi_[(size_t)phase_ * nOff_ + bestOff_]));
                snr_ = bn > 0 ? bestSig_ / bn : 0;
                locked = snr_ >= thr_;
                snrDb = snr_ > 0 ? 20.f * std::log10(snr_) : 0.f;
                offsetHz = (bestOff_ - nOff_ / 2) * kFs / N;
                if (locked) {
                    uint64_t blk = pipe_[((size_t)bestPhase_ * kInteg + pipePtr_[bestPhase_]) * nOff_ + bestOff_];
                    for (int b = 0; b < bits; b++) { const int ch = (int)(blk & 0xFF); blk >>= 8; if (ch) tf_.put(out, ch); }
                }
                if (snr_ > 100) snr_ = 0;
            }
            if (++phase_ >= nPhases_) phase_ -= nPhases_;
        }
    }

    void softDecode(float* sym, int slice, int freqOff) const {
        const Params& p = *p_;
        const float* e = en_[slice].data() + margin_ + freqOff;
        for (int b = 0; b < p.bits; b++) sym[b] = 0;
        float total = 0;
        for (int i = 0; i < p.tones; i++) {
            const unsigned s = binaryCode((unsigned)i);
            float v = e[2 * i];
            v *= v;
            total += v;
            for (int b = 0; b < p.bits; b++) { if (s >> b & 1) sym[b] -= v; else sym[b] += v; }
        }
        if (total > 0) for (int b = 0; b < p.bits; b++) sym[b] /= total;
    }

    // decodes the characters of the block ending at `ptr` (the oldest symbol); returns them packed, one per byte
    uint64_t decodeBlock(const float* inb, int ptr0, float& signalOut, float& noiseOut) {
        const Params& p = *p_;
        const int spb = p.spb, bits = p.bits, len = spb * bits;
        const int nShift = p.contestia ? 5 : 13;
        const uint64_t code = p.contestia ? kScrContestia : kScrOlivia;
        float signal = 0, noise = 0;
        uint64_t packed = 0;
        for (int fb = bits - 1; fb >= 0; fb--) {
            int ptr = ptr0, rot = fb, cb = (fb * nShift) & (spb - 1);
            for (int t = 0; t < spb; t++) {
                float bit = inb[ptr + rot];
                if (code >> cb & 1) bit = -bit;
                fhtBuf_[t] = bit;
                cb = (cb + 1) & (spb - 1);
                if (++rot >= bits) rot -= bits;
                ptr += bits; if (ptr >= len) ptr -= len;
            }
            fht(fhtBuf_.data(), spb);
            float peak = 0, sq = 0;
            int pos = 0;
            for (int t = 0; t < spb; t++) {
                const float s = fhtBuf_[t];
                sq += s * s;
                if (std::fabs(s) > std::fabs(peak)) { peak = s; pos = t; }
            }
            int ch = pos;
            if (peak < 0) ch += spb;
            sq -= peak * peak;
            if (p.contestia && ch > 0) { ch = ch == 59 ? ' ' : ch == 60 ? '\r' : ch == 61 ? 8 : ch + 32; }
            packed = (packed << 8) | (uint64_t)(ch & 0xFF);
            noise += sq / (spb - 1);
            signal += std::fabs(peak);
        }
        signalOut = signal / bits;
        noiseOut = noise / bits;
        return packed;
    }

    static constexpr int kInteg = 4;
    float thr_ = 4.0f;   // fldigi: 3.0; noise alone locked now and then at that level (checked over minutes of noise in the tests)
    KbdConfig cfg_;
    double centre_ = 1500;
    std::unique_ptr<Params> p_;
    std::vector<float> in_, tap_, shape_, en_[2], decIn_, sig_, noi_, fhtBuf_;
    std::vector<cf> buf_;
    Fft fft_;
    int tapPtr_ = 0, margin_ = 8, width_ = 0, nOff_ = 17, nPhases_ = 0, phase_ = 0, bestPhase_ = 0, bestOff_ = 0, bad_ = 0;
    float bestSig_ = 0, snr_ = 0;
    std::vector<int> decPtr_, pipePtr_;
    std::vector<uint64_t> pipe_;
    TextFilter tf_;
};

} // namespace

std::unique_ptr<Engine> makeOliviaEngine(const KbdConfig& c) { return std::make_unique<OliviaEngine>(c); }
std::unique_ptr<Modulator> makeOliviaMod(const KbdConfig& c, double centreHz) { return std::make_unique<OliviaMod>(c, centreHz); }

} // namespace kbd
} // namespace dect2
