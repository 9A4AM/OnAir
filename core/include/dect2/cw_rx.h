// CW (Morse) receiver: a skimmer that decodes every CW signal in a window around the dial frequency at once.
//
// The channel is mixed to 0 Hz and brought to 24 kHz complex (marine::Front). A 2048-point spectrum every 1024 samples (11.7 Hz bins),
// held with a fast attack and a slow release so a keyed carrier stays visible between its elements, finds the carriers: peaks 10 dB over
// the median of the window (the noise floor), at least 60 Hz apart. Each carrier gets its own signal path: mixed to 0 Hz, CIC to 1 kHz,
// a 65-tap low-pass (+-50 Hz), the envelope, and an adaptive threshold between a mark level (fast attack, follows the fading) and a noise
// level, with hysteresis. The key-down / key-up durations go to the timing decoder (cw_morse.h): speed 5 - 50 WPM followed from the
// dot / dash clusters, Farnsworth spacing, prosigns. A signal that stays silent for 30 s is dropped (unless it is the focus).
//
// The focus (setFocusHz, a click on the waterfall) is the signal of the big text view; it can have a sidetone: a 700 Hz tone keyed by its
// decoder, off by default (setSidetone).
//
// Written to the engine's contract: configure() once the sample rate is known, feed() from the analysis thread (never blocks),
// telemetry() and the setters from the interface thread.
#pragma once
#include "mode_tuning.h"
#include "ring.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dect2 {

struct CwTelemetry;   // cw_tel.h

class CwReceiver {
public:
    CwReceiver();
    ~CwReceiver();

    void configure(double inputRateHz);
    void setSignalOffset(double hz);     // where the dial frequency sits relative to 0 Hz in the input
    bool ready() const;                  // false when the input rate is too low
    void reset();                        // after a retune
    void feed(const cf32* x, size_t n);
    bool telemetry(CwTelemetry& out, uint64_t lastSeq);
    void setLogCallback(std::function<void(const std::string&)> cb);

    void setFocusHz(double offsetHz);    // the signal nearest this offset from the dial becomes the focus (a new one when none is near)
    void clearSignals();                 // forget every signal and its text
    void setThresholdDb(float db);       // how far over the noise floor a carrier must be to be followed (default 10)
    void setSidetone(bool on);           // a tone for the focused signal (off by default)
    void setSidetoneHz(float hz);
    void setVolume(float v);
    void setMuted(bool m);
    void setSilent(bool s);              // decode but do not open the sound device
    void setSidetoneTap(std::function<void(const float* stereo, size_t frames)> cb);   // tests: the 48 kHz sidetone

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning cwTuning();

// The CW segments of the HF bands (IARU Region 1): where the band presets tune
struct CwBand { const char* name; double loMhz, hiMhz; };
const CwBand* cwBands(int& count);

} // namespace dect2
