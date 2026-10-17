#include "dect2/df/source.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace dect2::df {

// ---------------------------------------------------------------- simulator
SimMultiChannelSource::SimMultiChannelSource(SimConfig c) : cfg_(std::move(c)), rng_(cfg_.seed) {
    for (size_t i = 0; i < cfg_.sources.size(); i++) srcRng_.emplace_back(cfg_.sources[i].seed ? cfg_.sources[i].seed : cfg_.seed * 1000 + i + 7);
    hist_.resize(cfg_.sources.size());
    lp_.assign(cfg_.sources.size(), {cd(0), cd(0)});
}

bool SimMultiChannelSource::read(Frame& out, size_t n) {
    int N = channels();
    out.assign(N, std::vector<cd>(n));
    double lambda = kC / cfg_.freqHz;
    std::vector<std::vector<cd>> steer;
    for (auto& s : cfg_.sources) steer.push_back(steering(cfg_.array, lambda, s.bearingDeg));
    for (size_t k = 0; k < n; k++, t_++) {
        std::vector<cd> wav(cfg_.sources.size());
        for (size_t s = 0; s < cfg_.sources.size(); s++) {
            const SimSource& ss = cfg_.sources[s];
            if (ss.copyOf >= 0) continue;
            // unit-power circular Gaussian message, rotated by the source's frequency offset
            cd g = cd(srcRng_[s].gauss(), srcRng_[s].gauss()) / std::sqrt(2.0);
            if (ss.bandwidthHz > 0) {   // two cascaded one-pole low-passes, scaled back to unit power
                const double a = std::exp(-kPi * ss.bandwidthHz / cfg_.sampleRate);
                lp_[s][0] = a * lp_[s][0] + (1 - a) * g;
                lp_[s][1] = a * lp_[s][1] + (1 - a) * lp_[s][0];
                g = lp_[s][1] / std::sqrt((1 - a) * (1 - a) * (1 - a) * (1 - a) * (1 + a * a) / std::pow(1 - a * a, 3));
            }
            wav[s] = g * std::polar(1.0, 2 * kPi * ss.offsetHz * (double)t_ / cfg_.sampleRate);
            auto& h = hist_[s]; h.push_back(wav[s]); if (h.size() > 64) h.erase(h.begin());
        }
        for (size_t s = 0; s < cfg_.sources.size(); s++) {   // reflections: the same waveform, `delay` samples late (0 = fully coherent)
            const SimSource& ss = cfg_.sources[s];
            if (ss.copyOf < 0) continue;
            if (ss.copyOf < (int)hist_.size() && (int)hist_[ss.copyOf].size() > ss.delay) { auto& h = hist_[ss.copyOf]; wav[s] = h[h.size() - 1 - ss.delay]; }
        }
        cd common = cd(rng_.gauss(), rng_.gauss()) / std::sqrt(2.0);
        for (int i = 0; i < N; i++) {
            cd v = 0;
            if (cfg_.calibrationNoise) v = common * std::pow(10.0, cfg_.calibrationSnrDb / 20.0);
            else for (size_t s = 0; s < cfg_.sources.size(); s++) v += wav[s] * steer[s][i] * std::pow(10.0, cfg_.sources[s].snrDb / 20.0);
            if (!cfg_.elementGains.empty()) v *= cfg_.elementGains[i];
            v += cd(rng_.gauss(), rng_.gauss()) / std::sqrt(2.0);
            out[i][k] = v;
        }
    }
    return true;
}

// ---------------------------------------------------------------- file
static bool readMeta(const std::string& path, int& ch, double& fs, double& fc) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    std::string s = ss.str();
    auto num = [&](const char* key, double& v) {
        size_t p = s.find(key);
        if (p == std::string::npos) return false;
        p = s.find(':', p + strlen(key));
        if (p == std::string::npos) return false;
        v = atof(s.c_str() + p + 1);
        return true;
    };
    double v;
    if (num("\"core:num_channels\"", v)) ch = (int)v;
    if (num("\"core:sample_rate\"", v)) fs = v;
    if (num("\"core:frequency\"", v)) fc = v;
    return true;
}

FileMultiChannelSource::FileMultiChannelSource(const std::string& path, int channels, double fs, double fc) : n_(channels), fs_(fs), fc_(fc) {
    std::string data = path;
    size_t dot = path.rfind('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot);
    if (ext == ".sigmf-meta" || ext == ".sigmf-data") {
        std::string base = path.substr(0, dot);
        data = base + ".sigmf-data";
        int ch = 0; double f1 = 0, f2 = 0;
        if (readMeta(base + ".sigmf-meta", ch, f1, f2)) {
            if (n_ <= 0 && ch > 0) n_ = ch;
            if (fs_ <= 0 && f1 > 0) fs_ = f1;
            if (fc_ <= 0 && f2 > 0) fc_ = f2;
        }
    }
    if (n_ > 0) f_ = fopen(data.c_str(), "rb");
}
FileMultiChannelSource::~FileMultiChannelSource() { if (f_) fclose((FILE*)f_); }
bool FileMultiChannelSource::read(Frame& out, size_t n) {
    if (!f_ || n_ <= 0) return false;
    std::vector<float> buf(n * n_ * 2);
    size_t got = fread(buf.data(), sizeof(float) * 2 * n_, n, (FILE*)f_);
    if (!got) return false;
    out.assign(n_, std::vector<cd>(got));
    for (size_t t = 0; t < got; t++) for (int c = 0; c < n_; c++) out[c][t] = cd(buf[(t * n_ + c) * 2], buf[(t * n_ + c) * 2 + 1]);
    return true;
}

// ---------------------------------------------------------------- Kraken header
template <class T> static T rd(const uint8_t* b, size_t off) { T v; memcpy(&v, b + off, sizeof v); return v; }

bool parseKrakenIqHeader(const uint8_t* b, size_t len, KrakenIqHeader& h) {
    if (len < kKrakenHeaderSize) return false;
    h.syncWord = rd<uint32_t>(b, 0);
    if (h.syncWord != kKrakenSyncWord) return false;
    h.frameType = rd<uint32_t>(b, 4);
    memcpy(h.hardwareId, b + 8, 16); h.hardwareId[16] = 0;
    h.unitId = rd<uint32_t>(b, 24);
    h.activeAntChs = rd<uint32_t>(b, 28);
    h.iooType = rd<uint32_t>(b, 32);
    h.rfCenterFreq = rd<uint64_t>(b, 40);          // 4 bytes of alignment padding at 36 (native C layout, per the public header notes)
    h.adcSamplingFreq = rd<uint64_t>(b, 48);
    h.samplingFreq = rd<uint64_t>(b, 56);
    h.cpiLength = rd<uint32_t>(b, 64);             // padding at 68
    h.timeStamp = rd<uint64_t>(b, 72);
    h.daqBlockIndex = rd<uint32_t>(b, 80);
    h.cpiIndex = rd<uint32_t>(b, 84);
    h.extIntegrationCntr = rd<uint64_t>(b, 88);
    h.dataType = rd<uint32_t>(b, 96);
    h.sampleBitDepth = rd<uint32_t>(b, 100);
    h.adcOverdriveFlags = rd<uint32_t>(b, 104);
    for (int i = 0; i < 32; i++) h.ifGains[i] = rd<uint32_t>(b, 108 + 4 * i);
    h.delaySyncFlag = rd<uint32_t>(b, 236);
    h.iqSyncFlag = rd<uint32_t>(b, 240);
    h.syncState = rd<uint32_t>(b, 244);
    h.noiseSourceState = rd<uint32_t>(b, 248);
    h.headerVersion = rd<uint32_t>(b, 1020);
    return true;
}

bool decodeKrakenPayload(const KrakenIqHeader& h, const uint8_t* p, size_t len, Frame& out) {
    int n = (int)h.activeAntChs;
    size_t T = h.cpiLength;
    if (n <= 0 || n > 32 || !T) return false;
    size_t bytes = h.sampleBitDepth == 32 ? 8 : 2;
    if (len < bytes * n * T) return false;
    out.assign(n, std::vector<cd>(T));
    for (int c = 0; c < n; c++)
        for (size_t t = 0; t < T; t++) {
            size_t o = ((size_t)c * T + t) * bytes;
            if (bytes == 8) out[c][t] = cd(rd<float>(p, o), rd<float>(p, o + 4));
            else out[c][t] = cd((int8_t)p[o], (int8_t)p[o + 1]);
        }
    return true;
}

// ---------------------------------------------------------------- DoA server line
static bool jsonNum(const std::string& s, const char* key, double& v) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = s.find(k);
    if (p == std::string::npos) return false;
    p = s.find(':', p + k.size());
    if (p == std::string::npos) return false;
    p++;
    while (p < s.size() && (s[p] == ' ' || s[p] == '"')) p++;
    char* e = nullptr;
    v = strtod(s.c_str() + p, &e);
    return e != s.c_str() + p;
}

ImportedBearing parseKrakenDoaLine(const std::string& line) {
    ImportedBearing r;
    double v;
    size_t b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return r;
    if (line[b] == '{') {
        const char* bk[] = {"doa", "max_doa", "bearing", "angle", "doa_angle"};
        for (auto k : bk) if (jsonNum(line, k, v)) { r.bearingDeg = wrap360(v); r.ok = true; break; }
        if (!r.ok) return r;
        const char* ck[] = {"confidence", "conf"};
        for (auto k : ck) if (jsonNum(line, k, v)) { r.confidence = v > 1.0 ? v / 100.0 : v; break; }
        const char* fk[] = {"freq", "frequency", "freq_hz", "center_freq"};
        for (auto k : fk) if (jsonNum(line, k, v)) { r.freqHz = v < 1e5 ? v * 1e6 : v; break; }
        if (jsonNum(line, "latitude", r.latDeg) && jsonNum(line, "longitude", r.lonDeg)) r.hasPos = true;
        return r;
    }
    // CSV: skip a leading timestamp (first field, large number), then bearing, confidence
    std::vector<double> f;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        char* e = nullptr;
        double x = strtod(tok.c_str(), &e);
        if (e != tok.c_str()) f.push_back(x);
    }
    size_t i = (!f.empty() && f[0] > 1e8) ? 1 : 0;
    if (f.size() < i + 1) return r;
    r.bearingDeg = wrap360(f[i]); r.ok = true;
    if (f.size() > i + 1) r.confidence = f[i + 1] > 1.0 ? f[i + 1] / 100.0 : f[i + 1];
    return r;
}

}   // namespace dect2::df
