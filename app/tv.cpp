// the TV side: player, service list, programme guide, teletext
#include "app.h"
#include "dect2/ac4_dec.h"

#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include "dect2/png.h"

namespace {
// ---- channel logos: a PNG the user picked for a service, copied to <data>/tv-logos/<service>.png and shown on the service card
struct LogoTex { gfx::Image* img = nullptr; int w = 0, h = 0; };
std::map<std::string, LogoTex> gTvLogos;   // service name -> texture (img null: no logo)

std::string tvLogoPath(const std::string& service) {
    std::string name = service;
    for (char& c : name) if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_' || (unsigned char)c >= 0x80)) c = '_';
    return plat::dataDir() + "/tv-logos/" + name + ".png";
}

const LogoTex* tvLogo(const std::string& service) {
    if (service.empty() || !gGfx) return nullptr;
    auto it = gTvLogos.find(service);
    if (it == gTvLogos.end()) {
        LogoTex t;
        std::ifstream f(std::filesystem::u8path(tvLogoPath(service)), std::ios::binary);
        if (f) {
            std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            std::vector<uint8_t> rgba;
            int w = 0, h = 0;
            if (dect2::decodePng(b.data(), b.size(), w, h, rgba) && w > 0 && h > 0 && rgba.size() >= (size_t)w * (size_t)h * 4) {
                t.img = gGfx->createImage(w, h, 0x00000000u);
                t.img->update(0, 0, w, h, reinterpret_cast<const uint32_t*>(rgba.data()));
                t.w = w; t.h = h;
            }
        }
        it = gTvLogos.emplace(service, t).first;
    }
    return it->second.img ? &it->second : nullptr;
}

void tvLogoSet(const std::string& service, const std::string& src) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(plat::dataDir() + "/tv-logos"), ec);
    std::filesystem::copy_file(std::filesystem::u8path(src), std::filesystem::u8path(tvLogoPath(service)), std::filesystem::copy_options::overwrite_existing, ec);
    auto it = gTvLogos.find(service);
    if (it != gTvLogos.end()) { delete it->second.img; gTvLogos.erase(it); }
}

void tvLogoClear(const std::string& service) {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(tvLogoPath(service)), ec);
    auto it = gTvLogos.find(service);
    if (it != gTvLogos.end()) { delete it->second.img; gTvLogos.erase(it); }
}

// <recordings folder>/<service>_<date>.ts
std::string tvRecordingPath(const std::string& service) {
    std::string name = service.empty() ? "service" : service;
    for (char& c : name) if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_' || (unsigned char)c >= 0x80)) c = '_';
    const std::time_t now = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof ts, "%Y%m%d_%H%M%S", std::localtime(&now));
    std::error_code ec;
    const std::string dir = plat::dataDir() + "/recordings";
    std::filesystem::create_directories(dir, ec);
    return dir + "/" + name + "_" + ts + ".ts";
}

std::string clockText(double s) { char b[24]; const int t = (int)s; if (t >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60); else snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60); return b; }

void tvRecordStart(App& a) {
    Player& pl = a.engine.player();
    if (pl.selected() < 0) return;
    std::string nm = "service";
    for (const auto& sv : a.ts.services) if (sv.id == pl.selected() && !sv.name.empty()) nm = sv.name;
    int64_t at = 0;
    if (a.tvRecAtOn) {   // the next time the clock shows HH:MM
        std::time_t now = std::time(nullptr); std::tm tmv = *std::localtime(&now);
        tmv.tm_hour = a.tvRecAtH; tmv.tm_min = a.tvRecAtM; tmv.tm_sec = 0; tmv.tm_isdst = -1;
        std::time_t t = std::mktime(&tmv);
        if (t <= now) t += 24 * 3600;
        at = (int64_t)t;
    }
    const std::string path = tvRecordingPath(nm);
    if (pl.startRecording(path, a.tvRecMin > 0 ? a.tvRecMin * 60.0 : 0.0, at)) { a.tvRecDonePath.clear(); a.tvRecErr.clear(); }
    else a.tvRecErr = "cannot record: the file cannot be written (" + path + ")";
}

// Record, timer, pause / live buffer
void tvRecordRow(App& a) {
    Player& pl = a.engine.player();
    const Player::RecStats rs = pl.recording();
    if (rs.active) a.tvRecWasActive = true;
    else if (a.tvRecWasActive) {
        a.tvRecWasActive = false; a.tvRecDonePath = rs.path;
        char b[96]; snprintf(b, sizeof b, "recorded %s (%.1f MB) to ", clockText(rs.seconds).c_str(), rs.bytes / 1e6);
        a.engine.log(std::string(b) + rs.path);
        if (rs.failed) a.engine.log("recording stopped: disk error");
        if (rs.droppedPackets) a.engine.log("the disk was too slow: " + std::to_string(rs.droppedPackets) + " packets are missing from the recording");
    }
    if (rs.active) {
        char b[96]; snprintf(b, sizeof b, "Stop  %s  %.1f MB###tvrec", clockText(rs.seconds).c_str(), rs.bytes / 1e6);
        ImGui::PushStyleColor(ImGuiCol_Text, pal::badRed());
        if (ImGui::Button(b)) pl.stopRecording();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stops the recording (R)");
        if (rs.droppedPackets) { flowNext(4 * gUi); ImGui::TextColored(pal::warnAmber(), "dropped %llu", (unsigned long long)rs.droppedPackets); if (ImGui::IsItemHovered()) ImGui::SetTooltip("The disk was too slow: some packets are missing from the recording."); }
    } else {
        ImGui::BeginDisabled(pl.selected() < 0);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float sp = ImGui::CalcTextSize(" ").x;
        if (ImGui::Button("    Record##tvrec")) tvRecordStart(a);
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p0.x + ImGui::GetStyle().FramePadding.x + 2.2f * sp, p0.y + ImGui::GetItemRectSize().y * 0.5f), 3.2f * gUi, pl.selected() >= 0 ? ImGui::GetColorU32(pal::badRed()) : IM_COL32(120, 80, 78, 255));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", pl.selected() < 0 ? "Play a service first" : "Saves this service (its video, audio and subtitles only) as a .ts file in the recordings folder (R)");
    }
    flowNext(6 * gUi);
    ImGui::BeginDisabled(rs.active);
    ImGui::SetNextItemWidth(70 * gUi);
    if (ImGui::InputInt("min##tvrecmin", &a.tvRecMin, 0, 0)) { a.tvRecMin = std::max(0, std::min(24 * 60, a.tvRecMin)); plat::prefs().setI("tvRecMin", a.tvRecMin); }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Stop the recording after this many minutes (0 = no limit)");
    flowNext(6 * gUi);
    if (ImGui::Checkbox("at##tvrecat", &a.tvRecAtOn)) plat::prefs().setB("tvRecAtOn", a.tvRecAtOn);
    ImGui::SameLine(0, 4 * gUi);
    ImGui::SetNextItemWidth(44 * gUi);
    if (ImGui::InputInt("##tvrech", &a.tvRecAtH, 0, 0)) { a.tvRecAtH = std::max(0, std::min(23, a.tvRecAtH)); plat::prefs().setI("tvRecAtH", a.tvRecAtH); }
    ImGui::SameLine(0, 2 * gUi); ImGui::TextUnformatted(":"); ImGui::SameLine(0, 2 * gUi);
    ImGui::SetNextItemWidth(44 * gUi);
    if (ImGui::InputInt("##tvrecm", &a.tvRecAtM, 0, 0)) { a.tvRecAtM = std::max(0, std::min(59, a.tvRecAtM)); plat::prefs().setI("tvRecAtM", a.tvRecAtM); }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Stop the recording at this clock time (the next time it shows it)");
    if (!a.tvRecDonePath.empty() && !rs.active) {
        flowNext(6 * gUi);
        if (ImGui::SmallButton("Show recording")) dect2::openUrl(a.tvRecDonePath.substr(0, a.tvRecDonePath.find_last_of("/\\")));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a.tvRecDonePath.c_str());
    }
    flowBreak();
    // pause / live buffer
    const Player::ShiftStats ss = pl.shift();
    ImGui::BeginDisabled(pl.selected() < 0);
    if (ImGui::Button(ss.paused ? "Play##tvps" : "Pause##tvps")) { if (ss.paused) pl.play(); else pl.pause(); }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Freeze the picture; the service keeps filling the buffer. Play resumes from here (Space)");
    flowNext(6 * gUi);
    static float drag = 0; static bool dragging = false;
    const float maxS = (float)std::max(1.0, ss.bufferedSec);
    float v = dragging ? drag : (float)std::max(0.0, ss.bufferedSec - ss.behindSec);
    ImGui::SetNextItemWidth(std::max(120.f * gUi, std::min(360.f * gUi, ImGui::GetContentRegionAvail().x - 260 * gUi)));
    char lab[48]; snprintf(lab, sizeof lab, ss.behind ? "-%s behind" : "live", clockText(dragging ? maxS - v : ss.behindSec).c_str());
    if (ImGui::SliderFloat("##tvseek", &v, 0, maxS, lab)) { drag = v; dragging = true; }
    if (ImGui::IsItemDeactivated()) { if (dragging) pl.seekBehind(v >= maxS - 0.5f ? 0 : maxS - v); dragging = false; }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("How far behind live the picture is. Drag to go back (up to %s) or forward.", clockText(ss.bufferedSec).c_str());
    flowNext(6 * gUi);
    ImGui::BeginDisabled(!ss.behind);
    if (ImGui::Button("Jump to live##tvlive")) pl.goLive();
    ImGui::EndDisabled();
    flowNext(6 * gUi);
    ImGui::SetNextItemWidth(84 * gUi);
    char cur[24]; snprintf(cur, sizeof cur, "%d MB", pl.bufferMB());
    if (ImGui::BeginCombo("buffer##tvbuf", cur)) {
        for (int mb : {64, 128, 256, 512, 1024, 2048}) { char l[24]; snprintf(l, sizeof l, "%d MB", mb); if (ImGui::Selectable(l, mb == pl.bufferMB())) { pl.setBufferMB(mb); a.tvBufMb = mb; plat::prefs().setI("tvBufMb", mb); } }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Memory for the live buffer (filled %.0f MB). When it is full the oldest part is dropped.", ss.bufferBytes / 1e6);
    ImGui::EndDisabled();
    if (!a.tvRecErr.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextColored(pal::warnAmber(), "%s", a.tvRecErr.c_str()); ImGui::PopTextWrapPos(); }
}
} // namespace

void teletextTab(App& a) {
    TeletextDecoder& tx = a.engine.teletext();
    const Player& pl = a.engine.player();
    if (tx.pid() < 0) {
        { ImGui::PushTextWrapPos(0); ImGui::TextDisabled(pl.selected() < 0 ? "Play a service (Player tab) - teletext is read from the playing service." : "The playing service has no teletext stream."); ImGui::PopTextWrapPos(); }
        return;
    }
    ImGui::SetNextItemWidth(80 * gUi);
    ImGui::InputInt("page", &a.ttxPage, 0, 0);
    a.ttxPage = std::max(100, std::min(899, a.ttxPage));
    ImGui::SameLine(); if (ImGui::SmallButton("<")) a.ttxPage = std::max(100, a.ttxPage - 1);
    ImGui::SameLine(); if (ImGui::SmallButton(">")) a.ttxPage = std::min(899, a.ttxPage + 1);
    ImGui::SameLine(); if (ImGui::SmallButton("100")) a.ttxPage = 100;
    ImGui::SameLine();
    {
        auto pages = tx.pages();
        { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%zu pages received, %llu packets", pages.size(), (unsigned long long)tx.packets()); ImGui::PopTextWrapPos(); }
        if (!pages.empty()) {
            ImGui::SameLine();
            std::string l = "available: ";
            int n = 0;
            for (int p : pages) { if (n++ >= 14) { l += "..."; break; } l += std::to_string(p) + " "; }
            ImGui::TextDisabled("%s", l.c_str());
        }
    }
    TtxPage pg;
    if (!tx.page(a.ttxPage, pg)) { { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("page %d has not been received yet (pages repeat every few seconds)", a.ttxPage); ImGui::PopTextWrapPos(); } return; }
    TtxGrid grid;
    ttxRender(pg, grid);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // Teletext cell aspect ~7×9 (ETS 300 706); 0.55 was too squat and glyphs bled between rows.
    constexpr float kTtxAspect = 7.f / 9.f;
    const float cw = std::max(6.f, std::min(avail.x / 40.f, avail.y / 25.f * kTtxAspect)), ch = cw / kTtxAspect;
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, ImVec2(o.x + cw * 40, o.y + ch * 25), IM_COL32(0, 0, 0, 255));
    const bool flashOn = std::fmod(ImGui::GetTime(), 1.0) < 0.6;
    for (int r = 0; r < 25; r++)
        for (int c = 0; c < 40; c++) {
            const TtxCell& cell = grid[r][c];
            const ImVec2 p0(o.x + c * cw, o.y + r * ch), p1(p0.x + cw, p0.y + ch);
            if (cell.bg) dl->AddRectFilled(p0, p1, ttxColour(cell.bg));
            if (cell.flash && !flashOn) continue;
            if (cell.mosaic) {
                const float gx = cell.separated ? 1.f : 0.f;
                for (int k = 0; k < 6; k++) {
                    if (!(cell.sextants & (1 << k))) continue;
                    const int cx = k & 1, cy = k >> 1;
                    dl->AddRectFilled(ImVec2(p0.x + cx * cw / 2 + gx, p0.y + cy * ch / 3 + gx), ImVec2(p0.x + (cx + 1) * cw / 2 - gx, p0.y + (cy + 1) * ch / 3 - gx), ttxColour(cell.fg));
                }
            } else if (cell.ch > ' ') {
                char buf[5] = {0};
                uint32_t u = cell.ch;
                if (u < 0x80) buf[0] = (char)u;
                else if (u < 0x800) { buf[0] = (char)(0xC0 | (u >> 6)); buf[1] = (char)(0x80 | (u & 0x3F)); }
                else { buf[0] = (char)(0xE0 | (u >> 12)); buf[1] = (char)(0x80 | ((u >> 6) & 0x3F)); buf[2] = (char)(0x80 | (u & 0x3F)); }
                ImFont* f = a.mono;
                const float fs = cell.dh ? ch * 2.0f * 0.8f : ch * 0.8f;
                if (cell.dh == 1) { dl->PushClipRect(p0, p1, true); dl->AddText(f, fs, ImVec2(p0.x, p0.y), ttxColour(cell.fg), buf); dl->PopClipRect(); }
                else if (cell.dh == 2) { dl->PushClipRect(p0, p1, true); dl->AddText(f, fs, ImVec2(p0.x, p0.y - ch), ttxColour(cell.fg), buf); dl->PopClipRect(); }
                else dl->AddText(f, fs, ImVec2(p0.x, p0.y), ttxColour(cell.fg), buf);
            }
        }
    char title[64];
    snprintf(title, sizeof title, "P%d%s", pg.number, pg.subtitle ? "  (subtitle page)" : "");
    dl->AddText(ImVec2(o.x + 2, o.y + 1), IM_COL32(255, 255, 255, 255), title);
    ImGui::Dummy(ImVec2(cw * 40, ch * 25));
}

const EpgEvent* epgCurrent(const App& a, int sid, const EpgEvent** next) {
    auto it = a.epg.find(sid);
    if (it == a.epg.end()) return nullptr;
    const int64_t now = utcNowOf(a);
    const EpgEvent* cur = nullptr;
    if (next) *next = nullptr;
    for (size_t i = 0; i < it->second.size(); i++) {
        const EpgEvent& e = it->second[i];
        if (e.start <= now && now < e.end()) { cur = &e; if (next && i + 1 < it->second.size()) *next = &it->second[i + 1]; break; }
        if (e.start > now) { if (next && !*next) *next = &e; break; }
    }
    return cur;
}

void guideTab(App& a) {
    const int64_t now = utcNowOf(a);
    if (a.ts.services.empty()) { { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("waiting for the service list..."); ImGui::PopTextWrapPos(); } return; }
    size_t total = 0;
    for (auto& kv : a.epg) total += kv.second.size();
    if (total == 0) {
        ImGui::TextWrapped("No programme information received yet. Broadcasters send it in the EIT tables, which repeat every few seconds (now/next) to a few minutes (schedule). "
                           "Some multiplexes send none at all.");
        { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("clock: %s%s", fmtLocal(now, "%a %d %b %H:%M").c_str(), a.ts.utcNow ? " (from the broadcast time signal)" : " (this computer's clock)"); ImGui::PopTextWrapPos(); }
        return;
    }
    const int playSid = a.engine.player().selected();
    if (playSid >= 0 && playSid != a.guidePlaySid) {
        a.guidePlaySid = playSid;
        if (a.epg.count(playSid)) { a.guideSid = playSid; a.guideEvent = -1; }
    } else if (a.guideSid < 0 || !a.epg.count(a.guideSid)) {
        if (playSid >= 0 && a.epg.count(playSid)) a.guideSid = playSid;
        else if (!a.epg.empty()) a.guideSid = a.epg.begin()->first;
    }
    ImGui::BeginChild("gsv", ImVec2(260 * gUi, 0), ImGuiChildFlags_Borders);
    for (auto& sv : a.ts.services) {
        auto it = a.epg.find(sv.id);
        if (it == a.epg.end()) continue;
        const EpgEvent* nx = nullptr;
        const EpgEvent* cur = epgCurrent(a, sv.id, &nx);
        ImGui::PushID(sv.id);
        if (ImGui::Selectable(sv.name.empty() ? std::to_string(sv.id).c_str() : sv.name.c_str(), a.guideSid == sv.id)) { a.guideSid = sv.id; a.guideEvent = -1; }
        ImGui::TextDisabled("  %s", cur ? (fmtLocal(cur->start, "%H:%M") + " " + cur->title).c_str() : "-");
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("gev", ImVec2(0, 0));
    const auto& evs = a.epg[a.guideSid];
    { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%zu events   local time %s", evs.size(), fmtLocal(now, "%a %d %b %H:%M").c_str()); ImGui::PopTextWrapPos(); }
    const float detailH = 150;
    ImGui::BeginChild("evlist", ImVec2(0, -detailH), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("evt", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("time", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("length", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("programme");
        std::string lastDay;
        for (size_t i = 0; i < evs.size(); i++) {
            const EpgEvent& e = evs[i];
            if (e.end() < now - 1800) continue; // hide what ended more than half an hour ago
            const bool cur = e.start <= now && now < e.end();
            const std::string day = fmtLocal(e.start, "%a %d %b");
            if (day != lastDay) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextColored(pal::heading(), "%s", day.c_str()); lastDay = day; }
            ImGui::TableNextRow();
            if (cur) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 110, 60, 110));
            ImGui::TableNextColumn();
            char lbl[64]; snprintf(lbl, sizeof lbl, "%s - %s##%d", fmtLocal(e.start, "%H:%M").c_str(), fmtLocal(e.end(), "%H:%M").c_str(), e.eventId);
            if (ImGui::Selectable(lbl, a.guideEvent == e.eventId, ImGuiSelectableFlags_SpanAllColumns)) a.guideEvent = e.eventId;
            ImGui::TableNextColumn(); ImGui::Text("%d min", (e.duration + 30) / 60);
            ImGui::TableNextColumn();
            if (cur) { const float f = (float)(now - e.start) / (float)e.duration; ImGui::Text("%s", e.title.c_str()); ImGui::SameLine(); ImGui::TextDisabled("  now, %.0f%%", f * 100); }
            else ImGui::TextUnformatted(e.title.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::BeginChild("evdet", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const EpgEvent* sel = nullptr;
    for (auto& e : evs) if (e.eventId == a.guideEvent) sel = &e;
    if (!sel) sel = epgCurrent(a, a.guideSid);
    if (sel) {
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.4f, 1), "%s", sel->title.c_str());
        { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s - %s  (%d min)%s%s", fmtLocal(sel->start, "%a %H:%M").c_str(), fmtLocal(sel->end(), "%H:%M").c_str(), (sel->duration + 30) / 60, genreName(sel->genre)[0] ? "   " : "", genreName(sel->genre)); ImGui::PopTextWrapPos(); }
        if (!sel->text.empty()) ImGui::TextWrapped("%s", sel->text.c_str());
        if (!sel->extended.empty()) ImGui::TextWrapped("%s", sel->extended.c_str());
    } else { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("select a programme"); ImGui::PopTextWrapPos(); }
    ImGui::EndChild();
    ImGui::EndChild();
}

void playerTab(App& a) {
    Player& pl = a.engine.player();
    PlayerStats ps = pl.stats();
    {   // channel selector (muxes found by the scanner) and the signal-quality bar
        const SavedChannel* cur = nullptr;
        for (auto& c : a.channels) if (std::fabs(c.freqMhz - a.freqMhz) < 0.01) cur = &c;
        char fb[48]; snprintf(fb, sizeof fb, "%.3f MHz", a.freqMhz);
        const std::string curLab = cur ? channelLabel(*cur) : std::string(fb);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("channel");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::min(360 * gUi, ImGui::GetContentRegionAvail().x - 60 * gUi));   // room for the + and x buttons
        if (ImGui::BeginCombo("##chan", ((cur && cur->favourite) ? "* " + curLab : curLab).c_str())) {
            if (a.channels.empty()) ImGui::TextDisabled("no channels yet - run a scan (Scan tab)");
            for (size_t i = 0; i < a.channels.size(); i++) {
                const SavedChannel& c = a.channels[i];
                std::string l = (c.favourite ? "* " : "") + channelLabel(c);
                char extra[64]; snprintf(extra, sizeof extra, "   SNR %.0f dB", c.snrDb);
                if (ImGui::Selectable((l + extra).c_str(), cur == &c)) tuneToChannel(a, c);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.mode.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+##fav")) { ImGui::OpenPopup("savefav"); if (!a.favName[0] && cur) snprintf(a.favName, sizeof a.favName, "%s", cur->name.c_str()); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save the current frequency as a favourite channel");
        if (ImGui::BeginPopup("savefav")) {
            ImGui::TextDisabled("name for %.3f MHz", a.freqMhz);
            ImGui::SetNextItemWidth(220 * gUi);
            ImGui::InputText("##favn", a.favName, sizeof a.favName);
            if (ImGui::Button("Save")) {
                SavedChannel sc; sc.freqMhz = a.freqMhz; sc.bwMhz = kBw[a.bwIdx].mhz; sc.name = a.favName; sc.favourite = true;
                if (cur) { sc.mode = cur->mode; sc.snrDb = cur->snrDb; sc.nServices = cur->nServices; }
                bool f = false;
                for (auto& c : a.channels) if (std::fabs(c.freqMhz - sc.freqMhz) < 0.01) { c = sc; f = true; }
                if (!f) a.channels.push_back(sc);
                std::sort(a.channels.begin(), a.channels.end(), [](const SavedChannel& x, const SavedChannel& y) { return x.freqMhz < y.freqMhz; });
                a.favName[0] = 0; savePrefs(a); ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (cur) {
            ImGui::SameLine();
            if (ImGui::SmallButton("x##del")) { const double f = cur->freqMhz; a.channels.erase(std::remove_if(a.channels.begin(), a.channels.end(), [&](const SavedChannel& c) { return std::fabs(c.freqMhz - f) < 0.01; }), a.channels.end()); savePrefs(a); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forget this channel");
        }
        const float qMin = ImGui::CalcTextSize("signal quality 100%  excellent").x + 16 * gUi;
        sameLineIf(qMin, 18 * gUi);   // on a line of its own when the tab is narrow
        qualityBar(a, std::max(qMin, ImGui::GetContentRegionAvail().x - 8));
    }
    {   // PLP selector for multi-PLP multiplexes, and a notice for anything the receiver cannot decode
        const RxTelemetry& rx = a.rx;
        if (rx.plpList.size() > 1) {
            static const char* mods[] = {"QPSK", "16-QAM", "64-QAM", "256-QAM"};
            static const char* rates[] = {"1/2", "3/5", "2/3", "3/4", "4/5", "5/6", "1/3", "2/5"};
            auto lab = [&](const RxTelemetry::PlpInfo& p) {
                char b[120];
                snprintf(b, sizeof b, "PLP %d  type %d  %s %s%s%s", p.id, p.type, p.mod >= 0 && p.mod < 4 ? mods[p.mod] : "?", p.cod >= 0 && p.cod < 8 ? rates[p.cod] : "?", p.type == 0 ? "  (common)" : "", p.supported ? "" : "  (not supported)");
                return std::string(b);
            };
            std::string cur = a.plpSel < 0 ? "automatic" : "PLP " + std::to_string(a.plpSel);
            if (a.plpSel < 0 && rx.plpSelectedId >= 0) cur += " (PLP " + std::to_string(rx.plpSelectedId) + ")";
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("PLP");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(260 * gUi);
            if (ImGui::BeginCombo("##plp", cur.c_str())) {
                if (ImGui::Selectable("automatic (first data PLP)", a.plpSel < 0)) { a.plpSel = -1; a.engine.selectPlp(-1); }
                for (auto& p : rx.plpList) if (ImGui::Selectable(lab(p).c_str(), a.plpSel == p.id)) { a.plpSel = p.id; a.engine.selectPlp(p.id); }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("This multiplex carries several physical layer pipes. The receiver decodes one at a time.");
        }
        for (auto& u : rx.unsupported) { ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.2f, 1), "! %s", u.c_str()); }
    }
    // the controls below the picture take what they took in the last frame (they wrap onto more lines in a narrow tab)
    ImGuiStorage* stor = ImGui::GetStateStorage();
    const ImGuiID ctlKey = ImGui::GetID("##tvctlh");
    const float h = std::max(60.f * gUi, ImGui::GetContentRegionAvail().y - stor->GetFloat(ctlKey, 44.f * gUi));
    ImGui::BeginChild("vbox", ImVec2(-1, h), ImGuiChildFlags_Borders);
    if (pl.selected() < 0) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("Pick a service in the list on the right to start playback."); ImGui::PopTextWrapPos(); }
    else if (!ps.videoNote.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", ps.videoNote.c_str()); ImGui::PopTextWrapPos(); }
    else if (!a.video.has()) ImGui::TextDisabled("%s", ps.hasAudio && !ps.hasVideo ? "audio only" : ps.status.c_str());
    else a.video.draw(ImGui::GetContentRegionAvail());
    const bool vboxKeys = (ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && !ImGui::GetIO().WantTextInput;   // Space and R act on the video panel
    ImGui::EndChild();
    if (vboxKeys && pl.selected() >= 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) { if (pl.shift().paused) pl.play(); else pl.pause(); }
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { if (pl.recording().active) pl.stopRecording(); else tvRecordStart(a); }
    }
    const float ctlY = ImGui::GetCursorPosY();
    if (ImGui::Button(pl.selected() >= 0 ? "Stop" : "Play")) { if (pl.selected() >= 0) pl.select(-1); else if (a.playReq < 0 && !a.ts.services.empty()) { int sid = a.ts.services[0].id; pl.select(sid); pl.setVolume(a.volume); } }
    flowNext();
    ImGui::SetNextItemWidth(160 * gUi);
    if (ImGui::SliderFloat("volume", &a.volume, 0, 1, "%.2f")) pl.setVolume(a.volume);
    flowNext();
    if (ImGui::Checkbox("mute", &a.muted)) pl.setMuted(a.muted);
    flowNext();
    ImGui::Checkbox("pop out", &a.popOut);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show the video in a window of its own (double-click it for full screen, Esc to leave)");
    flowNext();
    if (ImGui::Checkbox("on top", &a.popTop)) savePrefs(a);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep the pop-out video window above other windows");
    flowNext();
    if (ImGui::Button("Fullscreen")) { a.videoOnly = true; if (!glfwGetWindowMonitor(gWindow)) toggleFullscreen(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Video fills the screen (press F or Esc to leave)");
    flowNext();
    ImGui::Checkbox("deinterlace", &a.video.deint);
    flowNext();
    auto tracks = pl.audioTracks();
    if (!tracks.empty()) {
        static int cur = 0;
        cur = std::min(cur, (int)tracks.size() - 1);
        std::string lab = tracks[cur].codec + " " + tracks[cur].lang;
        ImGui::SetNextItemWidth(170 * gUi);
        if (ImGui::BeginCombo("audio", lab.c_str())) {
            for (int i = 0; i < (int)tracks.size(); i++) {
                std::string l = tracks[i].codec + " " + tracks[i].lang + " (" + std::to_string(tracks[i].channels) + " ch)";
                if (ImGui::Selectable(l.c_str(), i == cur)) { cur = i; pl.setAudioTrack(i); }
            }
            ImGui::EndCombo();
        }
        flowNext();
    }
    if (pl.subtitlesAvailable()) { if (ImGui::Checkbox("subtitles", &a.subsOn)) pl.setSubtitles(a.subsOn); }
    flowBreak();
    tvRecordRow(a);
    flowBreak();
    if (pl.selected() >= 0) {   // what is on now, and what comes next
        const EpgEvent* nx = nullptr;
        const EpgEvent* cur = epgCurrent(a, pl.selected(), &nx);
        if (cur) {
            const int64_t now = utcNowOf(a);
            const float f = std::min(1.f, std::max(0.f, (float)(now - cur->start) / (float)cur->duration));
            ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.4f, 1), "NOW");
            ImGui::SameLine();
            ImGui::Text("%s - %s  %s", fmtLocal(cur->start, "%H:%M").c_str(), fmtLocal(cur->end(), "%H:%M").c_str(), cur->title.c_str());
            char left[32]; snprintf(left, sizeof left, "%d min left", (int)std::max<int64_t>(0, (cur->end() - now + 30) / 60));
            sameLineIf(160 * gUi);
            ImGui::ProgressBar(f, ImVec2(160 * gUi, 12 * gUi), left);
            if (nx) { ImGui::TextDisabled("NEXT"); ImGui::SameLine(); ImGui::Text("%s  %s", fmtLocal(nx->start, "%H:%M").c_str(), nx->title.c_str()); }
        } else if (nx) {
            ImGui::TextDisabled("NEXT"); ImGui::SameLine(); ImGui::Text("%s  %s", fmtLocal(nx->start, "%H:%M").c_str(), nx->title.c_str());
        } else { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("no programme information is broadcast for this service"); ImGui::PopTextWrapPos(); }
    }
    stor->SetFloat(ctlKey, ImGui::GetCursorPosY() - ctlY);
}

// Multi-channel TV: the channels of the span, one receiver each (click: this channel plays and shows in detail, the others keep decoding)
static void multiChannelTable(App& a) {
    const std::vector<TvChannelRow>& rows = a.rx.tvChannels;
    sectionHeader(Ic::Radio, "Channels in the span");
    if (rows.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("looking for channels in the capture..."); ImGui::PopTextWrapPos(); return; }
    if (!ImGui::BeginTable("##mtvchans", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) return;
    ImGui::TableSetupColumn(tr("Channel MHz")); ImGui::TableSetupColumn(tr("State")); ImGui::TableSetupColumn(tr("Services")); ImGui::TableSetupColumn(tr("SNR dB"));
    ImGui::TableSetupColumn(tr("Load")); ImGui::TableSetupColumn(tr("Network"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (const TvChannelRow& r : rows) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        char b[48]; snprintf(b, sizeof b, "%.3f##tc%d", radioCenterMhz(a) + r.offsetHz / 1e6, r.slot);
        if (!r.occupied && !r.running) { ImGui::BeginDisabled(); ImGui::Selectable(b, false, ImGuiSelectableFlags_SpanAllColumns); ImGui::EndDisabled(); }
        else if (ImGui::Selectable(b, r.selected, ImGuiSelectableFlags_SpanAllColumns) && r.running) a.engine.selectTvChannel(r.slot);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(!r.occupied && !r.running ? tr("empty") : !r.running ? tr("waiting") : r.locked ? tr("locked") : r.state > 0 ? tr("partial") : tr("searching"));
        ImGui::TableNextColumn(); if (r.running) ImGui::Text("%d", r.services); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn(); if (r.running && r.locked) ImGui::Text("%.1f", r.snrDb); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn(); if (r.running) ImGui::Text("%.0f %%", r.cpuPct); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn(); ImGui::TextUnformatted(r.network.c_str());
    }
    ImGui::EndTable();
    ImGui::Spacing();
}

void rightPanel(App& a) {
    if (txViewFlag()) { txRight(a); return; }
    if (const ModeUi* mu = modeUi(a.family)) if (mu->list) {
        if (!a.engine.running() && a.rx.standard > 0) ImGui::TextDisabled("stopped - showing saved results");
        mu->list(a); return;
    }
    const bool run = a.engine.running();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // ---- clock: broadcast time (TDT) when the stream has one, otherwise this computer's clock
    {
        const int64_t now = utcNowOf(a);
        time_t t = (time_t)now; struct tm m; dect2::gmTime(t, &m);
        char hm[16], ss[8], date[40];
        strftime(hm, sizeof hm, "%H:%M", &m); strftime(ss, sizeof ss, ":%S", &m); strftime(date, sizeof date, "%a %d %b %Y", &m);
        if (pal::dev()) {   // one plain line: 14:41:34 UTC
            ImGui::Text("%s%s", hm, ss);
            ImGui::SameLine(0, 6 * gUi);
            ImGui::TextDisabled("UTC");
        } else {
        ImGui::PushFont(a.ui, 40.f);
        ImGui::TextUnformatted(hm);
        ImGui::PopFont();
        ImGui::SameLine(0, 4 * gUi);
        ImGui::BeginGroup();
        ImGui::TextDisabled("UTC");
        ImGui::TextDisabled("%s", ss);
        ImGui::EndGroup();
        }
        ImGui::TextDisabled("%s", date);
        {   // the parts that do not fit go on the next line (a narrow list)
            const std::string loc = "local " + fmtLocal(now, "%H:%M"), src = a.ts.utcNow ? "(broadcast time)" : "(computer clock)";
            sameLineIf(ImGui::CalcTextSize(loc.c_str()).x, 10 * gUi);
            ImGui::TextDisabled("%s", loc.c_str());
            sameLineIf(ImGui::CalcTextSize(src.c_str()).x, 12 * gUi);
            ImGui::TextDisabled("%s", src.c_str());
        }
    }
    ImGui::Spacing();
    if (a.dabMode) { dabStations(a); return; }
    if (a.fmMode) { fmRadioPanel(a); return; }
    if (a.engine.multiChannelActive()) multiChannelTable(a);
    sectionHeader(Ic::Tv, "Services");
    // ---- service cards
    const float cardH = 68 * gUi;
    ImGui::BeginChild("svcs", ImVec2(0, std::max(140.f * gUi, ImGui::GetContentRegionAvail().y * 0.40f)));
    dl = ImGui::GetWindowDrawList(); // draw into the child so the cards are clipped and scroll with it
    // multi-channel TV: the services of every channel of the span, each with its channel; else the one multiplex's
    const bool multi = a.engine.multiChannelActive() && !a.mtvSvc.empty();
    struct SvRow { const TsService* sv; int slot; double offHz; };
    std::vector<SvRow> svRows;
    if (multi) for (const MultiService& m : a.mtvSvc) svRows.push_back({&m.svc, m.slot, m.offsetHz});
    else for (const TsService& s : a.ts.services) svRows.push_back({&s, 0, 0});
    const int playSlot = multi ? a.engine.selectedTvChannel() : 0;
    if (svRows.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled(run ? "waiting for PAT / SDT..." : "start the receiver to see services"); ImGui::PopTextWrapPos(); }
    for (const SvRow& rw : svRows) {
        const TsService& sv = *rw.sv;
        std::string vc, ac;
        double kb = 0;
        bool hasVideo = false;
        for (auto& st : sv.streams) { kb += st.kbps; if (st.kind == "video") { hasVideo = true; if (vc.empty()) vc = st.codec; } if (st.kind == "audio" && ac.empty()) ac = st.codec; }
        const bool sel = sv.id == a.engine.player().selected() && rw.slot == playSlot;
        ImGui::PushID(rw.slot * 100000 + sv.id);
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##card", ImVec2(w, cardH));
        const bool hov = ImGui::IsItemHovered();
        if (!sv.name.empty() && ImGui::BeginPopupContextItem("##tvlogo")) {
            if (ImGui::MenuItem("Set logo (PNG)...")) { const std::string f = plat::openFileDialog(); if (!f.empty()) tvLogoSet(sv.name, f); }
            if (ImGui::MenuItem("Remove logo", nullptr, false, tvLogo(sv.name) != nullptr)) tvLogoClear(sv.name);
            ImGui::EndPopup();
        }
        if (clicked) {
            if (sel) a.engine.player().select(-1);
            else {
                if (multi && rw.slot != playSlot) a.engine.selectTvChannel(rw.slot);   // another channel of the span plays from now on, the others keep decoding
                a.engine.player().select(sv.id); a.engine.player().setVolume(a.volume);
            }
        }
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + cardH - 4), sel ? IM_COL32(30, 36, 40, 255) : hov ? IM_COL32(26, 27, 29, 255) : IM_COL32(16, 17, 19, 255), pal::rnd(4.f));
        dl->AddRect(p, ImVec2(p.x + w, p.y + cardH - 4), sel ? pal::remap(IM_COL32(115, 184, 209, 255)) : IM_COL32(48, 50, 52, 255), 4.f, 0, sel ? 1.6f : 1.f);
        // name and bit rate
        const std::string name = sv.name.empty() ? "service " + std::to_string(sv.id) : sv.name;
        const bool isRadio = sv.type == 0x02 || sv.type == 0x0A || (!hasVideo && !ac.empty());
        const LogoTex* logo = tvLogo(sv.name);
        float nameX = 34 * gUi;
        if (logo) {   // the logo the user gave this service, fitted into a box at the left
            const float bh = 24 * gUi, bw = 44 * gUi, sc = std::min(bh / (float)logo->h, bw / (float)logo->w);
            const float iw = (float)logo->w * sc, ih = (float)logo->h * sc;
            dl->AddImage(logo->img->texture(), ImVec2(p.x + 8 * gUi, p.y + 4 * gUi + (bh - ih) / 2), ImVec2(p.x + 8 * gUi + iw, p.y + 4 * gUi + (bh - ih) / 2 + ih));
            nameX = 8 * gUi + bw + 6 * gUi;
        } else if (!pal::dev()) icons::draw(isRadio ? Ic::Radio : Ic::Tv, ImVec2(p.x + 20 * gUi, p.y + 15 * gUi), 17.f * gUi, sel ? pal::remap(IM_COL32(120, 200, 255, 255)) : IM_COL32(120, 136, 156, 255), dl);
        char kbs[32]; fmtKbps(kbs, sizeof kbs, kb);
        const ImVec2 ks = ImGui::CalcTextSize(kbs);
        dl->AddText(a.ui, 16.f * gUi, ImVec2(p.x + nameX, p.y + 6 * gUi), IM_COL32(245, 247, 250, 255), ellipsize(name, w - nameX - ks.x - 18 * gUi, 16.f * gUi).c_str());
        dl->AddText(ImVec2(p.x + w - ks.x - 10 * gUi, p.y + 8 * gUi), IM_COL32(150, 158, 168, 255), kbs);
        // tag pills
        float x = p.x + 10 * gUi;
        const float ty = p.y + 27 * gUi;
        const bool radio = sv.type == 0x02 || sv.type == 0x0A || (!hasVideo && !ac.empty());
        x += tagAt(dl, ImVec2(x, ty), radio ? "radio" : hasVideo ? "TV" : sv.typeName(), IM_COL32(34, 74, 108, 255)) + 4;
        if (!vc.empty()) x += tagAt(dl, ImVec2(x, ty), vc.c_str(), IM_COL32(38, 46, 58, 255), IM_COL32(190, 200, 214, 255)) + 4;
        if (!ac.empty()) x += tagAt(dl, ImVec2(x, ty), ac.c_str(), IM_COL32(38, 46, 58, 255), IM_COL32(190, 200, 214, 255)) + 4;
        if (sv.scrambled) x += tagAt(dl, ImVec2(x, ty), "scrambled", IM_COL32(112, 52, 50, 255)) + 4;
        if (multi) { char cb[40]; snprintf(cb, sizeof cb, "channel %.3f MHz", radioCenterMhz(a) + rw.offHz / 1e6); x += tagAt(dl, ImVec2(x, ty), cb, IM_COL32(58, 74, 44, 255)) + 4; }
        // what is on
        const EpgEvent* cur = epgCurrent(a, sv.id);
        std::string line;
        if (cur) line = fmtLocal(cur->start, "%H:%M") + "-" + fmtLocal(cur->end(), "%H:%M") + "  " + cur->title;
        else if (!sv.now.empty()) line = sv.now;
        else if (!sv.provider.empty()) line = sv.provider;
        dl->PushClipRect(ImVec2(p.x + 8 * gUi, p.y), ImVec2(p.x + w - 8 * gUi, p.y + cardH), true);
        dl->AddText(ImVec2(p.x + 10 * gUi, p.y + 47 * gUi), IM_COL32(128, 136, 146, 255), ellipsize(line, w - 18 * gUi).c_str());
        dl->PopClipRect();
        ImGui::PopID();
    }
    ImGui::EndChild();

    // ---- what the player is doing, as label / value rows
    {
        const Player& pl = a.engine.player();
        const PlayerStats ps = pl.stats();
        sectionHeader(Ic::Play, "Player");
        if (pl.selected() < 0) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("click a service to play it"); ImGui::PopTextWrapPos(); }
        else if (ImGui::BeginTable("pkv", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 88);
            auto row = [&](Ic ic, const char* k, const char* fmt, auto... args) {
                ImGui::TableNextRow(); ImGui::TableNextColumn(); iconInline(ic, iconDim(), 0.9f); ImGui::SameLine(0, 5 * gUi); ImGui::TextDisabled("%s", k); ImGui::TableNextColumn();
                char b[120]; snprintf(b, sizeof b, fmt, args...); ImGui::TextUnformatted(b);
            };
            if (ps.hasVideo) row(Ic::Tv, "Video", "%s  %dx%d  %.1f fps  %s", ps.videoCodec.c_str(), ps.width, ps.height, ps.fps, ps.hardware ? "(hardware)" : "(software)");
            if (ps.hasAudio) row(Ic::Speaker, "Audio", "%s  %d ch", ps.audioCodec.c_str(), ps.audioChannels);
            row(Ic::Clock, "Buffer", "%.0f ms     A/V %+.0f ms", ps.audioBufferMs, ps.avOffsetMs);
            row(Ic::Camera, "Pictures", "%llu shown / %llu late", (unsigned long long)ps.shown, (unsigned long long)ps.late);
            row(Ic::Warning, "Errors", "%llu     underruns %d", (unsigned long long)ps.errors, ps.underruns);
            if (ps.repairEvents) row(Ic::Camera, "Repaired", "%llu damaged spans, %llu pictures replaced", (unsigned long long)ps.repairEvents, (unsigned long long)ps.repairedFrames);
            if (ps.concealEvents) row(Ic::Pulse, "Smoothed", "%llu gaps, %llu pictures generated (%s)", (unsigned long long)ps.concealEvents, (unsigned long long)ps.concealedFrames, ps.concealBackend.c_str());
            ImGui::TableNextRow(); ImGui::TableNextColumn(); iconInline(Ic::Pulse, iconDim(), 0.9f); ImGui::SameLine(0, 5 * gUi); ImGui::TextDisabled("Gaps"); ImGui::TableNextColumn();
            { bool on = a.engine.player().conceal(); if (ImGui::Checkbox("smooth picture gaps", &on)) a.engine.player().setConceal(on);
              if (ImGui::IsItemHovered()) ImGui::SetTooltip("When a fade swallows part of the stream or damages the pictures after it, generate the missing pictures\n(Apple ML interpolation) instead of freezing or showing smears. It hides the loss; it cannot bring the data back.\nThe sound is not changed: a dropout stays a clean silence."); }
#if defined(__APPLE__) || defined(_WIN32)
            ImGui::TableNextRow(); ImGui::TableNextColumn(); iconInline(Ic::Chip, iconDim(), 0.9f); ImGui::SameLine(0, 5 * gUi); ImGui::TextDisabled("Decode"); ImGui::TableNextColumn();
            { bool on = a.engine.player().hardwareDecode(); if (ImGui::Checkbox("hardware video decoding", &on)) { a.engine.player().setHardwareDecode(on); plat::prefs().setB("hwVideo", on); plat::prefs().flush(); }
              if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use the graphics chip to decode the video. It saves processor time for the receiver.\nSwitch it off if the picture shows green or garbled patches (a graphics driver problem)."); }
#endif
            ImGui::TableNextRow(); ImGui::TableNextColumn(); iconInline(a.muted ? Ic::Mute : Ic::Speaker, iconDim(), 0.9f); ImGui::SameLine(0, 5 * gUi); ImGui::TextDisabled("Volume"); ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            float vol = a.volume * 100.f;
            if (ImGui::SliderFloat("##vol", &vol, 0, 100, "%.0f %%")) { a.volume = vol / 100.f; a.engine.player().setVolume(a.volume); }
            ImGui::EndTable();
        }
        // a picture or sound OnAir's FFmpeg cannot decode (AVS+, AVS2, AVS3, DRA): why, and the codec note (docs/CODECS.md)
        if (pl.selected() >= 0) {
            if (!ps.videoNote.empty()) { codecHelpButton(CodecHelp::Avs); ImGui::SameLine(); ImGui::PushTextWrapPos(0); ImGui::TextColored(pal::warnAmber(), "%s", ps.videoNote.c_str()); ImGui::PopTextWrapPos(); }
            if (!ps.audioNote.empty()) { codecHelpButton(CodecHelp::DraTv); ImGui::SameLine(); ImGui::PushTextWrapPos(0); ImGui::TextColored(pal::warnAmber(), "%s", ps.audioNote.c_str()); ImGui::PopTextWrapPos(); }
            {   // ATSC 3.0 sound in AC-4 and no decoder library: say why there is no sound
                const dect2::Ac4Status a4 = dect2::ac4Status();
                if (a4.seen && a4.dropped && !a4.hasDecoder) { codecHelpButton(CodecHelp::Ac4); ImGui::SameLine(); ImGui::PushTextWrapPos(0); ImGui::TextColored(pal::warnAmber(), "This service's sound is AC-4 (Dolby). OnAir needs an optional FFmpeg with an AC-4 decoder to play it."); ImGui::PopTextWrapPos(); }
            }
            if (pl.hasSavableAudio()) {   // the raw DRA (else MPEG audio) stream to a file FFmpeg can play
                static std::string savedTo, saveErr;
                Player& mp = a.engine.player();
                if (mp.savingAudioStream()) {
                    if (ImGui::Button("Stop saving")) mp.stopSaveAudioStream();
                    ImGui::SameLine(); ImGui::TextDisabled("%.1f kB written", mp.savedAudioBytes() / 1024.0);
                } else {
                    if (ImGui::Button("Save stream")) {
                        std::string nm = "service";
                        for (const auto& sv : a.ts.services) if (sv.id == pl.selected() && !sv.name.empty()) nm = sv.name;
                        savedTo = mp.saveAudioStream(streamSavePath(nm));
                        saveErr = savedTo.empty() ? "cannot save: no DRA or MPEG audio stream, or the file cannot be written" : "";
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Write the raw audio stream of this service (DRA, else MPEG audio) to a file you can play with FFmpeg (see the README).");
                }
                if (!saveErr.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextColored(pal::warnAmber(), "%s", saveErr.c_str()); ImGui::PopTextWrapPos(); }
                else if (!savedTo.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", savedTo.c_str()); ImGui::PopTextWrapPos(); }
            }
        }
    }
    ImGui::Spacing();
    // ---- video preview and outputs
    {
        const float vw = ImGui::GetContentRegionAvail().x;
        const float vh = std::min(vw * 9.f / 16.f, std::max(60.f, ImGui::GetContentRegionAvail().y - 52.f));
        ImGui::BeginChild("video", ImVec2(0, vh), ImGuiChildFlags_Borders);
        const PlayerStats ps = a.engine.player().stats();
        if (a.engine.player().selected() < 0) ImGui::TextDisabled("no picture");
        else if (!ps.videoNote.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", ps.videoNote.c_str()); ImGui::PopTextWrapPos(); }
        else if (!ps.hasVideo && ps.hasAudio) ImGui::TextDisabled("audio only");
        else if (!a.video.has()) ImGui::TextDisabled("%s", ps.status.c_str());
        else a.video.draw(ImGui::GetContentRegionAvail());
        ImGui::EndChild();
    }
    {
        OutputStats os = a.engine.outputStats();
        iconLabel(Ic::Link, "Outputs", iconAccent());
        ImGui::SameLine(0, 10 * gUi);
        iconInline(Ic::File, os.fileOpen ? IM_COL32(120, 190, 235, 255) : IM_COL32(86, 94, 104, 255), 0.9f); ImGui::SameLine(0, 3 * gUi);
        ImGui::TextColored(os.fileOpen ? (pal::dev() ? pal::accent() : ImVec4(0.55f, 0.76f, 0.92f, 1)) : ImVec4(0.45f, 0.48f, 0.52f, 1), "file");
        ImGui::SameLine(0, 10 * gUi);
        iconInline(Ic::Globe, os.udpOpen ? IM_COL32(120, 190, 235, 255) : IM_COL32(86, 94, 104, 255), 0.9f); ImGui::SameLine(0, 3 * gUi);
        ImGui::TextColored(os.udpOpen ? (pal::dev() ? pal::accent() : ImVec4(0.55f, 0.76f, 0.92f, 1)) : ImVec4(0.45f, 0.48f, 0.52f, 1), "udp");
        if (os.fileOpen) { ImGui::SameLine(); ImGui::TextDisabled("%.1f MB", os.fileBytes / 1e6); }
        if (os.udpOpen) { ImGui::SameLine(); ImGui::TextDisabled("%s:%d%s", a.udpHost, a.out.port, a.out.rtp ? " RTP" : ""); }
    }
}

void tvTab(App& a) {
    subNav("tv", a.subTv, {"Player", "Guide", "Teletext"});
    if (a.subTv == 0) playerTab(a);
    else if (a.subTv == 1) guideTab(a);
    else teletextTab(a);
}

// channel estimate, impulse response and per-carrier SNR on one screen
void channelDashboard(App& a) {
    const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("chA", ImVec2(w * 0.5f - 4, h));
    channelTab(a);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("chB", ImVec2(0, h));
    ImGui::BeginChild("chB1", ImVec2(0, h * 0.5f - 2));
    impulseTab(a);
    ImGui::EndChild();
    ImGui::BeginChild("chB2", ImVec2(0, 0));
    snrTab(a);
    ImGui::EndChild();
    ImGui::EndChild();
}

