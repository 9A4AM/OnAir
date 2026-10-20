// Satellite beacon (TV sat beacon decoder) receiver telemetry, shared by the receiver and the UI.
// The first members are copied into RxTelemetry (history plot, top bar): keep them and their meaning.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace dect2 {

constexpr int kSbSpecPts = 256;       // points of the two spectrum plots
constexpr size_t kSbFrameMax = 48;    // frames kept in the list
constexpr int kSbLineMax = 6;         // narrow lines listed

struct SbLine {
    double offsetHz = 0;              // from the radio's centre
    float levelDb = 0;                // dB over the noise in one bin
    float relDb = 0;                  // dB below the main carrier (negative: weaker)
    bool side = false;                // within 120 kHz of the main carrier: a line of its modulation, not another carrier
};

struct SbFrame {
    uint32_t id = 0;
    std::vector<uint8_t> bytes;       // the frame aligned to the learned frame start, polarity normalised
    int fixedErrors = 0;              // bits that differ from the learned template in the columns that never change
    int counter = -1;                 // the minor frame counter (see SbTelemetry::counterByte), -1 when there is none
    bool good = false;                // fixedErrors small enough to trust the frame
};

// One row of the decoded view: a word (or a run of words) of the frame, what is known about it and how sure.
// confidence: 0 raw (seen as it is, the meaning is the spacecraft maker's and unknown), 1 estimated (inferred, `how` says from what), 2 decoded (checked against the data)
struct SbField {
    std::string name, value, unit, raw, how;
    int firstByte = 0, lastByte = 0;
    int confidence = 0;
};

// A plain line of the summary: satellite, slot, time, ... Missing things are listed too, as "not found".
struct SbInfo {
    std::string label, value, how;
    int confidence = 0;               // as SbField
};

struct SbTelemetry {
    uint64_t seq = 0;
    int state = 0;                    // 0 no carrier, 1 carrier locked, 2 subcarrier and bit clock locked, 3 frames found
    double cfoHz = 0;                 // the carrier relative to the radio's centre
    float snrDb = 0;                  // carrier C/N0 in dB-Hz (carrier power over the noise in 1 Hz)
    float levelDb = -200;             // carrier level, dBFS
    bool dataValid = false;           // frames are being decoded
    uint64_t blocksOk = 0, blocksBad = 0;   // good frames, frames with too many errors in the fixed columns
    // details
    double timeSec = 0, inputRate = 0, workRate = 0, centreHz = 0;
    double driftHzPerMin = 0;         // slope of the carrier frequency over the last minutes
    float noiseDb = -200;             // dBFS in one 1 Hz bin
    bool carrierLocked = false;
    float pmRmsDeg = 0;               // rms phase modulation of the carrier (the subcarrier tones and the data)
    double subcarrierHz = 0;          // centre of the telemetry tones (0: none found)
    double toneSpacingHz = 0;         // distance of the two tones (0: a single tone)
    int tones = 0;
    float subcarrierDb = 0;           // the strongest tone over the PM noise, dB
    double baud = 0;                  // bit rate (0: unknown)
    int coding = 0;                   // 0 NRZ, 1 bi-phase (Manchester): two chips per bit, the data is the sign of their difference
    bool clockLocked = false;
    int frameBits = 0;                // learned frame length (0: none yet)
    int frameOffset = 0;              // bit position of the frame start in the stream, modulo the frame length
    float frameAgreement = 0;         // 0.5 chance, 1 identical frames
    int fixedBits = 0;                // columns that never change
    std::string syncName;             // the known sync word the frames start with ("FAF320 (IRIG 106)"), empty: none, the frame start is learned
    int counterByte = -1;             // the byte of the frame that counts up from frame to frame (the minor frame counter), -1: none
    int counterMod = 0;               // the count wraps after this many frames (the major frame)
    uint64_t bitsTotal = 0, framesTotal = 0;
    std::vector<uint8_t> templateBytes;   // majority frame
    std::vector<uint8_t> fixedMask;       // 1 bits: columns that never change
    std::vector<float> colStability;      // per column of the frame, 0.5 random .. 1 fixed
    std::vector<SbFrame> frames;          // newest first
    std::vector<SbField> fields;          // the decoded view (see satbeacon_decode.h), from all frames seen
    std::vector<SbInfo> info;             // the summary lines
    uint64_t checksumOk = 0, checksumBad = 0;   // frames whose last byte is the sum of the others, when the frames have such a check
    uint64_t analysedFrames = 0;
    std::vector<SbLine> lines;            // narrow lines in the capture, strongest first (the first is the main carrier)
    double lineSpacingHz = 0;             // distance of the main carrier to the strongest other carrier (0: none)
    std::vector<float> rfSpectrum;        // kSbSpecPts dB over noise, the whole capture
    std::vector<float> pmSpectrum;        // kSbSpecPts dB over noise, 0 .. work rate / 2 of the carrier phase
    double pmSpanHz = 0;
    bool logging = false;
    std::string logPath;
};

inline std::string sbSummary(const SbTelemetry& t) {
    char b[200];
    if (t.state == 0) return "TV sat beacon decoder: no carrier in the capture";
    if (t.state == 1 || t.baud <= 0) snprintf(b, sizeof b, "TV sat beacon decoder: carrier %+.1f Hz, %.1f dB-Hz", t.cfoHz, (double)t.snrDb);
    else snprintf(b, sizeof b, "TV sat beacon decoder: carrier %.1f dB-Hz, %.0f bit/s on %.1f kHz, %llu frames", (double)t.snrDb, t.baud, t.subcarrierHz / 1e3, (unsigned long long)t.framesTotal);
    return b;
}

// the decoded data as CSV (one row per field and per summary line) and as JSON; satbeacon_decode.cpp
std::string sbExportCsv(const SbTelemetry& t);
std::string sbExportJson(const SbTelemetry& t);

} // namespace dect2
