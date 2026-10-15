// DRM packet mode demultiplexer for the Journaline data service (ES 201 980 clause 6.6.1), see dab_journaline.h.
#include "dect2/dab_journaline.h"
#include "dect2/drm_fec.h"

namespace dect2 {

void DrmPacketDemux::configure(int dataLen, int packetId) {
    if (dataLen != len_ || packetId != id_) { len_ = dataLen; id_ = packetId; reset(); }
}

bool DrmPacketDemux::packet(const uint8_t* p, size_t sz, std::vector<uint8_t>& out) {
    const uint32_t c = drm::crc16Bytes(p, sz - 2);
    if (c != (uint32_t)((p[sz - 2] << 8) | p[sz - 1])) { badPackets++; on_ = false; asm_.clear(); return false; }
    packets++;
    const bool first = p[0] & 0x80, last = p[0] & 0x40, ppi = p[0] & 0x08;
    const int id = (p[0] >> 4) & 3, ci = p[0] & 7;
    if (id != id_) return false;
    size_t n = sz - 3, at = 1;
    if (ppi) { n = p[1]; at = 2; if (n > sz - 4) return false; }
    if (n == 0 && first && last) { lastCi_ = ci; return false; }   // an empty packet
    if (on_ && !first && ci != ((lastCi_ + 1) & 7)) { on_ = false; asm_.clear(); }   // a packet was lost: the data unit is gone
    lastCi_ = ci;
    if (first) { asm_.clear(); on_ = true; }
    else if (!on_) return false;
    asm_.insert(asm_.end(), p + at, p + at + n);
    if (asm_.size() > 8215) { on_ = false; asm_.clear(); return false; }
    if (last) { out.swap(asm_); asm_.clear(); on_ = false; return true; }
    return false;
}

} // namespace dect2
