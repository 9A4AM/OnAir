#pragma once
// IQ recordings with a description: SigMF (.sigmf-meta + .sigmf-data) and two-channel WAV. probeIqFile() reads the rate, the centre
// frequency, the sample format and where the samples start, so the file plays in any mode without typing them in.
#include "dect2/source.h"
#include <cstdint>
#include <string>

namespace dect2 {

struct IqFileInfo {
    std::string dataPath;           // the file that holds the samples
    FileFormat format = FileFormat::CS8;
    bool bigEndian = false;
    double sampleRate = 0;          // Hz, 0 = the file does not say
    double centerHz = 0;            // Hz, 0 = the file does not say
    uint64_t offsetBytes = 0;       // first sample (SigMF header_bytes + captures[0].sample_start, or the WAV data chunk)
    uint64_t endBytes = 0;          // 0 = until the end of the file
    int annotations = 0;            // SigMF annotation count (informational)
    std::string description;
};

// .sigmf-meta, .sigmf-data (the meta beside it is found), .wav. Returns false with err set when the file cannot be read or is a
// kind OnAir cannot play (real-valued samples, 64-bit floats ...). Other extensions return false with an empty err.
bool probeIqFile(const std::string& path, IqFileInfo& out, std::string& err);

// SigMF writer used by tests and tools: writes the `.sigmf-meta` that describes a `.sigmf-data` file.
bool writeSigmfMeta(const std::string& metaPath, FileFormat fmt, bool bigEndian, double rateHz, double centerHz,
                    uint64_t sampleStart, const std::string& description, std::string& err);
const char* sigmfDatatype(FileFormat fmt, bool bigEndian);

} // namespace dect2
