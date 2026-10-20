// EDACS control channel receiver: FM discriminator at 76.8 kHz (8 samples per bit), dotting search, three-copy majority voting.
#pragma once
#include "dect2/common/ring.h"
#include "dect2/engine/mode_tuning.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dect2 {

struct EdacsTelemetry;

class EdacsReceiver {
public:
    EdacsReceiver();
    ~EdacsReceiver();
    void configure(double inputRateHz);
    void setSignalOffset(double hz);
    void setCenterHz(double hz);
    void setInverted(bool inv);   // the data polarity is not established: flip it when the words look inverted
    bool ready() const;
    void reset();
    void clearMessages();
    void feed(const cf32* x, size_t n);
    bool telemetry(EdacsTelemetry& out, uint64_t lastSeq);
    void setLogCallback(std::function<void(const std::string&)> cb);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning edacsTuning();

} // namespace dect2
