// "Identify this signal": the spectrum explorer's region and a right-click on any mode's spectrum hand a region to the engine's capture
// (dect2/engine/sig_ident.h); this window shows what it measured, the modes it could be, and switches to the best one on that frequency.
#include "app.h"
#include "dect2/engine/sig_ident.h"
#include "dect2/specx/specx_measure.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace {
namespace si = dect2::sigid;
struct IdState {
    bool open = false;
    double centreRadioHz = 0;   // the samples' centre (radio Hz) when it started
    double offHz = 0, widthHz = 0, rfHz = 0;
    uint64_t seq = 0;
    bool have = false;
    si::Result res;
};
IdState I;

std::string fq(double hz) { return dect2::specx::freqText(hz); }

// the app's mode-list family for a ModeTuning id (the first six are the original families)
int familyOf(const std::string& id) {
    if (id == "dvbt") return 0;
    if (id == "atsc") return 1;
    if (id == "dab") return 2;
    if (id == "isdbt") return 4;
    if (id == "fm") return 5;
    if (const ModeTuning* mt = modeTuningById(id)) return mt->stdMode - 2;
    return -1;
}

void openMode(App& a, const si::Candidate& c, double radioHz) {
    const int fam = familyOf(c.modeId);
    if (fam < 0) return;
    if (a.engine.running()) a.engine.stop();
    selectMode(a, fam, c.subId.empty() ? nullptr : c.subId.c_str());
    tuneFreq(a, radioHz / 1e6);
    startReceiver(a);
    I.open = false;
}

void row(const char* k, const std::string& v) {
    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextDisabled("%s", tr(k));
    ImGui::TableNextColumn(); ImGui::TextUnformatted(v.c_str());
}
} // namespace

void identifyStart(App& a, double sampleCentreRadioHz, double offHz, double widthHz, double rfOfCentreHz) {
    I = IdState();
    I.open = true;
    I.centreRadioHz = sampleCentreRadioHz; I.offHz = offHz; I.widthHz = widthHz; I.rfHz = rfOfCentreHz;
    if (a.engine.running() && a.engine.sampleRate() > 0) a.engine.sigid().start(a.engine.sampleRate(), offHz, widthHz, rfOfCentreHz);
}

void identifyAt(App& a, double clickMhz) {
    const double rc = radioCenterMhz(a) * 1e6, click = clickMhz * 1e6;
    double lo = click - 12.5e3, hi = click + 12.5e3;   // nothing found under the click: a 25 kHz channel around it
    const std::vector<double> x = xs(a);
    if (x.size() >= 16 && a.smooth.size() >= x.size()) {
        dect2::specx::SpecView v;
        v.db = a.smooth.data(); v.n = (int)x.size(); v.f0Hz = x[0] * 1e6; v.binHz = (x[1] - x[0]) * 1e6; v.enbw = 1;
        for (const auto& m : dect2::specx::detect(v, 6, 3))
            if (click >= m.loHz && click <= m.hiHz) { lo = m.loHz; hi = m.hiHz; break; }
    }
    identifyStart(a, rc, (lo + hi) / 2 - rc, hi - lo, rc);
}

void identifyWindow(App& a) {
    if (!I.open) return;
    si::Capture& cap = a.engine.sigid();
    si::Result r;
    if (cap.result(r, I.seq)) { I.res = std::move(r); I.have = true; }
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.4f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(420 * gUi, 0), ImVec2(620 * gUi, disp.y * 0.9f));
    if (ImGui::Begin(trw("Identify signal"), &I.open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        if (I.widthHz > 0) ImGui::TextDisabled(tr("Region %s, %s wide"), fq(I.rfHz + I.offHz).c_str(), fq(I.widthHz).c_str());
        else ImGui::TextDisabled(tr("The strongest signal around %s"), fq(I.rfHz).c_str());
        const int st = cap.state();
        if (!a.engine.running() && st != 3) ImGui::TextUnformatted(tr("Start the receiver to identify a signal."));
        else if (st == 1) ImGui::ProgressBar((float)cap.progress(), ImVec2(-1, 0), tr("collecting samples"));
        else if (st == 2) ImGui::TextUnformatted(tr("Working it out..."));
        if (I.have) {
            const si::Features& f = I.res.f;
            if (!f.valid) ImGui::TextWrapped("%s", f.why.empty() ? tr("Nothing to identify here.") : f.why.c_str());
            else {
                ImGui::Separator();
                ImGui::TextWrapped("%s", I.res.summary.c_str());
                const double radio = I.centreRadioHz + f.centreHz;
                const si::Candidate* top = !I.res.cands.empty() && I.res.cands[0].score >= 45 ? &I.res.cands[0] : nullptr;
                if (top) {
                    char b[160];
                    if (top->syncHits > 0) snprintf(b, sizeof b, tr("Looks like: %s (frame sync found %dx)"), top->name.c_str(), top->syncHits);
                    else snprintf(b, sizeof b, tr("Looks like: %s"), top->name.c_str());
                    ImGui::TextUnformatted(b);
                    if (familyOf(top->modeId) >= 0) {
                        snprintf(b, sizeof b, tr("Open %s on this frequency"), top->name.c_str());
                        if (ImGui::Button(b)) openMode(a, *top, radio);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Switch to %s and tune to %s"), top->name.c_str(), fq(radio).c_str());
                    }
                } else ImGui::TextDisabled("%s", tr("No mode fits well."));
                if (ImGui::BeginTable("##sidf", 2, ImGuiTableFlags_SizingFixedFit)) {
                    char b[128];
                    row(N_("Centre"), fq(f.rfHz));
                    snprintf(b, sizeof b, "%s / %s / %s", fq(f.bw3Hz).c_str(), fq(f.bw20Hz).c_str(), fq(f.bw26Hz).c_str()); row(N_("Width -3 / -20 / -26 dB"), b);
                    row(N_("Occupied (99 %)"), fq(f.obwHz));
                    snprintf(b, sizeof b, "%.1f dB", f.snrDb); row("SNR", b);
                    if (f.duty < 0.97) snprintf(b, sizeof b, tr("bursts, %.0f %% on, %d of about %.3g ms"), f.duty * 100, f.bursts, f.burstMs);
                    else snprintf(b, sizeof b, "%s", tr("continuous"));
                    row(N_("On the air"), b);
                    row(N_("Modulation"), si::modName(f.mod));
                    snprintf(b, sizeof b, tr("%.2f (1 constant, 2 noise-like)"), f.envKurt); row(N_("Envelope"), b);
                    if (f.symRate > 0) {
                        static const char* const kSrc[] = {"", "|x|^2 line", "FM discriminator line", "x^n lines", "chirp length", "OFDM symbol"};
                        snprintf(b, sizeof b, "%.1f Bd (%s, %.0f dB)", f.symRate, kSrc[f.symSrc < 6 ? f.symSrc : 0], f.symLineDb); row(N_("Symbol rate"), b);
                    }
                    if (f.fskLevels == 4) { snprintf(b, sizeof b, "%+.0f / %+.0f / %+.0f / %+.0f Hz", f.devHz[0], f.devHz[1], f.devHz[2], f.devHz[3]); row("FSK levels", b); }
                    if (f.fskLevels == 2) { snprintf(b, sizeof b, "%+.0f / %+.0f Hz", f.devHz[0], f.devHz[1]); row("FSK levels", b); }
                    if (f.mod == si::ModOfdm) { snprintf(b, sizeof b, "Tu %.4g us, guard %.4g us", f.ofdmTuSec * 1e6, f.ofdmTgSec * 1e6); row("OFDM", b); }
                    if (f.mod == si::ModChirp) { snprintf(b, sizeof b, "SF%d, %s", f.loraSf, fq(f.chirpBwHz).c_str()); row("LoRa", b); }
                    ImGui::EndTable();
                }
                if (!I.res.cands.empty()) {
                    ImGui::Separator();
                    ImGui::TextDisabled("%s", tr("Candidates"));
                    for (size_t i = 0; i < I.res.cands.size(); i++) {
                        const si::Candidate& c = I.res.cands[i];
                        ImGui::PushID((int)i);
                        ImGui::Text("%3.0f  %s", c.score, c.name.c_str());
                        if (c.syncTried) { ImGui::SameLine(); ImGui::TextDisabled(tr(c.syncHits ? "(sync %dx)" : "(no sync)"), c.syncHits); }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.why.c_str());
                        if (i > 0 && familyOf(c.modeId) >= 0) { ImGui::SameLine(); if (ImGui::SmallButton(tr("Open"))) openMode(a, c, radio); }
                        ImGui::PopID();
                    }
                }
            }
        }
        ImGui::Separator();
        if (ImGui::Button(tr("Again")) && a.engine.running()) {
            const double c = I.centreRadioHz, o = I.offHz, w = I.widthHz, rf = I.rfHz;
            identifyStart(a, c, o, w, rf);
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Close"))) I.open = false;
    }
    ImGui::End();
    if (!I.open && a.engine.sigid().state() != 0) a.engine.sigid().cancel();
}
