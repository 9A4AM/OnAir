// DGPS differential beacons (ITU-R M.823): MSK at 200 bit/s carrying RTCM SC-104 messages. Receive only.
// Clean-room from the public spec. Demodulation works on a block of baseband IQ with the beacon at 0 Hz after tuning.
#pragma once
#include <complex>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace dect2::dgps {

constexpr double kBitRate = 200.0;       // bit/s
constexpr double kDeviationHz = 50.0;    // MSK: the tones sit at the carrier +/- 50 Hz (modulation index 0.5)
constexpr int kWordBits = 30;            // RTCM SC-104 word: 24 data bits and 6 parity bits

// The two words that open an RTCM message.
struct Header {
    int type = 0;        // message type, 6 bits
    int stationId = 0;   // reference station ID, 10 bits
    int zcount = 0;      // modified Z-count, 13 bits (0.6 s units)
    int seq = 0;         // frame sequence number, 3 bits
    int length = 0;      // message length in words, 5 bits (as sent, not checked)
    int health = 0;      // station health, 3 bits (raw value)
};

struct Stats {
    double carrierOffsetHz = 0;   // mean instantaneous frequency of the last block, from the nominal carrier
    double levelDb = -200;        // mean power of the last block, dB relative to full scale (1.0)
    int framesOk = 0;             // headers whose preamble and parity hold (both header words)
    std::set<int> typesSeen;      // message types seen so far
    Header last;                  // the last valid header
};

// Receiver: MSK demodulation (integrate and dump on the instantaneous frequency, bit timing picked per block)
// followed by the RTCM word and header parse. Blocks should be a second or more long.
class Receiver {
public:
    void process(const std::complex<float>* iq, size_t n, double fs);
    const Stats& stats() const { return st_; }
    void reset();

private:
    Stats st_;
    std::vector<uint8_t> pending_;   // demodulated bits not yet scanned
};

// Test and tooling helpers: build the channel bits of an RTCM message and the MSK waveform of a bit stream.
// encodeMessage: header words (preamble, type, station; zcount, seq, length, health) then the 24-bit data words
// of 'extra', each with its 6 parity bits; one bit per entry (0 or 1). pol inverts the whole channel, since the
// beacon's polarity is not known in advance.
std::vector<uint8_t> encodeMessage(const Header& h, const std::vector<uint32_t>& extra, int pol);
std::vector<std::complex<float>> modulate(const std::vector<uint8_t>& bits, double fs, double offsetHz, float amp);

}  // namespace dect2::dgps
