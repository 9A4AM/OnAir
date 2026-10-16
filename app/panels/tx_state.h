// Shared state and entry points of the transmit panel (the guided panel): one TxUi, the request builder, the start / stop calls and the step drawers.
// Files: tx_ui.cpp (state, start / stop, the RX / TX switch), tx_payload_ui.cpp (the signal's own options), tx_panel.cpp (header, layout, shortcuts),
// tx_step_signal.cpp (step 1), tx_step_settings.cpp (step 2), tx_step_send.cpp + tx_preview.cpp (step 3).
#pragma once
#include "../app.h"
#include "dect2/tx/tx.h"
#include <atomic>
#include <future>
#include <string>
#include <vector>

namespace txui {

using namespace dect2;

struct TxUi {
    TxSession session;
    std::string modeId, radioKey;            // the transmitter's own mode and radio
    int family = -1;                         // mode-list family (same numbers as RX); -1: follow the receiver once
    char subId[32] = {};
    int dvbVariant = 1;                      // 1 DVB-T2, 2 DVB-T
    SynthConfig synth;                       // the mode's own signal options (the same fields as the receiver's built-in test signal)
    int bwIdx = 0;                           // DVB-T2 / DVB-T channel width: 8, 7, 6, 5 MHz
    double freqMhz = 0;                      // typed by the user, any value; pre-filled from the receiver when the mode is chosen
    double offsetMhz = 0;                    // added only to the frequency sent to the radio; the typed frequency stays freqMhz
    float gainDb = 0;
    bool gainSet = false, ampOn = false, loop = true;
    int txCh = 0;                            // 0 = TX1. Shown when the radio has, or may have, more than one output
    int onceSeconds = 10;
    bool view = false;                       // TX view (never saved)
    bool claimed = false, askNotice = false;
    std::string error, radioName, modeName, fileNote;
    double freqSent = 0;
    int tickFrame = -1;
    char callsign[16] = {}, grid[8] = {}, dest[24] = {}, name[16] = {}, path[64] = {}, file[512] = {}, text[256] = {};
    char rdsRt[128] = {}, audioDevice[64] = {};
    char provider[64] = {}, network[64] = {}, svc[4][48] = {}, svcFile[4][512] = {}, svcCam[4][96] = {}, videoDevice[96] = {};
    int nsvc = 1, serviceId = 1, videoSrc = 0, svcSrc[4] = {};
    int kind = 0, baudIdx = 0, shiftIdx = 0, variant = 0, wpm = 20, dbm = 37, function = 3;
    unsigned address = 0;
    double toneHz = 1000, lat = 0, lon = 0;
    bool reverse = false, analogVoice = false, audioMute = false, prefsLoaded = false;
    int audioSrc = 2;                        // 0 mic, 1 file, 2 tone, 3 silence
    float micGain = 1.f;
    double deviationHz = 0, preemphUs = 50;
    int rdsPi = 0x4A21, slot = 1, colorCode = 1, talkgroup = 0;
    bool carrierHeld = false, carrierOnly = false;
    Waterfall wf;
    bool wfInit = false;
    float specFrac = 0.45f;
    std::vector<double> specX, specY;
    // the guided panel
    bool dryRun = false;                     // "Test (dry run, no RF)": the generator runs into a null sink; no radio is opened or claimed
    bool popped = false;                     // the panel is in a window of its own
    bool pendingStart = false, pendingCarrier = false;   // Start was pressed: the transmission starts on the next frame so "Preparing" is shown
    bool sessionDry = false;                 // the running session is a test
    double startedAt = 0;                    // ImGui time of the start (the elapsed time shown)
    double appliedMhz = 0; float appliedGain = 0;   // what the running radio has now (a retune is offered when the fields differ)
    char search[64] = {};                    // the signal list's search box
    std::string info;                        // an inline note under the header (not an error)
    struct Recent { std::string id; double mhz = 0; float gain = 0; };
    std::vector<Recent> recent;              // the last signals sent (newest first), saved in the settings
    float meterPeak = 0, meterRms = 0;       // smoothed meters of what is shown
};

TxUi& ui();                                            // the one transmit state
extern const double kTxBw[4];                          // DVB channel widths: 8, 7, 6, 5 MHz
extern const ImVec4 kRed;
int familyOf(const TxModeSpec& m);                     // engine family of a transmit mode
const TxModeSpec* rxMode(const App& a);                // the transmit mode of the receiver's current selection
void ensureTxMode(App& a, TxUi& s);                    // first use: follow the receiver's mode once
void chooseMode(App& a, TxUi& s, const TxModeSpec& m); // a mode chosen for transmitting
std::vector<int> txRadios(const App& a);               // indices of the radios that can transmit
int rxHolder(const DeviceInfo& d);                     // the receiver holding a radio (0 none)
void release(TxUi& s);                                 // gives the claimed radio back
void loadPayloadPrefs(TxUi& s);                        // callsign, grid, text, offset, recent list from the settings
void saveStr(const char* key, const char* v);        // one text into the settings
void saveRecent(const TxUi& s);                        // writes the recent list
void pushRecent(TxUi& s, const std::string& id, double mhz, float gain);
TxRequest makeReq(const TxUi& s, const TxModeSpec& mode, bool carrier);
bool isVoiceId(const std::string& id);
bool isFtxId(const std::string& id);
bool isKbdId(const std::string& id);
bool isTvId(const std::string& id);
void payloadPanel(TxUi& s, const TxModeSpec& mode, bool running);   // the signal's own options (tx_payload_ui.cpp)
const DeviceInfo* bindRadio(App& a, TxUi& s, int& conflict);        // the chosen transmit radio (nullptr: none) and the receiver that holds it
bool canStart(App& a, TxUi& s, const DeviceInfo* radio, int conflict, const TxModeSpec* mode);   // everything needed to press Start is there
bool startTx(App& a, TxUi& s, const DeviceInfo* radio, const TxModeSpec* mode, bool carrier);
void stopTx(App& a, TxUi& s, const char* why);         // stops, gives the radio back, logs why
void txNoticePopup(TxUi& s);                           // the one-time notice before the first transmission

// the guided panel (tx_panel.cpp) and its three steps
enum class Note { Info, Warn, Error };
void banner(Note kind, const char* text);              // an inline message box (no popup)
void sectionTitle(int number, const char* title, const char* summary);   // "1  Signal" with the chosen value on the right
void stepSignal(App& a, TxUi& s, bool running);        // tx_step_signal.cpp
void stepSettings(App& a, TxUi& s, const DeviceInfo* radio, int conflict, const TxModeSpec* mode, bool running);   // tx_step_settings.cpp
void stepSend(App& a, TxUi& s, const DeviceInfo* radio, int conflict, const TxModeSpec* mode, bool running);       // tx_step_send.cpp
void drawStatusHeader(App& a, TxUi& s, const DeviceInfo* radio, int conflict, const TxModeSpec* mode);              // tx_panel.cpp

// the preview of what will be sent (tx_preview.cpp): IQ of the planned signal made on a worker thread, or the running session's tap
struct PreviewData { std::vector<cf32> iq; double rateHz = 0; std::string error; bool ready = false, busy = false; };
const PreviewData& previewFor(TxUi& s, const TxModeSpec& mode, const DeviceInfo* radio);
void previewDrop();                                    // forget it (the mode changed)

} // namespace txui
