// NXDN receiver: NXDN48 (2400 symbols/s, 6.25 kHz) and NXDN96 (4800 symbols/s, 12.5 kHz) four-level FSK, conventional and trunked.
//
// The channel is mixed to 0 Hz, decimated (CIC, windowed-sinc) to 48 kHz and FM-demodulated; two symbol filters (one per rate) feed two
// frame sync searches, so both rates are found without a setting. The 10-symbol frame sync word is found by correlation on every sample
// (its sign gives the polarity), its least-squares fit the level and the carrier offset; every next sync is looked for a frame later
// within a few samples, which follows clock errors. Each frame: PN9 descrambled, LICH, then SACCH / FACCH1 / CAC Viterbi decoded with
// their CRCs: VCALL (unit id, talkgroup, call type, cipher), TX_REL, status, short data, SITE_INFO / SRV_INFO. A call joined late is
// found from the SACCH superframe.
// Voice: AMBE+2 through the user-installed mbelib when it is there (nxdn_ambe.h); otherwise the voice frames are counted, no sound.
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

struct NxdnTelemetry;   // nxdn_tel.h

class NxdnReceiver {
public:
    NxdnReceiver();
    ~NxdnReceiver();

    void configure(double inputRateHz);
    void setSignalOffset(double hz);
    bool ready() const;
    void reset();
    void feed(const cf32* x, size_t n);
    bool telemetry(NxdnTelemetry& out, uint64_t lastSeq);
    void setLogCallback(std::function<void(const std::string&)> cb);

    void setVolume(float v);
    void setMuted(bool m);
    void setSilent(bool s);              // decode but never open the sound device
    void setAudioTap(std::function<void(const float* x, size_t n)> cb);   // tests: the 8 kHz voice (mbelib only)
    void setVoiceTap(std::function<void(const uint8_t bits[72])> cb);      // tests: every voice frame's 72 bits as received

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning nxdnTuning();

} // namespace dect2
