// Hamlib CAT control for classic transceivers: frequency and mode on the rig; IQ or speaker audio through a sound card when configured.
#ifdef DECT2_HAVE_HAMLIB

#include "dect2/hamlib_rig.h"
#include "dect2/source.h"
#include <hamlib/rig.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace dect2 {

namespace {

using Clock = std::chrono::steady_clock;

std::mutex gHamInitMu;
int gHamRefs = 0;

bool hamAcquire(std::string& err) {
    std::lock_guard<std::mutex> lk(gHamInitMu);
    if (gHamRefs == 0) {
        const int r = rig_load_all_backends();
        if (r != RIG_OK && r != -RIG_ENIMPL) {
            err = std::string("Hamlib: ") + rigerror(r);
            return false;
        }
    }
    gHamRefs++;
    return true;
}

void hamRelease() {
    std::lock_guard<std::mutex> lk(gHamInitMu);
    if (--gHamRefs < 0) gHamRefs = 0;
}

rmode_t hamModeOf(const TuneSettings& s) {
    const std::string m = radioOption(s, "hamlib_mode", "USB");
    if (m == "LSB" || m == "lsb") return RIG_MODE_LSB;
    if (m == "AM" || m == "am") return RIG_MODE_AM;
    if (m == "FM" || m == "fm") return RIG_MODE_FM;
    if (m == "CW" || m == "cw") return RIG_MODE_CW;
    if (m == "PKTLSB" || m == "pktlsb") return RIG_MODE_PKTLSB;
    if (m == "PKTUSB" || m == "pktusb") return RIG_MODE_PKTUSB;
    return RIG_MODE_USB;
}

class HamlibHandle {
public:
    bool open(int model, const std::string& path, std::string& err) {
        close();
        if (!hamAcquire(err)) return false;
        rig_ = rig_init((rig_model_t)model);
        if (!rig_) {
            err = "Hamlib: rig_init failed for model " + std::to_string(model);
            hamRelease();
            return false;
        }
        if (!path.empty()) {
            const int r = rig_set_conf(rig_, rig_token_lookup(rig_, "rig_path"), path.c_str());
            if (r != RIG_OK) {
                err = std::string("Hamlib rig_path: ") + rigerror(r);
                close();
                return false;
            }
        }
        const int r = rig_open(rig_);
        if (r != RIG_OK) {
            err = std::string("Hamlib open ") + path + ": " + rigerror(r);
            close();
            return false;
        }
        return true;
    }

    void close() {
        if (rig_) {
            rig_close(rig_);
            rig_cleanup(rig_);
            rig_ = nullptr;
        }
        if (ownedHam_) {
            hamRelease();
            ownedHam_ = false;
        }
    }

    ~HamlibHandle() { close(); }

    bool setRx(double hz, rmode_t mode, std::string& err) {
        if (!rig_) { err = "Hamlib: rig not open"; return false; }
        int r = rig_set_freq(rig_, RIG_VFO_RX, (freq_t)hz);
        if (r != RIG_OK) { err = std::string("Hamlib set RX frequency: ") + rigerror(r); return false; }
        r = rig_set_mode(rig_, RIG_VFO_RX, mode, RIG_PASSBAND_NORMAL);
        if (r != RIG_OK) { err = std::string("Hamlib set RX mode: ") + rigerror(r); return false; }
        return true;
    }

    bool setTx(double hz, rmode_t mode, std::string& err) {
        if (!rig_) { err = "Hamlib: rig not open"; return false; }
        int r = rig_set_freq(rig_, RIG_VFO_TX, (freq_t)hz);
        if (r != RIG_OK) { err = std::string("Hamlib set TX frequency: ") + rigerror(r); return false; }
        r = rig_set_mode(rig_, RIG_VFO_TX, mode, RIG_PASSBAND_NORMAL);
        if (r != RIG_OK) { err = std::string("Hamlib set TX mode: ") + rigerror(r); return false; }
        return true;
    }

    bool setPtt(bool on, std::string& err) {
        if (!rig_) { err = "Hamlib: rig not open"; return false; }
        const int r = rig_set_ptt(rig_, RIG_VFO_CURR, on ? RIG_PTT_ON : RIG_PTT_OFF);
        if (r != RIG_OK) { err = std::string("Hamlib PTT: ") + rigerror(r); return false; }
        return true;
    }

private:
    RIG* rig_ = nullptr;
    bool ownedHam_ = true;
};

DeviceInfo audioDevFor(const DeviceInfo& ham) {
    DeviceInfo a;
    a.kind = DeviceInfo::Audio;
    a.board = "audio";
    if (ham.nativeArgs.empty()) {
        a.serial = "silence";
        a.name = "silence";
    } else {
        a.serial = ham.nativeArgs;
        a.name = ham.nativeArgs;
    }
    return a;
}

class HamlibSource : public IqSource {
public:
    explicit HamlibSource(DeviceInfo d) : dev_(std::move(d)) {}
    ~HamlibSource() override { stop(); }

    bool start(const TuneSettings& s, IqRing& ring, std::string& err) override {
        stop();
        int model = 0;
        std::string path;
        if (!parseHamlibSerial(dev_.serial, model, path)) { err = "bad Hamlib rig id"; return false; }
        if (!rig_.open(model, path, err)) return false;
        if (!rig_.setRx(s.centerHz, hamModeOf(s), err)) { rig_.close(); return false; }
        iq_ = makeAudioSource(audioDevFor(dev_));
        if (!iq_ || !iq_->start(s, ring, err)) {
            rig_.close();
            iq_.reset();
            return false;
        }
        rate_ = iq_->sampleRate();
        return true;
    }

    void stop() override {
        if (iq_) { iq_->stop(); iq_.reset(); }
        rig_.close();
    }

    bool retune(const TuneSettings& s, std::string& err) override {
        if (!rig_.setRx(s.centerHz, hamModeOf(s), err)) return false;
        return iq_ ? iq_->retune(s, err) : true;
    }

    double sampleRate() const override { return rate_; }
    bool realtimeHardware() const override { return true; }

private:
    DeviceInfo dev_;
    HamlibHandle rig_;
    std::unique_ptr<IqSource> iq_;
    double rate_ = 48000;
};

class HamlibTx : public TxSink {
public:
    explicit HamlibTx(DeviceInfo d) : dev_(std::move(d)) {}
    ~HamlibTx() override { close(); }

    bool open(const TxTune& t, std::string& err) override {
        close();
        int model = 0;
        std::string path;
        if (!parseHamlibSerial(dev_.serial, model, path)) { err = "bad Hamlib rig id"; return false; }
        if (!rig_.open(model, path, err)) return false;
        TuneSettings ts;
        ts.centerHz = t.centerHz;
        ts.radio = t.radio;
        const rmode_t mode = hamModeOf(ts);
        if (!rig_.setTx(txRadioHz(t), mode, err)) { close(); return false; }
        if (!rig_.setPtt(true, err)) { close(); return false; }
        rate_ = t.sampleRate > 0 ? t.sampleRate : 48000;
        t0_ = Clock::now();
        sent_ = 0;
        return true;
    }

    bool write(const cf32*, size_t n, std::string& err) override {
        if (n == 0) return true;
        sent_ += (uint64_t)n;
        const auto due = t0_ + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(sent_ / rate_));
        std::this_thread::sleep_until(due);
        (void)err;
        return true;
    }

    bool retune(const TxTune& t, std::string& err) override {
        TuneSettings ts;
        ts.centerHz = t.centerHz;
        ts.radio = t.radio;
        return rig_.setTx(txRadioHz(t), hamModeOf(ts), err);
    }

    void close() override {
        std::string e;
        rig_.setPtt(false, e);
        rig_.close();
    }

    double sampleRate() const override { return rate_; }
    bool realtime() const override { return true; }
    std::string name() const override { return dev_.name; }

private:
    DeviceInfo dev_;
    HamlibHandle rig_;
    double rate_ = 48000;
    Clock::time_point t0_;
    uint64_t sent_ = 0;
};

} // namespace

bool hamlibSupported() { return true; }
std::unique_ptr<IqSource> makeHamlibSource(const DeviceInfo& d) { return std::make_unique<HamlibSource>(d); }
std::unique_ptr<TxSink> makeHamlibTxSink(const DeviceInfo& d) { return std::make_unique<HamlibTx>(d); }

} // namespace dect2

#endif // DECT2_HAVE_HAMLIB
