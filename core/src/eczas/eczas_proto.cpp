// e-Czas PCSK225 time frame decode (see eczas_proto.h). CRC/field layout from sp5wwp/e-Czas and GUM PDF v1.0.
#include "dect2/eczas/eczas_proto.h"
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
    putBitsMsb(raw, 57, tzHours & 1, 1);        // TZ0 is the low bit of the offset: the spec table lists TZ0 TZ1 = 1 0 as +1 h, 0 1 as +2 h, 1 1 as +3 h
    putBitsMsb(raw, 58, (tzHours >> 1) & 1, 1); // TZ1
    putBitsMsb(raw, 59, (flags >> 2) & 1, 1); // LS
    putBitsMsb(raw, 60, (flags >> 1) & 1, 1); // LSS
    putBitsMsb(raw, 61, flags & 1, 1);        // TZC
    putBitsMsb(raw, 62, txState & 1, 1);        // SK0 is the low bit: 1 0 planned off for a day, 0 1 for a week, 1 1 for longer
    putBitsMsb(raw, 63, (txState >> 1) & 1, 1); // SK1
}

void readFields(const uint8_t* raw, TimePacket& out) {
    out.timeMarker = (uint8_t)bitsMsb(raw, 24, 3);
    out.periods3s = (uint32_t)bitsMsb(raw, 27, 30);
    out.tzHours = (int)bitsMsb(raw, 57, 1) | ((int)bitsMsb(raw, 58, 1) << 1);   // TZ0 is the low bit
    out.leapAnnounce = bitsMsb(raw, 59, 1) != 0;
    out.leapSubtract = bitsMsb(raw, 60, 1) != 0;
    out.dstChangeSoon = bitsMsb(raw, 61, 1) != 0;
    out.txState = (uint8_t)(bitsMsb(raw, 62, 1) | (bitsMsb(raw, 63, 1) << 1));   // SK0 is the low bit
    out.utcSec = kEpochUtc + (int64_t)out.periods3s * 3;
    out.localSec = out.utcSec + 3600 * out.tzHours;
}

} // namespace

const char* txStateText(int s) {
    switch (s & 3) {
    case 0: return "normal";
    case 1: return "planned off for a day";      // SK0 SK1 = 1 0
    case 2: return "planned off for a week";     // 0 1
    default: return "planned off for longer than a week";
    }
}

void civilFromUnix(int64_t sec, int& y, int& mo, int& d, int& h, int& mi, int& s) {
    int64_t days = sec / 86400, r = sec % 86400;
    if (r < 0) { r += 86400; days--; }
    h = (int)(r / 3600); mi = (int)(r / 60 % 60); s = (int)(r % 60);
    const int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    mo = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400 + (mo <= 2));
}

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
