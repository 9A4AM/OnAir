#include "dect2/source/usb_fd.h"
#include <algorithm>
#include <mutex>

namespace dect2 {
namespace {
std::mutex gMu;
std::vector<UsbFdRadio>& reg() { static std::vector<UsbFdRadio> v; return v; }
}

bool usbFdAttach(const UsbFdRadio& r) {
    if (r.fd < 0 || r.name.empty()) return false;
    std::lock_guard<std::mutex> lk(gMu);
    auto& v = reg();
    for (auto& e : v) if (e.name == r.name) { e = r; return true; }
    v.push_back(r);
    return true;
}

void usbFdDetach(const std::string& name) {
    std::lock_guard<std::mutex> lk(gMu);
    auto& v = reg();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const UsbFdRadio& e) { return e.name == name; }), v.end());
}

std::vector<UsbFdRadio> usbFdList(const std::string& kind) {
    std::lock_guard<std::mutex> lk(gMu);
    std::vector<UsbFdRadio> out;
    for (const auto& e : reg()) if (kind.empty() || e.kind == kind) out.push_back(e);
    return out;
}

std::string usbFdKindForId(int vid, int pid) {
    if (vid == 0x0bda && (pid == 0x2832 || pid == 0x2838)) return "rtlsdr";   // RTL2832U (generic and the Blog/NESDR sticks that keep the Realtek id)
    if (vid == 0x1d50 && pid == 0x6089) return "hackrf";                      // HackRF One (Pro and Jawbreaker share 6089 / cc15 / 604b)
    if (vid == 0x1d50 && (pid == 0xcc15 || pid == 0x604b)) return "hackrf";
    if (vid == 0x1d50 && pid == 0x60a1) return "airspy";
    if (vid == 0x03eb && pid == 0x800c) return "airspyhf";
    return "";
}

} // namespace dect2
