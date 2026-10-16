// Hermes Lite 2 client: the HL2 speaks openHPSDR protocol 1 over UDP on port 1024 (HL2 wiki "Protocol"; XMC_SDR's HermesLite2_IO is the
// reference this follows). Discovery: 63 bytes EF FE 02 0.., broadcast; each radio answers EF FE 02 (idle) or 03 (streaming), its MAC, its
// gateware version and board id (6 for the HL2). Start/stop: 64 bytes EF FE 04 01/00, sent from the socket the radio is to stream to.
// The radio then sends EP6 packets (EF FE 01 06, a sequence number, two 512-byte frames: 7F 7F 7F, five status bytes C0..C4 and 63 samples
// of 24-bit I and Q and a 16-bit microphone word, all big endian) and expects a 1032-byte EP2 packet back for each one (its watchdog stops
// the stream without them). Every EP2 frame carries one control register in its C0..C4: 0x00 the sample rate (48, 96, 192 or 384 ksps),
// the open-collector outputs (the N2ADR filter board's relays) and the receiver count; 0x01 the TX NCO; 0x02 the RX1 NCO; 0x0a the LNA
// gain (-12 to +48 dB). The radio is only received with here: MOX is never set and the TX samples are zero.
// Future work: the 10 MHz external reference (VersaClock writes), more than one receiver, the EP4 wideband bandscope.
//
// The same client also speaks to a Red Pitaya running Pavel Demin's "SDR transceiver compatible with HPSDR" (red-pitaya-notes,
// projects/sdr_transceiver_hpsdr; the server source was read, no real board was at hand). Differences from the HL2: its discovery reply
// carries board id 1 (Hermes) and gateware 32; the receive NCO takes frequencies up to 61.44 MHz; it has no LNA register (register 0x0a
// is not sent) and no N2ADR filter board; one receiver is used (it offers up to 8, not used here). The frame layout, the rate bits
// (C1[1:0]: 48/96/192/384 ksps), the NCO registers (0x01 TX, 0x02 RX1) and start/stop are the HL2's own.
#include "native_common.h"
#include "../net/netcompat.h"
#include "dect2/net/netradio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#ifdef _WIN32
#include <mswsock.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/time.h>
#endif

namespace dect2 {
namespace native {

namespace {

constexpr int kHl2Port = 1024;
constexpr int kFrame = 512, kHeader = 8, kPacket = kHeader + 2 * kFrame;   // 1032
constexpr int kSamplesPerFrame = 63, kSamplesPerPacket = 2 * kSamplesPerFrame;
constexpr int kBoardHl2 = 6, kBoardHermes = 1;   // board ids of the discovery reply: the HL2's, and the Red Pitaya server's (Hermes)
constexpr double kRates[] = {48000, 96000, 192000, 384000};   // speed bits 0..3
constexpr double kMaxFreqHz = 38.4e6;                          // half the AD9866's 76.8 MHz clock
constexpr double kRpMaxFreqHz = 61.44e6;                       // freq_max of the Red Pitaya server

// what differs between the radios this client talks to
struct Profile {
    const char* name;      // in messages and in the radio list
    const char* board;     // DeviceInfo::board
    int boardId;           // byte 10 of the discovery reply
    double maxHz;
    bool lna, filters;     // register 0x0a (the LNA gain), the N2ADR filter board
};
const Profile kHl2 = {"Hermes Lite 2", "hermeslite2", kBoardHl2, kMaxFreqHz, true, true};
const Profile kRedPitaya = {"Red Pitaya", "redpitaya", kBoardHermes, kRpMaxFreqHz, false, false};
constexpr int kStartMs = 1500, kStallMs = 3000;

// the receive socket: a blocking recv gives up after ms; on Windows an ICMP "port unreachable" from an earlier send does not break it
sock_t udpSocket(int recvMs) {
    netInit();
    const sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kBadSock) return s;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (const sockaddr*)&a, sizeof a) != 0) { sockClose(s); return kBadSock; }
#ifdef _WIN32
    BOOL off = FALSE;
    DWORD got = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, nullptr, 0, &got, nullptr, nullptr);
    DWORD t = (DWORD)recvMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof t);
#else
    timeval tv{recvMs / 1000, (recvMs % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
    return s;
}

long long recvFrom(sock_t s, unsigned char* b, size_t n, sockaddr_in& from) {
    socklen_t len = sizeof from;
    return recvfrom(s, (char*)b, (int)n, 0, (sockaddr*)&from, &len);
}

// host (a name or an IPv4 address) and port as a socket address; false when it cannot be found
bool resolve4(const std::string& host, int port, sockaddr_in& out) {
    netInit();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return false;
    memcpy(&out, res->ai_addr, sizeof out);
    freeaddrinfo(res);
    return true;
}

std::string ip4(const sockaddr_in& a) {
    char b[INET_ADDRSTRLEN] = "";
    inet_ntop(AF_INET, &a.sin_addr, b, sizeof b);
    return b;
}

bool splitHostPort(const std::string& a, std::string& host, int& port) {
    const size_t c = a.rfind(':');
    if (c == std::string::npos || c == 0) return false;
    host = a.substr(0, c);
    port = atoi(a.c_str() + c + 1);
    return port > 0 && port < 65536;
}

// the broadcast address of every IPv4 network this computer is on (the limited broadcast 255.255.255.255 too: it does not leave a
// computer with several networks on every one of them)
std::vector<uint32_t> broadcastAddresses() {
    std::vector<uint32_t> r{0xffffffffu};
#ifdef _WIN32
    ULONG len = 16384;
    std::vector<unsigned char> buf(len);
    auto* ad = (IP_ADAPTER_ADDRESSES*)buf.data();
    const ULONG fl = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_INET, fl, nullptr, ad, &len) == ERROR_BUFFER_OVERFLOW) { buf.resize(len); ad = (IP_ADAPTER_ADDRESSES*)buf.data(); }
    if (GetAdaptersAddresses(AF_INET, fl, nullptr, ad, &len) == NO_ERROR) {
        for (auto* a = ad; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
                const unsigned bits = u->OnLinkPrefixLength;
                if (bits == 0 || bits >= 32) continue;
                const uint32_t ip = ntohl(((const sockaddr_in*)u->Address.lpSockaddr)->sin_addr.s_addr);
                r.push_back(ip | (0xffffffffu >> bits));
            }
        }
    }
#else
    ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) == 0) {
        for (auto* i = ifa; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
            if (!(i->ifa_flags & IFF_BROADCAST) || !i->ifa_broadaddr) continue;
            r.push_back(ntohl(((const sockaddr_in*)i->ifa_broadaddr)->sin_addr.s_addr));
        }
        freeifaddrs(ifa);
    }
#endif
    std::sort(r.begin(), r.end());
    r.erase(std::unique(r.begin(), r.end()), r.end());
    return r;
}

// The radios that answer a discovery packet sent to each of to within timeoutMs (each one once, in the order they answered)
std::vector<FoundNetRadio> discover(const std::vector<sockaddr_in>& to, int timeoutMs, bool broadcast, int boardId) {
    std::vector<FoundNetRadio> found;
    const sock_t s = udpSocket(50);
    if (s == kBadSock) return found;
    if (broadcast) {
        int one = 1;
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof one);
    }
    unsigned char q[63] = {0xEF, 0xFE, 0x02};
    for (const auto& a : to) sockSendTo(s, q, sizeof q, a);
    std::vector<uint32_t> seen;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    unsigned char b[256];
    while (std::chrono::steady_clock::now() < end) {
        sockaddr_in from{};
        const long long n = recvFrom(s, b, sizeof b, from);
        if (n < 11 || b[0] != 0xEF || b[1] != 0xFE || (b[2] != 0x02 && b[2] != 0x03)) continue;
        if (b[10] != boardId) continue;   // another openHPSDR radio (Hermes, Angelia, ...): its registers differ
        if (std::find(seen.begin(), seen.end(), from.sin_addr.s_addr) != seen.end()) continue;
        seen.push_back(from.sin_addr.s_addr);
        FoundNetRadio f;
        f.host = ip4(from);
        f.busy = b[2] == 0x03;
        char d[96];
        if (n > 0x15) snprintf(d, sizeof d, "MAC %02X:%02X:%02X:%02X:%02X:%02X, gateware %d.%d", b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[0x15]);
        else snprintf(d, sizeof d, "MAC %02X:%02X:%02X:%02X:%02X:%02X, gateware %d", b[3], b[4], b[5], b[6], b[7], b[8], b[9]);
        f.detail = d;
        found.push_back(f);
    }
    sockClose(s);
    return found;
}

double rateFor(double want, int& speed) {
    speed = 3;
    for (int k = 3; k >= 0; k--) if (kRates[k] >= want * 0.9999) speed = k;
    return kRates[speed];
}

// the N2ADR filter board's relays on the open-collector outputs (C2[7:1] of register 0x00): one low-pass filter per band, none above 30 MHz
uint8_t filterBits(double hz) {
    if (hz < 2.0e6) return 0x02;    // 160 m
    if (hz < 4.0e6) return 0x04;    // 80 m
    if (hz < 8.0e6) return 0x08;    // 60 / 40 m
    if (hz < 16.0e6) return 0x10;   // 30 / 20 m
    if (hz < 24.0e6) return 0x20;   // 17 / 15 m
    if (hz < 30.0e6) return 0x40;   // 12 / 10 m
    return 0x00;
}

// the registers sent, two per packet (one per frame): every one of them every second packet
constexpr uint8_t kCycle[] = {0x00, 0x01, 0x02, 0x0a};
constexpr uint8_t kCycleNoLna[] = {0x00, 0x01, 0x02, 0x02};   // a radio without the LNA register: the RX frequency twice

} // namespace

class HermesLite2Source : public NativeSource {
public:
    HermesLite2Source(const Profile& p, std::string addr) : p_(p), addr_(std::move(addr)) {}
    ~HermesLite2Source() override { stop(); }

protected:
    bool openDevice(const TuneSettings& s, std::string& err) override {
        std::string host;
        int port = 0;
        if (!splitHostPort(addr_, host, port)) { err = std::string(p_.name) + ": bad address " + addr_ + " (host:port)"; openFailure_ = OpenFailure::Final; return false; }
        if (!resolve4(host, port, dev_)) { err = std::string(p_.name) + ": cannot find the address " + host; openFailure_ = OpenFailure::Retry; return false; }
        sock_ = udpSocket(100);
        if (sock_ == kBadSock) { err = std::string(p_.name) + ": no UDP socket"; return false; }
        std::string e;
        if (!configure(s, e, false)) { err = e; return false; }
        txSeq_ = 0; cc_ = 0; rxSeqKnown_ = false;
        // the registers first (the rate it starts at), then start; it streams to this socket's address and port
        sendControl(); sendControl();
        command(true);
        sendControl(); sendControl();
        const auto t0 = std::chrono::steady_clock::now();
        auto lastStart = t0;
        unsigned char b[1500];
        bool got = false;
        while (!got && std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(kStartMs)) {
            sockaddr_in from{};
            const long long n = recvFrom(sock_, b, sizeof b, from);
            if (n >= kPacket && from.sin_addr.s_addr == dev_.sin_addr.s_addr && b[0] == 0xEF && b[1] == 0xFE && b[2] == 0x01 && b[3] == 0x06) got = true;
            else if (std::chrono::steady_clock::now() - lastStart > std::chrono::milliseconds(400)) { command(true); lastStart = std::chrono::steady_clock::now(); }
        }
        if (!got) {
            err = std::string(p_.name) + ": no samples from " + addr_ + " (is it on, and not streaming to another program?)";
            openFailure_ = OpenFailure::Retry;
            return false;
        }
        sendControl();
        fprintf(stderr, "%s: streaming from %s at %.0f ksps\n", p_.name, addr_.c_str(), rate_ / 1e3);
        fflush(stderr);
        if (lostAt_.time_since_epoch().count() != 0) {
            const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - lostAt_).count();
            const uint64_t lost = (uint64_t)std::llround(sec * rate_);
            if (ring_) ring_->noteLost(lost);
            fprintf(stderr, "%s: back after %.1f s, about %llu samples lost\n", p_.name, sec, (unsigned long long)lost);
            fflush(stderr);
            lostAt_ = {};
        }
        if (!e.empty()) err = e;
        return true;
    }
    // only kept here: the stream thread sends the registers with its next packets (a few ms at any rate)
    bool configure(const TuneSettings& s, std::string& err, bool live) override {
        (void)live;
        int speed = 3;
        rate_ = rateFor(s.sampleRate > 0 ? s.sampleRate : kRates[3], speed);
        requested_ = s.sampleRate;
        // asked as is: the frequency word of the protocol is 32-bit Hz, which is all that is held to (4.29 GHz)
        const double f = std::min(std::max(ppmCorrectedHz(s.centerHz, radioPpm(s)), 0.0), 4.29e9);
        freqHz_ = (uint32_t)std::llround(f);
        speed_ = speed;
        lna_ = (int)std::lround(std::min(std::max(s.gainDb, -12.0), 48.0)) + 12;
        oc_ = p_.filters && radioFlag(s, "filters", true) ? filterBits(f) : 0;
        swapIq_ = radioFlag(s, "swapiq", false);
        if (s.centerHz > p_.maxHz + 1) {
            char b[200];
            snprintf(b, sizeof b, "the %s is specified for up to %.1f MHz; the driver may not reach %.3f MHz or the signal may be weak or absent", p_.name, p_.maxHz / 1e6, s.centerHz / 1e6);
            err = b;
        } else checkRate(s.sampleRate, err);
        return true;
    }
    void closeDevice() override {
        if (sock_ != kBadSock) {
            command(false);
            sockClose(sock_);
        }
        sock_ = kBadSock;
    }
    bool restartForRate() const override { return false; }   // register 0x00 changes the rate while streaming
    void streamLoop() override {
        unsigned char b[1500];
        auto lastData = std::chrono::steady_clock::now(), lastStart = lastData;
        while (run_) {
            sockaddr_in from{};
            const long long n = recvFrom(sock_, b, sizeof b, from);
            const auto now = std::chrono::steady_clock::now();
            if (n < kPacket || from.sin_addr.s_addr != dev_.sin_addr.s_addr || b[0] != 0xEF || b[1] != 0xFE || b[2] != 0x01 || b[3] != 0x06) {
                if (now - lastData > std::chrono::milliseconds(kStallMs)) break;   // gone: the radio is opened again
                if (now - lastData > std::chrono::milliseconds(500) && now - lastStart > std::chrono::milliseconds(500)) { command(true); lastStart = now; }
                continue;
            }
            lastData = now;
            const uint32_t seq = (uint32_t)b[4] << 24 | (uint32_t)b[5] << 16 | (uint32_t)b[6] << 8 | b[7];
            if (rxSeqKnown_ && seq != rxSeq_ + 1) {
                const uint32_t gap = seq - rxSeq_ - 1;
                if (gap < 100000 && ring_) ring_->noteLost((uint64_t)gap * kSamplesPerPacket);
            }
            rxSeq_ = seq;
            rxSeqKnown_ = true;
            conv_.resize(kSamplesPerPacket);
            const size_t k = frame(b + kHeader, conv_.data(), swapIq_) + frame(b + kHeader + kFrame, conv_.data() + kSamplesPerFrame, swapIq_);
            push(conv_.data(), k == (size_t)kSamplesPerPacket ? k : 0);
            sendControl();   // one back for each one received: the radio's watchdog
        }
        if (run_) {
            fprintf(stderr, "%s: no samples from %s for %d s\n", p_.name, addr_.c_str(), kStallMs / 1000);
            fflush(stderr);
            lostAt_ = std::chrono::steady_clock::now();
        }
    }

private:
    // one frame's 63 samples into out (0 when it is not a frame). The HL2 sends Q before I (measured with XMC_SDR): taken the right way round.
    static size_t frame(const unsigned char* f, cf32* out, bool swapIq) {
        if (f[0] != 0x7F || f[1] != 0x7F || f[2] != 0x7F) return 0;
        const unsigned char* p = f + 8;
        for (int i = 0; i < kSamplesPerFrame; i++, p += 8) {
            const int32_t a = (int32_t)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8) >> 8;
            const int32_t c = (int32_t)((uint32_t)p[3] << 24 | (uint32_t)p[4] << 16 | (uint32_t)p[5] << 8) >> 8;
            out[i] = swapIq ? cf32(a / 8388608.f, c / 8388608.f) : cf32(c / 8388608.f, a / 8388608.f);
        }
        return kSamplesPerFrame;
    }
    // one EP2 frame with register reg in its C0..C4; the TX samples stay zero
    void controlFrame(unsigned char* f, uint8_t reg) const {
        memset(f, 0, kFrame);
        f[0] = f[1] = f[2] = 0x7F;
        f[3] = (uint8_t)(reg << 1);   // MOX (bit 0) never set
        uint32_t v = 0;
        switch (reg) {
        case 0x00:   // speed in C1[1:0], the open-collector outputs in C2[7:1], duplex (C4[2]: the RX NCO is its own) and one receiver
            v = (uint32_t)(speed_ & 3) << 24 | (uint32_t)oc_ << 16 | 0x04;
            break;
        case 0x01:   // the TX NCO: at the receive frequency
        case 0x02:   // the RX1 NCO
            v = freqHz_;
            break;
        case 0x0a:   // the LNA gain in its direct mode (C4[6]): -12 dB (0) to +48 dB (60)
            v = 0x40 | (uint32_t)(lna_ & 0x3f);
            break;
        }
        f[4] = (uint8_t)(v >> 24); f[5] = (uint8_t)(v >> 16); f[6] = (uint8_t)(v >> 8); f[7] = (uint8_t)v;
    }
    void sendControl() {
        unsigned char p[kPacket];
        p[0] = 0xEF; p[1] = 0xFE; p[2] = 0x01; p[3] = 0x02;
        p[4] = (uint8_t)(txSeq_ >> 24); p[5] = (uint8_t)(txSeq_ >> 16); p[6] = (uint8_t)(txSeq_ >> 8); p[7] = (uint8_t)txSeq_;
        txSeq_++;
        const uint8_t* cyc = p_.lna ? kCycle : kCycleNoLna;
        controlFrame(p + kHeader, cyc[cc_]);
        controlFrame(p + kHeader + kFrame, cyc[(cc_ + 1) % 4]);
        cc_ = (cc_ + 2) % 4;
        sockSendTo(sock_, p, sizeof p, dev_);
    }
    void command(bool start) {
        unsigned char c[64] = {0xEF, 0xFE, 0x04, (unsigned char)(start ? 0x01 : 0x00)};
        sockSendTo(sock_, c, sizeof c, dev_);
    }

    const Profile& p_;
    std::string addr_;
    sockaddr_in dev_{};
    sock_t sock_ = kBadSock;
    // what configure() set, read by the stream thread for the registers it sends
    std::atomic<uint32_t> freqHz_{0};
    std::atomic<int> speed_{3}, lna_{32};
    std::atomic<uint8_t> oc_{0};
    std::atomic<bool> swapIq_{false};
    uint32_t txSeq_ = 0, rxSeq_ = 0;
    bool rxSeqKnown_ = false;
    int cc_ = 0;
    std::chrono::steady_clock::time_point lostAt_{};
};

DeviceInfo hpsdr1Device(const Profile& p, const std::string& host, int port) {
    DeviceInfo d;
    d.kind = DeviceInfo::Native;
    d.board = p.board;
    d.nativeArgs = host + ":" + std::to_string(port);
    d.serial = d.nativeArgs;
    d.name = std::string(p.name) + " " + (port == kHl2Port ? host : d.nativeArgs) + " (network)";
    for (double r : kRates) d.rateRanges.push_back({r, r});
    d.minRateHz = kRates[0];
    d.maxRateHz = kRates[3];
    d.minFreqHz = 10e3;
    d.maxFreqHz = p.maxHz;
    const RadioSetting swap = boolSetting("swapiq", "Swap I and Q",
                                         "Mirror the spectrum if signals appear on the wrong side of the tuned frequency (the I and Q words of the stream are taken the other way round).",
                                         false);
    if (!p.lna) {   // no gain register is known for it: no gain control is offered
        d.settings = {ppmSetting(0.1), swap};
        return d;
    }
    d.gainMinDb = -12;
    d.gainMaxDb = 48;
    d.settings = {ppmSetting(0.1),
                  boolSetting("filters", "N2ADR filter board",
                              "Switch the low-pass filter for the band on the N2ADR filter board (its relays are on the open-collector outputs).\n"
                              "Above 30 MHz no filter is switched in. Without the board this changes nothing.",
                              true)};
    return d;
}

bool hpsdr1Probe(const Profile& p, const std::string& host, int port, int timeoutMs, std::string& err) {
    sockaddr_in a{};
    if (!resolve4(host, port, a)) { err = "cannot find the address " + host + " (a " + p.name + " needs an IPv4 address)"; return false; }
    if (!discover({a}, timeoutMs, false, p.boardId).empty()) return true;
    err = std::string("no ") + p.name + " answered at " + host + ":" + std::to_string(port) + " (wrong address, or the radio is off";
    err += p.boardId == kBoardHermes ? ", or its SDR transceiver app is not running)" : ")";
    return false;
}

DeviceInfo hermesLite2Device(const std::string& host, int port) { return hpsdr1Device(kHl2, host, port); }
bool hermesLite2Probe(const std::string& host, int port, int timeoutMs, std::string& err) { return hpsdr1Probe(kHl2, host, port, timeoutMs, err); }
std::unique_ptr<IqSource> makeHermesLite2(const DeviceInfo& d) { return std::make_unique<HermesLite2Source>(kHl2, d.nativeArgs); }

DeviceInfo redPitayaDevice(const std::string& host, int port) { return hpsdr1Device(kRedPitaya, host, port); }
bool redPitayaProbe(const std::string& host, int port, int timeoutMs, std::string& err) { return hpsdr1Probe(kRedPitaya, host, port, timeoutMs, err); }
std::unique_ptr<IqSource> makeRedPitaya(const DeviceInfo& d) { return std::make_unique<HermesLite2Source>(kRedPitaya, d.nativeArgs); }

} // namespace native

namespace {
std::vector<FoundNetRadio> discoverHpsdr1(int boardId, int timeoutMs) {
    std::vector<sockaddr_in> to;
    for (uint32_t b : native::broadcastAddresses()) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(native::kHl2Port);
        a.sin_addr.s_addr = htonl(b);
        to.push_back(a);
    }
    return native::discover(to, timeoutMs, true, boardId);
}
} // namespace

std::vector<FoundNetRadio> discoverHermesLite2(int timeoutMs) { return discoverHpsdr1(native::kBoardHl2, timeoutMs); }
std::vector<FoundNetRadio> discoverRedPitaya(int timeoutMs) { return discoverHpsdr1(native::kBoardHermes, timeoutMs); }

} // namespace dect2
