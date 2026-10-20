// KAWS ARQ. See kaws_arq.h.
#include "dect2/kaws/kaws_arq.h"
#include <algorithm>

namespace dect2::kaws {

bool ArqSender::start(uint8_t msgId, const Bits& message) {
    if (profile_ < 0 || profile_ >= kNumProfiles || kProfiles[profile_].K <= kBlockOverhead || msgId > 15) return false;
    std::vector<Bits> b = splitMessage(kProfiles[profile_].K, msgId, message);
    if (b.empty()) return false;
    msgId_ = msgId;
    message_ = message;
    blocks_ = b;
    acked_.assign(b.size(), false);
    attempts_.assign(b.size(), 0);
    active_ = true;
    return true;
}

std::vector<int> ArqSender::missing() const {
    std::vector<int> m;
    for (size_t i = 0; i < acked_.size(); i++)
        if (!acked_[i]) m.push_back((int)i);
    return m;
}

bool ArqSender::failed() const {
    if (!active_) return false;
    for (int i : missing())
        if (attempts_[(size_t)i] >= kMaxAttempts) return true;
    return false;
}

std::vector<Bits> ArqSender::nextBlocks() {
    std::vector<Bits> out;
    if (!active_ || failed()) return out;
    int cap = std::max(1, blocksPerSlot(profile_));
    for (int i : missing()) {
        if ((int)out.size() >= cap) break;
        out.push_back(blocks_[(size_t)i]);
        attempts_[(size_t)i]++;
    }
    return out;
}

bool ArqSender::onAck(const Ack& ack, bool follow) {
    if (!active_ || ack.msgId != msgId_) return false;
    for (size_t i = 0; i < acked_.size() && i < 16; i++)
        if ((ack.bitmap >> i) & 1u) acked_[i] = true;
    if (follow && ack.suggestedProfile != profile_ && ack.suggestedProfile >= 0 && ack.suggestedProfile <= 5 && !done()) {
        // the profile changes; blocks of the new size are cut from the same message and the acknowledgements restart (a bitmap refers to the old block size)
        profile_ = ack.suggestedProfile;
        rebuild();
    }
    return true;
}

void ArqSender::rebuild() {
    std::vector<Bits> b = splitMessage(kProfiles[profile_].K, msgId_, message_);
    if (b.empty()) return;   // would not fit in 16 blocks: keep going with the old profile's blocks is impossible, so stay inactive
    blocks_ = b;
    acked_.assign(b.size(), false);
    attempts_.assign(b.size(), 0);
}

bool ArqReceiver::onBlock(int K, const Bits& block) {
    uint8_t id, idx;
    Bits pl;
    if (!parseBlock(K, block, id, idx, pl)) return false;
    Msg& m = rx_[id];
    if (m.K != K) { m.K = K; m.payloads.clear(); }   // a profile change restarts the message at the new block size
    m.payloads[idx] = pl;
    return true;
}

bool ArqReceiver::complete(uint8_t id) const {
    auto it = rx_.find(id);
    if (it == rx_.end()) return false;
    const Msg& m = it->second;
    auto b0 = m.payloads.find(0);
    if (b0 == m.payloads.end()) return false;
    size_t bits = expectedMessageBits(b0->second);
    if (!bits) return false;
    size_t usable = (size_t)(m.K - kBlockOverhead);
    size_t need = (bits + usable - 1) / usable;
    for (size_t i = 0; i < need; i++)
        if (!m.payloads.count((int)i)) return false;
    return true;
}

Bits ArqReceiver::message(uint8_t id) const {
    Bits out;
    if (!complete(id)) return out;
    const Msg& m = rx_.at(id);
    size_t bits = expectedMessageBits(m.payloads.at(0));
    for (auto& kv : m.payloads) out.insert(out.end(), kv.second.begin(), kv.second.end());
    out.resize(bits);
    return out;
}

Ack ArqReceiver::makeAck(uint8_t id, double snrDb) const {
    Ack a;
    a.from = call_;
    a.msgId = id;
    auto it = rx_.find(id);
    if (it != rx_.end())
        for (auto& kv : it->second.payloads)
            if (kv.first < 32) a.bitmap |= 1u << kv.first;
    a.snrDb = (int)std::lround(std::max(-35.0, std::min(28.0, snrDb)));
    a.suggestedProfile = suggestProfile(snrDb);
    return a;
}

}  // namespace dect2::kaws
