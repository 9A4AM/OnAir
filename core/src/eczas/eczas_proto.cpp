// e-Czas PCSK225 time frame decode (see eczas_proto.h). CRC/field layout from sp5wwp/e-Czas and GUM PDF v1.0.
#include "dect2/eczas_proto.h"
#include <cstring>

namespace dect2::eczas {

namespace {

constexpr uint8_t kScramble[5] = {0x0A, 0x47, 0x55, 0x4D, 0x2B}; // "\nGUM+"

uint64_t bitsMsb(const uint8_t* f, int from, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) {
        const int bit = from + i;
        v = (v << 1) | ((f[bit / 8] >> (7 - bit % 8)) & 1);
    }
    return v;
}

void putBitsMsb(uint8_t* f, int from, uint64_t v, int n) {
    for (int i = 0; i < n; i++) {
        const int bit = from + i;
        const int b = (int)((v >> (n - 1 - i)) & 1);
        if (b) f[bit / 8] |= (uint8_t)(1 << (7 - bit % 8));
        else f[bit / 8] &= (uint8_t)~(1 << (7 - bit % 8));
    }
}

void packPayload(uint32_t periods3s, int tzHours, uint8_t flags, uint8_t txState, uint8_t raw[12]) {
    std::memset(raw, 0, 12);
    raw[0] = 0x55;
    raw[1] = 0x55;
    raw[2] = kTimeHeader;
    putBitsMsb(raw, 24, kTimeMarker & 7, 3);
    putBitsMsb(raw, 27, periods3s & 0x3FFFFFFFu, 30);
    putBitsMsb(raw, 57, tzHours & 3, 2);
    putBitsMsb(raw, 59, (flags >> 2) & 1, 1); // LS
    putBitsMsb(raw, 60, (flags >> 1) & 1, 1); // LSS
    putBitsMsb(raw, 61, flags & 1, 1);        // TZC
    putBitsMsb(raw, 62, txState & 3, 2);
}

void readFields(const uint8_t* raw, TimePacket& out) {
    out.timeMarker = (uint8_t)bitsMsb(raw, 24, 3);
    out.periods3s = (uint32_t)bitsMsb(raw, 27, 30);
    out.tzHours = (int)bitsMsb(raw, 57, 2);
    out.leapAnnounce = bitsMsb(raw, 59, 1) != 0;
    out.leapSubtract = bitsMsb(raw, 60, 1) != 0;
    out.dstChangeSoon = bitsMsb(raw, 61, 1) != 0;
    out.txState = (uint8_t)bitsMsb(raw, 62, 2);
    out.utcSec = kEpochUtc + (int64_t)out.periods3s * 3 + 3600 * out.tzHours;
}

} // namespace

uint8_t crc8Poly07(const uint8_t* in, int len) {
    uint16_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= in[i];
        for (int j = 0; j < 8; j++) {
            crc <<= 1;
            if (crc & 0x100) crc = (crc ^ 0x07) & 0xFF;
        }
    }
    return (uint8_t)(crc & 0xFF);
}

bool decodeTimeFrame(const uint8_t frame[kFrameBytes], TimePacket& out) {
    out = {};
    if (frame[0] != 0x55 || frame[1] != 0x55) {
        out.status = DecodeStatus::TooShort;
        return false;
    }
    if (frame[2] != kTimeHeader) {
        out.status = DecodeStatus::WrongType;
        return false;
    }
    const uint8_t* data = frame + 3;
    if (crc8Poly07(data, 5) != frame[11]) {
        out.status = DecodeStatus::BadCrc;
        return false;
    }
    uint8_t raw[12];
    std::memcpy(raw, frame, 12);
    for (int i = 0; i < 5; i++) raw[3 + i] ^= kScramble[i];
    readFields(raw, out);
    if (out.timeMarker != kTimeMarker) {
        out.status = DecodeStatus::WrongType;
        return false;
    }
    out.status = DecodeStatus::CrcOk;
    return true;
}

void encodeTimeFrameForTest(uint32_t periods3s, int tzHours, uint8_t flags, uint8_t txState, uint8_t out[kFrameBytes]) {
    packPayload(periods3s, tzHours, flags, txState, out);
    uint8_t* data = out + 3;
    for (int i = 0; i < 5; i++) data[i] ^= kScramble[i];
    out[11] = crc8Poly07(data, 5);
}

} // namespace dect2::eczas
