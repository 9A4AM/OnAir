// Voice inversion descrambler for listening to analog voice (airband, NFM, WFM, SSB audio): undoes "frequency inversion" scramblers, which only
// mirror the audio spectrum (f -> carrier - f). This is NOT encryption: anybody can undo it with a mixer and a lowpass filter.
//  - Simple inversion: mix with a carrier (typically 2.55 .. 3.4 kHz), lowpass.
//  - Split-band inversion: the band lo..hi is cut at a split frequency and each part is inverted inside its own range.
//  - Search: tries carriers (or split frequencies) offline on a few seconds of captured audio and scores how speech-like each result is
//    (energy inside the voice band, spectral tilt, little hum and little energy above the voice band, 4 Hz syllable modulation).
// Own code (clean-room: a mixer with a phase accumulator and Butterworth / Linkwitz-Riley filters, nothing copied from other projects).
// Digital voice is never touched: only the analog audio chains use this; encrypted traffic is not decoded.
// process() never allocates or locks (everything is sized in prepare()); latency is the filter delay, about 1 to 2 ms; while it is off the
// input pointer itself is returned and nothing is computed. The config is atomics (any thread writes, the audio thread reads).
#pragma once
#include <atomic>
#include <memory>
#include <vector>

namespace dect2 {

enum class VoiceInvMode { Simple = 0, Split = 1 };

struct VoiceInversionSettings {
    bool on = false;
    VoiceInvMode mode = VoiceInvMode::Simple;
    float carrierHz = 3000.f;   // simple inversion: f -> carrier - f
    float splitHz = 1700.f;     // split-band: where the band is cut
    float loHz = 300.f, hiHz = 3000.f;   // split-band: the band edges (the sound outside them is removed)
};

// Common carrier frequencies used by inverters
extern const float kVoiceInvPresets[5];   // 2550 2700 3000 3300 3400

class VoiceInversionConfig {
public:
    static VoiceInversionConfig& global();
    void set(const VoiceInversionSettings& s);
    VoiceInversionSettings get() const;
    std::atomic<bool> on{false};
    std::atomic<int> mode{0};
    std::atomic<float> carrierHz{3000.f}, splitHz{1700.f}, loHz{300.f}, hiHz{3000.f};
    // The last seconds of the sound going into the descrambler (mono), kept while it is on or while `capture` is set (the UI sets it for the search).
    std::atomic<bool> capture{false};
    static constexpr int kCapN = 1 << 19;                         // samples (10 s at 48 kHz)
    std::atomic<int> capRate{48000};
    std::atomic<unsigned long long> capW{0};
    std::unique_ptr<std::atomic<float>[]> cap;
    VoiceInversionConfig();
    // Copies the last `seconds` of the capture (newest last) into out; returns the rate (0 = nothing captured yet).
    int snapshot(std::vector<float>& out, double seconds) const;
};

class VoiceInversion {
public:
    VoiceInversion();
    ~VoiceInversion();
    VoiceInversion(const VoiceInversion&) = delete;
    VoiceInversion& operator=(const VoiceInversion&) = delete;
    void prepare(int sampleRate);                                   // not on the audio thread (rates from 8 kHz up)
    void setConfig(VoiceInversionConfig* c) { cfg_ = c; }          // default: the global one
    static constexpr int kMaxChunk = 1024;
    // Interleaved stereo, up to kMaxChunk frames. Returns `in` itself while off, else `scratch` (kMaxChunk * 2 floats, caller's; both channels the mono mix).
    const float* process(const float* in, int frames, float* scratch, bool* active = nullptr);
    void reset();
    struct Impl;

private:
    std::unique_ptr<Impl> p_;
    VoiceInversionConfig* cfg_;
};

// How speech-like a mono signal is: 0 (not at all) .. about 1. *modulation gets the share of the envelope power at 2..8 Hz (syllables, ~0.3+ for speech).
double voiceSpeechScore(const float* x, int n, int rate, double* modulation = nullptr);

struct VoiceInvSearchResult {
    bool found = false;       // a clearly speech-like result exists
    float value = 0.f;        // the best carrier (simple) or split frequency (split-band), Hz
    double score = 0.0, baseline = 0.0;   // score of the best candidate and of the untouched sound
    double modulation = 0.0;
};
// Offline, on any non-audio thread. Simple: candidates 2000 .. 3600 Hz; split-band: lo+250 .. hi-250 using base.loHz / hiHz. `progress` 0..1, `cancel` stops it.
VoiceInvSearchResult voiceInversionSearch(const float* x, int n, int rate, VoiceInvMode mode, const VoiceInversionSettings& base,
                                          std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

}  // namespace dect2
