// Sigfox uplink receiver (passive). Part of the ISM sensors mode; see ism_rx.h.
//
// A Sigfox uplink is an ultra-narrowband DBPSK signal (100 bit/s, 600 bit/s in RC2 / RC4), about 100 Hz wide, anywhere in 192 kHz around the
// band's centre (868.130 MHz RC1, 902.2 RC2, 920.8 RC3 / RC4), sent three times on different frequencies. The receiver brings the dial
// frequency to 0 Hz, decimates to about 400 kHz, watches a 100 Hz-resolution spectrogram for narrow carriers that stay up, and when one
// ends demodulates it: carrier frequency from the squared signal, symbol timing, differential bits, preamble, frame.
//
// Frame layout used (bits, MSB first; from public reverse engineering of the uplink, see the notes in ism_sigfox.cpp; not all verified):
//   preamble 19 (1010...), frame type 13, header 16 (LI 2, repeat flag 1, spare 1, sequence number 12), device ID 32,
//   payload 0..12 bytes, authentication code 16..40 (secret key: shown, never checked), CRC-16 (shown, not checked).
// Nothing is decrypted or verified: the key is secret and payloads are often encrypted by the application.
#pragma once
#include "ring.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dect2 {

// One message: the repetitions (up to three, on different frequencies) of one uplink are merged
struct SigfoxMessage {
    int64_t wallTime = 0, firstWall = 0;
    double sec = 0;              // signal time of the first repetition
    std::string id;              // 8 hex digits
    uint32_t seq = 0;            // 12 bit sequence number
    std::string payload;         // hex
    int len = 0;                 // payload bytes
    std::string mac, crc;        // as received (hex), not verified
    uint32_t frameType = 0;      // the 13 bit field as received
    int li = 0;                  // the 2 bit field after the frame type, as received
    bool repeatFlag = false;
    int baud = 100;
    int reps = 1;                // repetitions heard
    std::vector<double> freqs;   // one per repetition
    float levelDb = -200;        // strongest repetition, dBFS (carrier power)
};

struct SigfoxTelemetry {
    std::vector<SigfoxMessage> messages;   // newest first, at most kSigfoxMsgMax
    // waterfall: rows of cols cells over +-96 kHz around the dial, oldest row first, 0..255 = 0..42 dB over the noise
    int cols = 0;
    std::vector<uint8_t> rows;             // cols x nRows
    uint64_t rowCount = 0;                 // rows made so far (counts past the ones kept)
    uint64_t carriers = 0;                 // narrow carriers followed to their end
    uint64_t frames = 0;                   // uplinks decoded (every repetition)
    bool active = false;                   // the dial is on a Sigfox band (or unknown, for files): the receiver runs
};
constexpr size_t kSigfoxMsgMax = 300;
constexpr int kSigfoxCols = 192;
constexpr size_t kSigfoxRowsMax = 160;

class SigfoxRx {
public:
    SigfoxRx();
    ~SigfoxRx();
    // inputRate: sample rate of feed(); dialOffsetHz: where the dial (band centre) sits in the input; dialHz: its absolute frequency (0: unknown)
    void configure(double inputRate, double dialOffsetHz, double dialHz);
    void feed(const cf32* x, size_t n);
    const SigfoxTelemetry& telemetry() const;
    void clear();
    static bool onSigfoxBand(double dialHz);   // 0 (unknown) counts as on
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// ---- test signal (nothing is transmitted): one uplink as bits, and its samples
// payload 0..12 bytes. The frame type, MAC and CRC are generated from the layout above.
std::vector<uint8_t> sigfoxFrameBits(uint32_t id, uint32_t seq, const std::vector<uint8_t>& payload, bool repeatFlag = false);
struct SigfoxTx {
    double offsetHz = 0;         // from the dial
    double startSec = 0;
    double levelDb = 0;          // 0 dB: 12 dB over the noise in 100 Hz at the generator's snrDb
    uint32_t id = 0, seq = 0;
    std::vector<uint8_t> payload;
    int baud = 100;
    bool repeatFlag = false;
};
// n samples at rate with the dial at dialOffsetHz, complex white noise of 0.1 rms per component set so that a 0 dB uplink has snrDb in
// 100 Hz (noise-only when txs is empty); cfoHz moves everything.
std::vector<cf32> sigfoxSynth(double rate, double dialOffsetHz, double seconds, double snrDb, double cfoHz, const std::vector<SigfoxTx>& txs, uint32_t seed);

} // namespace dect2
