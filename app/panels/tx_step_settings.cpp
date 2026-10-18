// Step 2 of the transmit panel: the radio, the frequency (with the radio's own range as information), the output level, the signal's own options and the
// "Test (dry run, no RF)" switch. Nothing here limits a value: a frequency or level outside the radio's range is sent to the driver as typed, with a note.
// While a transmission runs only the frequency and the level can be changed, and "Apply" retunes the running radio.
#include "tx_state.h"
#include "../ui/icons.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace txui;

namespace {

// "70 MHz to 6 GHz" style text of a range (empty when unknown)
std::string rangeText(double lo, double hi) {
    if (hi <= lo) return "";
    auto f = [](double hz) {
        char b[32];
        if (hz >= 1e9) snprintf(b, sizeof b, "%g GHz", hz / 1e9);
        else snprintf(b, sizeof b, "%g MHz", hz / 1e6);
        return std::string(b);
    };
    return f(lo) + " - " + f(hi);
}

// the radio choice and what to tell when there is none (receive-only radios are named with the reason)
void radioRow(App& a, TxUi& s, const DeviceInfo*& radio, bool running) {
    const std::vector<int> radios = txRadios(a);
    ImGui::BeginDisabled(running || s.dryRun);
    ImGui::SetNextItemWidth(std::max(160.f * gUi, ImGui::GetContentRegionAvail().x - 150 * gUi));
    if (ImGui::BeginCombo(tr("Transmit radio"), radio ? radio->name.c_str() : tr("Choose a radio to transmit with."))) {
        for (int i : radios) {
            const DeviceInfo& d = a.devices[(size_t)i];
            const int h = rxHolder(d);
            char nm[256];
            if (h) snprintf(nm, sizeof nm, tr("%s (in use by RX %d)"), d.name.c_str(), h); else snprintf(nm, sizeof nm, "%s", d.name.c_str());
            if (ImGui::Selectable(nm, radio == &d)) { s.radioKey = deviceClaimKey(d); radio = &d; s.gainSet = false; }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (radios.empty() && !s.dryRun) {
        banner(Note::Info, tr("No radio that can transmit was found. Plug one in (HackRF, PlutoSDR, or a SoapySDR radio with a transmitter) and refresh the radio list. The test below needs no radio."));
        int shown = 0;
        for (const auto& d : a.devices) {
            if (!d.isRadio() || d.canTransmit || shown >= 3) continue;
            ImGui::TextDisabled("%s: %s", d.name.c_str(), txUnavailableReason(d).c_str());
            shown++;
        }
    }
}

// frequency field + the radio's range as a hint + the offset
void frequencyRow(TxUi& s, const DeviceInfo* radio) {
    ImGui::SetNextItemWidth(160 * gUi);
    ImGui::InputDouble(tr("Frequency (MHz)"), &s.freqMhz, 0.0, 0.0, "%.6f");
    if (radio) {
        const std::string r = rangeText(radio->minFreqHz, radio->maxFreqHz);
        if (!r.empty()) { ImGui::SameLine(); ImGui::TextDisabled(tr("radio range %s"), r.c_str()); }
    }
    if (radio && !s.dryRun) {
        const std::string note = txRangeInfo(*radio, s.freqMhz * 1e6);
        if (!note.empty()) banner(Note::Info, note.c_str());
    }
}

// the output level: a slider in dB with the radio's own stage names under it
void levelRow(TxUi& s, const DeviceInfo* radio) {
    const bool hasRange = radio && radio->txGainMaxDb > radio->txGainMinDb;
    if (radio && (!s.gainSet || s.gainDb < radio->txGainMinDb - 1e-3 || (hasRange && s.gainDb > radio->txGainMaxDb + 1e-3))) {
        s.gainDb = (float)(hasRange ? radio->txGainMinDb : 0);
        s.gainSet = true;
    }
    ImGui::SetNextItemWidth(hasRange ? std::max(160.f * gUi, ImGui::GetContentRegionAvail().x - 150 * gUi) : 120 * gUi);
    if (hasRange) ImGui::SliderFloat(tr("Output level (dB)"), &s.gainDb, (float)radio->txGainMinDb, (float)radio->txGainMaxDb, "%.1f dB");
    else ImGui::InputFloat(tr("Output level (dB)"), &s.gainDb, 0, 0, "%.1f");
    if (radio) {
        if (!radio->txGainNote.empty()) ImGui::TextDisabled("%s", tr(radio->txGainNote.c_str()));
        if (radio->kind == DeviceInfo::HackRF) ImGui::Checkbox(tr("+14 dB amplifier"), &s.ampOn);
        // the output choice: a radio with one output has no choice; txChannels 0 means not known until it has been opened
        int nOut = radio->txChannels;
        if (nOut <= 0) nOut = radio->board == "uhd" ? 4 : 2;
        if (radio->txChannels == 1) s.txCh = 0;
        else {
            if (s.txCh < 0 || s.txCh >= nOut) s.txCh = 0;
            char cur[16];
            snprintf(cur, sizeof cur, "TX%d", s.txCh + 1);
            ImGui::SetNextItemWidth(90 * gUi);
            if (ImGui::BeginCombo(tr("TX channel"), cur)) {
                for (int c = 0; c < nOut; c++) {
                    char lab[32];
                    snprintf(lab, sizeof lab, "TX%d", c + 1);
                    if (ImGui::Selectable(lab, s.txCh == c)) s.txCh = c;
                }
                ImGui::EndCombo();
            }
        }
    }
}

} // namespace

namespace txui {

// Step 2: Settings
void stepSettings(App& a, TxUi& s, const DeviceInfo* radio, int conflict, const TxModeSpec* mode, bool running) {
    sectionTitle(2, tr("Settings"), s.dryRun ? tr("test, no RF") : radio ? radio->name.c_str() : "");
    if (!mode) { ImGui::TextDisabled("%s", tr("Choose a signal in step 1.")); return; }

    ImGui::BeginDisabled(running);
    if (ImGui::Checkbox(tr("Test (dry run, no RF)"), &s.dryRun)) plat::basePrefs().setB("tx.dryRun", s.dryRun);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Runs the signal generator at the radio's sample rate into nothing: no radio is opened and nothing is sent. The spectrum and levels below show what would go out."));

    radioRow(a, s, radio, running);
    if (conflict && !s.dryRun) {
        char b[400];
        snprintf(b, sizeof b, tr("%s is in use by RX %d, and this radio cannot receive and transmit at the same time. Choose another radio for transmitting, or stop that receiver first."), radio->name.c_str(), conflict);
        banner(Note::Warn, b);
    }

    frequencyRow(s, radio);
    levelRow(s, radio);
    if (running) {   // a running radio can be retuned: the fields above are live
        const bool differs = std::fabs(s.freqMhz - s.appliedMhz) > 1e-9 || std::fabs(s.gainDb - s.appliedGain) > 1e-3f;
        ImGui::BeginDisabled(!differs);
        if (ImGui::Button(tr("Apply frequency and level now"))) {
            std::string err;
            TxTune t = makeReq(s, *mode, false).tune;
            if (s.session.retune(t, err)) { s.appliedMhz = s.freqMhz; s.appliedGain = s.gainDb; s.freqSent = t.centerHz; s.error.clear(); }
            else s.error = err;
        }
        ImGui::EndDisabled();
    }

    ImGui::BeginDisabled(running);
    if (ImGui::TreeNode(tr("Offset, repeat and timing"))) {
        ImGui::SetNextItemWidth(140 * gUi);
        if (ImGui::InputDouble(tr("Offset (MHz)"), &s.offsetMhz, 0.0, 0.0, "%.6f")) plat::basePrefs().setD("tx.offsetMhz", s.offsetMhz);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("This is added to the frequency sent to the radio, for a radio that radiates off frequency."));
        if (s.offsetMhz != 0) { ImGui::SameLine(); ImGui::TextDisabled(tr("radio is told %.6f MHz"), s.freqMhz + s.offsetMhz); }
        ImGui::Checkbox(tr("Repeat until stopped"), &s.loop);
        if (!s.loop) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80 * gUi);
            ImGui::InputInt(tr("seconds"), &s.onceSeconds);
            s.onceSeconds = std::max(1, std::min(3600, s.onceSeconds));
        }
        ImGui::NewLine();
        ImGui::SetNextItemWidth(100 * gUi);
        if (ImGui::BeginCombo(tr("TX sample rate"), s.autoSampleRate ? tr("Auto") : tr("Manual"))) {
            if (ImGui::Selectable(tr("Auto"), s.autoSampleRate)) s.autoSampleRate = true;
            if (ImGui::Selectable(tr("Manual"), !s.autoSampleRate)) s.autoSampleRate = false;
            ImGui::EndCombo();
        }
        if (!s.autoSampleRate) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140 * gUi);
            ImGui::InputDouble(tr("TX Fs (Msps)"), &s.sampleRateMsps, 0, 0, "%.6f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Requested transmit IQ sample rate. For a shared RX/TX radio, set the same hardware rate on both sides. Stop transmitting before changing it."));
        } else if (radio && mode) {
            ImGui::SameLine();
            const TxRequest r = makeReq(s, *mode, false);
            ImGui::TextDisabled(tr("Requested: %.3f Msps"), txRequestRate(*mode, r, TxRateLimits{radio->txMinRateHz, radio->txMaxRateHz}) / 1e6);
        }
        ImGui::TreePop();
    }

    ImGui::SeparatorText(mode->name);
    payloadPanel(s, *mode, running);
    if (const ModeUi* mu = modeUi(familyOf(*mode)); mu && mu->synth && !isVoiceId(mode->id ? mode->id : "") && !isFtxId(mode->id ? mode->id : "") && std::string(mode->id) != "rtty" && std::string(mode->id) != "atv") {
        if (ImGui::TreeNode(tr("Receiver-style options (demo signal)"))) {
            const SynthConfig saved = a.tune.synth;
            a.tune.synth = s.synth;
            bool changed = false;
            mu->synth(a, changed);
            s.synth = a.tune.synth;
            a.tune.synth = saved;
            ImGui::TreePop();
        }
    }
    ImGui::EndDisabled();
}

} // namespace txui
