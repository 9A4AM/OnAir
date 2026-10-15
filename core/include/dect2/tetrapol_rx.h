// TETRAPOL network information receiver: GMSK 8 kbit/s, 10 / 12.5 kHz channels (PAS 0001-2).
// Data frames on the downlink control channel are decoded; D_SYSTEM_INFO (codop 0x90) is shown when the 66-bit
// payload carries it in the test format. Receive only: no voice, no identities, nothing decrypted.
#pragma once
#include "mode_tuning.h"
#include "ring.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dect2 {

struct TetrapolTelemetry;

class TetrapolReceiver {
public:
    TetrapolReceiver();
    ~TetrapolReceiver();

    void configure(double inputRateHz);
    void setSignalOffset(double hz);
    void setCenterHz(double hz);
    void setSpacingHz(int hz);            // 10000 or 12500
    bool ready() const;
    void reset();
    void clearCells();
    void feed(const cf32* x, size_t n);
    bool telemetry(TetrapolTelemetry& out, uint64_t lastSeq);
    void clearResults();
    std::string saveResults() const;
    bool loadResults(const std::string& blob);
    void setLogCallback(std::function<void(const std::string&)> cb);
    bool busy() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning tetrapolTuning();

} // namespace dect2
