// The transmitter: the RX / TX switch at the top left of the interface and the Transmit panel that fills the main area in TX view. What can be sent is the
// table in core/include/dect2/tx/tx.h; the session, the radios' sinks and the rules are in core/src/tx/tx.cpp and the radios' sources.
//
// The transmitter is independent of the receivers: it has its own radio choice and its own state (not an App's), so a receiver goes on receiving while it
// transmits (the owner's example: a HackRF Pro transmits, an RTL-SDR receives and decodes it). A radio is never used by both unless it is genuinely full
// duplex: a radio a receiver holds is refused with a plain sentence (txClaimRadio, device_claims.h), never by silently stopping the receiver.
//
// Rules this file keeps: transmitting never starts by itself (only the big button), is never saved in the settings (only "the notice was read" is; the
// view itself opens as RX), and stops when it is stopped, the radio fails or the program ends. Nothing is limited here: frequency and gain go to the
// driver as typed, and the driver's own refusal is shown as it came.
#include "../app.h"
#include "dect2/hfdig/hfdig_sstv.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace dect2;

#include "tx_state.h"
#include "dect2/hfdig/hfdig_sstv.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace dect2;

namespace {
// The texts the core gives (refusal reasons, the radios' gain notes) come through tr() above; listed here so the translation files can be checked against the sources
[[maybe_unused]] const char* const kCoreTexts[] = {
    N_("GNSS is not transmitted: its generator is a satellite-signal simulator (spoofing)"),
    N_("TETRAPOL is not transmitted: receive only, control-channel system information"),
    N_("cellular signals are not transmitted: the generators model a network cell for testing the decoder"),
    N_("this is an analysis mode: there is nothing to transmit"),
    N_("Bluetooth is not transmitted: it would impersonate a Bluetooth advertiser"),
    N_("Wi-Fi is not transmitted: the generator models access-point beacons for the decoder"),
    N_("Drone Remote ID is not transmitted: it rides on Wi-Fi and Bluetooth advertisements"),
    N_("ANT+ is not transmitted: it would impersonate a sensor network"),
    N_("this is a receive-only sensor harvester: there is nothing meaningful to transmit"),
    N_("this is a spectrum analyser: there is nothing to transmit"),
    N_("this is a general listening mode: there is nothing specific to transmit"),
    N_("time signals are not transmitted: the generator imitates broadcast clocks (spoofing)"),
    N_("this is a signal workbench: it is for measurement, not a radio service"),
    N_("Multitone paging is not transmitted: the generator is not the real (unpublished) format, only a detector test"),
    N_("this mode has no transmit generator"),
    N_("TX VGA 0 to 47 dB; the +14 dB amplifier has its own switch"),
    N_("the AD9361's TX attenuation shown as a gain: -89.75 dB (least power) to 0 dB (most)"),
    N_("the driver's own TX gain scale (known after the radio has been opened once)"),
    N_("Live voice is encoded with Codec 2 (libcodec2)."),
    N_("libcodec2 is not loaded: live encode is off. Destination callsigns still go on the air; voice bits are the known test pattern. A 1 kHz tone, silence or a WAV/MP3 can still be sent as analog NFM."),
    N_("Live voice is encoded with Codec 2 (FreeDV)."),
    N_("libcodec2 is not loaded: live encode is off. A 1 kHz tone, silence or a WAV/MP3 can still be sent."),
    N_("This vocoder cannot encode: OnAir loads mbelib for decode only (AMBE/IMBE). Destination ID / talkgroup / colour code still go on the air; voice bits are the test pattern. A 1 kHz tone, silence or a WAV/MP3 can be sent as analog NFM instead."),
    N_("TETRA ACELP is not encoded here. Colour code and IDs are shown; the downlink test pattern is sent. A 1 kHz tone, silence or a WAV/MP3 can be sent as analog NFM instead."),
};
}

namespace txui {

TxUi& ui() {
    static TxUi u;
    return u;
}

const double kTxBw[4] = {8, 7, 6, 5};

// the engine family of a transmit mode (the inverse of engineStd), for the mode's own synthetic-signal controls
int familyOf(const TxModeSpec& m) {
    switch (m.stdMode) {
    case 0: return 0;
    case 3: return 1;
    case 4: return 2;
    case 5: return 3;
    case 6: return 4;
    case 7: return 5;
    default: return m.stdMode - 2;
    }
}

const TxModeSpec* rxMode(const App& a) { return txModeFor(engineStd(a), a.stdMode == 2 ? 2 : 1); }

void ensureTxMode(App& a, TxUi& s) {
    if (s.family >= 0) return;
    s.family = a.family;
    s.dvbVariant = a.stdMode == 2 ? 2 : 1;
    s.subId[0] = 0;
    int n = 0;
    const SubMode* t = subModes(n);
    if (a.sub >= 1 && a.sub <= n) snprintf(s.subId, sizeof s.subId, "%s", t[a.sub - 1].id);
    const TxModeSpec* m = txModeForList(s.family, s.subId[0] ? s.subId : nullptr, s.dvbVariant);
    if (m) { s.modeId = m->id; s.synth = (rxMode(a) == m) ? a.tune.synth : SynthConfig(); s.freqMhz = a.freqMhz; }
}

// the mode chosen for transmitting: its options and frequency start from the receiver's when it is the receiver's own mode
void chooseMode(App& a, TxUi& s, const TxModeSpec& m) {
    if (s.modeId != m.id) previewDrop();
    s.modeId = m.id;
    s.bwIdx = 0;
    const TxModeSpec* rm = rxMode(a);
    if (rm && rm == &m) { s.synth = a.tune.synth; s.freqMhz = a.freqMhz; }
    else {
        s.synth = SynthConfig();
        const ModeTuning* mt = modeTuning(m.stdMode);
        s.freqMhz = mt && mt->defMhz > 0 ? mt->defMhz : a.freqMhz;
    }
}

const ImVec4 kRed(0.90f, 0.12f, 0.12f, 1.f);

std::vector<int> txRadios(const App& a) {
    std::vector<int> v;
    for (int i = 0; i < (int)a.devices.size(); i++) if (a.devices[(size_t)i].isRadio() && a.devices[(size_t)i].canTransmit) v.push_back(i);
    return v;
}

// the receiver holding a radio the transmitter wants (0: none, or the radio can do both)
int rxHolder(const DeviceInfo& d) {
    const int h = deviceHolder(d);
    return h != 0 && h != kTxClaimOwner && !d.txFullDuplex ? h : 0;
}

void release(TxUi& s) {
    if (s.claimed) { txReleaseRadio(); s.claimed = false; }
}

void loadPayloadPrefs(TxUi& s) {
    if (s.prefsLoaded) return;
    s.prefsLoaded = true;
    plat::Prefs& p = plat::basePrefs();
    snprintf(s.callsign, sizeof s.callsign, "%s", p.getS("tx.callsign", "").c_str());
    snprintf(s.grid, sizeof s.grid, "%s", p.getS("tx.grid", "").c_str());
    snprintf(s.text, sizeof s.text, "%s", p.getS("tx.text", "").c_str());
    s.offsetMhz = p.getD("tx.offsetMhz", 0);
    s.dryRun = p.getB("tx.dryRun", false);
    // recent: "id,mhz,gain;id,mhz,gain"
    const std::string rec = p.getS("tx.recent", "");
    size_t pos = 0;
    while (pos < rec.size() && s.recent.size() < 6) {
        size_t e = rec.find(';', pos);
        if (e == std::string::npos) e = rec.size();
        const std::string item = rec.substr(pos, e - pos);
        pos = e + 1;
        const size_t c1 = item.find(','), c2 = item.rfind(',');
        if (c1 == std::string::npos || c2 == c1) continue;
        TxUi::Recent r;
        r.id = item.substr(0, c1);
        r.mhz = atof(item.substr(c1 + 1, c2 - c1 - 1).c_str());
        r.gain = (float)atof(item.substr(c2 + 1).c_str());
        if (txModeById(r.id)) s.recent.push_back(r);
    }
}

void saveRecent(const TxUi& s) {
    std::string o;
    char b[96];
    for (const auto& r : s.recent) { snprintf(b, sizeof b, "%s,%.6f,%.1f", r.id.c_str(), r.mhz, (double)r.gain); if (!o.empty()) o += ';'; o += b; }
    plat::basePrefs().setS("tx.recent", o);
}

// the signal just sent goes to the front of the recent list (one row per signal)
void pushRecent(TxUi& s, const std::string& id, double mhz, float gain) {
    s.recent.erase(std::remove_if(s.recent.begin(), s.recent.end(), [&](const TxUi::Recent& r) { return r.id == id; }), s.recent.end());
    TxUi::Recent r; r.id = id; r.mhz = mhz; r.gain = gain;
    s.recent.insert(s.recent.begin(), r);
    if (s.recent.size() > 6) s.recent.resize(6);
    saveRecent(s);
}

void saveStr(const char* key, const char* v) {
    plat::basePrefs().setS(key, v);
}

TxRequest makeReq(const TxUi& s, const TxModeSpec& mode, bool carrier) {
    TxRequest r;
    r.modeId = mode.id;
    r.synth = s.synth;
    r.bandwidthMhz = kTxBw[s.bwIdx];
    r.loop = s.loop;
    r.onceSeconds = s.onceSeconds;
    r.tune.centerHz = s.freqMhz * 1e6;
    r.tune.offsetHz = s.offsetMhz * 1e6;
    r.tune.gainDb = s.gainDb;
    r.tune.ampOn = s.ampOn;
    r.tune.channel = s.txCh < 0 ? 0 : s.txCh;
    r.callsign = s.callsign;
    r.grid = s.grid;
    r.text = s.text[0] ? s.text : s.rdsRt;
    r.dest = s.dest;
    r.path = s.path;
    r.file = s.file;
    r.name = s.name;
    r.provider = s.provider;
    r.network = s.network;
    r.serviceId = s.serviceId > 0 ? s.serviceId : 1;
    for (int i = 0; i < std::max(1, std::min(4, s.nsvc)); i++) if (s.svc[i][0]) r.services.push_back(s.svc[i]);
    if (r.services.empty() && s.name[0]) r.services.push_back(s.name);
    r.kind = s.kind;
    r.baudIdx = s.baudIdx;
    r.shiftIdx = s.shiftIdx;
    r.variant = s.variant;
    r.wpm = s.wpm;
    r.dbm = s.dbm;
    r.function = s.function;
    r.address = s.address;
    r.toneHz = s.toneHz;
    r.lat = s.lat;
    r.lon = s.lon;
    r.reverse = s.reverse;
    r.carrierTune = carrier;
    r.audioSrc = s.audioSrc;
    r.audioDevice = s.audioDevice;
    r.audioMute = s.audioMute;
    r.micGain = s.micGain;
    r.deviationHz = s.deviationHz;
    r.preemphUs = s.preemphUs;
    r.rdsPi = s.rdsPi;
    r.slot = s.slot;
    r.colorCode = s.colorCode;
    r.talkgroup = s.talkgroup;
    r.analogVoice = s.analogVoice;
    r.videoSrc = s.videoSrc;
    r.videoDevice = s.videoDevice;
    for (int i = 0; i < std::max(1, std::min(4, s.nsvc)); i++) {
        DemoTsService p;
        p.name = s.svc[i][0] ? s.svc[i] : (s.name[0] && i == 0 ? s.name : std::string("OnAir ") + std::to_string(i + 1));
        p.picture = i % 3;
        if (s.svcSrc[i] == 1) p.file = s.svcFile[i];
        if (s.svcSrc[i] == 2) { p.liveCamera = true; p.camera = s.svcCam[i]; }
        r.mux.push_back(std::move(p));
    }
    return r;
}

// Everything needed to press Start is there: a mode, and (unless this is a test) a radio that no receiver holds
bool canStart(App&, TxUi& s, const DeviceInfo* radio, int conflict, const TxModeSpec* mode) {
    (void)s;
    return mode && (s.dryRun || (radio && conflict == 0));
}

bool startTx(App& a, TxUi& s, const DeviceInfo* radio, const TxModeSpec* mode, bool carrier) {
    s.error.clear();
    s.info.clear();
    if (!s.dryRun && !plat::basePrefs().getB("txNoticeAccepted", false)) { s.askNotice = true; return false; }
    std::string err;
    TxRequest req = makeReq(s, *mode, carrier);
    std::unique_ptr<TxSink> sink;
    TxRateLimits lim;
    if (s.dryRun) {   // no radio, no claim, no map suspension: the generator runs at the radio's rate into a paced null sink
        sink = makeNullTxSink();
        if (radio) lim = TxRateLimits{radio->txMinRateHz, radio->txMaxRateHz};
    } else {
        if (!txClaimRadio(*radio, err)) { s.error = err; return false; }
        s.claimed = true;
        sink = makeTxSink(*radio);
        if (!sink) { s.error = txUnavailableReason(*radio); if (s.error.empty()) s.error = tr("This radio has no transmit driver in OnAir."); release(s); return false; }
        lim = TxRateLimits{radio->txMinRateHz, radio->txMaxRateHz};
        mapPauseForTransmit(a);   // before the radio is keyed, so this frame cannot post what the receiver hears from it
    }
    if (s.session.start(std::move(sink), req, lim, err)) {
        s.radioName = s.dryRun ? std::string(tr("test (no RF)")) : radio->name; s.modeName = mode->name; s.freqSent = req.tune.centerHz;
        s.fileNote = s.session.stats().fileNote;
        s.carrierOnly = carrier;
        s.sessionDry = s.dryRun;
        s.startedAt = ImGui::GetTime();
        s.appliedMhz = s.freqMhz; s.appliedGain = s.gainDb;
        if (!carrier) pushRecent(s, mode->id, s.freqMhz, s.gainDb);
        a.engine.log(std::string(s.dryRun ? "transmit test (no RF): " : "transmit: ") + (carrier ? "carrier tune " : "") + mode->name + " on " + s.radioName + " at " + std::to_string(s.freqMhz) + " MHz");
        return true;
    }
    s.error = err;
    if (!s.session.running()) { release(s); if (!s.dryRun) mapTransmitAbort(a); }
    return false;
}

// stops whatever is running (a transmission or a test), gives the radio back and says why in the engine log
void stopTx(App& a, TxUi& s, const char* why) {
    const bool was = s.session.running() || s.claimed;
    s.session.stop();
    release(s);
    s.radioName.clear(); s.carrierOnly = false; s.carrierHeld = false; s.sessionDry = false; s.pendingStart = false; s.pendingCarrier = false;
    if (was) a.engine.log(std::string("transmit: stopped ") + why);
}

// the chosen transmit radio (the first free one when none is chosen) and the receiver that holds it
const DeviceInfo* bindRadio(App& a, TxUi& s, int& conflict) {
    const std::vector<int> radios = txRadios(a);
    const DeviceInfo* radio = nullptr;
    for (int i : radios) if (deviceClaimKey(a.devices[(size_t)i]) == s.radioKey) radio = &a.devices[(size_t)i];
    if (!radio && s.radioKey.empty()) {
        for (int i : radios) if (!rxHolder(a.devices[(size_t)i])) { radio = &a.devices[(size_t)i]; s.radioKey = deviceClaimKey(*radio); break; }
    }
    conflict = radio ? rxHolder(*radio) : 0;
    return radio;
}

// the one-time notice before the first transmission (a popup: it is the owner's notice, shown once)
void txNoticePopup(TxUi& s) {
    if (s.askNotice) { ImGui::OpenPopup("##txnotice"); s.askNotice = false; }
    ImGui::SetNextWindowSize(ImVec2(480 * gUi, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##txnotice", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushTextWrapPos(460 * gUi);
        ImGui::TextColored(pal::heading(), "%s", tr("Before you transmit"));
        ImGui::Spacing();
        ImGui::TextUnformatted(tr("Transmitting needs a licence or permission where you are. You are responsible for what you emit and for not interfering with others "
                                  "(including services that people rely on for safety). Connect an antenna, an attenuator or a dummy load to the radio before you start. "
                                  "OnAir does not limit the frequency or the power: the radio's own limits are the only ones."));
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button(tr("I understand"), ImVec2(-1, 0))) { plat::basePrefs().setB("txNoticeAccepted", true); plat::basePrefs().flush(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

} // namespace txui

using namespace txui;

bool& txViewFlag() { return ui().view; }
bool txActive() { return ui().session.running(); }

void txShutdown() {
    TxUi& s = ui();
    s.session.stop();
    release(s);
}

// every frame (the first call of a frame does the work): a transmission that ended by itself or failed gives its radio back; the on-screen indicator
void txTick(App&) {
    TxUi& s = ui();
    if (s.tickFrame == ImGui::GetFrameCount()) return;
    s.tickFrame = ImGui::GetFrameCount();
    if (!s.session.running() && (s.claimed || s.sessionDry)) {
        const TxSession::Stats st = s.session.stats();
        if (st.failed && s.error.empty()) s.error = st.error;
        s.session.stop();
        release(s);
        s.radioName.clear();
        s.carrierOnly = false;
        if (!s.sessionDry) mapTransmitEnded();
        s.sessionDry = false;
    }
    if (!s.session.running()) return;
    char b[220];
    const TxSession::Stats st = s.session.stats();
    snprintf(b, sizeof b, "%s   %s   %.6f MHz   %.0f s", s.sessionDry ? tr("TEST - NO RF") : tr("TRANSMITTING"), s.modeName.c_str(), s.freqSent / 1e6, st.seconds);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ts = ImGui::CalcTextSize(b);
    const float pad = 8 * gUi;
    const float x = (ImGui::GetIO().DisplaySize.x - ts.x) * 0.5f - pad;
    const bool blink = std::fmod(ImGui::GetTime(), 1.2) < 0.8;
    const ImU32 c1 = s.sessionDry ? IM_COL32(60, 120, 170, 245) : IM_COL32(200, 20, 20, 245), c2 = s.sessionDry ? IM_COL32(40, 90, 140, 245) : IM_COL32(140, 10, 10, 245);
    dl->AddRectFilled(ImVec2(x, 4 * gUi), ImVec2(x + ts.x + 2 * pad, 4 * gUi + ts.y + 2 * pad), blink ? c1 : c2, 6 * gUi);
    dl->AddText(ImVec2(x + pad, 4 * gUi + pad), IM_COL32(255, 255, 255, 255), b);
}

// The RX / TX slider at the top left. Switching the view starts and stops nothing.
void txViewSwitch(App&) {
    TxUi& s = ui();
    const float h = ImGui::GetFrameHeight(), w = h * 3.2f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##rxtx", ImVec2(w, h));
    if (ImGui::IsItemClicked()) s.view = !s.view;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Switch between the receiver (RX) and the transmitter (TX). The transmitter has its own radio, so a receiver can keep receiving while you transmit."));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool on = s.view, tx = s.session.running();
    const ImU32 bg = tx ? IM_COL32(190, 25, 25, 255) : on ? IM_COL32(150, 60, 40, 255) : IM_COL32(60, 70, 85, 255);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h * 0.5f);
    const float kr = h * 0.5f - 2 * gUi;
    const ImVec2 kc(on ? p.x + w - h * 0.5f : p.x + h * 0.5f, p.y + h * 0.5f);
    dl->AddCircleFilled(kc, kr, IM_COL32(245, 245, 245, 255));
    const char* lab = on ? tr("TX") : tr("RX");
    const ImVec2 ts = ImGui::CalcTextSize(lab);
    const float cx = on ? p.x + (w - h) * 0.5f : p.x + h + (w - h) * 0.5f;   // the label sits in the free half of the track
    dl->AddText(ImVec2(cx - ts.x * 0.5f, p.y + (h - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), lab);
    if (tx && !on) { ImGui::SameLine(); ImGui::TextColored(kRed, "%s", tr("TRANSMITTING")); }
    ImGui::Dummy(ImVec2(w, 1.f));   // grow ##root so SetCursorPos of the slider does not extend past the window
}

void txSelectMode(App& a, int family, const char* subId) {
    TxUi& s = ui();
    s.family = family;
    snprintf(s.subId, sizeof s.subId, "%s", subId ? subId : "");
    const TxModeSpec* m = txModeForList(family, subId, s.dvbVariant);
    if (m) chooseMode(a, s, *m);
    else { s.modeId.clear(); s.modeName.clear(); }
}

bool txModeHighlighted(const ModeDef& m) {
    TxUi& s = ui();
    if (s.family < 0) return false;
    if (m.family != s.family) return false;
    if (!m.subId || !*m.subId) return s.subId[0] == 0;
    return std::string(s.subId) == m.subId;
}

