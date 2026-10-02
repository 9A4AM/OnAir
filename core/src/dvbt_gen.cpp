#include "dect2/dvbt_gen.h"
#include "dect2/fftutil.h"
#include <cmath>
#include <cstring>

namespace dect2 {
namespace dvbt {

Generator::Generator(const Params& p, PacketSource src, unsigned seed)
    : p_(p), N_(::dect2::dvbt::fftN(p.mode)), G_(guardSamples(p.mode, p.guard)), K_(carriersK(p.mode)), src_(std::move(src)), rng_(seed), enc_(p.crHp) {
    if (!src_) {
        src_ = [this, n = 0u](uint8_t* pkt) mutable {
            pkt[0] = 0x47;
            const unsigned pid = 0x100 + (n++ % 4);
            pkt[1] = (uint8_t)(pid >> 8); pkt[2] = (uint8_t)pid; pkt[3] = 0x10 | (n & 15);
            for (int i = 4; i < 188; i++) pkt[i] = (uint8_t)rng_();
        };
    }
    tpsVal_.assign(tpsCarriers(p.mode).size(), 1.f);
    carriers_.assign(K_, cf32(0, 0));
    fft_ = new Fft(N_);
}

namespace {
struct FftHolder { };
}

void Generator::refill() {
    const int m = bitsPerCell(p_.mod);
    uint8_t ts[8 * 188], sc[8 * 188], rs[8 * 204], il[8 * 204];
    for (int i = 0; i < 8; i++) { src_(ts + i * 188); ts[i * 188] = 0x47; }
    scramble(ts, 8, sc);
    for (int i = 0; i < 8; i++) rsEncode(sc + i * 188, rs + i * 204);
    ci_.process(rs, il, sizeof il);
    std::vector<uint8_t> bits(sizeof il * 8), coded;
    for (size_t i = 0; i < sizeof il; i++) for (int j = 0; j < 8; j++) bits[i * 8 + j] = (il[i] >> (7 - j)) & 1;
    enc_.encode(bits, coded);
    codedQ_.insert(codedQ_.end(), coded.begin(), coded.end());
    const size_t blk = (size_t)126 * m;
    const size_t use = codedQ_.size() / blk * blk;
    if (use) {
        std::vector<uint8_t> in(codedQ_.begin(), codedQ_.begin() + use), words;
        bitInterleave(in, p_.mod, words);
        wordsQ_.insert(wordsQ_.end(), words.begin(), words.end());
        codedQ_.erase(codedQ_.begin(), codedQ_.begin() + use);
    }
}

void Generator::nextSymbol(std::vector<cf32>& out) {
    const int Nd = dataCarriers(p_.mode);
    while ((int)wordsQ_.size() < Nd) refill();
    std::vector<uint8_t> words(wordsQ_.begin(), wordsQ_.begin() + Nd);
    wordsQ_.erase(wordsQ_.begin(), wordsQ_.begin() + Nd);
    std::vector<cf32> cells, inter(Nd);
    mapSymbol(words, p_.mod, p_.hier, cells);
    const auto& H = symbolPermutation(p_.mode);
    if (sym_ % 2) for (int q = 0; q < Nd; q++) inter[q] = cells[H[q]];
    else for (int q = 0; q < Nd; q++) inter[H[q]] = cells[q];

    std::vector<uint8_t> roles;
    carrierRoles(p_.mode, sym_, roles);
    const auto& tpsK = tpsCarriers(p_.mode);
    const auto tps = tpsBits(p_, frame_);
    const auto& w = prbsW();
    int di = 0;
    for (int k = 0; k < K_; k++) {
        if (roles[k] == 0) carriers_[k] = inter[di++];
        else if (roles[k] == 1 || roles[k] == 2) carriers_[k] = cf32(pilotValue(k), 0);
    }
    for (size_t i = 0; i < tpsK.size(); i++) {
        if (sym_ == 0) tpsVal_[i] = w[tpsK[i]] ? -1.f : 1.f;
        else if (tps[sym_]) tpsVal_[i] = -tpsVal_[i];
        carriers_[tpsK[i]] = cf32(tpsVal_[i], 0);
    }
    // IFFT: carrier k sits at frequency index k - (K-1)/2
    std::vector<cf32> X(N_, cf32(0, 0));
    const int kc = (K_ - 1) / 2;
    for (int k = 0; k < K_; k++) X[((k - kc) % N_ + N_) % N_] = carriers_[k];
    fft_->inverse(X.data());
    // unit mean power: the unnormalised inverse has power sum|c|^2, scale by 1/sqrt(sum |c|^2 / N ... ) -> use the nominal carrier count
    const float scale = 1.f / std::sqrt((float)K_ * 1.04f);
    out.resize((size_t)N_ + G_);
    for (int i = 0; i < N_; i++) out[G_ + i] = X[i] * scale;
    for (int i = 0; i < G_; i++) out[i] = out[N_ + i];
    if (++sym_ == symbolsPerFrame()) { sym_ = 0; frame_ = (frame_ + 1) % 4; }
}

} // namespace dvbt
} // namespace dect2
