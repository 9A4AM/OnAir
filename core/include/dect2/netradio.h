// Radios on the network that the user adds by address (issues #28, #54): a PlutoSDR by IP, an rtl_tcp server, a SoapyRemote server.
// They are listed with the other radios (listRadios) once setNetworkRadios() has them.
#pragma once
#include "source.h"
#include <string>
#include <vector>

namespace dect2 {

struct NetRadio {
    enum Type { Pluto = 0, RtlTcp = 1, SoapyRemote = 2, SpyServer = 3 } type = RtlTcp;
    std::string host;   // name or address ("ip:" in front of a Pluto's is accepted and dropped)
    int port = 0;       // 0 = the type's default (a Pluto: libiio's own, 30431)
    bool operator==(const NetRadio& o) const { return type == o.type && host == o.host && port == o.port; }
};

const char* netRadioTypeName(NetRadio::Type t);       // "PlutoSDR", "rtl_tcp", "SoapyRemote", "SpyServer"
int netRadioDefaultPort(NetRadio::Type t);            // 0 (Pluto), 1234, 55132
const char* netRadioDefaultHost(NetRadio::Type t);    // "192.168.2.1" for a Pluto, "" otherwise
int netRadioPort(const NetRadio& r);                  // the port connected to (a Pluto without one: 30431, its iiod)

// SpyServer requires the optional SoapySpyServer module on the OnAir computer.
// The list as the settings keep it: "rtltcp,host,port;pluto,host,0;soapyremote,host,port" (and back; bad items are skipped)
std::string netRadiosToString(const std::vector<NetRadio>& v);
std::vector<NetRadio> netRadiosFromString(const std::string& s);

// The radios listRadios() adds after the ones it finds (thread-safe)
void setNetworkRadios(const std::vector<NetRadio>& v);
std::vector<NetRadio> networkRadios();
// The device entries of these radios: a Pluto as the native "ip:<host>" entry, rtl_tcp as the native "rtltcp" one, SoapyRemote as the
// SoapySDR device "driver=remote,remote=tcp://host:port" (left out when built without SoapySDR)
std::vector<DeviceInfo> netRadioDevices(const std::vector<NetRadio>& v);
// The name the entry of r has in the radio list
std::string netRadioDeviceName(const NetRadio& r);

// Whether the radio answers within timeoutMs: a TCP connection to it (an rtl_tcp server must also send its "RTL0" header). Blocks: call it
// on a worker thread. false: err says why, in plain words.
bool probeNetRadio(const NetRadio& r, int timeoutMs, std::string& err);

} // namespace dect2
