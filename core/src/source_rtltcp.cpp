// rtl_tcp client: an RTL-SDR served over the network by rtl_tcp (librtlsdr's server, also SDR# / SDR++ / OpenWebRX servers that speak it).
// The protocol: on connect the server sends 12 bytes, "RTL0", the tuner type and its number of gains (big endian 32-bit each), then streams
// unsigned 8-bit IQ without end. Commands are 5 bytes: one command byte and a big-endian 32-bit parameter.
#include "tcp_client.h"
#include "native_common.h"
#include <map>

namespace dect2 {
namespace native {

namespace {

enum : uint8_t {
    kCmdFreq = 0x01, kCmdRate = 0x02, kCmdGainMode = 0x03, kCmdGain = 0x04, kCmdPpm = 0x05, kCmdIfGain = 0x06, kCmdTestMode = 0x07,
    kCmdAgc = 0x08, kCmdDirect = 0x09, kCmdOffset = 0x0a, kCmdGainIndex = 0x0d, kCmdBiasTee = 0x0e,
};
enum { kTunerUnknown = 0, kE4000 = 1, kFC0012 = 2, kFC0013 = 3, kFC2580 = 4, kR820T = 5, kR828D = 6 };
constexpr int kConnectMs = 3000;   // an unreachable server is reported after this
constexpr int kStallMs = 3000;     // no samples for this long: the connection is taken as lost

// The tuner's gains in tenths of a dB, as librtlsdr's tuner drivers list them (what rtlsdr_get_tuner_gains gives the server)
std::vector<int> tunerGains(int tuner) {
    switch (tuner) {
    case kE4000: return {-10, 15, 40, 65, 90, 115, 140, 165, 190, 215, 240, 290, 340, 420};
    case kFC0012: return {-99, -40, 71, 179, 192};
    case kFC0013: return {-99, -73, -65, -63, -60, -58, -54, 58, 61, 63, 65, 67, 68, 70, 71, 179, 181, 182, 184, 186, 188, 191, 197};
    case kFC2580: return {0};
    default:   // R820T, R828D (and an unknown tuner: the most common one)
        return {0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254, 280, 297, 328, 338, 364, 372, 386, 402, 421, 434, 439, 445, 480, 496};
    }
}
const char* tunerName(int t) {
    switch (t) {
    case kE4000: return "E4000"; case kFC0012: return "FC0012"; case kFC0013: return "FC0013"; case kFC2580: return "FC2580";
    case kR820T: return "R820T"; case kR828D: return "R828D"; default: return "unknown tuner";
    }
}

bool splitHostPort(const std::string& a, std::string& host, int& port) {
    const size_t c = a.rfind(':');
    if (c == std::string::npos || c == 0) return false;
    host = a.substr(0, c);
    if (host.size() > 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);   // [::1]:1234
    port = atoi(a.c_str() + c + 1);
    return port > 0 && port < 65536;
}

uint32_t be32(const unsigned char* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

} // namespace

class RtlTcpSource : public NativeSource {
public:
    explicit RtlTcpSource(std::string addr) : addr_(std::move(addr)) {}
    ~RtlTcpSource() override { stop(); }

protected:
    bool openDevice(const TuneSettings& s, std::string& err) override {
        std::string host;
        int port = 0;
        if (!splitHostPort(addr_, host, port)) { err = "rtl_tcp: bad address " + addr_ + " (host:port)"; openFailure_ = OpenFailure::Final; return false; }
        std::string e;
        const sock_t k = tcpConnect(host, port, kConnectMs, e);
        if (k == kBadSock) { err = "rtl_tcp: " + e; openFailure_ = OpenFailure::Retry; return false; }
        sock_ = k;
        sockRecvTimeout(sock_, kConnectMs);
        unsigned char hdr[12];
        if (!sockRecvAll(sock_, hdr, sizeof hdr)) { err = "rtl_tcp: " + addr_ + " connected but sent no greeting (another client may have it)"; return false; }
        if (memcmp(hdr, "RTL0", 4) != 0) { err = "rtl_tcp: " + addr_ + " is not an rtl_tcp server"; openFailure_ = OpenFailure::Final; return false; }
        tuner_ = (int)be32(hdr + 4);
        const uint32_t nGains = be32(hdr + 8);
        gains_ = tunerGains(tuner_);
        sockRecvTimeout(sock_, kStallMs);
        fprintf(stderr, "rtl_tcp: connected to %s (%s, %u gains)\n", addr_.c_str(), tunerName(tuner_), (unsigned)nGains);
        fflush(stderr);
        // the samples missed while the connection was down (the receiver sees a gap of that many)
        if (lostAt_.time_since_epoch().count() != 0) {
            const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - lostAt_).count();
            const uint64_t n = (uint64_t)std::llround(sec * (rate_ > 0 ? rate_ : 0));
            lost_ += n;
            if (ring_) ring_->noteLost(n);
            fprintf(stderr, "rtl_tcp: reconnected after %.1f s, about %llu samples lost\n", sec, (unsigned long long)n);
            fflush(stderr);
            lostAt_ = {};
        }
        sentRate_ = sentFreq_ = 0;
        sentGain_ = sentPpm_ = sentDs_ = sentBias_ = -1000;
        carry_ = -1;
        if (!command(kCmdGainMode, 1) || !command(kCmdAgc, 0)) { err = "rtl_tcp: the connection to " + addr_ + " dropped"; return false; }   // manual gain: OnAir runs its own AGC
        return configure(s, err, false);
    }
    bool configure(const TuneSettings& s, std::string& err, bool live) override {
        (void)live;
        if (sock_ == kBadSock) return false;
        if (s.sampleRate > 3.2e6) {
            char b[160];
            snprintf(b, sizeof b, "an RTL-SDR (rtl_tcp) gives at most 3.2 Msps; this mode needs about %.0f Msps: use a faster radio", s.sampleRate / 1e6);
            err = b;
            openFailure_ = OpenFailure::Final;
            return false;
        }
        double want = std::min(s.sampleRate > 0 ? s.sampleRate : 2.048e6, 2.56e6);
        if (want < 900001 && (want > 300000 || want <= 225000)) want = 900001;
        const uint32_t rate = (uint32_t)std::lround(want);
        bool ok = true;
        if (rate != sentRate_) { ok = ok && command(kCmdRate, rate); sentRate_ = rate; rate_ = rate; }
        requested_ = s.sampleRate;
        const double f = std::max(s.centerHz, 0.0);
        const std::string mode = radioOption(s, "ds", "off");
        const int ds = (mode == "i" || mode == "q") && f < 24e6 ? (mode == "i" ? 1 : 2) : 0;
        if (ds != sentDs_) { ok = ok && command(kCmdDirect, (uint32_t)ds); sentDs_ = ds; }
        const int ppm = (int)std::lround(radioPpm(s));
        if (ppm != sentPpm_) { ok = ok && command(kCmdPpm, (uint32_t)ppm); sentPpm_ = ppm; }
        const uint32_t hz = (uint32_t)std::llround(std::min(f, 4.29e9));
        if (hz != sentFreq_) { ok = ok && command(kCmdFreq, hz); sentFreq_ = hz; }
        const int bias = s.biasTee ? 1 : 0;
        if (bias != sentBias_ && (bias || sentBias_ == 1)) { ok = ok && command(kCmdBiasTee, (uint32_t)bias); }
        sentBias_ = bias;
        // the closest gain the tuner has
        const int target = (int)std::lround(s.gainDb * 10);
        int best = gains_.empty() ? target : gains_[0];
        for (int g : gains_) if (std::abs(g - target) < std::abs(best - target)) best = g;
        if (best != sentGain_) { ok = ok && command(kCmdGain, (uint32_t)best); sentGain_ = best; }
        if (!ok) err = "rtl_tcp: the connection to " + addr_ + " dropped";
        return ok;
    }
    void closeDevice() override {
        std::lock_guard<std::mutex> lk(sendMu_);
        if (sock_ != kBadSock) {
            if (sentBias_ == 1) { const unsigned char c[5] = {kCmdBiasTee, 0, 0, 0, 0}; sockSendAll(sock_, c, 5); }   // antenna power off
            sockShutdown(sock_);
            sockClose(sock_);
        }
        sock_ = kBadSock;
        sentBias_ = -1000;
    }
    bool restartForRate() const override { return false; }   // the server changes the rate while streaming
    void streamLoop() override {
        std::vector<unsigned char> buf(1 << 16);
        while (run_) {
            const long long k = sockRecv(sock_, buf.data() + (carry_ >= 0 ? 1 : 0), buf.size() - 1);
            if (k <= 0) break;   // closed, failed or stalled
            size_t n = (size_t)k;
            if (carry_ >= 0) { buf[0] = (unsigned char)carry_; n++; carry_ = -1; }
            if (n & 1) { carry_ = buf[n - 1]; n--; }   // half an IQ pair: kept for the next read
            convert(buf.data(), n);
        }
        if (run_) {
            fprintf(stderr, "rtl_tcp: lost the connection to %s\n", addr_.c_str());
            fflush(stderr);
            lostAt_ = std::chrono::steady_clock::now();
        }
    }
    void interrupt() override { if (sock_ != kBadSock) sockShutdown(sock_); }

public:
    uint64_t lostSamples() const { return lost_; }

private:
    bool command(uint8_t cmd, uint32_t v) {
        const unsigned char c[5] = {cmd, (unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v};
        std::lock_guard<std::mutex> lk(sendMu_);
        return sock_ != kBadSock && sockSendAll(sock_, c, 5);
    }
    void convert(const unsigned char* b, size_t bytes) {
        const size_t n = bytes / 2;
        conv_.resize(n);
        for (size_t i = 0; i < n; i++) conv_[i] = cf32((b[2 * i] - 127.4f) / 128.f, (b[2 * i + 1] - 127.4f) / 128.f);
        push(conv_.data(), n);
    }

    std::string addr_;
    sock_t sock_ = kBadSock;
    std::mutex sendMu_;
    int tuner_ = kTunerUnknown;
    std::vector<int> gains_;
    uint32_t sentRate_ = 0, sentFreq_ = 0;
    int sentGain_ = -1000, sentPpm_ = -1000, sentDs_ = -1000, sentBias_ = -1000;
    int carry_ = -1;
    std::chrono::steady_clock::time_point lostAt_{};
    std::atomic<uint64_t> lost_{0};
};

// The entry of an rtl_tcp server at host:port
DeviceInfo rtlTcpDevice(const std::string& host, int port) {
    DeviceInfo d;
    d.kind = DeviceInfo::Native;
    d.board = "rtltcp";
    const bool v6 = host.find(':') != std::string::npos;
    d.nativeArgs = (v6 ? "[" + host + "]" : host) + ":" + std::to_string(port);
    d.serial = d.nativeArgs;
    d.name = "rtl_tcp " + d.nativeArgs + " (network)";
    d.maxRateHz = 2.56e6; d.minRateHz = 0.9e6;
    d.rateRanges = {{0.225001e6, 0.3e6}, {0.900001e6, 2.56e6}};
    d.gainMinDb = 0; d.gainMaxDb = 49.6;
    d.hasBiasTee = true;   // rtl_tcp of the RTL-SDR Blog library (command 0x0e); other servers ignore it
    d.settings = {ppmSetting(1),
                  choiceSetting("ds", "Direct sampling (HF)",
                                "Below 24 MHz the tuner cannot receive: the RTL2832U can sample an HF antenna on one of its inputs directly.\n"
                                "I / Q: that input below 24 MHz (the RTL-SDR Blog V3 has its HF input on Q). Off: never.",
                                {"off", "i", "q"}, {"Off", "I input", "Q input"}, "off")};
    return d;
}

std::unique_ptr<IqSource> makeRtlTcp(const DeviceInfo& d) { return std::make_unique<RtlTcpSource>(d.nativeArgs); }

} // namespace native
} // namespace dect2
