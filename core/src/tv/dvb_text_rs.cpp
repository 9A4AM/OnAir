// dvbText() on top of the Rust crate rust/onair_dvbsi (DECT2_RUST_DVBSI): the prefix dispatch, ISO/IEC 6937 and the clean-up are Rust; the character set tables
// the system provides come back through convertCharset() (dvb_text.cpp), registered with the crate before main. Empty when the crate is not built.
#ifdef DECT2_RUST_DVBSI
#include "dect2/tv/dvb_text.h"
#include "dect2/rs/dvbsi.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace dect2::dvbsi {

namespace {
// The conversion routine the crate calls: the whole of convertCharset(), copied into the crate's buffer.
size_t convertCb(const uint8_t* charset, const uint8_t* src, size_t n, uint8_t* out, size_t cap) {
    const std::string s = convertCharset((const char*)charset, src, (int)n);
    const size_t k = std::min(s.size(), cap);
    memcpy(out, s.data(), k);
    return k;
}
struct Register { Register() { onair_dvbsi_set_convert(convertCb); } } registerConvert;
} // namespace

std::string dvbText(const uint8_t* p, int n, bool gbDefault) {
    if (n <= 0) return {};
    std::vector<uint8_t> b((size_t)n * 4 + 16);
    const size_t len = onair_dvbsi_dvb_text(p, (size_t)n, gbDefault, b.data(), b.size());
    return len > b.size() ? std::string() : std::string((const char*)b.data(), len);
}

} // namespace dect2::dvbsi
#endif
