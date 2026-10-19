// KAAL voice transmitter (port of kaal_tx.py, streaming): 16 kHz speech -> codec frames (320 bits) -> LDPC -> pseudo-random interleave per
// 4-frame block -> 16-QAM -> pilots + 32-symbol preamble per frame -> RRC at 6 samples per symbol -> sweep signature at -10 dB, with the
// beacon slice optionally added. Generated at 48 kHz and resampled when the output rate differs.
#pragma once
#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include "dect2/kaal/kaal_beacon.h"

namespace dect2 {
namespace kaal {

// Output scale: the unit-power Python signal times this gives a peak of about 0.5 for speech.
constexpr float kTxGain = 0.4f;
// Mean power of the shaped data signal (symbol power of a frame, 210 / 202, spread over 6 samples per symbol). The sweep and the
// beacon levels are relative to this fixed value, since a stream cannot know its own average in advance (the Python measures it).
constexpr double kTxDataPower = 210.0 / 202.0 / 6.0;
constexpr double kTxBeaconDb = -10.0;   // beacon slice level relative to the data power

class VoiceTx {
public:
    VoiceTx();
    ~VoiceTx();
    VoiceTx(const VoiceTx&) = delete;
    VoiceTx& operator=(const VoiceTx&) = delete;

    void configure(double sampleRate);               // output sample rate; internally 48 kHz, resample if it differs. Resets the stream.
    void pushAudio(const int16_t* pcm, size_t n);    // 16 kHz mono speech in, any chunk size
    size_t popIq(std::complex<float>* out, size_t maxSamples);   // transmit IQ out, unit peak about 0.5; may return fewer; silence is NOT inserted
    void flush();                                    // pad the last partial codec frame and 4-frame block with zeros; ends the stream
    void setBeaconMix(bool on);                      // when on, adds the beacon slice to the output on the 2 s / 10 s schedule
    void setBeacon(const BeaconFields& f, const std::string& text);

    size_t available() const;                        // IQ samples ready for popIq
private:
    struct Impl;
    Impl* p_;
};

}  // namespace kaal
}  // namespace dect2
