// KAAL as a transmit mode: the TX session's generator. Speech comes from TxRequest::pullAudio (48 kHz stereo: the microphone, a WAV/MP3 file, a tone
// or silence, chosen by the session), is brought to 16 kHz mono and goes to kaal::VoiceTx, whose IQ is made at the radio's rate.
// An empty callsign or grid is the demo identity ONAIR / AA00aa, like the other modes.
// Station identity (the beacon slice): TxRequest::callsign, grid, lat, lon, text (100 characters at most), opt "kaal.powerDbm" (-40 .. 87.5) and
// opt "kaal.beacon" (1 = send the beacon slice, 0 = voice only). Nothing about mode, frequency or power is limited here.
#pragma once
#include "dect2/tx/tx.h"
#include <memory>
#include <string>

namespace dect2 {

// nullptr and err (plain words) when the beacon fields are not usable or the rate is below 48 kHz
std::unique_ptr<TxGenerator> makeKaalTxGenerator(const TxRequest& r, double rateHz, std::string& err);

// The beacon fields of a request as VoiceTx wants them; false and err when the callsign (up to 9 of " 0-9A-Z/-", lower case is raised) or the
// grid (6 characters, e.g. KO26ab) cannot be sent.
bool kaalBeaconFromRequest(const TxRequest& r, std::string& callsign, std::string& grid, double& powerDbm, std::string& text, std::string& err);

} // namespace dect2
