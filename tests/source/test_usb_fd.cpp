// USB radios handed over as descriptors (core/include/dect2/source/usb_fd.h, core/src/source/usb_fd_io.h): the registry, the transfers on a FAKE usbfs
// (chunking of bulk reads, errno -> libusb codes, string descriptors) and, when the Rust USB layer is built, the RTL-SDR driver's table filled from
// the registry. No radio is involved: whether a real dongle answers through a real Android descriptor is not tested here.
#include "../../core/src/source/usb_fd_io.h"
#include "dect2/source/usb_fd.h"
#ifdef DECT2_RUST_USB
#include "../../core/src/source/rtl_fd_shim.h"
#endif
#include <cstdio>
#include <vector>

using namespace dect2;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

struct FakeSys : UsbFdSys {
    std::vector<int> bulkLens;           // sizes asked
    int bulkFail = 0;                    // -errno for the call number failAt (1 based), 0 = none
    int failAt = 0, calls = 0, shortAt = 0;
    int claimRet = 0, claimed = 0, released = 0, detached = 0;
    uint8_t lastType = 0;
    int control(int, uint8_t type, uint8_t req, uint16_t val, uint16_t, void* data, uint16_t len, uint32_t) override {
        lastType = type;
        uint8_t* d = static_cast<uint8_t*>(data);
        if (req == 6 && (val >> 8) == 3) {                   // GET_DESCRIPTOR string
            if ((val & 0xff) == 0) { d[0] = 4; d[1] = 3; d[2] = 0x09; d[3] = 0x04; return 4; }
            const char* s = "Blog V4";
            int n = 2;
            for (const char* c = s; *c; c++) { d[n++] = (uint8_t)*c; d[n++] = 0; }
            d[0] = (uint8_t)n; d[1] = 3;
            return n;
        }
        if (req == 0x99) return -ETIMEDOUT;
        return len;
    }
    int bulk(int, uint8_t ep, void*, int len, uint32_t) override {
        bulkLens.push_back(len);
        calls++;
        if (ep != 0x81) return -EINVAL;
        if (failAt && calls == failAt) return -bulkFail;
        if (shortAt && calls == shortAt) return len / 2;
        return len;
    }
    int claim(int, unsigned) override { if (claimRet == 0) claimed++; return claimRet; }
    int release(int, unsigned) override { released++; return 0; }
    int reset(int) override { return 0; }
    int clearHalt(int, uint8_t) override { return 0; }
    int detachKernel(int, unsigned) override { detached++; return -ENODATA; }
};

int main() {
    // registry
    UsbFdRadio a; a.name = "/dev/bus/usb/001/004"; a.kind = "rtlsdr"; a.fd = 7; a.vid = 0x0bda; a.pid = 0x2838; a.product = "Blog V4";
    CHECK(usbFdAttach(a));
    UsbFdRadio bad = a; bad.fd = -1;
    CHECK(!usbFdAttach(bad));
    bad = a; bad.name = "";
    CHECK(!usbFdAttach(bad));
    UsbFdRadio h; h.name = "/dev/bus/usb/001/005"; h.kind = "hackrf"; h.fd = 8;
    CHECK(usbFdAttach(h));
    CHECK(usbFdList().size() == 2 && usbFdList("rtlsdr").size() == 1 && usbFdList("airspy").empty());
    a.fd = 9;
    CHECK(usbFdAttach(a) && usbFdList().size() == 2 && usbFdList("rtlsdr")[0].fd == 9);   // same name replaces
    usbFdDetach("/dev/bus/usb/001/005");
    CHECK(usbFdList().size() == 1);
    usbFdDetach("nothing");
    CHECK(usbFdList().size() == 1);
    CHECK(usbFdKindForId(0x0bda, 0x2838) == "rtlsdr" && usbFdKindForId(0x1d50, 0x6089) == "hackrf" && usbFdKindForId(0x1d50, 0x60a1) == "airspy" &&
          usbFdKindForId(0x03eb, 0x800c) == "airspyhf" && usbFdKindForId(0x1234, 1).empty());

    // errno mapping
    CHECK(usbErrnoToLibusb(ETIMEDOUT) == kUsbTimeout && usbErrnoToLibusb(ENODEV) == kUsbNoDevice && usbErrnoToLibusb(ESHUTDOWN) == kUsbNoDevice &&
          usbErrnoToLibusb(EPIPE) == kUsbPipe && usbErrnoToLibusb(EBUSY) == kUsbBusy && usbErrnoToLibusb(EACCES) == kUsbAccess && usbErrnoToLibusb(0) == 0);

    // transfers
    FakeSys sys;
    {
        UsbFdDevice d(sys, 7);
        CHECK(d.claimInterface(0) == 0 && sys.claimed == 1 && sys.detached == 1);   // a failed kernel detach is ignored
        uint8_t buf[70000];
        CHECK(d.bulkRead(0x01, buf, 40000, 100) == 40000);   // the endpoint's direction bit is set for the caller
        CHECK((sys.bulkLens == std::vector<int>{16384, 16384, 7232}));
        sys.bulkLens.clear(); sys.calls = 0; sys.shortAt = 2;
        CHECK(d.bulkRead(0x81, buf, 40000, 100) == 16384 + 8192);   // a short piece ends the read
        sys.shortAt = 0; sys.calls = 0; sys.failAt = 2; sys.bulkFail = ETIMEDOUT;
        CHECK(d.bulkRead(0x81, buf, 40000, 100) == 16384);          // bytes read before the error are kept
        sys.calls = 0; sys.failAt = 1; sys.bulkFail = ENODEV;
        CHECK(d.bulkRead(0x81, buf, 100, 100) == kUsbNoDevice);
        sys.failAt = 0;
        CHECK(d.controlIn(0x40, 1, 0, 0, buf, 4, 100) == 4 && (sys.lastType & 0x80));
        CHECK(d.controlOut(0xc0, 1, 0, 0, buf, 4, 100) == 4 && !(sys.lastType & 0x80));
        CHECK(d.controlIn(0x40, 0x99, 0, 0, buf, 4, 100) == kUsbTimeout);
        uint8_t s[32];
        CHECK(d.stringDescriptor(2, s, sizeof s) == 7 && std::string((char*)s) == "Blog V4");
        CHECK(d.stringDescriptor(0, s, sizeof s) == kUsbInvalid);
        const uint8_t wide[] = {8, 3, 'A', 0, 0x3a, 0x04, 'B', 0};   // a Cyrillic letter in the middle becomes '?'
        CHECK(UsbFdDevice::parseStringDescriptor(wide, 8, s, sizeof s) == 3 && std::string((char*)s) == "A?B");
        CHECK(UsbFdDevice::parseStringDescriptor(wide, 1, s, sizeof s) == kUsbIo);
    }
    CHECK(sys.released == 1);   // destructor releases the interface
    {
        FakeSys busy; busy.claimRet = -EBUSY;
        UsbFdDevice d(busy, 7);
        CHECK(d.claimInterface(0) == kUsbBusy);
    }

#ifdef DECT2_RUST_USB
    // the driver's table: enumeration from the registry, and a clean failure when the dongle does not speak the RTL2832U protocol
    {
        using namespace dect2::native;
        rtlfd::sysOverride() = &sys;
        char v[256], p[256], sn[256];
        CHECK(rtlfd::getDeviceCount() == 1 && rtlfd::getUsbStrings(0, v, p, sn) == 0 && std::string(p) == "Blog V4");
        CHECK(rtlfd::getUsbStrings(3, v, p, sn) < 0);
        sys.claimRet = -EBUSY;
        void* dev = nullptr;
        CHECK(rtlfd::open(&dev, 0) == kUsbBusy && dev == nullptr);
        sys.claimRet = 0;
        dev = nullptr;
        const int r = rtlfd::open(&dev, 0);   // the fake answers every control read with zeros: no tuner is found, the open must fail without a crash
        printf("open on a silent fake: %d\n", r);
        if (r == 0 && dev) rtlfd::close(dev);
        CHECK(rtlfd::open(&dev, 5) < 0);
    }
#endif
    printf(failures ? "usb_fd: %d FAILED\n" : "usb_fd: ok\n", failures);
    return failures ? 1 : 0;
}
