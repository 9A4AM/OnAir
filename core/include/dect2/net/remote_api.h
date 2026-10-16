// Remote-control API: a small HTTP/1.1 + WebSocket (RFC 6455) server so scripts and other programs can list the radios, start and stop the receiver,
// set the mode, frequency, sample rate and gain, read the telemetry as JSON, subscribe to telemetry and the spectrum over a WebSocket, and fetch
// the latest spectrum line. No dependencies (sockets only). Off until the user turns it on; it listens on 127.0.0.1 unless "lan" is set, and a
// connection from any other address needs the token (lan without a token is refused). The endpoints are listed in README.md.
//
// Hardening: header block <= 8 KB, body <= 4 KB, 5 s to send a request, 16 connections (8 WebSockets), POST needs Content-Type: application/json
// (so a web page cannot send one without a CORS preflight, which is never granted: the server sends no CORS headers), a request that carries an
// Origin header must come from the server's own origin, and a loopback request must name a loopback Host (DNS rebinding).
#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace dect2 {

struct RemoteApiConfig {
    bool on = false;
    int port = 8765;          // 0: any free port (tests); config() then tells which
    bool lan = false;         // listen on every address (else 127.0.0.1 only); needs a token
    std::string token;        // >= 16 characters; required from every non-loopback peer (Authorization: Bearer <token>, or ?token=)
};

struct RemoteApiStatus {
    bool listening = false;
    std::string lastError;
    int httpClients = 0, wsClients = 0;   // connections open now
    uint64_t requests = 0, rejected = 0;  // handled / refused (bad auth, bad request, too many connections)
};

// What the program behind the API offers. The server calls it from its connection threads, one call at a time (the server serialises them), so a
// host that owns a GUI must hand work to its own thread. Every *Json function returns a complete JSON value; the setters return false with err set.
class RemoteApiHost {
public:
    virtual ~RemoteApiHost() = default;
    virtual std::string statusJson() = 0;                         // running, device, mode, frequency, rate, gain
    virtual std::string devicesJson() = 0;                        // the radios and sources the host knows
    virtual std::string modesJson() = 0;                          // the modes (family numbers) setMode accepts
    virtual std::string telemetryJson(uint64_t& seq) = 0;         // the active mode's telemetry; seq changes when it is a new report
    virtual std::string spectrumJson(int maxBins, uint64_t& seq) = 0;   // the latest spectrum line (at most maxBins values)
    virtual bool start(int deviceIndex, std::string& err) = 0;    // deviceIndex < 0: the selected one
    virtual bool stop(std::string& err) = 0;
    virtual bool setMode(int family, const std::string& subId, std::string& err) = 0;
    virtual bool setFrequencyHz(double hz, std::string& err) = 0;
    virtual bool setSampleRateHz(double hz, std::string& err) = 0;   // 0: automatic
    virtual bool setGainDb(double db, std::string& err) = 0;
};

class RemoteApiServer {
public:
    explicit RemoteApiServer(RemoteApiHost& host);
    ~RemoteApiServer();
    // Starts, restarts or stops the server as the config says. False (and status().lastError) when it cannot listen or lan has no usable token.
    bool configure(const RemoteApiConfig& c);
    RemoteApiConfig config() const;
    RemoteApiStatus status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// A fresh random token of 24 hex characters
std::string remoteApiNewToken();

// ---- helpers the server is built from (public so they can be tested)
std::string sha1(const std::string& data);                 // 20 raw bytes
std::string base64(const std::string& data);
std::string wsAcceptKey(const std::string& clientKey);     // base64(sha1(key + GUID)), RFC 6455 4.2.2
std::string jsonEscape(const std::string& s);              // the content of a JSON string (no quotes)
std::string jsonNum(double v);                             // a number, or null for NaN / inf
// one WebSocket frame, server to client (never masked)
std::string wsFrame(int opcode, const std::string& payload);

struct JsonVal { enum Kind { Null, Num, Str, Bool } kind = Null; double num = 0; std::string str; bool b = false; };
// A flat JSON object ({"a": 1, "b": "x", "c": true}); nesting, arrays and anything malformed give false. Keys are kept in `out`.
bool parseFlatJson(const std::string& text, std::map<std::string, JsonVal>& out);

} // namespace dect2
