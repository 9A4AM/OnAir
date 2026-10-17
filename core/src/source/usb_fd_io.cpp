#if defined(__linux__)
#include "usb_fd_io.h"
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>

namespace dect2 {
namespace {
struct LinuxSys : UsbFdSys {
    int control(int fd, uint8_t type, uint8_t req, uint16_t val, uint16_t idx, void* data, uint16_t len, uint32_t ms) override {
        usbdevfs_ctrltransfer c{};
        c.bRequestType = type; c.bRequest = req; c.wValue = val; c.wIndex = idx; c.wLength = len; c.timeout = ms; c.data = data;
        const int r = ioctl(fd, USBDEVFS_CONTROL, &c);
        return r < 0 ? -errno : r;
    }
    int bulk(int fd, uint8_t ep, void* data, int len, uint32_t ms) override {
        usbdevfs_bulktransfer b{};
        b.ep = ep; b.len = (unsigned)len; b.timeout = ms; b.data = data;
        const int r = ioctl(fd, USBDEVFS_BULK, &b);
        return r < 0 ? -errno : r;
    }
    int claim(int fd, unsigned iface) override { return ioctl(fd, USBDEVFS_CLAIMINTERFACE, &iface) < 0 ? -errno : 0; }
    int release(int fd, unsigned iface) override { return ioctl(fd, USBDEVFS_RELEASEINTERFACE, &iface) < 0 ? -errno : 0; }
    int reset(int fd) override { return ioctl(fd, USBDEVFS_RESET, 0) < 0 ? -errno : 0; }
    int clearHalt(int fd, uint8_t ep) override { unsigned e = ep; return ioctl(fd, USBDEVFS_CLEAR_HALT, &e) < 0 ? -errno : 0; }
    int detachKernel(int fd, unsigned iface) override {
        usbdevfs_ioctl io{};
        io.ifno = (int)iface; io.ioctl_code = USBDEVFS_DISCONNECT; io.data = nullptr;
        return ioctl(fd, USBDEVFS_IOCTL, &io) < 0 ? -errno : 0;
    }
};
}
UsbFdSys& usbFdLinuxSys() { static LinuxSys s; return s; }
} // namespace dect2
#endif
