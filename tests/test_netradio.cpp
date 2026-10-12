// Network radios (netradio.h): the rtl_tcp client against a fake rtl_tcp server on localhost, a user-added Pluto address, the saved list.
#include "../core/src/tcp_client.h"
#include "dect2/netradio.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <csignal>
#endif

using namespace dect2;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// A fake rtl_tcp server: greets with "RTL0" (R820T, 29 gains), streams I=200, Q=50 and records every command it gets
struct FakeServer {
    sock_t ls = kBadSock;
    int port = 0;
    std::thread th;
    std::atomic<bool> run{true}, dropNow{false};
    std::atomic<int> connections{0};
    std::mutex mu;
    std::vector<std::pair<int, uint32_t>> cmds;

    bool start() {
        netInit();
        ls = socket(AF_INET, SOCK_STREAM, 0);
        if (ls == kBadSock) return false;
        int one = 1;
        setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (bind(ls, (sockaddr*)&a, sizeof a) != 0 || listen(ls, 2) != 0) return false;
        socklen_t len = sizeof a;
        getsockname(ls, (sockaddr*)&a, &len);
        port = ntohs(a.sin_port);
        th = std::thread([this] { loop(); });
        return true;
    }
    bool has(int cmd, uint32_t v) {
        std::lock_guard<std::mutex> lk(mu);
        for (auto& c : cmds) if (c.first == cmd && c.second == v) return true;
        return false;
    }
    bool waitFor(int cmd, uint32_t v, int ms = 3000) {
        for (int i = 0; i < ms / 10; i++) { if (has(cmd, v)) return true; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        return false;
    }
    void loop() {
        while (run) {
            fd_set rd; FD_ZERO(&rd); FD_SET(ls, &rd);
            timeval tv{0, 100000};
            if (select((int)ls + 1, &rd, nullptr, nullptr, &tv) <= 0) continue;
            sock_t c = accept(ls, nullptr, nullptr);
            if (c == kBadSock) continue;
            connections++;
            const unsigned char hdr[12] = {'R', 'T', 'L', '0', 0, 0, 0, 5, 0, 0, 0, 29};
            sockSendAll(c, hdr, sizeof hdr);
            std::vector<unsigned char> chunk(4096);
            for (size_t i = 0; i < chunk.size(); i += 2) { chunk[i] = 200; chunk[i + 1] = 50; }
            std::vector<unsigned char> pend;
            while (run && !dropNow) {
                // the commands that came in
                fd_set r2; FD_ZERO(&r2); FD_SET(c, &r2);
                timeval t0{0, 0};
                if (select((int)c + 1, &r2, nullptr, nullptr, &t0) > 0) {
                    unsigned char b[256];
                    const long long k = sockRecv(c, b, sizeof b);
                    if (k <= 0) break;
                    pend.insert(pend.end(), b, b + k);
                    while (pend.size() >= 5) {
                        const uint32_t v = (uint32_t)pend[1] << 24 | (uint32_t)pend[2] << 16 | (uint32_t)pend[3] << 8 | pend[4];
                        { std::lock_guard<std::mutex> lk(mu); cmds.push_back({pend[0], v}); }
                        pend.erase(pend.begin(), pend.begin() + 5);
                    }
                }
                if (!sockSendAll(c, chunk.data(), chunk.size())) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            sockShutdown(c);
            sockClose(c);
            dropNow = false;
        }
    }
    void stop() { run = false; if (th.joinable()) th.join(); if (ls != kBadSock) sockClose(ls); }
};

static void testRtlTcp() {
    FakeServer srv;
    CHECK(srv.start());
    if (!srv.port) return;
    NetRadio nr; nr.type = NetRadio::RtlTcp; nr.host = "127.0.0.1"; nr.port = srv.port;
    std::string err;
    CHECK(probeNetRadio(nr, 2000, err));
    const auto devs = netRadioDevices({nr});
    CHECK(devs.size() == 1);
    if (devs.empty()) { srv.stop(); return; }
    const DeviceInfo& d = devs[0];
    CHECK(d.kind == DeviceInfo::Native && d.board == "rtltcp" && d.nativeArgs == "127.0.0.1:" + std::to_string(srv.port));
    CHECK(d.name == netRadioDeviceName(nr));
    // wait for the probe's connection to be over before the source connects
    for (int i = 0; i < 100 && srv.connections < 1; i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const int before = srv.connections;

    auto src = makeSource(d);
    CHECK(src != nullptr);
    if (!src) { srv.stop(); return; }
    IqRing ring(1u << 20);
    TuneSettings s;
    s.sampleRate = 2.048e6;
    s.centerHz = 100e6;
    s.gainDb = 30;
    s.radio["ppm"] = "3";
    err.clear();
    CHECK(src->start(s, ring, err));
    CHECK(srv.waitFor(0x03, 1));            // manual gain
    CHECK(srv.waitFor(0x08, 0));            // AGC off
    CHECK(srv.waitFor(0x02, 2048000));      // rate
    CHECK(srv.waitFor(0x01, 100000000));    // frequency
    CHECK(srv.waitFor(0x05, 3));            // ppm
    CHECK(srv.waitFor(0x04, 297));          // 30 dB -> the R820T's 29.7 dB
    CHECK(std::fabs(src->sampleRate() - 2.048e6) < 1);

    // the samples arrive, as the server sent them
    std::vector<cf32> buf(4096);
    size_t got = 0;
    for (int i = 0; i < 300 && got < buf.size(); i++) { got += ring.read(buf.data() + got, buf.size() - got); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    CHECK(got == buf.size());
    bool okVals = got > 0;
    for (size_t i = 0; i < got; i++) if (std::fabs(buf[i].real() - (200 - 127.4f) / 128.f) > 1e-5f || std::fabs(buf[i].imag() - (50 - 127.4f) / 128.f) > 1e-5f) okVals = false;
    CHECK(okVals);

    // live changes
    s.centerHz = 101.5e6; s.gainDb = 40;
    CHECK(src->retune(s, err));
    CHECK(srv.waitFor(0x01, 101500000));
    CHECK(srv.waitFor(0x04, 402));
    s.sampleRate = 1.024e6;
    CHECK(src->retune(s, err));
    CHECK(srv.waitFor(0x02, 1024000));

    // the server drops the connection: the source connects again, sends its settings again and counts the samples it missed
    const uint64_t dropped0 = ring.dropped();
    { std::lock_guard<std::mutex> lk(srv.mu); srv.cmds.clear(); }
    srv.dropNow = true;
    bool back = false;
    for (int i = 0; i < 500 && !back; i++) { back = srv.connections >= before + 2; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    CHECK(back);
    CHECK(srv.waitFor(0x01, 101500000));
    CHECK(srv.waitFor(0x02, 1024000));
    CHECK(ring.dropped() > dropped0);
    ring.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(ring.available() > 0);   // streaming again
    src->stop();
    srv.stop();

    // nothing listening there any more: a clear error, quickly
    err.clear();
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(!probeNetRadio(nr, 2000, err));
    CHECK(!err.empty());
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
    fprintf(stderr, "unreachable: %s\n", err.c_str());
}

static void testPluto() {
    NetRadio p; p.type = NetRadio::Pluto; p.host = "10.1.2.3";
    auto v = netRadioDevices({p});
    CHECK(v.size() == 1);
    if (v.size() == 1) {
        CHECK(v[0].kind == DeviceInfo::Native && v[0].board == "pluto");
        CHECK(v[0].nativeArgs == "ip:10.1.2.3");
        CHECK(v[0].steadyRateHz == 0);            // a real network: the converter's full rate is offered
        CHECK(v[0].maxRateHz > 60e6);
    }
    p.host = "ip:192.168.2.1";                    // typed by hand: a Pluto+ on Ethernet often has the USB-gadget address too (#54), so no USB limit
    v = netRadioDevices({p});
    CHECK(v.size() == 1 && v[0].nativeArgs == "ip:192.168.2.1" && v[0].steadyRateHz == 0);
    p.host = "pluto.lan"; p.port = 30432;
    v = netRadioDevices({p});
    CHECK(v.size() == 1 && v[0].nativeArgs == "ip:pluto.lan:30432");
    // registered radios are what listRadios() appends
    setNetworkRadios({p});
    CHECK(networkRadios().size() == 1 && networkRadios()[0] == p);
    setNetworkRadios({});
}

static void testPrefs() {
    NetRadio a; a.type = NetRadio::Pluto; a.host = "192.168.2.1";
    NetRadio b; b.type = NetRadio::RtlTcp; b.host = "raspberrypi.local"; b.port = 1234;
    NetRadio c; c.type = NetRadio::SoapyRemote; c.host = "10.0.0.7"; c.port = 55132;
    const std::string s = netRadiosToString({a, b, c});
    const auto back = netRadiosFromString(s);
    CHECK(back.size() == 3 && back[0] == a && back[1] == b && back[2] == c);
    CHECK(netRadiosFromString("junk;rtltcp,,1;bogus,h,1").empty());
    CHECK(netRadioDefaultPort(NetRadio::RtlTcp) == 1234 && netRadioDefaultPort(NetRadio::SoapyRemote) == 55132 && netRadioDefaultPort(NetRadio::Pluto) == 0);
#ifdef DECT2_HAVE_SOAPY
    const auto v = netRadioDevices({c});
    CHECK(v.size() == 1 && v[0].kind == DeviceInfo::Soapy && v[0].soapyArgs == "driver=remote,remote=tcp://10.0.0.7:55132");
#else
    CHECK(netRadioDevices({c}).empty());
#endif
}

int main() {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);   // the fake server writes to clients that went away
#endif
    testPrefs();
    testPluto();
    testRtlTcp();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    printf("netradio: all passed\n");
    return 0;
}
