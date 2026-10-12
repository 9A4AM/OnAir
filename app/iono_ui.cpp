// Ionospheric sounder screens: the sounder picker and timing lock, the band coverage strip (what the radio has captured of 0 .. 30 MHz,
// the span now, the columns the shown sounder was heard in), the ionogram (group range against frequency) with a read-out under the
// mouse, the timeline of sweeps per sounder, PNG and CSV export, the list of sounders, and a short "?" guide.
#include "app.h"
#include "dect2/iono_dsp.h"
#include "dect2/iono_gen.h"
#include "dect2/iono_rx.h"
#include "dect2/iono_tel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace {

namespace io = dect2::iono;

const double kRangeKm[] = {1500, 3000};
const char* const kRangeName[] = {"0 - 1500 km", "0 - 3000 km"};
const ImU32 kSounderCol[] = {IM_COL32(255, 200, 90, 255), IM_COL32(110, 200, 255, 255), IM_COL32(150, 230, 120, 255), IM_COL32(240, 120, 200, 255),
                             IM_COL32(200, 160, 255, 255), IM_COL32(255, 140, 110, 255)};

struct State {
    int sel = -1;                 // the sounder shown (index in the telemetry's list), -1 the latest heard
    bool locked = true;
    int range = 1;
    float dbLo = 0, dbHi = 18;
    std::string note;
    gfx::Image* img = nullptr;
    uint64_t imgSeq = 0;
    bool loaded = false;
};
State S;

void tip(const char* t) { if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", t); }
bool live(const App& a) { return a.engine.running() && a.rx.standard == 53 && a.rx.iono.state == 1; }
ImU32 sounderCol(int id) { return kSounderCol[(size_t)((id - 1) % 6 + 6) % 6]; }

void load() {
    if (S.loaded) return;
    S.loaded = true;
    auto& p = plat::prefs();
    S.locked = p.getB("ionoLocked", true);
    S.range = (int)std::clamp(p.getI("ionoRange", 1), 0L, 1L);
    S.dbHi = (float)std::clamp(p.getD("ionoDbHi", 18.0), 3.0, 40.0);
}
void save() {
    auto& p = plat::prefs();
    p.setB("ionoLocked", S.locked); p.setI("ionoRange", S.range); p.setD("ionoDbHi", S.dbHi);
}

std::string clock(double t, bool withDate = false) {   // stream time: UTC with a radio, seconds from the start for a file
    char b[48];
    if (t > 1e8) {
        const std::time_t s = (std::time_t)std::floor(t);
        std::tm tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &s);
#else
        gmtime_r(&s, &tm);
#endif
        std::strftime(b, sizeof b, withDate ? "%Y-%m-%d %H:%M:%S UTC" : "%H:%M:%S", &tm);
    } else snprintf(b, sizeof b, "%.1f s", t);
    return b;
}

std::string sounderName(const dect2::IonoSounder& s) {
    char b[96];
    if (s.periodSec > 0) snprintf(b, sizeof b, "#%d  %.0f kHz/s, every %.0f s at +%.0f s", s.id, s.rateHz / 1e3, s.periodSec, s.offsetSec);
    else snprintf(b, sizeof b, "#%d  %.0f kHz/s (period not known yet)", s.id, s.rateHz / 1e3);
    return b;
}

uint32_t cellCol(float db) {
    if (std::isnan(db)) return 0xFF000000u;
    return Waterfall::jet(std::clamp((db - S.dbLo) / std::max(0.5f, S.dbHi - S.dbLo), 0.f, 1.f));
}

int rowsShown() { return std::min(io::kRows, (int)std::lround(kRangeKm[S.range] / io::groupRangeKm(io::kDelayBin))); }

// the ionogram as pixels, top row = the largest range shown
void pixels(const dect2::IonoTelemetry& t, std::vector<uint32_t>& px, int& w, int& h) {
    w = io::kCols; h = rowsShown();
    px.assign((size_t)w * h, 0xFF000000u);
    if (t.ionoDb.size() != (size_t)io::kRows * io::kCols) return;
    for (int y = 0; y < h; y++) {
        const int row = h - 1 - y;
        for (int c = 0; c < w; c++) px[(size_t)y * w + c] = cellCol(t.ionoDb[(size_t)row * io::kCols + (size_t)c]);
    }
}

void renderImage(const App& a) {
    if (!gGfx) return;
    if (!S.img) S.img = gGfx->createImage(io::kCols, io::kRows, 0xFF000000u);
    const uint64_t key = a.rx.iono.seq * 64 + (uint64_t)S.range * 8 + (uint64_t)S.dbHi;
    if (key == S.imgSeq) return;
    S.imgSeq = key;
    std::vector<uint32_t> px;
    int w, h;
    pixels(a.rx.iono, px, w, h);
    S.img->update(0, 0, w, h, px.data());
}

void helpButton() {
    if (ImGui::SmallButton("?##iohelp")) ImGui::OpenPopup("##iohelpp");
    tip("What the ionospheric sounders are and how to hear them");
    if (ImGui::BeginPopup("##iohelpp")) {
        ImGui::PushTextWrapPos(470 * gUi);
        ImGui::TextUnformatted("Chirp sounders and ionograms");
        ImGui::Separator();
        ImGui::BulletText("Ionosondes and oblique sounders sweep a carrier across 0 - 30 MHz at a fixed rate (most often 100 kHz/s, also 125, "
                          "150, 200 and 500 kHz/s) and start each sweep at a fixed second of their repetition period. They are heard across a "
                          "continent: what comes back from the ionosphere arrives a few milliseconds later, on one or more paths (E, F1, F2, "
                          "several hops).");
        ImGui::BulletText("The sweeps are found by dechirping: the samples are multiplied by the conjugate of a chirp of each candidate rate. "
                          "A sweep of that rate turns into a steady tone, an echo delayed by tau into a tone r x tau lower; its FFT is the delay "
                          "profile at that frequency. All the profiles together are the ionogram: group range (c x tau, km) against frequency.");
        ImGui::BulletText("Radio: a wide one sees the most of the band per sweep (a HackRF at 20 Msps around 10 MHz sees 1 - 19 MHz). A narrow "
                          "radio sees the sweep only while it crosses its span: the strip above the ionogram shows what was captured. Use an HF "
                          "antenna (a long wire or a loop); a HackRF needs it below 30 MHz without the amplifier when broadcast stations are strong.");
        ImGui::BulletText("Timing locked: the sounders run from GPS and start on whole seconds, so with the computer clock right (NTP) the "
                          "delays are absolute. Clock errors of the computer shift the whole ionogram up or down; untick it to show delays "
                          "relative to the first echo. A frequency error of the radio shifts the delay too (100 Hz = 1 ms at 100 kHz/s): a "
                          "GPS-locked or calibrated radio is best.");
        ImGui::BulletText("The period and start offset are learned from two sweeps of the same rate; after that each sweep is expected and "
                          "its whole crossing goes into the ionogram.");
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
}

void savePng(App& a) {
    if (!live(a) || a.rx.iono.ionoDb.empty()) { S.note = "nothing to save yet"; return; }
    char nm[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(nm, sizeof nm, "ionogram_%Y%m%d_%H%M%S.png", std::localtime(&now));
    const std::string path = saveFileDialog(nm);
    if (path.empty()) return;
    std::vector<uint32_t> px;
    int w, h;
    pixels(a.rx.iono, px, w, h);
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    for (size_t i = 0; i < px.size(); i++) {
        rgba[i * 4] = (uint8_t)(px[i] & 0xFF); rgba[i * 4 + 1] = (uint8_t)((px[i] >> 8) & 0xFF);
        rgba[i * 4 + 2] = (uint8_t)((px[i] >> 16) & 0xFF); rgba[i * 4 + 3] = 255;
    }
    S.note = gfx::writePng(path.c_str(), rgba.data(), w, h) ? "saved " + path : "could not write " + path;
}

void saveCsv(App& a) {
    if (!live(a) || a.rx.iono.ionoDb.empty()) { S.note = "nothing to save yet"; return; }
    char nm[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(nm, sizeof nm, "ionogram_%Y%m%d_%H%M%S.csv", std::localtime(&now));
    const std::string path = saveFileDialog(nm);
    if (path.empty()) return;
    const std::string csv = io::ionogramCsv(a.rx.iono);
    FILE* f = fopen(path.c_str(), "wb");
    S.note = f && fwrite(csv.data(), 1, csv.size(), f) == csv.size() ? "saved " + path : "could not write " + path;
    if (f) fclose(f);
}

void controls(App& a) {
    const auto& t = a.rx.iono;
    bool changed = false;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Sounder"); ImGui::SameLine();
    ImGui::SetNextItemWidth(260 * gUi);
    const std::string cur = S.sel >= 0 && S.sel < (int)t.sounders.size() ? sounderName(t.sounders[(size_t)S.sel]) : "the latest heard";
    if (ImGui::BeginCombo("##iosel", cur.c_str())) {
        if (ImGui::Selectable("the latest heard", S.sel < 0)) S.sel = -1;
        for (size_t i = 0; i < t.sounders.size(); i++)
            if (ImGui::Selectable(sounderName(t.sounders[i]).c_str(), S.sel == (int)i)) S.sel = (int)i;
        ImGui::EndCombo();
    }
    tip("The sounder whose ionogram is shown");
    flowNext(10 * gUi);
    changed |= ImGui::Checkbox("Timing locked", &S.locked);
    tip("Sweeps start on whole seconds (GPS-timed sounders): delays are absolute with the computer clock right; off: relative to the first echo");
    flowNext(10 * gUi);
    ImGui::TextDisabled("Range"); ImGui::SameLine();
    ImGui::SetNextItemWidth(110 * gUi);
    changed |= ImGui::Combo("##iorange", &S.range, kRangeName, 2);
    flowNext(10 * gUi);
    ImGui::TextDisabled("Scale"); ImGui::SameLine();
    ImGui::SetNextItemWidth(90 * gUi);
    changed |= ImGui::SliderFloat("##iodb", &S.dbHi, 3, 40, "0 - %.0f dB");
    tip("Colour scale: dB over the median of each frequency column");
    flowNext(10 * gUi);
    if (ImGui::SmallButton("Clear")) { a.engine.iono().reset(); S.sel = -1; }
    tip("Forget the sounders, their ionograms and the coverage");
    flowNext(6 * gUi);
    if (ImGui::SmallButton("Save PNG")) savePng(a);
    tip("The ionogram as a picture: 0 - 30 MHz across (50 kHz a pixel), group range up (15 km a pixel)");
    flowNext(6 * gUi);
    if (ImGui::SmallButton("Save CSV")) saveCsv(a);
    tip("Frequency, delay, group range and dB, one line per ionogram cell with data");
    flowNext(6 * gUi);
    helpButton();
    if (!S.note.empty()) { flowNext(8 * gUi); ImGui::TextDisabled("%s", S.note.c_str()); }
    flowEnd();
    if (changed) save();
}

// x of a frequency in the plot area [l, r]
float xOf(double hz, float l, float r) { return (float)(l + hz / io::kBandHz * (r - l)); }

void coverage(App& a, float h) {
    const auto& t = a.rx.iono;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float l = p0.x + 52 * gUi, r = p0.x + w - 6 * gUi;
    dl->AddRectFilled(ImVec2(l, p0.y), ImVec2(r, p0.y + h), IM_COL32(16, 18, 24, 255));
    const float cw = (r - l) / io::kCols;
    if (live(a)) {
        for (int c = 0; c < io::kCols; c++) {
            const float x = l + c * cw;
            if (c < (int)t.covered.size() && t.covered[(size_t)c]) dl->AddRectFilled(ImVec2(x, p0.y), ImVec2(x + cw + 0.5f, p0.y + h), IM_COL32(52, 58, 76, 255));
            if (c < (int)t.swept.size() && t.swept[(size_t)c]) dl->AddRectFilled(ImVec2(x, p0.y + h * 0.55f), ImVec2(x + cw + 0.5f, p0.y + h), IM_COL32(120, 90, 160, 255));
        }
        if (t.spanHiHz > t.spanLoHz) dl->AddRect(ImVec2(xOf(t.spanLoHz, l, r), p0.y), ImVec2(xOf(t.spanHiHz, l, r), p0.y + h), IM_COL32(200, 200, 230, 220));
    }
    dl->AddText(ImVec2(p0.x + 3, p0.y + (h - ImGui::GetTextLineHeight()) / 2), IM_COL32(130, 136, 150, 255), "band");
    ImGui::Dummy(ImVec2(w, h));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("0 - 30 MHz: grey what the radio has captured since the start, the frame the span now, purple where the shown "
                          "sounder was heard");
}

void ionogram(App& a, float h) {
    const auto& t = a.rx.iono;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lh = ImGui::GetTextLineHeight();
    const float l = p0.x + 52 * gUi, r = p0.x + w - 6 * gUi, top = p0.y + 2, bot = p0.y + h - lh - 6 * gUi;
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(8, 9, 13, 255));
    ImGui::InvisibleButton("##ioplot", ImVec2(w, h));
    const ImU32 grid = IM_COL32(60, 64, 80, 160), txt = IM_COL32(130, 136, 150, 255);
    const int rows = rowsShown();
    const double maxKm = rows * io::groupRangeKm(io::kDelayBin);
    if (live(a) && S.img && !t.ionoDb.empty())
        dl->AddImage(S.img->texture(), ImVec2(l, top), ImVec2(r, bot), ImVec2(0, 0), ImVec2(1, (float)rows / io::kRows));
    char b[96];
    for (int mhz = 0; mhz <= 30; mhz += 5) {
        const float x = xOf(mhz * 1e6, l, r);
        dl->AddLine(ImVec2(x, top), ImVec2(x, bot), grid);
        snprintf(b, sizeof b, "%d", mhz);
        const float tw = ImGui::CalcTextSize(b).x;
        dl->AddText(ImVec2(std::clamp(x - tw / 2, l, r - tw), bot + 3 * gUi), txt, b);
    }
    dl->AddText(ImVec2(r - ImGui::CalcTextSize("MHz").x - 2, bot + 3 * gUi), txt, "MHz");
    const int step = maxKm > 2000 ? 500 : 250;
    for (int km = 0; km <= (int)maxKm; km += step) {
        const float y = (float)(bot - km / maxKm * (bot - top));
        dl->AddLine(ImVec2(l, y), ImVec2(r, y), grid);
        snprintf(b, sizeof b, "%d", km);
        dl->AddText(ImVec2(p0.x + 3, std::clamp(y - lh / 2, top, bot - lh)), txt, b);
    }
    dl->AddText(ImVec2(p0.x + 3, p0.y + h - lh - 2), txt, "km");
    if (!live(a)) {
        dl->AddText(ImVec2(l + 10, top + 10), txt, a.engine.running() ? "listening for sweeps ..." : "start the receiver to look for chirp sounders (\"?\" for how)");
        return;
    }
    if (t.sel < 0) { dl->AddText(ImVec2(l + 10, top + 10), txt, "no sounder heard yet: the ionogram builds up as sweeps cross the captured span"); }
    else {
        const auto& s = t.sounders[(size_t)t.sel];
        snprintf(b, sizeof b, "#%d  %.0f kHz/s  %s", s.id, s.rateHz / 1e3, t.locked ? "group range (c x delay)" : "relative group range (timing not locked)");
        dl->AddText(ImVec2(l + 6, top + 4), sounderCol(s.id), b);
    }
    if (ImGui::IsItemHovered() && t.ionoDb.size() == (size_t)io::kRows * io::kCols) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const double hz = std::clamp((m.x - l) / std::max(1.f, r - l), 0.f, 1.f) * io::kBandHz;
        const double km = std::clamp((bot - m.y) / std::max(1.f, bot - top), 0.f, 1.f) * maxKm;
        const int c = std::clamp((int)(hz / io::kColHz), 0, io::kCols - 1), row = std::clamp((int)std::lround(km / io::groupRangeKm(io::kDelayBin)), 0, io::kRows - 1);
        const float v = t.ionoDb[(size_t)row * io::kCols + (size_t)c];
        if (std::isnan(v)) ImGui::SetTooltip("%.2f MHz, %.0f km (%.2f ms)\nnot swept", hz / 1e6, km, io::rowDelay(row) * 1e3);
        else ImGui::SetTooltip("%.2f MHz, %.0f km (%.2f ms)\n%.1f dB", hz / 1e6, km, io::rowDelay(row) * 1e3, v);
    }
}

void timeline(App& a, float h) {
    const auto& t = a.rx.iono;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float l = p0.x + 52 * gUi, r = p0.x + w - 6 * gUi, lh = ImGui::GetTextLineHeight();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(6, 7, 10, 255));
    dl->AddText(ImVec2(p0.x + 3, p0.y + 2), IM_COL32(130, 136, 150, 255), "sweeps");
    ImGui::Dummy(ImVec2(w, h));
    if (!live(a) || t.timeline.empty()) {
        dl->AddText(ImVec2(l + 10, p0.y + 4), IM_COL32(110, 116, 128, 255), "timeline: one mark per sweep heard, a row per sounder");
        return;
    }
    const double span = 1800, t1 = t.nowSec, t0 = t1 - span;
    const int n = std::max(1, (int)t.sounders.size());
    const float rowH = std::max(4.f, (h - lh - 6) / n);
    for (int m = 0; m <= 6; m++) {
        const float x = (float)(l + m / 6.0 * (r - l));
        dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + h - lh - 2), IM_COL32(40, 44, 56, 255));
        char b[24]; snprintf(b, sizeof b, "-%d min", (6 - m) * 5);
        if (m < 6) dl->AddText(ImVec2(x + 2, p0.y + h - lh - 1), IM_COL32(110, 116, 128, 255), b);
    }
    for (const auto& e : t.timeline) {
        if (e.t0 < t0) continue;
        int k = 0;
        for (size_t i = 0; i < t.sounders.size(); i++) if (t.sounders[i].id == e.sounder) k = (int)i;
        const float x = (float)(l + (e.t0 - t0) / span * (r - l)), y = p0.y + 2 + k * rowH;
        dl->AddRectFilled(ImVec2(x - 1.5f * gUi, y), ImVec2(x + 1.5f * gUi, y + rowH - 1), sounderCol(e.sounder));
    }
    if (ImGui::IsItemHovered()) {
        const float mx = ImGui::GetIO().MousePos.x;
        const dect2::IonoEvent* best = nullptr; float bd = 8 * gUi;
        for (const auto& e : t.timeline) { const float x = (float)(l + (e.t0 - t0) / span * (r - l)); if (std::fabs(x - mx) < bd) { bd = std::fabs(x - mx); best = &e; } }
        if (best) ImGui::SetTooltip("%s  sounder #%d, %.0f kHz/s\n%.2f - %.2f MHz heard, %.1f dB%s", clock(best->t0).c_str(), best->sounder, best->rateHz / 1e3,
                                    best->fLoHz / 1e6, best->fHiHz / 1e6, best->snrDb, best->predicted ? "\nexpected from the period" : "");
    }
}

void tick(App& a) {
    load();
    auto& r = a.engine.iono();
    r.setLocked(S.locked);
    r.setSelected(S.sel);
    a.tune.synth.modeVal[0] = a.tune.centerHz / 1e6;   // the test signal places its sweeps for this frequency (read when it starts)
    if (live(a) && S.sel >= (int)a.rx.iono.sounders.size()) S.sel = -1;
}

void tab(App& a) {
    load();
    renderImage(a);
    controls(a);
    coverage(a, 14 * gUi);
    const float avail = ImGui::GetContentRegionAvail().y;
    const float tl = std::clamp(avail * 0.18f, 50 * gUi, 120 * gUi);
    ionogram(a, std::max(140 * gUi, avail - tl - 6 * gUi));
    timeline(a, std::max(40 * gUi, ImGui::GetContentRegionAvail().y - 2));
}

void list(App& a) {
    const auto& t = a.rx.iono;
    if (!live(a) || t.sounders.empty()) { ImGui::TextDisabled("no sounder heard yet"); return; }
    for (size_t i = 0; i < t.sounders.size(); i++) {
        const auto& s = t.sounders[i];
        ImGui::PushID((int)i);
        const bool shown = t.sel == (int)i;
        char b[64]; snprintf(b, sizeof b, "#%d  %.0f kHz/s%s", s.id, s.rateHz / 1e3, s.active ? "  (sweeping)" : "");
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(sounderCol(s.id)));
        if (ImGui::Selectable(b, shown)) S.sel = (int)i;
        ImGui::PopStyleColor();
        ImGui::Indent(10 * gUi);
        if (s.periodSec > 0) ImGui::TextDisabled("every %.0f s, starts at +%.0f s; next %s", s.periodSec, s.offsetSec, clock(s.nextT0).c_str());
        else ImGui::TextDisabled("period: one more sweep needed");
        ImGui::TextDisabled("%d sweeps%s, %.1f dB, %.1f - %.1f MHz", s.sweeps, s.missed ? (", " + std::to_string(s.missed) + " missed").c_str() : "",
                            s.snrDb, s.fLoHz / 1e6, s.fHiHz / 1e6);
        ImGui::TextDisabled("last heard %s (%.0f s ago)", clock(s.lastSeen).c_str(), std::max(0.0, t.nowSec - s.lastSeen));
        ImGui::Unindent(10 * gUi);
        ImGui::PopID();
    }
}

void receiver(App& a) {
    const auto& t = a.rx.iono;
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "listening" : "stopped"); return; }
    ImGui::TextWrapped("%s", dect2::ionoSummary(t).c_str());
    ImGui::TextWrapped("Detector: %d-point FFT (%.1f ms, %.0f Hz bins), %llu FFTs, %llu chirp peaks not yet tied to a sweep; candidate rates 100, 125, 150, "
                       "200 and 500 kHz/s", t.fftSize, t.rate > 0 ? 1e3 * t.fftSize / t.rate : 0.0, t.fftSize ? t.rate / t.fftSize : 0.0,
                       (unsigned long long)t.frames, (unsigned long long)t.hits);
    ImGui::TextWrapped("Ionogram: 0 - 30 MHz in 50 kHz columns, delay 0 - 10 ms in 0.05 ms rows (group range 0 - 3000 km, 15 km a row)");
    ImGui::TextWrapped("Time: %s; %s", clock(t.nowSec, true).c_str(), t.locked ? "sweep starts rounded to whole seconds (absolute delays)" : "timing not locked (relative delays)");
    if (t.spanHiHz <= t.spanLoHz) ImGui::TextColored(ImVec4(1, .6f, .3f, 1), "The captured span is outside 0 - 30 MHz: tune the radio into the HF band");
}

void status(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State"); ImGui::SameLine();
    ImGui::TextUnformatted(live(a) ? dect2::ionoSummary(a.rx.iono).c_str() : (a.engine.running() ? "listening" : "stopped"));
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "Ionospheric sounders";
    if (live(a)) {
        const auto& t = a.rx.iono;
        char b[128];
        snprintf(b, sizeof b, "%zu sounder%s, %zu sweeps, %.1f - %.1f MHz captured", t.sounders.size(), t.sounders.size() == 1 ? "" : "s", t.timeline.size(),
                 t.spanLoHz / 1e6, t.spanHiHz / 1e6);
        l2 = b;
    }
}

void synth(App& a, bool& changed) {
    dect2::SynthConfig& sc = a.tune.synth;
    { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("two sounders in noise: 100 kHz/s every 30 s at +2 s and 500 kHz/s every 20 s at +7 s, each on two paths (1.6 / 2.4 ms and 1.8 / 2.9 ms)"); ImGui::PopTextWrapPos(); }
    bool off = sc.modeOpt[0] == 1;
    if (ImGui::Checkbox("Noise only", &off)) { sc.modeOpt[0] = off ? 1 : 0; changed = true; }
    // the sweeps are placed for the frequency the radio is set to
    const double mhz = a.tune.centerHz / 1e6;
    if (std::fabs(sc.modeVal[0] - mhz) > 1e-6) { sc.modeVal[0] = mhz; changed = true; }
}

} // namespace

extern const ModeUi kIonoUi;
const ModeUi kIonoUi = {
    .sideTitle = "SOUNDERS",
    .tabName = "Ionogram",
    .tabIcon = Ic::Wave,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .synth = synth,
    .tick = tick,
};
