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
extern const ModeUi kGlobalstarUi;
extern const ModeUi kVdesUi;
extern const ModeUi kZigbeeUi;
extern const ModeUi kFlarmUi;
extern const ModeUi kHfbeaconUi;
extern const ModeUi kDjiUi;
extern const ModeUi kNbiotUi;
extern const ModeUi kHalowUi;
extern const ModeUi kCodarUi;
extern const ModeUi kRidUi;
extern const ModeUi kMeteorUi;
extern const ModeUi kNrUi;
extern const ModeUi kGoesUi;
extern const ModeUi kHrptUi;

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
    case 62: return &kGlobalstarUi;
    case 63: return &kVdesUi;
    case 65: return &kZigbeeUi;
    case 66: return &kFlarmUi;
    case 67: return &kHfbeaconUi;
    case 68: return &kDjiUi;
    case 69: return &kNbiotUi;
    case 70: return &kHalowUi;
    case 72: return &kCodarUi;
    case 56: return &kRidUi;
    case 59: return &kMeteorUi;
    case 57: return &kNrUi;
    case 60: return &kGoesUi;
    case 64: return &kHrptUi;
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

// ---------------------------------------------------------------- second mode

namespace {
// The mode's own screens drawn for the second mode: a.rx holds its report meanwhile, and its sound is pushed muted unless asked for
struct SecondScope {
    App& a;
    bool muted;
    explicit SecondScope(App& app) : a(app), muted(app.muted) {
        std::swap(a.rx, a.second.rx);
        a.muted = muted || !a.second.audio;
        a.second.drawing = true;
    }
    ~SecondScope() {
        std::swap(a.rx, a.second.rx);
        a.muted = muted;
        a.second.drawing = false;
    }
};
}   // namespace

bool setSecondMode(App& a, int idx, double mhz) {
    if (idx < 0 || idx >= kNumModes) {
        a.engine.clearSecondary();
        a.second.modeIdx = -1;
        a.second.rx = RxTelemetry(); a.second.seq = 0;
        return true;
    }
    const ModeDef& m = kModes[idx];
    if (m.subId) if (const SubMode* sm = subModeById(m.subId)) applySubMode(a.engine, sm);   // one protocol: only its decoder
    a.engine.setSecondarySound(a.second.audio);
    const bool ok = a.engine.setSecondaryHz(m.family + 2, mhz * 1e6);
    a.second.rx = RxTelemetry(); a.second.seq = 0;
    a.second.modeIdx = ok ? idx : -1;
    a.second.mhz = mhz;
    return ok;
}

void secondModeTick(App& a) {
    if (a.second.modeIdx < 0) return;
    if (a.engine.secondaryStandard() == 0) { a.second.modeIdx = -1; return; }   // refused or turned off in the engine
    RxTelemetry t;
    if (a.engine.latestRx2(t, a.second.seq)) { a.second.seq = t.seq; a.second.rx = std::move(t); }
    const ModeUi* mu = modeUi(kModes[a.second.modeIdx].family);
    if (mu && mu->tick) { SecondScope s(a); mu->tick(a); }
}

void secondModeTabs(App& a) {
    if (a.second.modeIdx < 0) return;
    const ModeDef& m = kModes[a.second.modeIdx];
    char name[96];
    snprintf(name, sizeof name, "Second: %s", m.name);
    const ModeUi* mu = modeUi(m.family);
    if (!tabItem(name, mu ? mu->tabIcon : Ic::Layers)) return;
    if (!a.engine.secondaryRunning()) {
        const std::string note = a.engine.secondaryNote();
        ImGui::TextDisabled("%s at %.4f MHz is not running%s%s", m.name, a.second.mhz, note.empty() ? "" : ": ", note.c_str());
    }
    if (mu && mu->tab) { SecondScope s(a); mu->tab(a); }
    ImGui::EndTabItem();
}
