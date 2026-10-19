// DME (distance measuring equipment) receiver: the ground beacon answers an aircraft interrogation with pulse pairs. The
// pulses are 3.5 us wide, the two pulses of a pair are 12 us apart on the X channel and 36 us apart on the Y channel
// (ICAO Annex 10 Vol I, DME). The receiver works on the detected envelope (|IQ| or AM demodulated video) at a rate of at least
// 2 Msps: it finds the pulses with a peak-relative threshold, pairs them by spacing, classifies X and Y, measures the pair rate,
// and reads the beacon identification: the Morse ident keys the pair rate to about 1350 pairs per second, the search and track
// rate is 2700 pairs per second. Clean-room from the public text only; the ident timing is a dot of 0.1 s and a dash of 0.3 s.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace dect2::dme {

enum class Channel : uint8_t { None, X, Y };
const char* channelName(Channel c);

constexpr double kPulseWidthUs = 3.5;      // nominal width of one reply pulse
constexpr double kPulseTolUs = 1.5;        // accepted width is 3.5 +- 1.5 us
constexpr double kSpacingXUs = 12.0;       // X channel pulse spacing
constexpr double kSpacingYUs = 36.0;       // Y channel pulse spacing
constexpr double kSpacingTolUs = 1.5;      // accepted spacing is the nominal value +- 1.5 us
constexpr double kIdentRatePps = 1350.0;   // pair rate while the ident tone is keyed
constexpr double kSearchRatePps = 2700.0;  // pair rate of the normal reply stream

struct Telemetry {
    Channel channel = Channel::None;   // the channel most pairs of the last second used
    double pairRatePps = 0.0;          // pulse pairs per second over the last second
    int pairsX = 0, pairsY = 0;        // pairs counted since the start
    double levelDb = -200.0;           // envelope peak level, dB
    bool identKeyed = false;           // the ident tone is on now
    std::string ident;                 // decoded ident letters (a space between words), empty until one letter is out
};

class Receiver {
public:
    // sampleRate: envelope samples per second (at least 2e6)
    void configure(double sampleRate);
    void reset();
    // Appends the envelope samples: linear amplitude, any scale (the threshold follows the peak).
    void feed(const float* env, size_t n);

    const Telemetry& telemetry() const { return t_; }
    double timeSec() const { return sampleCount_ / (fs_ > 0 ? fs_ : 1.0); }

private:
    void onPulse(double startSec);
    void closeBin();
    void decodeLetter();
    void updateTelemetry(double now);

    double fs_ = 0.0;
    double decay_ = 0.0;          // per-sample decay of the peak follower (time constant 0.2 s)
    double peak_ = 0.0;
    bool inPulse_ = false;
    uint64_t startSample_ = 0;
    uint64_t sampleCount_ = 0;

    // pair pairing: the last accepted pulse waits for its partner
    bool havePrev_ = false;
    double prevStartSec_ = 0.0;

    // pair history for the rate and the channel (last second)
    std::deque<std::pair<double, Channel>> pairs_;

    // ident: 25 ms bins of the pair rate, keyed when the rate is near 1350 pps
    uint64_t binSamples_ = 0;
    uint64_t binEnd_ = 0;
    int binPairs_ = 0;
    bool keyed_ = false;
    int runBins_ = 0;             // length of the current keyed / unkeyed run in bins
    bool letterFlushed_ = false;
    bool wordFlushed_ = false;
    std::string code_;            // dots and dashes of the letter being received

    Telemetry t_;
};

// Morse decoding helper used by the ident: the letter for a dot-dash group, '?' when unknown.
char morseLetter(const std::string& code);

} // namespace dect2::dme
