// USB radios that the platform opened for us and handed over as a file descriptor (Android: UsbManager.openDevice().getFileDescriptor(), no root,
// no libusb). The app registers each opened radio here; the drivers list and open them from this registry instead of scanning the USB bus.
// The descriptor stays owned by the app (UsbDeviceConnection); detach before closing it.
#pragma once
#include <string>
#include <vector>

namespace dect2 {

struct UsbFdRadio {
    std::string name;          // the platform's device name (Android: "/dev/bus/usb/001/004"), the key for detach
    std::string kind;          // which driver: "rtlsdr", "hackrf", "airspy", "airspyhf"
    int fd = -1;               // usbfs descriptor, read/write
    int vid = 0, pid = 0;
    std::string manufacturer, product, serial;   // the strings the platform read (may be empty)
};

// Adds the radio (replaces one with the same name). false: bad descriptor or empty name.
bool usbFdAttach(const UsbFdRadio& r);
// Removes it (hot-plug out, or the app is done with the connection). Streams on it fail and the driver reopens or stops.
void usbFdDetach(const std::string& name);
// The attached radios of one kind ("" = all), in the order they were attached.
std::vector<UsbFdRadio> usbFdList(const std::string& kind = "");
// Which driver an USB id belongs to ("" if none of the supported radios). Same table as the app's device filter.
std::string usbFdKindForId(int vid, int pid);

} // namespace dect2
