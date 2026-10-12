// The screens of the modes added after FM: the table that maps a family to its ModeUi, and small helpers for the shell.
#include "app.h"

extern const ModeUi kDvbsUi;
extern const ModeUi kDtmbUi;
extern const ModeUi kAtvUi;
extern const ModeUi kDmrUi;
extern const ModeUi kDrmUi;
extern const ModeUi kAdsbUi;
extern const ModeUi kGnssUi;
extern const ModeUi kSondeUi;
extern const ModeUi kAisUi;
extern const ModeUi kMarineUi;
extern const ModeUi kAcarsUi;
extern const ModeUi kInmcUi;
extern const ModeUi kAeroUi;
extern const ModeUi kIridiumUi;
extern const ModeUi kMeshUi;
extern const ModeUi kHdrUi;
extern const ModeUi kCdrUi;
extern const ModeUi kPagerUi;
extern const ModeUi kPacketUi;
extern const ModeUi kHfdigUi;
extern const ModeUi kAirbandUi;
extern const ModeUi kVdl2Ui;
extern const ModeUi kIsmUi;
extern const ModeUi kVorUi;
extern const ModeUi kM17Ui;
extern const ModeUi kCellUi;
extern const ModeUi kCwUi;
extern const ModeUi kDstarUi;
extern const ModeUi kHfdlUi;
extern const ModeUi kNxdnUi;
extern const ModeUi kDpmrUi;
extern const ModeUi kMacUi;
extern const ModeUi kGsmrUi;
extern const ModeUi kDvbcUi;
extern const ModeUi kOrbcommUi;
extern const ModeUi kTetraUi;
extern const ModeUi kPskUi;
extern const ModeUi kJs8Ui;
extern const ModeUi kSpecxUi;
extern const ModeUi kSpecanUi;
extern const ModeUi kUplinkUi;
extern const ModeUi kAleUi;
extern const ModeUi kAstroUi;
extern const ModeUi kP25Ui;
extern const ModeUi kBleUi;
extern const ModeUi kIonoUi;
extern const ModeUi kQo100Ui;
extern const ModeUi kWifiUi;
extern const ModeUi kGeneralUi;
extern const ModeUi kTimeUi;
extern const ModeUi kCubesatUi;
extern const ModeUi kRidUi;
extern const ModeUi kMeteorUi;
extern const ModeUi kGoesUi;

const ModeUi* modeUi(int family) {
    switch (family) {
    case 6: return &kDvbsUi;
    case 7: return &kDtmbUi;
    case 8: return &kAtvUi;
    case 9: return &kDmrUi;
    case 10: return &kDrmUi;
    case 11: return &kAdsbUi;
    case 12: return &kGnssUi;
    case 13: return &kSondeUi;
    case 14: return &kAisUi;
    case 15: return &kMarineUi;
    case 16: return &kAcarsUi;
    case 17: return &kInmcUi;
    case 18: return &kAeroUi;
    case 19: return &kIridiumUi;
    case 20: return &kMeshUi;
    case 21: return &kHdrUi;
    case 22: return &kCdrUi;
    case 23: return &kPagerUi;
    case 24: return &kPacketUi;
    case 25: return &kHfdigUi;
    case 26: return &kAirbandUi;
    case 27: return &kVdl2Ui;
    case 28: return &kIsmUi;
    case 30: return &kVorUi;
    case 31: return &kM17Ui;
    case 32: return &kCellUi;
    case 34: return &kCwUi;
    case 33: return &kDstarUi;
    case 35: return &kHfdlUi;
    case 38: return &kNxdnUi;
    case 45: return &kDpmrUi;
    case 47: return &kMacUi;
    case 39: return &kGsmrUi;
    case 41: return &kSpecxUi;
    case 40: return &kDvbcUi;
    case 44: return &kOrbcommUi;
    case 46: return &kTetraUi;
    case 36: return &kPskUi;
    case 37: return &kJs8Ui;
    case 42: return &kSpecanUi;
    case 43: return &kUplinkUi;
    case 49: return &kAleUi;
    case 48: return &kAstroUi;
    case 50: return &kP25Ui;
    case 51: return &kBleUi;
    case 52: return &kIonoUi;
    case 53: return &kQo100Ui;
    case 54: return &kWifiUi;
    case 55: return &kGeneralUi;
    case 58: return &kTimeUi;
    case 61: return &kCubesatUi;
    case 56: return &kRidUi;
    case 59: return &kMeteorUi;
    case 60: return &kGoesUi;
    default: return nullptr;
    }
}

// the title of the list on the right: STATIONS for FM and DAB, the mode's own, or SERVICES
const char* listTitle(const App& a, bool upper) {
    if (const ModeUi* mu = modeUi(a.family)) {
        if (upper) return mu->sideTitle;
        static char b[8][32];
        static int k = 0;
        char* o = b[k++ & 7];
        snprintf(o, 32, "%s", mu->sideTitle);
        for (int i = 1; o[i]; i++) o[i] = (char)tolower((unsigned char)o[i]);
        return o;
    }
    const bool stations = a.fmMode || a.dabMode;
    return upper ? (stations ? "STATIONS" : "SERVICES") : (stations ? "Stations" : "Services");
}
