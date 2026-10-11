// Cell towers screens: the cell table (operator, band, EARFCN, PCI, eNB / sector, TAC, bandwidth, RSRP / RSRQ, last seen), cells grouped
// by site, the neighbour lists of SIB4 / SIB5, the strongest cell per operator, a band scan over the LTE EARFCN raster, and an optional
// map from a tower position file the user downloads (OpenCelliD CSV: radio,mcc,net,area,cell,unit,lon,lat,...; opencellid.org,
// CC BY-SA 4.0; nothing is bundled or fetched). Only what cells broadcast to every phone is shown.
#include "app.h"
#include "adsb_map.h"
#include "dect2/cell_rx.h"
#include "dect2/cell_tel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Pos { double lat, lon; };
struct State {
    int view = 0;                 // 0 cells, 1 sites, 2 neighbours, 3 operators, 4 map, 5 log
    int sel = -1;                 // selected cell (index in cells)
    bool bands[8] = {true, true, true, true, true, false, false, false};
    bool scanning = false;
    std::vector<double> plan;     // centres, MHz
    size_t step = 0;
    double stepAt = 0;
    float dwell = 3.0f;
    std::map<uint64_t, Pos> towers;   // (mcc, mnc, eci) -> position
    std::string towerFile, towerNote;
    adsbmap::View map;
};
State S;
const int kScanBands[8] = {20, 8, 3, 1, 7, 28, 32, 38};

bool live(const App& a) { return a.engine.running() && a.rx.standard == 33; }

std::string ago(int64_t t) {
    if (t <= 0) return "-";
    const long d = (long)(std::time(nullptr) - t);
    char b[32];
    if (d < 60) snprintf(b, sizeof b, "%ld s", std::max(0L, d)); else if (d < 3600) snprintf(b, sizeof b, "%ld min", d / 60); else snprintf(b, sizeof b, "%ld h", d / 3600);
    return b;
}

uint64_t towerKey(int mcc, int mnc, uint32_t eci) { return ((uint64_t)mcc << 44) | ((uint64_t)mnc << 32) | eci; }

void loadTowers(const std::string& path) {
    std::ifstream f(path);
    if (!f) { S.towerNote = "cannot open the file"; return; }
    S.towers.clear();
    std::string line;
    size_t n = 0;
    while (std::getline(f, line)) {
        std::stringstream ss(line);
        std::vector<std::string> c;
        std::string v;
        while (std::getline(ss, v, ',')) c.push_back(v);
        if (c.size() < 8 || c[0] != "LTE") continue;
        const int mcc = atoi(c[1].c_str()), mnc = atoi(c[2].c_str());
        const uint32_t eci = (uint32_t)strtoul(c[4].c_str(), nullptr, 10);
        S.towers[towerKey(mcc, mnc, eci)] = {atof(c[7].c_str()), atof(c[6].c_str())};
        n++;
    }
    char b[96]; snprintf(b, sizeof b, "%zu LTE cells loaded", n);
    S.towerNote = b; S.towerFile = path;
}

const Pos* towerOf(const dect2::CellTower& c) {
    if (!c.hasSib1) return nullptr;
    auto it = S.towers.find(towerKey(c.mcc, c.mnc, c.cellId));
    return it == S.towers.end() ? nullptr : &it->second;
}

std::string opText(const dect2::CellTower& c) {
    if (!c.hasSib1 || c.plmns.empty()) return "-";
    return c.op.empty() ? c.plmns[0] : c.plmns[0] + " " + c.op;
}

void makePlan(const App& a) {
    S.plan.clear();
    const double span = std::max(1.5, a.tune.sampleRate / 1e6 * 0.75);   // the part of the sample band searched whole
    for (int i = 0; i < 8; i++) {
        if (!S.bands[i]) continue;
        const dect2::LteBand* b = dect2::lteBandInfo(kScanBands[i]);
        if (!b) continue;
        for (double c = b->dlLowMhz + span / 2; c - span / 2 < b->dlHighMhz; c += span) S.plan.push_back(std::round(c * 10) / 10);
    }
}

void scanBar(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Bands");
    for (int i = 0; i < 8; i++) {
        flowNext(6 * gUi);
        char l[16]; snprintf(l, sizeof l, "B%d##cb%d", kScanBands[i], i);
        ImGui::Checkbox(l, &S.bands[i]);
        if (ImGui::IsItemHovered()) if (const dect2::LteBand* b = dect2::lteBandInfo(kScanBands[i])) ImGui::SetTooltip("Band %d: %.0f - %.0f MHz (%s)", b->band, b->dlLowMhz, b->dlHighMhz, b->region);
    }
    flowNext(10 * gUi);
    if (!S.scanning) {
        if (ImGui::SmallButton("Scan") && a.engine.running()) { makePlan(a); S.step = 0; S.stepAt = 0; S.scanning = !S.plan.empty(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step the radio over the chosen bands; every LTE carrier in each step is searched. Receive only.");
    } else {
        if (ImGui::SmallButton("Stop scan")) S.scanning = false;
        ImGui::SameLine(); ImGui::TextDisabled("%zu / %zu", S.step + 1, S.plan.size());
    }
    flowNext(10 * gUi);
    if (ImGui::SmallButton("Clear")) a.engine.cell().clearCells();
    flowEnd();
}

void cellTable(App& a) {
    const dect2::CellTelemetry& t = a.rx.cell;
    // a narrow window keeps the columns that identify a cell; the rest are in the detail lines below
    const bool wide = ImGui::GetContentRegionAvail().x > 980 * gUi;
    const int nc = wide ? 11 : 6;
    const ImGuiTableFlags fl = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV;
    if (!ImGui::BeginTable("##cells", nc, fl, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    if (wide) for (const char* h : {"Operator", "Band", "EARFCN", "MHz", "PCI", "eNB / sector", "TAC", "Width", "RSRP", "RSRQ", "Seen"}) ImGui::TableSetupColumn(h);
    else for (const char* h : {"Operator", "Band", "EARFCN", "PCI", "eNB / sector", "RSRP"}) ImGui::TableSetupColumn(h);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < t.cells.size(); i++) {
        const auto& c = t.cells[i];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        char l[96]; snprintf(l, sizeof l, "%s##c%zu", wide ? opText(c).c_str() : (c.hasSib1 && !c.plmns.empty() ? c.plmns[0].c_str() : "-"), i);
        if (ImGui::Selectable(l, S.sel == (int)i, ImGuiSelectableFlags_SpanAllColumns)) S.sel = (int)i;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s, %.1f MHz, TAC %u, %.1f MHz wide, RSRQ %.1f dB, seen %s ago", opText(c).c_str(), c.freqHz / 1e6, c.tac, c.nRb * 0.2, c.rsrqDb, ago(c.lastWall).c_str());
        ImGui::TableNextColumn(); if (c.band) ImGui::Text("B%d", c.band); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn(); if (c.earfcn >= 0) ImGui::Text("%d", c.earfcn); else ImGui::TextDisabled("-");
        if (wide) { ImGui::TableNextColumn(); ImGui::Text("%.1f", c.freqHz / 1e6); }
        ImGui::TableNextColumn(); ImGui::Text("%d", c.pci);
        ImGui::TableNextColumn(); if (c.hasSib1) ImGui::Text("%u/%d", c.enb, c.sector); else ImGui::TextDisabled("-");
        if (wide) { ImGui::TableNextColumn(); if (c.hasSib1) ImGui::Text("%u", c.tac); else ImGui::TextDisabled("-"); }
        if (wide) { ImGui::TableNextColumn(); ImGui::Text("%.1f MHz", c.nRb * 0.2); }
        ImGui::TableNextColumn(); ImGui::Text("%.0f", c.rsrpDb);
        if (wide) { ImGui::TableNextColumn(); ImGui::Text("%.1f", c.rsrqDb); }
        if (wide) { ImGui::TableNextColumn(); ImGui::TextUnformatted(ago(c.lastWall).c_str()); }
    }
    ImGui::EndTable();
}

void detail(App& a) {
    ImGui::PushTextWrapPos(0);
    struct Pop { ~Pop() { ImGui::PopTextWrapPos(); } } pop_;
    const dect2::CellTelemetry& t = a.rx.cell;
    if (S.sel < 0 || S.sel >= (int)t.cells.size()) { ImGui::TextDisabled("select a cell"); return; }
    const auto& c = t.cells[(size_t)S.sel];
    ImGui::Text("PCI %d, %s, %d RB, %d port%s, %s CP, PHICH %s Ng %d, SFN %d", c.pci, c.tdd ? "TDD" : "FDD", c.nRb, c.ports, c.ports == 1 ? "" : "s", c.extCp ? "extended" : "normal",
                c.phichDur ? "extended" : "normal", c.phichRes, c.sfn);
    ImGui::Text("RSRP %.1f dBFS, RSRQ %.1f dB, SINR %.1f dB, offset %+.0f Hz", c.rsrpDb, c.rsrqDb, c.sinrDb, c.cfoHz);
    if (c.hasSib1) {
        std::string pl; for (const auto& p : c.plmns) pl += p + " ";
        ImGui::Text("SIB1: %s(%s) TAC %u, cell id %u (eNB %u, sector %d)%s", pl.c_str(), dect2::lteCountry(c.mcc), c.tac, c.cellId, c.enb, c.sector, c.barred ? ", barred" : "");
    }
    if (c.hasSib2) ImGui::Text("SIB2: reference signal %d dBm, p-b %d, UL %s", c.refSignalPowerDbm, c.pb, c.ulEarfcn >= 0 ? std::to_string(c.ulEarfcn).c_str() : "default spacing");
    if (c.hasSib3) ImGui::Text("SIB3: priority %d, hysteresis %d dB", c.prio, c.qHystDb);
}

void sitesView(App& a) {
    const dect2::CellTelemetry& t = a.rx.cell;
    std::map<std::string, std::vector<const dect2::CellTower*>> sites;
    for (const auto& c : t.cells) if (c.hasSib1) { char k[64]; snprintf(k, sizeof k, "%s eNB %u", c.plmns.empty() ? "?" : c.plmns[0].c_str(), c.enb); sites[k].push_back(&c); }
    if (sites.empty()) { ImGui::TextDisabled("no cell identified yet (SIB1)"); return; }
    if (!ImGui::BeginChild("##sites")) { ImGui::EndChild(); return; }
    for (const auto& s : sites) {
        const auto* f = s.second[0];
        char h[128]; snprintf(h, sizeof h, "%s %s, TAC %u: %zu cell%s", s.first.c_str(), f->op.c_str(), f->tac, s.second.size(), s.second.size() == 1 ? "" : "s");
        if (ImGui::TreeNodeEx(h, ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto* c : s.second) ImGui::BulletText("sector %d, PCI %d, B%d EARFCN %d, %.0f dBFS", c->sector, c->pci, c->band, c->earfcn, c->rsrpDb);
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
}

void neighView(App& a) {
    const dect2::CellTelemetry& t = a.rx.cell;
    if (!ImGui::BeginChild("##nb")) { ImGui::EndChild(); return; }
    bool any = false;
    for (const auto& c : t.cells) {
        if (!c.hasSib4 && !c.hasSib5) continue;
        any = true;
        char h[96]; snprintf(h, sizeof h, "PCI %d %s##n%d", c.pci, opText(c).c_str(), c.pci);
        if (!ImGui::TreeNode(h)) continue;
        if (c.hasSib4) { std::string s; for (const auto& n : c.intra) { s += std::to_string(n.pci) + " "; } ImGui::Text("same frequency: %s", s.c_str()); }
        for (const auto& q : c.inter) {
            std::string s; for (const auto& n : q.cells) s += std::to_string(n.pci) + " ";
            ImGui::Text("EARFCN %d (B%d, %.1f MHz), priority %d %s", q.earfcn, dect2::lteBandOfEarfcn(q.earfcn), dect2::lteEarfcnToHz(q.earfcn) / 1e6, q.priority, s.c_str());
        }
        ImGui::TreePop();
    }
    if (!any) ImGui::TextDisabled("no neighbour list read yet (SIB4 / SIB5)");
    ImGui::EndChild();
}

void opView(App& a) {
    const dect2::CellTelemetry& t = a.rx.cell;
    std::map<std::string, const dect2::CellTower*> best;
    for (const auto& c : t.cells) if (c.hasSib1 && !c.plmns.empty()) { auto& b = best[c.plmns[0]]; if (!b || c.rsrpDb > b->rsrpDb) b = &c; }
    if (best.empty()) { ImGui::TextDisabled("no operator identified yet"); return; }
    for (const auto& b : best)
        ImGui::BulletText("%s %s: PCI %d, B%d EARFCN %d, eNB %u / %d, RSRP %.0f dBFS, RSRQ %.1f dB", b.first.c_str(), b.second->op.c_str(), b.second->pci, b.second->band, b.second->earfcn,
                          b.second->enb, b.second->sector, b.second->rsrpDb, b.second->rsrqDb);
}

void mapView(App& a) {
    if (ImGui::SmallButton("Load tower file")) { const std::string p = openFileDialog(); if (!p.empty()) loadTowers(p); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A CSV of public tower positions you downloaded yourself, OpenCelliD format (opencellid.org, CC BY-SA 4.0):\nradio,mcc,net,area,cell,unit,lon,lat,...  Cells are matched by MCC, MNC and the 28-bit cell id.");
    ImGui::SameLine(); ImGui::TextDisabled("%s", S.towerNote.empty() ? "no file loaded" : S.towerNote.c_str());
    const ImVec2 sz = ImGui::GetContentRegionAvail();
    if (sz.y < 40) return;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    adsbmap::draw(S.map, sz);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), true);
    for (const auto& c : a.rx.cell.cells)
        if (const Pos* p = towerOf(c)) {
            const ImVec2 q = adsbmap::project(p->lat, p->lon);
            dl->AddCircleFilled(q, 5 * gUi, ImGui::GetColorU32(pal::accent()));
            char b[48]; snprintf(b, sizeof b, "%u/%d", c.enb, c.sector);
            dl->AddText(ImVec2(q.x + 7 * gUi, q.y - 7 * gUi), IM_COL32(230, 230, 230, 255), b);
        }
    dl->PopClipRect();
}

void logView(App& a) {
    for (const auto& e : a.rx.cell.events) ImGui::TextUnformatted(e.text.c_str());
    if (a.rx.cell.events.empty()) ImGui::TextDisabled("nothing yet");
}

void tick(App& a) {
    if (!S.scanning) return;
    if (!a.engine.running()) { S.scanning = false; return; }
    const double now = ImGui::GetTime();
    if (S.stepAt == 0 || now - S.stepAt >= S.dwell) {
        if (S.stepAt != 0) S.step++;
        if (S.step >= S.plan.size()) { S.scanning = false; return; }
        tuneFreq(a, S.plan[S.step]);
        S.stepAt = now;
    }
}

void tab(App& a) {
    scanBar(a);
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Receive only: the public information every LTE cell broadcasts to all phones (operator, band, cell identity). Nothing addressed to a phone is decoded.");
    ImGui::PopTextWrapPos();
    subNav("cellv", S.view, {"Cells", "Sites", "Neighbours", "Operators", "Map", "Log"});
    if (S.view == 4) { mapView(a); return; }
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "start the receiver to find the cells around you"); return; }
    if (S.view == 0) {
        const float h = ImGui::GetContentRegionAvail().y;
        if (ImGui::BeginChild("##ct", ImVec2(0, h * 0.7f))) cellTable(a);
        ImGui::EndChild();
        detail(a);
    } else if (S.view == 1) sitesView(a);
    else if (S.view == 2) neighView(a);
    else if (S.view == 3) opView(a);
    else logView(a);
}

void list(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    for (const auto& c : a.rx.cell.cells) ImGui::Text("%-14s PCI %3d %.0f", opText(c).c_str(), c.pci, c.rsrpDb);
    if (a.rx.cell.cells.empty()) ImGui::TextDisabled("none yet");
}

void receiver(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const dect2::CellTelemetry& t = a.rx.cell;
    ImGui::Text("%s", dect2::cellSummary(t).c_str());
    ImGui::Text("%llu blocks searched, %llu SI messages, %llu failed", (unsigned long long)t.blocks, (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad);
    ImGui::Text("input %.2f Msps, %.1f dBFS", t.inputRate / 1e6, t.levelDb);
    for (const auto& c : t.carriers) ImGui::BulletText("carrier %.2f MHz, %.1f MHz wide, %.0f dB", c.freqHz / 1e6, c.widthHz / 1e6, c.levelDb);
}

void status(App& a) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("State"); ImGui::SameLine();
    ImGui::TextUnformatted(live(a) ? dect2::cellSummary(a.rx.cell).c_str() : (a.engine.running() ? "starting" : "stopped"));
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "Cell towers";
    if (live(a)) l2 = dect2::cellSummary(a.rx.cell);
}

} // namespace

extern const ModeUi kCellUi;
const ModeUi kCellUi = {
    .sideTitle = "CELLS",
    .tabName = "Cells",
    .tabIcon = Ic::Radio,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .tick = tick,
};
