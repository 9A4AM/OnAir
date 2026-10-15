// Native radios (RTL-SDR, Airspy, BladeRF, LimeSDR, PlutoSDR, USRP, SDRplay, Fobos SDR): the list of connected ones and the way to open one.
// The drivers are in source_rtlsdr.cpp, source_airspy.cpp, source_bladerf.cpp, source_limesdr.cpp, source_plutosdr.cpp, source_usrp.cpp,
// source_sdrplay.cpp, source_airspyhf.cpp, source_fobos.cpp and source_rx888.cpp;
// each loads the radio's own library at run time (native_common.h), so nothing here depends on those libraries being installed.
#include "native_common.h"

namespace dect2 {

// ---- airspyhf
namespace native {
void listAirspyHf(std::vector<DeviceInfo>& out);
std::unique_ptr<IqSource> makeAirspyHf(const DeviceInfo& d);
// ---- fobos
void listFobos(std::vector<DeviceInfo>& out);
std::unique_ptr<IqSource> makeFobos(const DeviceInfo& d);
// ---- rx888
void listRx888(std::vector<DeviceInfo>& out);
std::unique_ptr<IqSource> makeRx888(const DeviceInfo& d);
}

std::vector<DeviceInfo> listNativeDevices(std::string& err) {
    std::vector<DeviceInfo> out;
    if (native::nativeDisabled()) return out;
    try {
        native::listRtl(out);
        native::listAirspy(out);
        native::listBlade(out);
        native::listLime(out);
        native::listPluto(out);
        native::listUsrp(out);
        native::listSdrplay(out);
        native::listAirspyHf(out);   // ---- airspyhf
        native::listFobos(out);      // ---- fobos
        native::listRx888(out);      // ---- rx888
    } catch (const std::exception& e) {
        err = std::string("native radios: ") + e.what();
    }
    return out;
}

// The radios of the native drivers that can transmit: the PlutoSDR (libiio). The LimeSDR, BladeRF and USRP can transmit through SoapySDR
// when its module is installed; their native drivers here are receive-only.
std::unique_ptr<TxSink> makeNativeTxSink(const DeviceInfo& d) {
    if (d.board == "pluto") return native::makePlutoTx(d);
    return nullptr;
}

std::unique_ptr<IqSource> makeNativeSource(const DeviceInfo& d) {
    if (d.board == "rtlsdr") return native::makeRtl(d);
    if (d.board == "airspy") return native::makeAirspy(d);
    if (d.board == "bladerf") return native::makeBlade(d);
    if (d.board == "lime") return native::makeLime(d);
    if (d.board == "pluto") return native::makePluto(d);
    if (d.board == "usrp") return native::makeUsrp(d);
    if (d.board == "sdrplay") return native::makeSdrplay(d);
    if (d.board == "airspyhf") return native::makeAirspyHf(d);   // ---- airspyhf
    if (d.board == "rtltcp") return native::makeRtlTcp(d);
    if (d.board == "spyserver") return native::makeSpyServer(d);
    if (d.board == "hermeslite2") return native::makeHermesLite2(d);
    if (d.board == "redpitaya") return native::makeRedPitaya(d);
    if (d.board == "fobos") return native::makeFobos(d);         // ---- fobos
    if (d.board == "rx888") return native::makeRx888(d);         // ---- rx888
    return nullptr;
}

} // namespace dect2
