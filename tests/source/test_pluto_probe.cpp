// Which PlutoSDR boards listing may ask about their firmware (#215). No hardware: the decision function is checked directly. A network board the
// user did not add is never asked (no network contact while listing or scanning), and nothing here writes to a device.
#include "../../core/src/source/native_common.h"
#include <cstdio>
#include <string>

using namespace dect2;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
    // a network address the user did not add (a scan found it): off
    CHECK(!native::plutoProbeAllowed("ip:192.168.1.50", false));
    CHECK(!native::plutoProbeAllowed("ip:pluto.local", false));
    CHECK(!native::plutoProbeAllowed("ip:192.168.2.1", false));
    CHECK(!native::plutoProbeAllowed("ip:10.0.0.7:50000", false));
    // a network address the user added: on
    CHECK(native::plutoProbeAllowed("ip:192.168.1.50", true));
    CHECK(native::plutoProbeAllowed("ip:pluto.local", true));
    // a USB board is never asked over the network, added or not
    CHECK(!native::plutoProbeAllowed("usb:1.4.5", false));
    CHECK(!native::plutoProbeAllowed("usb:1.4.5", true));
    CHECK(!native::plutoProbeAllowed("", false));
    if (fails) { fprintf(stderr, "%d checks failed\n", fails); return 1; }
    printf("test_pluto_probe ok\n");
    return 0;
}
