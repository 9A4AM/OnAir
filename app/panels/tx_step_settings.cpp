// The Basic block of the transmit panel: the radio and the test switch, the output level, what to send (the signal's own options) and, folded below, the
// Advanced block (tx_params.cpp). The frequency is in the top bar, like the receiver's. Nothing here limits a value: a frequency or level outside the radio's
// range is sent to the driver as typed, with a note. While a transmission runs only the frequency and the level can be changed, and "Apply" retunes the radio.
#include "tx_state.h"
#include "../ui/typography.h"
#include "../ui/icons.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "../platform/platform.h"

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
    ImGui::BeginDisabled(running || noRadio(s));
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
    if (radios.empty() && !noRadio(s)) {
        banner(Note::Info, tr("No radio that can transmit was found. Plug one in (HackRF, PlutoSDR, or a SoapySDR radio with a transmitter) and refresh the radio list. The test below needs no radio."));
        int shown = 0;
        for (const auto& d : a.devices) {
            if (!d.isRadio() || d.canTransmit || shown >= 3) continue;
            ImGui::TextDisabled("%s: %s", d.name.c_str(), txUnavailableReason(d).c_str());
            shown++;
        }
    }
}

// the radio's own range as information (never a block) for the frequency in the top bar
void rangeNote(TxUi& s, const DeviceInfo* radio) {
    if (!radio) return;
    const std::string r = rangeText(radio->minFreqHz, radio->maxFreqHz);
    if (!r.empty()) ImGui::TextDisabled(tr("radio range %s"), r.c_str());
    if (!noRadio(s)) {
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

// Where the signal goes: a radio, or an IQ file (no radio is opened or claimed, none of the radio notices apply). The folder, the format and the sample
// rate of the file, and what has been written so far.
void destinationPanel(TxUi& s, const TxModeSpec& mode, bool running) {
    plat::Prefs& pr = plat::basePrefs();
    ImGui::BeginDisabled(running);
    const char* dn[] = {"Radio", "IQ file"};
    ImGui::SetNextItemWidth(160 * gUi);
    if (ImGui::BeginCombo("Destination", dn[s.destKind == 1 ? 1 : 0])) {
        for (int i = 0; i < 2; i++) if (ImGui::Selectable(dn[i], s.destKind == i)) { s.destKind = i; pr.setI("tx.destKind", i); }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Radio sends the signal to the transmit radio. IQ file writes the same IQ to a file instead: no radio is needed.");
    if (s.destKind == 1) {
        ImGui::SetNextItemWidth(std::max(160.f * gUi, ImGui::GetContentRegionAvail().x - 190 * gUi));
        if (ImGui::InputText("Folder##ifp", s.ifPath, sizeof s.ifPath)) pr.setS("tx.ifPath", s.ifPath);
        ImGui::SameLine();
        if (ImGui::Button("Browse...##ifp")) { const std::string d = plat::openFolderDialog(); if (!d.empty()) { snprintf(s.ifPath, sizeof s.ifPath, "%s", d.c_str()); pr.setS("tx.ifPath", s.ifPath); } }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "The folder for the file. The name is made from the mode, the frequency, the rate and the time. Empty: the current folder.");
        ImGui::SetNextItemWidth(250 * gUi);
        const int fi = std::max(0, std::min(kIqFileFormats - 1, s.ifFormat));
        if (ImGui::BeginCombo("Format", iqFileFormatName((IqFileFormat)fi))) {
            for (int i = 0; i < kIqFileFormats; i++) if (ImGui::Selectable(iqFileFormatName((IqFileFormat)i), fi == i)) { s.ifFormat = i; pr.setI("tx.ifFormat", i); }
            ImGui::EndCombo();
        }
        int nr = 0;
        const double* rates = iqFileRates(nr);
        char cur[48];
        snprintf(cur, sizeof cur, "%g ksps", s.ifRateHz / 1e3);
        ImGui::SetNextItemWidth(160 * gUi);
        if (ImGui::BeginCombo("Sample rate", cur)) {
            for (int i = 0; i < nr; i++) {
                if (rates[i] < mode.minRateHz - 1 || (rates[i] < 13000 && mode.minRateHz <= 0)) continue;   // below what the mode's generator works at
                char lab[48];
                snprintf(lab, sizeof lab, "%g ksps", rates[i] / 1e3);
                if (ImGui::Selectable(lab, std::fabs(s.ifRateHz - rates[i]) < 1)) { s.ifRateHz = rates[i]; pr.setD("tx.ifRateHz", s.ifRateHz); }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "The sample rate of the file. The signal is made at this rate (or resampled to it). The centre frequency written to a SigMF file is the frequency above.");
        ImGui::TextDisabled("%s", "A microphone is written at its own speed; a WAV file is written as fast as it is made and the file is closed at its end.");
    }
    ImGui::EndDisabled();
    if (s.destKind == 1 && (running || s.ifCounters)) {
        const IqFileTxCounters* c = s.ifCounters.get();
        if (c) {
            const double mb = (double)c->bytes.load() / 1048576.0;
            const double sec = c->seconds();
            ImGui::Text("%s %.2f MB, %d:%02d.%d", running ? "Writing:" : "Written:", mb, (int)sec / 60, (int)sec % 60, (int)(sec * 10) % 10);
            if (!s.ifPathUsed.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", s.ifPathUsed.c_str()); }
        }
    }
}

// The Basic block: radio, level, what to send; then Advanced (folded)
void stepSettings(App& a, TxUi& s, const DeviceInfo* radio, int conflict, const TxModeSpec* mode, bool running) {
    if (!mode) { ImGui::TextDisabled("%s", tr("Choose a signal in the list.")); return; }
    ImGui::BeginDisabled(running);
    if (ImGui::Checkbox(tr("Test (dry run, no RF)"), &s.dryRun)) plat::basePrefs().setB("tx.dryRun", s.dryRun);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Runs the signal generator at the radio's sample rate into nothing: no radio is opened and nothing is sent. The spectrum and levels show what would go out."));

    destinationPanel(s, *mode, running);
    radioRow(a, s, radio, running);
    if (conflict && !noRadio(s)) {
        char b[400];
        snprintf(b, sizeof b, tr("%s is in use by RX %d, and this radio cannot receive and transmit at the same time. Choose another radio for transmitting, or stop that receiver first."), radio->name.c_str(), conflict);
        banner(Note::Warn, b);
    }
    if (s.destKind != 1) { rangeNote(s, radio); levelRow(s, radio); }
    if (running && s.destKind != 1) {   // a running radio can be retuned: the frequency and the level are live
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

    ImGui::Spacing();
    ImGui::BeginDisabled(running);
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

    ImGui::Spacing();
    ImGui::SetNextItemOpen(s.advOpen, ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1, 1, 1, 0.045f));
    s.advOpen = ImGui::CollapsingHeader(tr("Advanced"));
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Every setting of this signal: modulation, audio, filters, output and timing. The defaults are sensible; open this only to change them."));
    if (s.advOpen) advancedPanel(s, *mode, running);
}

} // namespace txui
