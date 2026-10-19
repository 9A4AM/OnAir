// SITOR teleprinter decoder (see hfdig_sitor.h).
#include "dect2/hfdig/hfdig_sitor.h"
#include <array>

namespace dect2 {
namespace {

// ITA2 letters and figures shift tables (ITU-T F.1), index = 5-bit code. 31 = LTRS, 27 = FIGS.
constexpr char kLetters[32] = {0,   'E', '\n', 'A', ' ', 'S', 'I', 'U', '\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
                               'T', 'Z',  'L',  'W', 'H', 'Y', 'P', 'Q', 'O',  'B', 'G', 0,   'M', 'X', 'V', 0};
constexpr char kFigures[32] = {0,   '3', '\n', '-', ' ', '\'', '8', '7', '\r', '$', '4', 0,   ',', '!', ':', '(',
                               '5', '"', ')',  '2', '#', '6',  '0', '1', '9',  '?', '&', 0,   '.', '/', ';', 0};
constexpr int kLtrs = 31;
constexpr int kFigs = 27;

constexpr int popcount7(unsigned w) {
    int n = 0;
    for (unsigned b = 0; b < 7; b++) n += (w >> b) & 1;
    return n;
}

// The 32 smallest four-of-seven words, in order: code5 -> word.
struct FecBTables {
    std::array<uint8_t, 32> encode{};
    std::array<int8_t, 128> decode{};
};
constexpr FecBTables buildFecB() {
    FecBTables t{};
    for (auto& d : t.decode) d = -1;
    int n = 0;
    for (unsigned w = 0; w < 128 && n < 32; w++) {
        if (popcount7(w) != 4) continue;
        t.encode[n] = static_cast<uint8_t>(w);
        t.decode[w] = static_cast<int8_t>(n);
        n++;
    }
    return t;
}
constexpr FecBTables kFecB = buildFecB();

}  // namespace

char sitorIta2Char(uint8_t code5, bool figures) {
    if (code5 > 31) return 0;
    return figures ? kFigures[code5] : kLetters[code5];
}

int sitorFecBDecode(uint8_t word7) {
    if (word7 > 127) return -1;
    return kFecB.decode[word7];
}

uint8_t sitorFecBEncode(uint8_t code5) { return kFecB.encode[code5 & 31]; }

SitorDecoder::SitorDecoder(Mode mode, int lag) : mode_(mode), lag_(lag < 1 ? 1 : lag) { history_.assign(lag_, -1); }

void SitorDecoder::setMode(Mode mode) {
    mode_ = mode;
    clear();
}

void SitorDecoder::clear() {
    history_.assign(lag_, -1);
    pushes_ = 0;
    figures_ = false;
    text_.clear();
    chars_ = 0;
    errors_ = 0;
}

void SitorDecoder::emit(uint8_t code5) {
    if (code5 == kLtrs) { figures_ = false; return; }
    if (code5 == kFigs) { figures_ = true; return; }
    char c = sitorIta2Char(code5, figures_);
    if (!c) return;
    text_.push_back(c);
    chars_++;
}

void SitorDecoder::push(uint8_t word7) {
    int code = sitorFecBDecode(word7);
    if (mode_ == Mode::FecB) {
        if (code < 0) { errors_++; return; }
        emit(static_cast<uint8_t>(code));
        return;
    }
    // FEC-A: accept when this valid word equals the one `lag` words back; then the slot is used up.
    size_t slot = static_cast<size_t>(pushes_++ % static_cast<uint64_t>(lag_));
    int back = history_[slot];
    history_[slot] = code;
    if (code < 0) { errors_++; return; }
    if (back == code) {
        emit(static_cast<uint8_t>(code));
        history_[slot] = -1;
    }
}

}  // namespace dect2
