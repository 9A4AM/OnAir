#include "dect2/source.h"

#include <Accelerate/Accelerate.h>
#include <libhackrf/hackrf.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>

namespace dect2 {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------- HackRF

namespace {

std::mutex g_hackrfInitMutex;
int g_hackrfRefs = 0;

bool hackrfAcquire(std::string& err) {
    std::lock_guard<std::mutex> lk(g_hackrfInitMutex);
    if (g_hackrfRefs == 0) {
        int r = hackrf_init();
        if (r != HACKRF_SUCCESS) {
            err = std::string("hackrf_init: ") + hackrf_error_name((hackrf_error)r);
            return false;
        }
    }
    g_hackrfRefs++;
    return true;
}

void hackrfRelease() {
    std::lock_guard<std::mutex> lk(g_hackrfInitMutex);
    if (--g_hackrfRefs == 0) hackrf_exit();
}

class HackrfSource : public IqSource {
public:
    explicit HackrfSource(std::string serial) : serial_(std::move(serial)) {}
    ~HackrfSource() override { stop(); }

    bool start(const TuneSettings& s, IqRing& ring, std::string& err) override {
        if (!hackrfAcquire(err)) return false;
        ring_ = &ring;
        int r = serial_.empty() ? hackrf_open(&dev_) : hackrf_open_by_serial(serial_.c_str(), &dev_);
        if (r != HACKRF_SUCCESS) {
            err = std::string("open: ") + hackrf_error_name((hackrf_error)r);
            hackrfRelease();
            return false;
        }
        if (!apply(s, err)) {
            hackrf_close(dev_);
            dev_ = nullptr;
            hackrfRelease();
            return false;
        }
        rate_ = s.sampleRate;
        r = hackrf_start_rx(dev_, &HackrfSource::rxCb, this);
        if (r != HACKRF_SUCCESS) {
            err = std::string("start_rx: ") + hackrf_error_name((hackrf_error)r);
            hackrf_close(dev_);
            dev_ = nullptr;
            hackrfRelease();
            return false;
        }
        return true;
    }

    void stop() override {
        if (!dev_) return;
        hackrf_stop_rx(dev_);
        hackrf_close(dev_);
        dev_ = nullptr;
        hackrfRelease();
    }

    bool retune(const TuneSettings& s, std::string& err) override { return dev_ ? apply(s, err, true) : false; }
    double sampleRate() const override { return rate_; }
    bool realtimeHardware() const override { return true; }

private:
    bool apply(const TuneSettings& s, std::string& err, bool live = false) {
        auto chk = [&](int r, const char* what) {
            if (r == HACKRF_SUCCESS) return true;
            err = std::string(what) + ": " + hackrf_error_name((hackrf_error)r);
            return false;
        };
        if (!live || s.sampleRate != rate_) {
            if (!chk(hackrf_set_sample_rate(dev_, s.sampleRate), "set_sample_rate")) return false;
            rate_ = s.sampleRate;
        }
        uint32_t bw = s.basebandFilterHz > 0 ? (uint32_t)s.basebandFilterHz
                                              : hackrf_compute_baseband_filter_bw((uint32_t)(s.sampleRate * 0.5));
        if (!chk(hackrf_set_baseband_filter_bandwidth(dev_, bw), "set_baseband_filter")) return false;
        if (!chk(hackrf_set_freq(dev_, (uint64_t)s.centerHz), "set_freq")) return false;
        if (!chk(hackrf_set_lna_gain(dev_, (uint32_t)s.lnaDb), "set_lna_gain")) return false;
        if (!chk(hackrf_set_vga_gain(dev_, (uint32_t)s.vgaDb), "set_vga_gain")) return false;
        if (!chk(hackrf_set_amp_enable(dev_, s.ampOn ? 1 : 0), "set_amp_enable")) return false;
        return true;
    }

    static int rxCb(hackrf_transfer* t) {
        auto* self = static_cast<HackrfSource*>(t->rx_ctx);
        const int8_t* p = reinterpret_cast<const int8_t*>(t->buffer);
        size_t n = (size_t)t->valid_length / 2;
        constexpr size_t kChunk = 4096;
        cf32 tmp[kChunk];
        size_t i = 0;
        while (i < n) {
            size_t m = std::min(kChunk, n - i);
            for (size_t k = 0; k < m; k++) tmp[k] = cf32(p[2 * (i + k)] * (1.0f / 128), p[2 * (i + k) + 1] * (1.0f / 128));
            self->ring_->write(tmp, m);
            i += m;
        }
        return 0;
    }

    std::string serial_;
    hackrf_device* dev_ = nullptr;
    IqRing* ring_ = nullptr;
    double rate_ = 0;
};

// ---------------------------------------------------------------- paced thread base

class PacedSource : public IqSource {
public:
    ~PacedSource() override { stop(); }
    bool start(const TuneSettings& s, IqRing& ring, std::string& err) override {
        ring_ = &ring;
        rate_ = effectiveRate(s);
        if (!prepare(s, err)) return false;
        run_ = true;
        th_ = std::thread([this] { loop(); });
        return true;
    }
    void stop() override {
        run_ = false;
        if (th_.joinable()) th_.join();
    }
    bool retune(const TuneSettings&, std::string&) override { return true; }
    double sampleRate() const override { return rate_; }

protected:
    virtual double effectiveRate(const TuneSettings& s) { return s.sampleRate; }
    virtual bool prepare(const TuneSettings&, std::string&) { return true; }
    virtual size_t produce(cf32* dst, size_t maxN) = 0; // return samples produced; 0 = end of data

    void loop() {
        std::vector<cf32> buf(1 << 15);
        auto t0 = Clock::now();
        uint64_t sent = 0;
        while (run_) {
            size_t n = produce(buf.data(), buf.size());
            if (n == 0) break;
            ring_->write(buf.data(), n);
            sent += n;
            auto due = t0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(sent / rate_));
            std::this_thread::sleep_until(due);
        }
    }

    IqRing* ring_ = nullptr;
    double rate_ = 0;
    std::atomic<bool> run_{false};
    std::thread th_;
};

// ---------------------------------------------------------------- file

class FileSource : public PacedSource {
public:
    FileSource(std::string path, FileFormat fmt, double rate, bool loop)
        : path_(std::move(path)), fmt_(fmt), fileRate_(rate), loop_(loop) {}
    ~FileSource() override { stop(); if (f_) fclose(f_); }

protected:
    double effectiveRate(const TuneSettings&) override { return fileRate_; }
    bool prepare(const TuneSettings&, std::string& err) override {
        f_ = fopen(path_.c_str(), "rb");
        if (!f_) { err = "cannot open " + path_; return false; }
        return true;
    }
    size_t produce(cf32* dst, size_t maxN) override {
        size_t bps = fmt_ == FileFormat::CF32 ? 8 : 2;
        raw_.resize(maxN * bps);
        size_t got = fread(raw_.data(), bps, maxN, f_);
        if (got == 0 && loop_) { rewind(f_); got = fread(raw_.data(), bps, maxN, f_); }
        for (size_t i = 0; i < got; i++) {
            switch (fmt_) {
            case FileFormat::CS8: dst[i] = cf32((int8_t)raw_[2*i] / 128.0f, (int8_t)raw_[2*i+1] / 128.0f); break;
            case FileFormat::CU8: dst[i] = cf32((raw_[2*i] - 127.5f) / 127.5f, (raw_[2*i+1] - 127.5f) / 127.5f); break;
            case FileFormat::CF32: { float v[2]; memcpy(v, &raw_[8*i], 8); dst[i] = cf32(v[0], v[1]); } break;
            }
        }
        return got;
    }

private:
    std::string path_;
    FileFormat fmt_;
    double fileRate_;
    bool loop_;
    FILE* f_ = nullptr;
    std::vector<uint8_t> raw_;
};

// ---------------------------------------------------------------- synthetic DVB-T2-like signal

class SyntheticSource : public PacedSource {
public:
    ~SyntheticSource() override { stop(); }

    bool retune(const TuneSettings& s, std::string&) override {
        std::lock_guard<std::mutex> lk(mu_);
        cfg_ = s.synth;
        regen_ = true;
        return true;
    }

protected:
    double effectiveRate(const TuneSettings& s) override { return nativeRateHz(s.bandwidthMhz); }
    bool prepare(const TuneSettings& s, std::string&) override {
        cfg_ = s.synth;
        regen_ = true;
        return true;
    }

    size_t produce(cf32* dst, size_t maxN) override {
        std::lock_guard<std::mutex> lk(mu_);
        if (regen_) {
            gen_ = std::make_unique<T2Generator>(cfg_.tx);
            sbuf_.assign(4, cf32(0, 0));
            pos_ = 1;
            regen_ = false;
            delay_.assign(8192, cf32(0, 0));
            dpos_ = 0;
        }
        const double step = 1.0 + cfg_.sroPpm * 1e-6;
        const double nsig = std::pow(10.0, -cfg_.snrDb / 20.0) / std::sqrt(2.0);
        const float ea = cfg_.echoDb > 0 ? (float)std::pow(10.0, -cfg_.echoDb / 20.0) : 0.f;
        const int ed = std::min<int>(std::max(1, cfg_.echoDelay), (int)delay_.size() - 1);
        const double dph = 2 * M_PI * cfg_.cfoHz / rate_;
        std::normal_distribution<float> nd(0.f, 1.f);
        const float gain = 0.25f; // leave ADC headroom: OFDM peak/rms is ~12 dB
        for (size_t i = 0; i < maxN; i++) {
            size_t i0 = (size_t)pos_;
            while (sbuf_.size() < i0 + 4) { gen_->nextFrame(frame_); sbuf_.insert(sbuf_.end(), frame_.begin(), frame_.end()); }
            cf32 v;
            if (cfg_.sroPpm == 0) v = sbuf_[i0];
            else { // 4-point Lagrange interpolation
                float f = (float)(pos_ - i0);
                cf32 a = sbuf_[i0 - 1], b = sbuf_[i0], c = sbuf_[i0 + 1], d = sbuf_[i0 + 2];
                float w0 = -f * (f - 1) * (f - 2) / 6, w1 = (f + 1) * (f - 1) * (f - 2) / 2,
                      w2 = -(f + 1) * f * (f - 2) / 2, w3 = (f + 1) * f * (f - 1) / 6;
                v = a * w0 + b * w1 + c * w2 + d * w3;
            }
            pos_ += step;
            delay_[dpos_ & 8191] = v;
            if (ea > 0) v += ea * delay_[(dpos_ - ed) & 8191];
            dpos_++;
            phase_ += dph;
            if (phase_ > M_PI) phase_ -= 2 * M_PI;
            if (phase_ < -M_PI) phase_ += 2 * M_PI;
            v *= cf32((float)std::cos(phase_), (float)std::sin(phase_));
            v += cf32(nd(rng_), nd(rng_)) * (float)nsig;
            v *= gain;
            // 8-bit ADC, like the HackRF
            auto q = [](float x) { return std::round(std::min(127.f, std::max(-128.f, x * 128.f))) / 128.f; };
            dst[i] = cf32(q(v.real()), q(v.imag()));
        }
        size_t drop = (size_t)pos_ - 2;
        if (drop > 0 && drop < sbuf_.size()) {
            sbuf_.erase(sbuf_.begin(), sbuf_.begin() + drop);
            pos_ -= drop;
        }
        return maxN;
    }

private:
    std::mutex mu_;
    SynthConfig cfg_;
    bool regen_ = true;
    std::unique_ptr<T2Generator> gen_;
    std::vector<cf32> sbuf_, frame_, delay_;
    double pos_ = 1, phase_ = 0;
    size_t dpos_ = 0;
    std::mt19937 rng_{99};
};

} // namespace

// ---------------------------------------------------------------- public API

std::vector<DeviceInfo> listHackrfDevices(std::string& err) {
    std::vector<DeviceInfo> out;
    if (!hackrfAcquire(err)) return out;
    hackrf_device_list_t* l = hackrf_device_list();
    if (l) {
        for (int i = 0; i < l->devicecount; i++) {
            DeviceInfo d;
            d.kind = DeviceInfo::HackRF;
            d.serial = l->serial_numbers[i] ? l->serial_numbers[i] : "";
            d.board = hackrf_usb_board_id_name(l->usb_board_ids[i]);
            std::string tail = d.serial.size() > 8 ? d.serial.substr(d.serial.size() - 8) : d.serial;
            d.name = d.board + " (" + tail + ")";
            out.push_back(d);
        }
        hackrf_device_list_free(l);
    }
    hackrfRelease();
    return out;
}

std::unique_ptr<IqSource> makeSource(const DeviceInfo& d) {
    switch (d.kind) {
    case DeviceInfo::HackRF: return std::make_unique<HackrfSource>(d.serial);
    case DeviceInfo::Synthetic: return std::make_unique<SyntheticSource>();
    default: return nullptr;
    }
}

FileFormat guessFormat(const std::string& path) {
    auto ends = [&](const char* e) { size_t n = strlen(e); return path.size() >= n && path.compare(path.size() - n, n, e) == 0; };
    if (ends(".cu8") || ends(".u8")) return FileFormat::CU8;
    if (ends(".cf32") || ends(".fc32") || ends(".cfile")) return FileFormat::CF32;
    return FileFormat::CS8;
}

std::unique_ptr<IqSource> makeFileSource(const std::string& path, FileFormat fmt, double sampleRate, bool loop) {
    return std::make_unique<FileSource>(path, fmt, sampleRate, loop);
}

} // namespace dect2
