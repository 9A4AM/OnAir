// Live transcripts in the app (core/src/transcribe): the settings, the speech model (downloaded on request or put in the models folder), the taps on
// the airband channels and on the sound of the other modes, the Transcripts window, the live captions of the airband screen, the "Voice" section of
// an aircraft and the transcripts shared to the map. docs/TRANSCRIBE.md.
#pragma once
#include "dect2/acars/acars_log.h"
#include <cstdint>
#include <string>
#include <vector>

struct App;

namespace tx {

void tick(App& first);                       // once per frame (cheap: it works about once a second)
void shutdown();
bool& windowOpen();                          // View > Transcripts
void windowPanel(App& a);                         // the Transcripts window (list, search, filters, export, settings, speech model)
void captions(App& a);                       // the live captions strip at the top of the airband screen
// What was said to or about an aircraft, newest first (from the callsign or registration spoken, or the aircraft the call was matched to).
void voiceSection(const App& a, uint32_t icao, const std::string& callsign, const std::string& reg);
size_t voiceCount(uint32_t icao, const std::string& callsign, const std::string& reg);
// Airband transcripts for a map-upload track as messages of mode "VOICE" (only when the user shares them).
std::vector<dect2::AcarsLogEntry> voiceForTrack(const std::string& id, const std::string& label, int callsignHolders);
// The visible indicator: true and the words while transcription is running ("Transcribing airband with tiny.en").
bool indicator(std::string& text);

}  // namespace tx
