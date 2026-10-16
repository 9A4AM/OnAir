#include "dect2/hamlib_rig.h"
#include <cstdio>

using namespace dect2;

int main() {
    int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, m); fails++; } } while (0)
    HamlibRig r;
    r.model = 1025;
    r.path = "/dev/ttyUSB0";
    r.label = "shack";
    r.audioId = "mic-id";
    const std::string s = hamlibRigsToString({r});
    const auto back = hamlibRigsFromString(s);
    CHECK(back.size() == 1 && back[0] == r, "hamlib rig round-trip");
    int m = 0;
    std::string p;
    CHECK(parseHamlibSerial(hamlibRigSerial(1025, "/dev/ttyUSB0"), m, p) && m == 1025 && p == "/dev/ttyUSB0", "serial parse");
    const auto devs = hamlibRigDevices({r});
    CHECK(devs.size() == 1 && devs[0].kind == DeviceInfo::Hamlib && devs[0].serial == hamlibRigSerial(1025, "/dev/ttyUSB0"), "device entry");
#undef CHECK
    return fails ? 1 : 0;
}
