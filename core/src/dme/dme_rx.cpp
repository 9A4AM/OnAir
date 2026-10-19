// DME receiver: envelope pulses -> pulse pairs (X 12 us, Y 36 us) -> pair rate, channel and the Morse ident. See dme_rx.h.
#include "dect2/dme/dme_rx.h"

#include <algorithm>
#include <cmath>

namespace dect2::dme {

namespace {
constexpr double kBinSec = 0.025;        // ident bin: the pair rate is measured in 25 ms steps
constexpr double kDashSec = 0.2;         // an on-run longer than this is a dash (dot 0.1 s, dash 0.3 s)
constexpr double kLetterGapSec = 0.2;    // an off-run this long ends a letter
constexpr double kWordGapSec = 0.5;      // an off-run this long ends a word
constexpr double kKeyLoPps = 900.0;      // the ident is keyed when the bin rate is between these two
constexpr double kKeyHiPps = 1800.0;
constexpr size_t kIdentMax = 40;
} // namespace

const char* channelName(Channel c) {
    switch (c) {
    case Channel::X: return "X";
    case Channel::Y: return "Y";
    default: return "none";
    }
}

char morseLetter(const std::string& code) {
    static const struct { const char* code; char ch; } kTable[] = {
        {".-", 'A'},     {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'},   {".", 'E'},     {"..-.", 'F'}, {"--.", 'G'},
        {"....", 'H'},   {"..", 'I'},   {".---", 'J'}, {"-.-", 'K'},   {".-..", 'L'},  {"--", 'M'},    {"-.", 'N'},
        {"---", 'O'},    {".--.", 'P'}, {"--.-", 'Q'}, {".-.", 'R'},   {"...", 'S'},   {"-", 'T'},     {"..-", 'U'},
        {"...-", 'V'},   {".--", 'W'},  {"-..-", 'X'}, {"-.--", 'Y'},  {"--..", 'Z'},  {"-----", '0'}, {".----", '1'},
        {"..---", '2'},  {"...--", '3'}, {"....-", '4'}, {".....", '5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'},
        {"----.", '9'},
    };
    for (const auto& e : kTable)
        if (code == e.code) return e.ch;
    return '?';
}

void Receiver::configure(double sampleRate) {
    fs_ = sampleRate;
    decay_ = fs_ > 0 ? std::exp(-1.0 / (0.2 * fs_)) : 0.0;    // peak follower, time constant 0.2 s
    binSamples_ = fs_ > 0 ? static_cast<uint64_t>(std::llround(kBinSec * fs_)) : 0;
    reset();
}

void Receiver::reset() {
    peak_ = 0.0;
    inPulse_ = false;
    startSample_ = 0;
    sampleCount_ = 0;
    havePrev_ = false;
    prevStartSec_ = 0.0;
    pairs_.clear();
    binEnd_ = binSamples_;
    binPairs_ = 0;
    keyed_ = false;
    runBins_ = 0;
    letterFlushed_ = false;
    wordFlushed_ = false;
    code_.clear();
    t_ = Telemetry();
}

void Receiver::feed(const float* env, size_t n) {
    if (fs_ <= 0.0 || binSamples_ == 0) return;
    for (size_t k = 0; k < n; k++) {
        const double x = env[k];
        peak_ = std::max(x, peak_ * decay_);
        const bool above = peak_ > 0.0 && x > 0.5 * peak_;     // a pulse is the envelope above half its recent peak
        if (!inPulse_ && above) {
            inPulse_ = true;
            startSample_ = sampleCount_;
        } else if (inPulse_ && !above) {
            inPulse_ = false;
            const double widthUs = (double)(sampleCount_ - startSample_) * 1e6 / fs_;
            if (std::fabs(widthUs - kPulseWidthUs) <= kPulseTolUs) onPulse(startSample_ / fs_);
        }
        sampleCount_++;
        if (sampleCount_ >= binEnd_) {
            closeBin();
            binEnd_ += binSamples_;
        }
    }
    t_.levelDb = 20.0 * std::log10(peak_ + 1e-12);
}

void Receiver::onPulse(double startSec) {
    if (havePrev_) {
        const double dtUs = (startSec - prevStartSec_) * 1e6;
        Channel ch = Channel::None;
        if (std::fabs(dtUs - kSpacingXUs) <= kSpacingTolUs) ch = Channel::X;
        else if (std::fabs(dtUs - kSpacingYUs) <= kSpacingTolUs) ch = Channel::Y;
        if (ch != Channel::None) {
            pairs_.emplace_back(startSec, ch);
            if (ch == Channel::X) t_.pairsX++;
            else t_.pairsY++;
            binPairs_++;
            havePrev_ = false;          // the second pulse is used up: the next pulse starts a new pair
            return;
        }
    }
    havePrev_ = true;
    prevStartSec_ = startSec;
}

void Receiver::closeBin() {
    const double rate = binPairs_ / kBinSec;
    binPairs_ = 0;
    const bool on = rate >= kKeyLoPps && rate <= kKeyHiPps;
    if (on == keyed_) {
        runBins_++;
    } else {
        if (keyed_) {                    // an on-run just ended: a dot or a dash
            const double dur = runBins_ * kBinSec;
            code_ += dur < kDashSec ? '.' : '-';
        }
        keyed_ = on;
        runBins_ = 1;
        letterFlushed_ = false;
        wordFlushed_ = false;
    }
    if (!keyed_) {                       // the off-run: a letter gap ends the letter, a longer one the word
        const double off = runBins_ * kBinSec;
        if (!code_.empty() && !letterFlushed_ && off >= kLetterGapSec) {
            decodeLetter();
            letterFlushed_ = true;
        }
        if (!wordFlushed_ && off >= kWordGapSec) {
            if (!t_.ident.empty() && t_.ident.back() != ' ') t_.ident += ' ';
            wordFlushed_ = true;
        }
    }
    t_.identKeyed = keyed_;
    updateTelemetry(timeSec());
}

void Receiver::decodeLetter() {
    t_.ident += morseLetter(code_);
    code_.clear();
    if (t_.ident.size() > kIdentMax) t_.ident.erase(0, t_.ident.size() - kIdentMax);
}

void Receiver::updateTelemetry(double now) {
    while (!pairs_.empty() && pairs_.front().first < now - 1.0) pairs_.pop_front();
    int nx = 0, ny = 0;
    for (const auto& p : pairs_) {
        if (p.second == Channel::X) nx++;
        else ny++;
    }
    const double window = std::max(0.05, std::min(1.0, now));
    t_.pairRatePps = static_cast<double>(pairs_.size()) / window;
    if (nx == 0 && ny == 0) t_.channel = Channel::None;
    else t_.channel = nx >= ny ? Channel::X : Channel::Y;
}

} // namespace dect2::dme
