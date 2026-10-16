# core/include/dect2/common: Small shared helpers

Public headers (#include "dect2/common/<name>.h") for this topic. Ring buffer, mini XML parser, inflate, PNG writer, text character sets, platform and time compatibility headers.

Shared helpers that replaced per-mode copies (all header-only, tested against the original implementations in tests/common/test_common_*.cpp): crc.h (CRC-16/32 engines and presets), bits.h (bit fields, popcount, parity, Gray, bit reversal), linecode.h (NRZI, Manchester), endian.h (big/little-endian load/store/put), str.h (case, trim, split, hex, JSON escape), window.h (Bessel I0 for Kaiser windows), mathconst.h (kPi, speed of light, 1/sqrt2), timecompat.h (gmTime, unixNow, steady/system seconds, UTC text).

Mode folders (core/src/<mode>/) hold one decoder each; this folder holds code shared by all of them. See ARCHITECTURE.md.
