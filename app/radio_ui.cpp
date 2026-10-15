// The "Radio settings" section under the gain controls: the radio's own settings its driver lists (DeviceInfo::settings: frequency
// correction, direct sampling, notch filters, gain modes, ...), only those the selected radio has. The values live in a.tune.radio, are
// saved per radio (app_state.cpp: syncRadioSettings / saveRadioSettings) and reach a running radio at once (a retune); one that takes
// effect when the radio is opened restarts a running receiver. A value set back to its default is dropped, so the driver does what it always did.
#include "app.h"
#include "dect2/converter.h"
#include <cmath>
#include <map>

namespace {

std::string numText(double v) {
    char b[48];
    snprintf(b, sizeof b, "%.6g", v);
    return b;
}

// one setting; true when its value changed (the new value is in v, "" = back to the default)
bool settingRow(const RadioSetting& r, std::string& v, bool vertical) {
    const std::string cur = v.empty() ? r.def : v;
    bool changed = false;
    ImGui::PushID(r.key.c_str());
    const float w = (vertical ? 150 : 170) * gUi;
    switch (r.type) {
    case RadioSetting::Bool: {
        bool on = cur == "1";
        if (ImGui::Checkbox(r.label.c_str(), &on)) { v = on ? "1" : "0"; changed = true; }
        break;
    }
    case RadioSetting::Choice: {
        size_t at = 0;
        for (size_t i = 0; i < r.values.size(); i++) if (r.values[i] == cur) at = i;
        ImGui::TextDisabled("%s", r.label.c_str());
        if (ImGui::IsItemHovered() && !r.help.empty()) ImGui::SetTooltip("%s", r.help.c_str());
        ImGui::SetNextItemWidth(w);
        if (ImGui::BeginCombo("##v", at < r.names.size() ? r.names[at].c_str() : cur.c_str())) {
            for (size_t i = 0; i < r.values.size(); i++)
                if (ImGui::Selectable(i < r.names.size() ? r.names[i].c_str() : r.values[i].c_str(), i == at) && i != at) { v = r.values[i]; changed = true; }
            ImGui::EndCombo();
        }
        break;
    }
    case RadioSetting::Number: {
        // typed values are taken when the field is left (not at every key); the step buttons take effect at once
        static std::map<std::string, double> editing;
        auto it = editing.find(r.key);
        double val = it != editing.end() ? it->second : atof(cur.c_str());
        ImGui::TextDisabled("%s", r.label.c_str());
        if (ImGui::IsItemHovered() && !r.help.empty()) ImGui::SetTooltip("%s", r.help.c_str());
        ImGui::SetNextItemWidth(w);
        char fmt[32];
        const int dec = r.step >= 1 ? 0 : r.step >= 0.1 ? 1 : r.step >= 0.01 ? 2 : 3;
        snprintf(fmt, sizeof fmt, "%%.%df%s%s", dec, r.unit.empty() ? "" : " ", r.unit.c_str());
        if (ImGui::InputDouble("##v", &val, r.step > 0 ? r.step : 0.0, r.step > 0 ? r.step * 10 : 0.0, fmt)) editing[r.key] = val;
        it = editing.find(r.key);
        if (it != editing.end() && !ImGui::IsItemActive()) {
            double x = it->second;
            if (r.maxV > r.minV) x = std::min(r.maxV, std::max(r.minV, x));
            if (r.step >= 1) x = std::round(x);
            editing.erase(it);
            const std::string nv = numText(x);
            if (nv != numText(atof(cur.c_str()))) { v = nv; changed = true; }
        }
        break;
    }
    }
    if (r.type == RadioSetting::Bool && ImGui::IsItemHovered() && !r.help.empty()) ImGui::SetTooltip("%s", r.help.c_str());
    ImGui::PopID();
    if (changed && (v == r.def || (r.type == RadioSetting::Number && numText(atof(v.c_str())) == numText(atof(r.def.c_str()))))) v.clear();
    return changed;
}

// The converter in front of the radio (converter.h): up-converter or LNB. The app then works with the frequency at the antenna; the radio
// is tuned to the IF. Stored in a.tune.radio under the conv.* keys, so it is saved per radio with the other settings. True when it changed.
bool converterUi(App& a, bool vertical) {
    (void)vertical;
    using namespace dect2;
    bool changed = false;
    auto& R = a.tune.radio;
    auto get = [&](const char* k, const char* def) { auto it = R.find(k); return it != R.end() ? it->second : std::string(def); };
    auto put = [&](const char* k, const std::string& v, const char* def) { if (v.empty() || v == def) R.erase(k); else R[k] = v; changed = true; };
    ImGui::Separator();
    ImGui::TextUnformatted(tr("Converter (up-converter / LNB)"));
    const std::string id = get(convKeyPreset(), "none");
    const ConverterPreset* cur = findConverterPreset(id);
    if (!cur) cur = &converterPresets()[0];
    ImGui::SetNextItemWidth(260 * gUi);
    if (ImGui::BeginCombo("##convpreset", tr(cur->name))) {
        for (const auto& p : converterPresets()) {
            if (ImGui::Selectable(tr(p.name), &p == cur)) {
                put(convKeyPreset(), p.id, "none");
                R.erase(convKeyLo()); R.erase(convKeyMode()); R.erase(convKeyPpm());
                if (p.custom) { R[convKeyLo()] = "125"; R[convKeyMode()] = "add"; }
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Show and tune the frequency at the antenna while the radio is tuned to what the converter outputs."));
    if (cur->custom) {
        double lo = atof(get(convKeyLo(), "125").c_str());
        ImGui::SetNextItemWidth(120 * gUi);
        if (ImGui::InputDouble(tr("LO (MHz)"), &lo, 0, 0, "%.6f") && lo >= 0) put(convKeyLo(), numText(lo), "");
        const std::string m = get(convKeyMode(), "add");
        int mi = m == "sub" ? 1 : m == "high" ? 2 : 0;
        const char* names[3] = {tr("Up: radio = RF + LO"), tr("Down, LO below: radio = RF - LO"), tr("Down, LO above: radio = LO - RF (mirrored)")};
        ImGui::SetNextItemWidth(300 * gUi);
        if (ImGui::Combo("##convmode", &mi, names, 3)) put(convKeyMode(), mi == 1 ? "sub" : mi == 2 ? "high" : "add", "");
    }
    if (cur->hasPpm) {
        double ppm = atof(get(convKeyPpm(), "0").c_str());
        ImGui::SetNextItemWidth(120 * gUi);
        if (ImGui::InputDouble(tr("LO correction (ppm)"), &ppm, 0, 0, "%.3f")) put(convKeyPpm(), numText(ppm), "0");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("How far the converter's oscillator runs fast (positive) or slow. The LO used is LO x (1 + ppm / 1e6)."));
    }
    if (std::string(cur->id) != "none") {
        bool inv = get(convKeyInvert(), "0") == "1";
        if (ImGui::Checkbox(tr("Mirror the spectrum (swap I and Q)"), &inv)) put(convKeyInvert(), inv ? "1" : "", "");
        const Converter c = converterFromRadio(R);
        const double rf = a.freqMhz * 1e6;
        char b[256];
        if (c.active) {
            snprintf(b, sizeof b, tr("antenna %.6g MHz -> %s LO %.6g -> radio %.6g MHz%s"), rf / 1e6, c.mode == Converter::Add ? tr("up-converter") : tr("LNB"),
                     c.effLoHz() / 1e6, c.rfToIf(rf) / 1e6, c.inverts() ? tr("  (mirrored: I/Q swapped)") : "");
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "%s", b);
            if (!c.reachable(rf)) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", tr("This frequency is not reachable through the converter."));
            ImGui::PopTextWrapPos();
        } else ImGui::TextDisabled("%s", tr("Set the LO above 0 MHz."));
    }
    return changed;
}

}   // namespace

void radioSettingsUi(App& a, bool vertical) {
    if (a.devIdx < 0 || a.devIdx >= (int)a.devices.size()) return;
    const DeviceInfo& d = a.devices[a.devIdx];
    if (!d.isRadio()) return;
    syncRadioSettings(a);
    int changedN = 0;
    for (const auto& r : d.settings) if (a.tune.radio.count(r.key)) changedN++;
    if (a.tune.radio.count(dect2::convKeyPreset())) changedN++;
    char title[64];
    snprintf(title, sizeof title, changedN ? tr("Radio settings (%d changed)###radioset") : tr("Radio settings###radioset"), changedN);
    bool open = false;
    if (vertical) {
        ImGui::SetNextItemOpen(false, ImGuiCond_FirstUseEver);
        open = ImGui::TreeNodeEx(title, ImGuiTreeNodeFlags_SpanAvailWidth);
    } else {   // a one-line bar: a button with the settings in a popup (the caller places it)
        if (ImGui::SmallButton(tr(changedN ? "Radio settings*" : "Radio settings"))) ImGui::OpenPopup("##radiosetpop");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("%s: frequency correction and the other settings of this radio"), title);
        open = ImGui::BeginPopup("##radiosetpop");
    }
    if (!open) return;
    bool restart = false, retune = false;
    for (const auto& r : d.settings) {
        auto it = a.tune.radio.find(r.key);
        std::string v = it != a.tune.radio.end() ? it->second : std::string();
        if (!settingRow(r, v, vertical)) continue;
        if (v.empty()) a.tune.radio.erase(r.key); else a.tune.radio[r.key] = v;
        saveRadioSettings(a);
        if (r.restart) restart = true; else retune = true;
    }
    if (converterUi(a, vertical)) { saveRadioSettings(a); retune = true; }
    {   // the level offset of this radio, for the spectrum in dBm (plots.cpp): only the picture changes, the radio is not touched
        float off = dbmOffset(a);
        ImGui::SetNextItemWidth((vertical ? 150 : 170) * gUi);
        if (ImGui::InputFloat(tr("dBm offset (dB)"), &off, 0.5f, 5.f, "%+.1f")) setDbmOffset(a, std::max(-200.f, std::min(200.f, off)));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Added to a level in dBFS to give dBm for this radio: feed a known signal into the antenna port and set the difference. Used when the spectrum shows dBm (Spectrum button on the plot)."));
    }
    if (d.kind == DeviceInfo::Soapy && d.settings.size() <= 1) ImGui::TextDisabled("%s", tr("The driver's own settings show up here\nonce this radio has been started."));
    if (vertical) ImGui::TreePop(); else ImGui::EndPopup();
    if (!a.engine.running()) return;   // the next start takes them
    if (restart) { a.engine.log("radio setting changed: restarting the radio"); startReceiver(a); }
    else if (retune) { a.tune.centerHz = a.freqMhz * 1e6; a.engine.retune(a.tune); }
}
