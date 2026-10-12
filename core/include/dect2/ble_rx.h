// Bluetooth LE advertising monitor: the packets devices broadcast openly on the three advertising channels (37, 38, 39 at 2402, 2426 and
// 2480 MHz), the same information a scanner app such as nRF Connect shows. Receive only: it never connects, pairs or transmits, and does
// not follow connections (a CONNECT_IND is shown as a connection request, nothing more).
//
// LE 1M PHY: GFSK, BT 0.5, modulation index 0.5, 1 Msym/s. The channel is mixed to 0 Hz, low-pass filtered and resampled to 4 Msps
// (4 samples per bit), FM-demodulated and smoothed over a bit. Each of the four sample phases slices bits against a running mean and
// looks for the advertising access address 0x8E89BED6 (one bit error allowed); the best phase of the hit gives the eye, the decision
// level (the carrier offset) and the SNR. The PDU is dewhitened for the tuned channel first and the other two after it, and kept when
// the CRC-24 (init 0x555555) passes, which also says which channel it came from (a file or a hop needs no channel setting).
// Decoded: the PDU types (ADV_IND, ADV_DIRECT_IND, ADV_NONCONN_IND, ADV_SCAN_IND, SCAN_REQ, SCAN_RSP, CONNECT_IND, ADV_EXT_IND with its
// common extended header) and the advertising data (ble_proto.h). Devices are kept by address; reset() keeps them, clearDevices() not.
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

struct BleTelemetry;   // ble_tel.h

class BleReceiver {
public:
    BleReceiver();
    ~BleReceiver();

    void configure(double inputRateHz);
    void setSignalOffset(double hz);
    void setCenterHz(double hz);          // the radio's centre: which advertising channel is tried first (0: unknown)
    bool ready() const;
    void reset();                         // the demodulator only: the device table stays
    void clearDevices();
    void feed(const cf32* x, size_t n);
    bool telemetry(BleTelemetry& out, uint64_t lastSeq);
    void setLogCallback(std::function<void(const std::string&)> cb);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

ModeTuning bleTuning();

} // namespace dect2
