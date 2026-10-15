// Radiosonde bit decoders (DFM, M10, M20): shared entry points. The decoders are in sonde_dfm.cpp, sonde_m10.cpp, sonde_m20.cpp.
#include "dect2/sonde_bits.h"

namespace dect2 {

double sondeFramePeriodS(const std::string& type) {
    if (type == "DFM") return 280.0 / 1250.0;       // frames of 280 bits at 1250 bit/s follow each other without a gap
    if (type == "LMS6") return 0.5;                 // two MK2A subframes per second
    return 1.0;                                     // M10, M20: one frame a second
}

} // namespace dect2
