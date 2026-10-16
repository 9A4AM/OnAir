// Hamlib CAT rig entries the user adds (serial port + model). IQ/audio uses a separate sound-card path when configured.
#pragma once
#include "source.h"
#include <string>
#include <vector>

namespace dect2 {

struct HamlibRig {
    int model = 0;              // Hamlib rig model number (rigctl -l), e.g. 1025 for IC-7300
    std::string path;           // serial device: /dev/cu.usbmodem…, COM3, …
    std::string label;          // optional display name (empty: built from model + path)
    std::string audioId;        // AudioIn device id for IQ/audio (empty: silence at 48 kHz, CAT only)
    bool operator==(const HamlibRig& o) const { return model == o.model && path == o.path && label == o.label && audioId == o.audioId; }
};

// prefs "hamlibRigs": "model,path,label,audioId;…" (label and audioId may be empty)
std::string hamlibRigsToString(const std::vector<HamlibRig>& v);
std::vector<HamlibRig> hamlibRigsFromString(const std::string& s);

void setHamlibRigs(const std::vector<HamlibRig>& v);
std::vector<HamlibRig> hamlibRigs();

// DeviceInfo::serial is "model:path" (path after the first colon)
std::string hamlibRigSerial(int model, const std::string& path);
bool parseHamlibSerial(const std::string& serial, int& model, std::string& path);

std::string hamlibRigDeviceName(const HamlibRig& r);
std::vector<DeviceInfo> hamlibRigDevices(const std::vector<HamlibRig>& v);

bool hamlibSupported();   // false when built without Hamlib

std::unique_ptr<IqSource> makeHamlibSource(const DeviceInfo& d);
std::unique_ptr<TxSink> makeHamlibTxSink(const DeviceInfo& d);

} // namespace dect2
