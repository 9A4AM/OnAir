// IQ sample sources: HackRF, recorded file, built-in synthetic DVB-T2-like signal.
#pragma once
#include "ring.h"
#include "t2gen.h"
#include <memory>
#include <string>
#include <vector>

namespace dect2 {

// Synthetic test-signal configuration (never transmitted; samples are generated in memory only).
struct SynthConfig {
    TxParams tx;           // FFT size, guard interval, ...
    double snrDb = 30;
    double cfoHz = 0;
    double sroPpm = 0;
    double echoDb = 0;     // 0 = off; otherwise echo attenuation
    int echoDelay = 300;   // samples
    bool gainModel = false; // scale the level with the LNA/VGA/amp gains (for testing AGC): 62 dB total = the nominal level
};

struct TuneSettings {
    double bandwidthMhz = 8;
    SynthConfig synth;
    double centerHz = 522e6;
    double sampleRate = 18285714.2857; // 2x native rate for an 8 MHz channel
    double basebandFilterHz = 0;       // 0 = auto
    int lnaDb = 16;                    // 0..40 step 8
    int vgaDb = 20;                    // 0..62 step 2
    bool ampOn = false;
};

struct DeviceInfo {
    enum Kind { Synthetic, File, HackRF } kind = Synthetic;
    std::string name;   // display name
    std::string serial; // HackRF serial / file path
    std::string board;  // "HackRF One", "HackRF Pro", ...
};

class IqSource {
public:
    virtual ~IqSource() = default;
    virtual bool start(const TuneSettings& s, IqRing& ring, std::string& err) = 0;
    virtual void stop() = 0;
    virtual bool retune(const TuneSettings& s, std::string& err) = 0; // live changes
    virtual double sampleRate() const = 0;
    virtual bool realtimeHardware() const { return false; }
};

std::vector<DeviceInfo> listHackrfDevices(std::string& err);
std::unique_ptr<IqSource> makeSource(const DeviceInfo& d);

// File format helpers (sidecar-free for now): .cs8 interleaved signed 8-bit IQ, .cu8 unsigned, .cf32 float.
enum class FileFormat { CS8, CU8, CF32 };
FileFormat guessFormat(const std::string& path);
std::unique_ptr<IqSource> makeFileSource(const std::string& path, FileFormat fmt, double sampleRate, bool loop);

} // namespace dect2
