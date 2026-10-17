// DRM signalling and message formats: FAC, SDC data entities, text message, AAC audio super frame.
#include "dect2/drm/drm_msg.h"
#include "dect2/drm/drm_fec.h"
#include "dect2/drm/drm_proto.h"
#include <mutex>
#include <set>
#include <algorithm>
#include <cstring>

namespace dect2 { namespace drm {

namespace {
struct BitWriter {
    uint8_t* p; int n = 0;
    explicit BitWriter(uint8_t* b) : p(b) {}
    void put(uint32_t v, int bits) { for (int i = bits - 1; i >= 0; i--) p[n++] = (uint8_t)((v >> i) & 1); }
};
struct BitReader {
    const uint8_t* p; int n = 0;
    explicit BitReader(const uint8_t* b) : p(b) {}
    uint32_t get(int bits) { uint32_t v = 0; for (int i = 0; i < bits; i++) v = (v << 1) | (p[n++] & 1u); return v; }
};
} // namespace

// ---------------------------------------------------------------- FAC

int facInfoBits(bool modeE) { return modeE ? 116 : 72; }

void facBuild(const FacInfo& f, uint8_t* bits) {
    BitWriter w(bits);
    w.put(f.enhancement ? 1 : 0, 1); w.put((uint32_t)f.identity, 2); w.put(f.modeE ? 1 : 0, 1);
    w.put((uint32_t)f.occupancy, 3); w.put((uint32_t)f.interleaver, 1); w.put((uint32_t)f.mscMode, 2); w.put((uint32_t)f.sdcMode, 1);
    w.put((uint32_t)f.numServicesCode, 4); w.put((uint32_t)f.reconfig, 3); w.put((uint32_t)f.toggle, 1); w.put(0, 1);
    const int sets = f.modeE ? 2 : 1;
    for (int i = 0; i < sets; i++) {
        const FacService& s = f.svc[i];
        w.put(s.id, 24); w.put((uint32_t)s.shortId, 2); w.put(s.audioCa ? 1 : 0, 1); w.put((uint32_t)s.language, 4);
        w.put(s.data ? 1 : 0, 1); w.put((uint32_t)s.descriptor, 5); w.put(s.dataCa ? 1 : 0, 1); w.put(0, 6);
    }
    // the CRC covers 64 bits (mode E: 108 bits and 4 zero bits that are not transmitted)
    uint8_t tmp[120];
    std::memcpy(tmp, bits, (size_t)w.n);
    int cn = w.n;
    if (f.modeE) { for (int i = 0; i < 4; i++) tmp[cn++] = 0; }
    w.put(crc8(tmp, (size_t)cn), 8);
}

// The parsing of FAC, SDC, the text message segments and the audio super frame, and the descriptive tables, is drm_proto (Rust).
bool facParse(const uint8_t* bits, bool modeE, FacInfo& f) { return drmp::facParse(bits, modeE, f); }

int facAudioServices(int c) { return drmp::facAudioServices(c); }
int facDataServices(int c) { return drmp::facDataServices(c); }

namespace {
// The names are made by drm_proto; hand out a stable pointer for each distinct one.
const char* intern(const std::string& s) {
    static std::mutex mu;
    static std::set<std::string> names;
    std::lock_guard<std::mutex> lk(mu);
    return names.insert(s).first->c_str();
}
}

const char* languageName(int c) { return intern(drmp::languageName(c)); }
const char* programmeTypeName(int c) { return intern(drmp::programmeTypeName(c)); }
const char* mscModeName(bool modeE, int m) { return intern(drmp::mscModeName(modeE, m)); }
const char* occupancyName(bool modeE, int o) { return intern(drmp::occupancyName(modeE, o)); }

// ---------------------------------------------------------------- SDC

int SdcAudio::rateHz() const {
    if (coding == 3) { static const int r[8] = {0, 0, 16000, 19200, 24000, 32000, 38400, 48000}; return r[rateCode & 7]; }
    static const int r[8] = {0, 12000, 0, 24000, 0, 48000, 0, 0};
    return r[rateCode & 7];
}

void sdcPutEntity(std::vector<uint8_t>& field, int type, int version, const std::vector<uint8_t>& body) {
    // header: length of the body in whole bytes after the first 4 bits (7 bits), version (1), type (4); then the first nibble of the body
    const int len = body.empty() ? 0 : (int)body.size() - 1;
    field.push_back((uint8_t)((len << 1) | (version & 1)));
    field.push_back((uint8_t)(((type & 15) << 4) | (body.empty() ? 0 : (body[0] >> 4) & 15)));
    // the nibble is carried in the second byte; the following body bytes are whole bytes
    for (size_t i = 1; i < body.size(); i++) field.push_back(body[i]);
}

std::vector<uint8_t> sdcEntityMux(const SdcMux& m) {
    std::vector<uint8_t> b;
    b.push_back((uint8_t)((((m.protA & 3) << 2) | (m.protB & 3)) << 4));
    for (int i = 0; i < m.nStreams; i++) {
        const SdcStream& s = m.stream[i];
        b.push_back((uint8_t)(s.lenA >> 4));
        b.push_back((uint8_t)(((s.lenA & 15) << 4) | ((s.lenB >> 8) & 15)));
        b.push_back((uint8_t)(s.lenB & 255));
    }
    return b;
}
std::vector<uint8_t> sdcEntityAudio(const SdcAudio& a) {
    std::vector<uint8_t> b;
    b.push_back((uint8_t)((((a.shortId & 3) << 2) | (a.streamId & 3)) << 4));
    b.push_back((uint8_t)(((a.coding & 3) << 6) | ((a.sbr & 1) << 5) | ((a.mode & 3) << 3) | (a.rateCode & 7)));
    b.push_back((uint8_t)(((a.text ? 1 : 0) << 7) | ((a.enhancement ? 1 : 0) << 6) | ((a.mps & 7) << 3)));
    for (uint8_t c : a.config) b.push_back(c);
    return b;
}
std::vector<uint8_t> sdcEntityApp(const SdcApp& a) {
    std::vector<uint8_t> b;
    b.push_back((uint8_t)((((a.shortId & 3) << 2) | (a.streamId & 3)) << 4));
    if (a.packet) {
        b.push_back((uint8_t)(0x80 | ((a.dataUnit ? 1 : 0) << 6) | ((a.packetId & 3) << 4) | (a.domain & 7)));
        b.push_back((uint8_t)a.packetLen);
    } else {
        b.push_back((uint8_t)(a.domain & 7));
    }
    for (uint8_t c : a.appData) b.push_back(c);
    return b;
}
std::vector<uint8_t> sdcEntityLabel(int shortId, const std::string& text) {
    std::vector<uint8_t> b;
    b.push_back((uint8_t)((shortId & 3) << 6));
    for (char c : text) b.push_back((uint8_t)c);
    return b;
}
std::vector<uint8_t> sdcEntityLang(int shortId, const std::string& lang, const std::string& country) {
    std::vector<uint8_t> b;
    b.push_back((uint8_t)((shortId & 3) << 6));
    for (int i = 0; i < 3; i++) b.push_back((uint8_t)(i < (int)lang.size() ? lang[(size_t)i] : '-'));
    for (int i = 0; i < 2; i++) b.push_back((uint8_t)(i < (int)country.size() ? country[(size_t)i] : '-'));
    return b;
}
std::vector<uint8_t> sdcEntityTime(const SdcTime& t) {
    uint8_t bits[40];
    BitWriter w(bits);
    w.put((uint32_t)t.mjd, 17); w.put((uint32_t)t.hour, 5); w.put((uint32_t)t.minute, 6);
    if (t.hasOffset) { w.put(0, 2); w.put(t.offsetHalfHours < 0 ? 1 : 0, 1); w.put((uint32_t)std::abs(t.offsetHalfHours), 5); }
    // the first 4 bits are the nibble of the entity header; the rest are whole bytes
    std::vector<uint8_t> b(1 + (size_t)(w.n - 4) / 8);
    b[0] = (uint8_t)((bits[0] << 7) | (bits[1] << 6) | (bits[2] << 5) | (bits[3] << 4));
    for (int i = 4; i < w.n; i++) b[1 + (size_t)(i - 4) / 8] |= (uint8_t)(bits[i] << (7 - (i - 4) % 8));
    return b;
}

int sdcParse(const uint8_t* d, int bytes, SdcInfo& out) { return drmp::sdcParse(d, bytes, out); }

void sdcBuildBits(int afsIndex, const std::vector<uint8_t>& field, int fieldBytes, std::vector<uint8_t>& bits) {
    bits.clear();
    std::vector<uint8_t> crcIn;
    for (int i = 3; i >= 0; i--) { bits.push_back((uint8_t)((afsIndex >> i) & 1)); }
    for (int i = 0; i < 4; i++) crcIn.push_back(0);
    for (int i = 3; i >= 0; i--) crcIn.push_back((uint8_t)((afsIndex >> i) & 1));
    for (int i = 0; i < fieldBytes; i++) {
        const uint8_t v = i < (int)field.size() ? field[(size_t)i] : 0;
        for (int j = 7; j >= 0; j--) { bits.push_back((v >> j) & 1); crcIn.push_back((v >> j) & 1); }
    }
    const uint32_t c = crc16(crcIn.data(), crcIn.size());
    for (int j = 15; j >= 0; j--) bits.push_back((uint8_t)((c >> j) & 1));
}

bool sdcCheckBits(const uint8_t* bits, int fieldBytes, int& afsIndex) { return drmp::sdcCheckBits(bits, fieldBytes, afsIndex); }

int dateToMjd(int y, int m, int d) { return drmp::dateToMjd(y, m, d); }
void mjdToDate(int mjd, int& y, int& m, int& d) { drmp::mjdToDate(mjd, y, m, d); }

// ---------------------------------------------------------------- text message

void TextDecoder::reset() { *this = TextDecoder(); }

bool TextDecoder::feed(const uint8_t* four) {
    changed_ = false;
    const bool marker = four[0] == 0xFF && four[1] == 0xFF && four[2] == 0xFF && four[3] == 0xFF;
    if (marker) { seg_.clear(); need_ = -1; return false; }
    if (need_ == 0) return false;                       // idle: no segment in progress (all zero units)
    seg_.insert(seg_.end(), four, four + 4);
    if (need_ < 0 && seg_.size() >= 2) {
        need_ = drmp::textSegmentNeed(seg_[0]);
    }
    if (need_ > 0 && (int)seg_.size() >= need_) { finishSegment(); need_ = 0; seg_.clear(); }
    else if (seg_.size() > 40) { need_ = 0; seg_.clear(); bad_++; }
    return changed_;
}

void TextDecoder::finishSegment() {
    const drmp::TextSeg s = drmp::textSegment(seg_.data(), seg_.size(), need_);   // the CRC-16 and the header fields
    const int n = need_ - 4;
    if (!s.crcOk) { bad_++; return; }
    ok_++;
    const bool toggle = s.toggle, last = s.last, command = s.command;
    if (command) {
        if (s.cmd == 1 && !text_.empty()) { text_.clear(); changed_ = true; }       // clear display
        return;
    }
    const int segnum = s.segnum;
    if (!haveToggle_ || toggle != toggle_) {            // a different message: start again
        haveToggle_ = true; toggle_ = toggle; got_ = 0; last_ = -1;
        for (auto& p : parts_) p.clear();
    }
    parts_[segnum].assign((const char*)seg_.data() + 2, (size_t)n);
    got_ |= (uint8_t)(1u << segnum);
    if (last) last_ = segnum;
    if (last_ >= 0 && (got_ & ((1u << (last_ + 1)) - 1)) == ((1u << (last_ + 1)) - 1)) {
        std::string t;
        for (int i = 0; i <= last_; i++) t += parts_[i];
        if (t != text_) { text_ = t; changed_ = true; }
    }
}

void TextEncoder::set(const std::string& message) {
    stream_.clear(); pos_ = 0;
    if (message.empty()) return;
    toggle_ = !toggle_;
    const bool toggle = toggle_;
    const int nseg = (int)std::min<size_t>(8, (message.size() + 15) / 16);
    for (int s = 0; s < nseg; s++) {
        const size_t off = (size_t)s * 16;
        const std::string body = message.substr(off, 16);
        std::vector<uint8_t> seg;
        const bool first = s == 0, last = s == nseg - 1;
        seg.push_back((uint8_t)((toggle ? 0x80 : 0) | (first ? 0x40 : 0) | (last ? 0x20 : 0) | ((int)body.size() - 1)));
        seg.push_back(first ? 0xF0 : (uint8_t)((s & 7) << 4));
        for (char c : body) seg.push_back((uint8_t)c);
        const uint32_t c = crc16Bytes(seg.data(), seg.size());
        seg.push_back((uint8_t)(c >> 8)); seg.push_back((uint8_t)c);
        while (seg.size() % 4) seg.push_back(0);
        for (int i = 0; i < 4; i++) stream_.push_back(0xFF);
        stream_.insert(stream_.end(), seg.begin(), seg.end());
    }
}

void TextEncoder::next(uint8_t* four) {
    if (stream_.empty()) { std::memset(four, 0, 4); return; }
    for (int i = 0; i < 4; i++) four[i] = stream_[pos_ + (size_t)i];
    pos_ += 4;
    if (pos_ >= stream_.size()) pos_ = 0;
}

// ---------------------------------------------------------------- AAC audio super frame

int aacNumFrames(bool modeE, int rateHz) { return drmp::aacNumFrames(modeE, rateHz); }
int aacHeaderBytes(int n) { return drmp::aacHeaderBytes(n); }

bool aacSuperFrameParse(const uint8_t* lf, int len, int lenA, int n, AacSuperFrame& out) { return drmp::aacSuperFrameParse(lf, len, lenA, n, out); }

bool aacSuperFrameBuild(const std::vector<std::vector<uint8_t>>& frames, const std::vector<uint8_t>& crc, int hp, int len, std::vector<uint8_t>& lf) {
    const int n = (int)frames.size();
    const int hdr = aacHeaderBytes(n);
    int total = 0;
    for (const auto& f : frames) { if ((int)f.size() < hp) return false; total += (int)f.size(); }
    if (hdr + n + total > len) return false;
    lf.assign((size_t)len, 0);
    uint8_t bits[160];
    int nb = 0;
    int acc = 0;
    for (int i = 0; i < n - 1; i++) {
        acc += (int)frames[(size_t)i].size();
        for (int j = 11; j >= 0; j--) bits[nb++] = (uint8_t)((acc >> j) & 1);
    }
    if (n == 10) for (int j = 0; j < 4; j++) bits[nb++] = 0;
    for (int i = 0; i < nb; i++) lf[(size_t)i / 8] |= (uint8_t)(bits[i] << (7 - i % 8));
    size_t hpPos = (size_t)hdr, lpPos = (size_t)hdr + (size_t)n * (size_t)(hp + 1);
    for (int i = 0; i < n; i++) {
        const auto& f = frames[(size_t)i];
        std::copy(f.begin(), f.begin() + hp, lf.begin() + (ptrdiff_t)hpPos); hpPos += (size_t)hp;
        lf[hpPos++] = i < (int)crc.size() ? crc[(size_t)i] : 0;
        std::copy(f.begin() + hp, f.end(), lf.begin() + (ptrdiff_t)lpPos); lpPos += f.size() - (size_t)hp;
    }
    return true;
}

}} // namespace dect2::drm
