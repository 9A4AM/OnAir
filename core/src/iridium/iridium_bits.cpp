// Iridium bursts as gr-iridium iridium-extractor "RAW:" lines (see iridium_bits.h).
#include "dect2/common/u8file.h"
#include "dect2/iridium/iridium_bits.h"
#include "dect2/iridium/iridium_phy.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace dect2 {

const char* const kIridiumRawRegex =
    R"(^(RAW|RWA|NC1): ([^ ]*) (-?[\d.]+) (\d+) (?:N:([+-]?\d+(?:\.\d+)?)([+-]\d+(?:\.\d+)?)|A:(\w+)) [IL]:(\w+) +(\d+)% ([\d.]+|inf|nan) +(\d+) ([\[\]<> 01]+)(.*)$)";

std::string iridiumRawLine(const IridiumBurstBits& b, long long t0, double unixTime, uint64_t id) {
    std::string bits = b.downlink ? iridium::kUwDlBits : iridium::kUwUlBits;
    bits.reserve(bits.size() + b.bits.size());
    for (uint8_t v : b.bits) bits.push_back(v ? '1' : '0');
    const double ms = std::max(0.0, (unixTime - (double)t0) * 1e3);
    // gr-iridium: magnitude = burst level over the noise floor's reference, noise = noise floor, both in dB; ours from the symbol
    // level and its SNR. Clamped so the printed field keeps its width and the parser's number pattern.
    const double mag = std::clamp(b.levelDb, -99.0, 99.0);
    const double noise = std::clamp(b.levelDb - b.snrDb, -99.0, 99.0);
    const double level = std::pow(10.0, b.levelDb / 20);
    const int conf = (int)std::lround(std::clamp(b.confidence, 0.0, 100.0));
    char head[256];
    snprintf(head, sizeof head, "RAW: i-%lld-t1 %012.4f %010d N:%05.2f%+06.2f I:%011llu %3d%% %.5f %3d ", t0, ms, (int)(b.freqHz + 0.5), mag, noise,
             (unsigned long long)id, conf, level, (int)(b.bits.size() / 2));
    return head + bits;
}

IridiumBitsWriter::~IridiumBitsWriter() { close(); }

bool IridiumBitsWriter::open(const std::string& path, std::string* err) {
    std::lock_guard<std::mutex> lk(mu_);
    if (f_) { fclose(f_); f_ = nullptr; }
    try {
        const std::filesystem::path p = dect2::u8file(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
#ifdef _WIN32
        f_ = _wfopen(p.c_str(), L"wb");
#else
        f_ = fopen(p.c_str(), "wb");
#endif
    } catch (const std::exception&) { f_ = nullptr; }
    if (!f_) { if (err) *err = "cannot write " + path; return false; }
    path_ = path;
    t0_ = -1;
    lines_ = 0;
    return true;
}
void IridiumBitsWriter::close() {
    std::lock_guard<std::mutex> lk(mu_);
    if (f_) fclose(f_);
    f_ = nullptr;
}
bool IridiumBitsWriter::isOpen() const { std::lock_guard<std::mutex> lk(mu_); return f_ != nullptr; }
uint64_t IridiumBitsWriter::lines() const { std::lock_guard<std::mutex> lk(mu_); return lines_; }
std::string IridiumBitsWriter::path() const { std::lock_guard<std::mutex> lk(mu_); return path_; }

void IridiumBitsWriter::write(const IridiumBurstBits& b, double unixTime) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!f_) return;
    if (t0_ < 0) t0_ = (long long)std::floor(unixTime);   // gr-iridium: the whole second of the first burst
    const std::string s = iridiumRawLine(b, t0_, unixTime, lines_);
    fputs(s.c_str(), f_);
    fputc('\n', f_);
    if ((++lines_ & 31) == 0) fflush(f_);
}

} // namespace dect2
