// LBJ frame decode (stub): sync, BCH and BCD field extraction to be implemented in a follow-up.
#include "dect2/lbj_proto.h"

namespace dect2::lbj {

bool tryDecodeFrame(const uint8_t* bits, size_t nBits, LbjMessage& out, double timeSec, double centreHz) {
    (void)bits;
    (void)nBits;
    (void)out;
    (void)timeSec;
    (void)centreHz;
    return false;
}

} // namespace dect2::lbj
