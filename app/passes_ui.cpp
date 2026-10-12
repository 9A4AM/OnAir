// The satellite pass planner (View > Passes): the next 24 hours of passes of the satellites OnAir's modes receive (weather satellites
// on 137 MHz, the ISS, Orbcomm, a short editable list of amateur satellites) over the receiver's position, a polar sky plot of the
// current or chosen pass, and optional tracking: Doppler-corrected retuning about once a second, the right mode started at AOS and
// an IQ recording (the same as Record IQ) until LOS. TLEs are downloaded from Celestrak or Keplerpass on request, or pasted.
#include "app.h"
#include "dect2/passes.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
fs::path fsPath(const std::string& p) { return fs::path(reinterpret_cast<const char8_t*>(p.c_str())); }

constexpr int kSpecx = 41, kAprs = 24, kOrbcomm = 44, kMeteor = 59, kCubesat = 61;   // mode families: Spectrum explorer (no decoder), APRS (AFSK), Orbcomm, Meteor-M LRPT, CubeSat telemetry

struct Source { const char* file; const char* url; };
const Source kSources[] = {
    {"weather.txt", "https://celestrak.org/NORAD/elements/gp.php?GROUP=weather&FORMAT=tle"},
    {"stations.txt", "https://celestrak.org/NORAD/elements/gp.php?GROUP=stations&FORMAT=tle"},
    {"orbcomm.txt", "https://celestrak.org/NORAD/elements/gp.php?GROUP=orbcomm&FORMAT=tle"},
    {"amateur.txt", "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle"},
};
const char* const kPasted = "pasted.txt";
const Source kKeplerpass = {"keplerpass.txt", "https://data.keplerpass.com/PU4ELT/tle.txt"};

enum Group { Weather, Iss, Orbcomm, Amateur, NumGroups };
const char* const kGroupLabel[NumGroups] = {"Weather (137 MHz)", "ISS", "Orbcomm", "Amateur"};

struct Known { const char* name; int norad; double mhz; const char* mode; int family; int group; };
const Known kKnown[] = {
    {"NOAA 15", 25338, 137.620, "APT", kSpecx, Weather},
    {"NOAA 18", 28654, 137.9125, "APT", kSpecx, Weather},
    {"NOAA 19", 33591, 137.100, "APT", kSpecx, Weather},
    {"Meteor-M N2-3", 57166, 137.100, "LRPT", kMeteor, Weather},
    {"Meteor-M N2-4", 59051, 137.900, "LRPT", kMeteor, Weather},
    {"ISS APRS", 25544, 145.825, "APRS", kAprs, Iss},
    {"ISS voice", 25544, 145.800, "FM voice", kSpecx, Iss},
};

struct Cube { std::string name; int norad = 0; double mhz = 0; bool aprs = false; };

struct Sat {
    std::string name, mode;
    int norad = 0, family = kSpecx, group = Weather;
    double mhz = 0;
    dect2::Sgp4 sgp;
};
struct Row { int sat; dect2::Pass p; };

struct State {
    bool loaded = false;
    std::map<int, dect2::Tle> tles;     // by NORAD number; pasted ones win
    std::vector<Sat> sats;
    std::vector<std::string> missing;   // listed satellites without a TLE
    std::vector<Row> rows;
    bool dirty = true;
    double computedAt = 0, lat = 1e9, lon = 1e9;
    float minEl = 10;
    bool show[NumGroups] = {true, true, true, true};
    std::vector<Cube> cubes;
    // the chosen pass (a row): its satellite is the one tracked
    int selSat = -1; double selAos = 0;
    std::string selName;                // selSat again after the list is rebuilt
    bool track = false, autoStart = false, autoRecord = false;
    // tracking
    bool inPass = false, weStarted = false, weRecord = false, recTried = false;
    std::string passName;               // the satellite of the pass under way
    double passLos = 0, lastTune = 0, nominalMhz = 0;
    // TLE data
    bool showPaste = false;
    bool showSources = false;
    char newSourceUrl[512] = "";
    std::vector<std::pair<std::string,std::string>> customSources;
    std::vector<char> paste = std::vector<char>(32768, 0);
    std::string msg;
    char cubeName[40] = ""; int cubeNorad = 0; double cubeMhz = 145.9; bool cubeAprs = false;
};
State S;

// the download runs on its own thread: these outlive it
std::atomic<int> gDl{0};   // 0 idle, 1 working, 2 finished
std::mutex gDlMu;
std::string gDlMsg;

std::string tleDir() { return plat::dataDir() + "/tle"; }

double nowSec() { return (double)std::time(nullptr); }

std::string readText(const std::string& path) {
    std::ifstream f(fsPath(path), std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

void saveCubes() {
    std::string s;
    for (const Cube& c : S.cubes) { char b[160]; snprintf(b, sizeof b, "%s|%d|%.4f|%d;", c.name.c_str(), c.norad, c.mhz, c.aprs ? 1 : 0); s += b; }
    plat::prefs().setS("passCubes", s);
}

void loadCubes() {
    const std::string def = "AO-73 (FUNcube-1)|39417|145.935|0;SO-50|27607|436.795|0;RS-44|44909|435.640|0;";
    const std::string s = plat::prefs().getS("passCubes", def);
    S.cubes.clear();
    std::stringstream ss(s);
    for (std::string item; std::getline(ss, item, ';');) {
        std::stringstream is(item);
        std::string n, no, mhz, ap;
        if (!std::getline(is, n, '|') || !std::getline(is, no, '|') || !std::getline(is, mhz, '|')) continue;
        std::getline(is, ap, '|');
        Cube c; c.name = n; c.norad = atoi(no.c_str()); c.mhz = atof(mhz.c_str()); c.aprs = ap == "1";
        if (c.norad > 0 && c.mhz > 0) S.cubes.push_back(c);
    }
}

void buildSats() {
    S.sats.clear(); S.missing.clear();
    auto add = [&](const std::string& name, int norad, double mhz, const std::string& mode, int fam, int grp) {
        auto it = S.tles.find(norad);
        if (it == S.tles.end()) { S.missing.push_back(name); return; }
        Sat s; s.name = name; s.norad = norad; s.mhz = mhz; s.mode = mode; s.family = fam; s.group = grp;
        if (!s.sgp.init(it->second)) { S.missing.push_back(name + " (orbit not usable)"); return; }
        S.sats.push_back(std::move(s));
    };
    for (const Known& k : kKnown) add(k.name, k.norad, k.mhz, k.mode, k.family, k.group);
    const ModeTuning* ot = modeTuning(46);
    for (const auto& [no, t] : S.tles)
        if (t.name.rfind("ORBCOMM", 0) == 0) add(t.name, no, ot ? ot->defMhz : 137.25, "Orbcomm", kOrbcomm, Orbcomm);
    for (const Cube& c : S.cubes) add(c.name, c.norad, c.mhz, c.aprs ? "APRS" : "CubeSat telemetry", c.aprs ? kAprs : kCubesat, Amateur);
    S.selSat = -1;
    for (int i = 0; i < (int)S.sats.size(); i++) if (!S.selName.empty() && S.sats[i].name == S.selName) S.selSat = i;
    S.rows.clear();
    S.dirty = true;
}

// Custom source URLs are saved separately from the downloaded TLE files.
// Use generated local filenames, never remote URL fragments as paths.
void loadCustomSources() {
    S.customSources.clear();
    std::istringstream in(readText(tleDir() + "/custom_sources.txt"));
    std::string name, url;
    while (std::getline(in, name, '\t') && std::getline(in, url)) {
        if (name.rfind("user_", 0) == 0 && name.size() < 64 &&
            name.find('/') == std::string::npos && name.find('\\\\') == std::string::npos &&
            url.rfind("https://", 0) == 0)
            S.customSources.emplace_back(name, url);
    }
}
void saveCustomSources() {
    std::error_code ec;
    fs::create_directories(fsPath(tleDir()), ec);
    std::ofstream out(fsPath(tleDir() + "/custom_sources.txt"), std::ios::trunc);
    for (const auto& item : S.customSources) out << item.first << '\t' << item.second << '\n';
}

void loadTles() {
    S.tles.clear();
    for (const Source& s : kSources) for (const dect2::Tle& t : dect2::parseTleText(readText(tleDir() + "/" + s.file))) S.tles[t.norad] = t;
    for (const dect2::Tle& t : dect2::parseTleText(readText(tleDir() + "/" + kKeplerpass.file))) S.tles[t.norad] = t;
    for (const auto& src : S.customSources) for (const dect2::Tle& t : dect2::parseTleText(readText(tleDir() + "/" + src.first))) S.tles[t.norad] = t;
    for (const dect2::Tle& t : dect2::parseTleText(readText(tleDir() + "/" + kPasted))) S.tles[t.norad] = t;
    buildSats();
}

void loadState() {
    if (S.loaded) return;
    S.loaded = true;
    plat::Prefs& p = plat::prefs();
    S.minEl = (float)p.getD("passMinEl", 10);
    for (int g = 0; g < NumGroups; g++) { char k[24]; snprintf(k, sizeof k, "passShow%d", g); S.show[g] = p.getB(k, true); }
    loadCubes();
    loadCustomSources();
    loadTles();
}

void startDownload(bool keplerpass = false, std::string customFile = {}, std::string customUrl = {}) {
    if (gDl.load() == 1) return;
    gDl = 1;
    { std::lock_guard<std::mutex> lk(gDlMu); gDlMsg = "downloading..."; }
    const std::string dir = tleDir();
    std::thread([dir, keplerpass, customFile, customUrl] {
        std::error_code ec;
        fs::create_directories(fsPath(dir), ec);
        int ok = 0, n = 0;
        const bool custom = !customUrl.empty();
        const size_t sourceCount = (keplerpass || custom) ? 1 : sizeof kSources / sizeof kSources[0];
        for (size_t i = 0; i < sourceCount; ++i) {
            const Source& source = keplerpass ? kKeplerpass : kSources[i];
            const std::string filename = custom ? customFile : source.file;
            const std::string url = custom ? customUrl : source.url;
            const std::string path = dir + "/" + filename, part = path + ".part";
            if (plat::fetchUrl(url, part, "OnAir/" ONAIR_VERSION " (passes)")) {
                const size_t got = dect2::parseTleText(readText(part)).size();
                if (got > 0) { fs::rename(fsPath(part), fsPath(path), ec); if (!ec) { ok++; n += (int)got; } }
            }
            fs::remove(fsPath(part), ec);
        }
        std::string m = !ok ? (custom ? "Custom TLE download failed (network or invalid TLE)" : keplerpass ? "Keplerpass download failed (check network or TLE format)" : "the download failed (no network, or Celestrak refused)")
                            : std::string(custom ? "Custom source: downloaded " : keplerpass ? "Keplerpass: downloaded " : "Celestrak: downloaded ") + std::to_string(n) + " TLEs";
        if (ok && ok < (int)sourceCount) m += "; some lists failed";
        { std::lock_guard<std::mutex> lk(gDlMu); gDlMsg = m; }
        gDl = 2;
    }).detach();
}

void compute(App& a) {
    double lat, lon;
    receiverLocation(lat, lon);
    const double now = nowSec();
    if (!S.dirty && lat == S.lat && lon == S.lon && now - S.computedAt < 600) return;
    S.dirty = false; S.lat = lat; S.lon = lon; S.computedAt = now;
    dect2::Observer o; o.latDeg = lat; o.lonDeg = lon;
    S.rows.clear();
    for (int i = 0; i < (int)S.sats.size(); i++)
        for (const dect2::Pass& p : dect2::findPasses(S.sats[i].sgp, o, now - 1800, now + 86400, S.minEl))
            if (p.los > now) S.rows.push_back({i, p});
    std::sort(S.rows.begin(), S.rows.end(), [](const Row& x, const Row& y) { return x.p.aos < y.p.aos; });
    (void)a;
}

std::string timeText(double t) {
    const std::time_t tt = (std::time_t)t, nw = std::time(nullptr);
    std::tm lt = *std::localtime(&tt);
    const std::tm today = *std::localtime(&nw);
    char b[32];
    if (lt.tm_yday != today.tm_yday || lt.tm_year != today.tm_year) std::strftime(b, sizeof b, "%a %H:%M:%S", &lt);
    else std::strftime(b, sizeof b, "%H:%M:%S", &lt);
    return b;
}

bool lookNow(const Sat& s, double t, dect2::Look& l) {
    dect2::Observer o; o.latDeg = S.lat; o.lonDeg = S.lon;
    return dect2::lookAt(s.sgp, o, t, l);
}

const Row* activeOrNext(int sat) {   // the pass under way, else the next one (of one satellite, or of any shown)
    const double now = nowSec();
    for (const Row& r : S.rows) {
        if (r.p.los < now) continue;
        if (sat >= 0 ? r.sat == sat : S.show[S.sats[r.sat].group]) return &r;
    }
    return nullptr;
}

void retuneTo(App& a, double mhz) {
    a.freqMhz = mhz;
    if (a.engine.running()) { a.tune.centerHz = a.freqMhz * 1e6; a.engine.retune(a.tune); }
}

void endPass(App& a, bool los) {
    S.inPass = false;
    a.engine.log("pass of " + S.passName + (los ? " ends (LOS)" : ": tracking stopped"));
    if (S.weRecord) { a.engine.stopRecording(); S.weRecord = false; }
    if (S.weStarted && a.engine.running()) a.engine.stop();
    S.weStarted = false;
    a.engine.cubesat().setTracked(0, "");
    retuneTo(a, S.nominalMhz);
}

// once a frame, open or not: follow the tracked satellite's pass
void trackTick(App& a) {
    const bool have = S.selSat >= 0 && S.selSat < (int)S.sats.size();
    const double now = nowSec();
    if (S.inPass && (!have || !S.track || S.sats[S.selSat].name != S.passName)) { endPass(a, false); return; }
    if (S.inPass && now > S.passLos) { endPass(a, true); return; }
    if (!have || !S.track) return;
    const Sat& sat = S.sats[S.selSat];
    const Row* r = activeOrNext(S.selSat);
    const bool active = r && r->p.aos <= now && now <= r->p.los;
    if (active && !S.inPass) {   // AOS
        S.inPass = true; S.passLos = r->p.los; S.passName = sat.name; S.weStarted = false; S.weRecord = false; S.recTried = false; S.nominalMhz = sat.mhz; S.lastTune = 0;
        a.engine.log("pass of " + sat.name + " begins (AOS)");
        if (sat.family == kCubesat) a.engine.cubesat().setTracked(sat.norad, sat.name);   // names the frames of the pass
        if (S.autoStart && !a.engine.running()) {
            if (a.family != sat.family) selectMode(a, sat.family);
            a.freqMhz = sat.mhz;
            startReceiver(a);
            S.weStarted = a.engine.running();
        } else if (S.autoStart && a.family != sat.family) a.engine.log("the receiver runs another mode: the pass is tracked, the mode not changed");
    }
    if (!S.inPass) return;
    if (S.autoRecord && !S.recTried && a.engine.running() && !a.engine.recordingStats().active && a.devices[a.devIdx].isRadio()) {
        S.recTried = true;
        const FileFormat f = a.recFormat == 1 ? FileFormat::CF32 : FileFormat::CS8;
        const std::string path = plat::dataDir() + "/recordings/" + a.engine.recordingName(f);
        std::string err;
        if (a.engine.startRecording(path, f, err)) { S.weRecord = true; a.recDonePath.clear(); }
        else a.engine.log("recording failed: " + err);
    }
    if (now - S.lastTune >= 1.0) {   // Doppler: about once a second
        S.lastTune = now;
        dect2::Look l;
        if (lookNow(sat, now, l)) retuneTo(a, (S.nominalMhz * 1e6 + dect2::dopplerHz(S.nominalMhz * 1e6, l.rangeRateKmS)) / 1e6);
    }
}

void skyPlot(const Row* r, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    const float R = size * 0.5f - ImGui::GetFontSize() * 1.1f;
    const ImVec2 c(p0.x + size * 0.5f, p0.y + size * 0.5f);
    const ImU32 grid = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.5f), text = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    for (int k = 1; k <= 3; k++) dl->AddCircle(c, R * k / 3.f, grid, 64, 1.f);
    dl->AddLine(ImVec2(c.x - R, c.y), ImVec2(c.x + R, c.y), grid);
    dl->AddLine(ImVec2(c.x, c.y - R), ImVec2(c.x, c.y + R), grid);
    const char* nesw[4] = {"N", "E", "S", "W"};
    for (int k = 0; k < 4; k++) {
        const float a = k * 1.5707963f, d = R + ImGui::GetFontSize() * 0.55f;
        const ImVec2 ts = ImGui::CalcTextSize(nesw[k]);
        dl->AddText(ImVec2(c.x + d * std::sin(a) - ts.x * 0.5f, c.y - d * std::cos(a) - ts.y * 0.5f), text, nesw[k]);
    }
    dl->AddText(ImVec2(c.x + 2, c.y - R / 3.f * 2 - ImGui::GetFontSize()), text, "60");
    dl->AddText(ImVec2(c.x + 2, c.y - R / 3.f - ImGui::GetFontSize()), text, "30");
    if (!r) return;
    const Sat& s = S.sats[r->sat];
    auto at = [&](const dect2::Look& l) {
        const float rr = R * (float)(90 - std::max(0.0, l.elDeg)) / 90.f, az = (float)(l.azDeg * 3.14159265 / 180);
        return ImVec2(c.x + rr * std::sin(az), c.y - rr * std::cos(az));
    };
    std::vector<ImVec2> pts;
    const double step = std::max(5.0, (r->p.los - r->p.aos) / 120);
    for (double t = r->p.aos; t <= r->p.los + 0.1; t += step) { dect2::Look l; if (lookNow(s, std::min(t, r->p.los), l)) pts.push_back(at(l)); }
    const ImU32 acc = ImGui::GetColorU32(pal::accent());
    if (pts.size() > 1) dl->AddPolyline(pts.data(), (int)pts.size(), acc, 0, 2.f);
    if (!pts.empty()) {
        dl->AddCircleFilled(pts.front(), 3.5f, ImGui::GetColorU32(pal::okGreen()));
        dl->AddCircleFilled(pts.back(), 3.5f, ImGui::GetColorU32(pal::badRed()));
    }
    const double now = nowSec();
    if (now >= r->p.aos && now <= r->p.los) {
        dect2::Look l;
        if (lookNow(s, now, l)) { const ImVec2 q = at(l); dl->AddCircleFilled(q, 5.f, ImGui::GetColorU32(pal::warnAmber())); dl->AddCircle(q, 8.f, ImGui::GetColorU32(pal::warnAmber()), 24, 1.5f); }
    }
}

void tleSection(App& a) {
    if (gDl.load() == 2) { gDl = 0; std::lock_guard<std::mutex> lk(gDlMu); S.msg = gDlMsg; loadTles(); }
    else if (gDl.load() == 1) { std::lock_guard<std::mutex> lk(gDlMu); S.msg = gDlMsg; }
    if (!ImGui::CollapsingHeader("TLE data", S.tles.empty() ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
    ImGui::BeginDisabled(gDl.load() == 1);
    if (ImGui::Button(gDl.load() == 1 ? "Downloading..." : "Download TLEs from Celestrak")) startDownload(false);
    ImGui::SameLine();
    if (ImGui::Button("Download TLEs from Keplerpass")) startDownload(true);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        std::string tip = "Downloads only when you click. Fetches:";
        for (const Source& s : kSources) tip += std::string("\n") + s.url;
        tip += "\nSaved in " + tleDir();
        ImGui::SetTooltip("%s", tip.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(S.showPaste ? "Hide paste box" : "Paste TLEs...")) S.showPaste = !S.showPaste;
    if (!S.msg.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", S.msg.c_str()); }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Sources: Celestrak and data.keplerpass.com/PU4ELT/tle.txt");
    if (ImGui::Button(S.showSources ? "Hide TLE servers" : "Manage TLE servers")) S.showSources = !S.showSources;
    if (S.showSources) {
        ImGui::TextDisabled("Built-in: Celestrak and Keplerpass (always available)");
        ImGui::SetNextItemWidth(-140.f);
        ImGui::InputTextWithHint("##addTleUrl", "https://example.com/satellites.tle", S.newSourceUrl, sizeof S.newSourceUrl);
        ImGui::SameLine();
        ImGui::BeginDisabled(gDl.load() == 1);
        if (ImGui::Button("Add server")) {
            const std::string url = S.newSourceUrl;
            const bool exists = std::any_of(S.customSources.begin(), S.customSources.end(),
                [&](const auto& src) { return src.second == url; });
            if (url.rfind("https://", 0) != 0 || url.find_first_of(" \t\r\n") != std::string::npos)
                S.msg = "Enter a valid HTTPS URL";
            else if (exists) S.msg = "Server already listed";
            else {
                unsigned id = 1;
                std::string file;
                do {
                    file = "user_" + std::to_string(id++) + ".tle";
                } while (std::any_of(S.customSources.begin(), S.customSources.end(),
                    [&](const auto& src) { return src.first == file; }) ||
                    fs::exists(fsPath(tleDir() + "/" + file)));
                S.customSources.emplace_back(file, url);
                saveCustomSources();
                S.newSourceUrl[0] = '\0';
                S.msg = "TLE server saved";
            }
        }
        for (size_t i = 0; i < S.customSources.size();) {
            ImGui::PushID((int)i);
            const auto item = S.customSources[i];
            ImGui::TextWrapped("%s", item.second.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Download")) startDownload(false, item.first, item.second);
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                std::error_code ec;
                fs::remove(fsPath(tleDir() + "/" + item.first), ec);
                S.customSources.erase(S.customSources.begin() + (std::ptrdiff_t)i);
                saveCustomSources();
                loadTles();
                S.msg = "TLE server deleted";
                ImGui::PopID();
                continue;
            }
            ImGui::PopID();
            ++i;
        }
        ImGui::EndDisabled();
    }

    ImGui::PopTextWrapPos();
    double oldest = 0;
    for (const Sat& s : S.sats) oldest = std::max(oldest, nowSec() / 86400.0 + 2440587.5 - s.sgp.tle().epochJd);
    if (S.tles.empty()) ImGui::TextColored(pal::warnAmber(), "No TLEs yet: download them or paste some.");
    else {
        ImGui::Text("%d TLEs, %d satellites listed", (int)S.tles.size(), (int)S.sats.size());
        if (oldest > 14) { ImGui::SameLine(); ImGui::TextColored(pal::warnAmber(), "the oldest is %.0f days old: download again", oldest); }
    }
    if (!S.missing.empty() && !S.tles.empty()) {
        std::string m = "No TLE for: ";
        for (size_t i = 0; i < S.missing.size(); i++) m += (i ? ", " : "") + S.missing[i];
        ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", m.c_str()); ImGui::PopTextWrapPos();
    }
    if (S.showPaste) {
        ImGui::InputTextMultiline("##tlepaste", S.paste.data(), S.paste.size(), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 8));
        if (ImGui::Button("Use pasted TLEs")) {
            const std::string txt(S.paste.data());
            const std::vector<dect2::Tle> v = dect2::parseTleText(txt);
            if (v.empty()) S.msg = "no valid TLE in the pasted text (two lines per satellite, checksums must match)";
            else {
                std::error_code ec;
                fs::create_directories(fsPath(tleDir()), ec);
                std::string old = readText(tleDir() + "/" + kPasted);
                std::ofstream f(fsPath(tleDir() + "/" + kPasted), std::ios::binary);
                f << old << (old.empty() || old.back() == '\n' ? "" : "\n") << txt << "\n";
                f.close();
                S.msg = "added " + std::to_string(v.size()) + " pasted TLEs";
                S.paste[0] = 0;
                loadTles();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Forget pasted TLEs")) { std::error_code ec; fs::remove(fsPath(tleDir() + "/" + kPasted), ec); loadTles(); S.msg = "pasted TLEs removed"; }
    }
    if (ImGui::TreeNode("Amateur satellites")) {
        int del = -1;
        for (int i = 0; i < (int)S.cubes.size(); i++) {
            ImGui::PushID(i);
            ImGui::Text("%s  #%d  %.3f MHz%s", S.cubes[i].name.c_str(), S.cubes[i].norad, S.cubes[i].mhz, S.cubes[i].aprs ? "  APRS" : "");
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) del = i;
            ImGui::PopID();
        }
        if (del >= 0) { S.cubes.erase(S.cubes.begin() + del); saveCubes(); savePrefs(a); buildSats(); }
        ImGui::SetNextItemWidth(130 * gUi); ImGui::InputTextWithHint("##cname", "name", S.cubeName, sizeof S.cubeName);
        ImGui::SameLine(); ImGui::SetNextItemWidth(80 * gUi); ImGui::InputInt("##cno", &S.cubeNorad, 0, 0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("NORAD catalogue number");
        ImGui::SameLine(); ImGui::SetNextItemWidth(90 * gUi); ImGui::InputDouble("##cmhz", &S.cubeMhz, 0, 0, "%.3f MHz");
        ImGui::SameLine(); ImGui::Checkbox("APRS", &S.cubeAprs);
        ImGui::SameLine();
        ImGui::BeginDisabled(!S.cubeName[0] || S.cubeNorad <= 0 || S.cubeMhz <= 0);
        if (ImGui::Button("Add")) {
            Cube c; c.name = S.cubeName; c.norad = S.cubeNorad; c.mhz = S.cubeMhz; c.aprs = S.cubeAprs;
            for (auto& ch : c.name) if (ch == '|' || ch == ';') ch = ' ';
            S.cubes.push_back(c); saveCubes(); savePrefs(a); buildSats();
            S.cubeName[0] = 0; S.cubeNorad = 0;
        }
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
}

void window(App& a) {
    ImGui::SetNextWindowSize(ImVec2(820 * gUi, 560 * gUi), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Passes", &a.passesOpen)) { ImGui::End(); return; }
    {   // where the receiver is
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Receiver position");
        ImGui::SameLine();
        receiverPositionFields(a);
        if (!receiverLocationSet()) { ImGui::SameLine(); ImGui::TextColored(pal::warnAmber(), "not set: the times are for a placeholder position"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The same position as the ADS-B antenna position");
    }
    tleSection(a);
    {   // filters
        bool ch = false;
        for (int g = 0; g < NumGroups; g++) {
            if (g) ImGui::SameLine();
            if (ImGui::Checkbox(kGroupLabel[g], &S.show[g])) { char k[24]; snprintf(k, sizeof k, "passShow%d", g); plat::prefs().setB(k, S.show[g]); ch = true; }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110 * gUi);
        if (ImGui::SliderFloat("##minel", &S.minEl, 0, 45, "min el %.0f deg")) { S.dirty = true; plat::prefs().setD("passMinEl", S.minEl); ch = true; }
        if (ch) savePrefs(a);
    }
    compute(a);
    const float plotW = 250 * gUi;
    const double now = nowSec();
    // the table of passes
    ImGui::BeginChild("##passlist", ImVec2(std::max(200.f, ImGui::GetContentRegionAvail().x - plotW - 8 * gUi), 0));
    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##passes", 7, tf)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Satellite", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("AOS");
        ImGui::TableSetupColumn("LOS");
        ImGui::TableSetupColumn("Max el");
        ImGui::TableSetupColumn("Duration");
        ImGui::TableSetupColumn("Frequency");
        ImGui::TableSetupColumn("Mode");
        ImGui::TableHeadersRow();
        int shown = 0;
        for (const Row& r : S.rows) {
            const Sat& s = S.sats[r.sat];
            if (!S.show[s.group] || r.p.los < now) continue;
            shown++;
            const bool active = r.p.aos <= now && now <= r.p.los;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID((int)(r.sat * 100003 + (int64_t)r.p.aos % 100000));
            const bool sel = S.selSat == r.sat && std::fabs(S.selAos - r.p.aos) < 1;
            if (active) ImGui::PushStyleColor(ImGuiCol_Text, pal::okGreen());
            if (ImGui::Selectable(s.name.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns)) { S.selSat = r.sat; S.selAos = r.p.aos; S.selName = s.name; }
            if (active) ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(active ? "now" : timeText(r.p.aos).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(timeText(r.p.los).c_str());
            ImGui::TableNextColumn(); ImGui::Text("%.0f deg", r.p.maxElDeg);
            ImGui::TableNextColumn(); { const int d = (int)std::lround(r.p.los - r.p.aos); ImGui::Text("%d:%02d", d / 60, d % 60); }
            ImGui::TableNextColumn(); ImGui::Text("%.3f", s.mhz);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(s.mode.c_str());
            if (ImGui::IsItemHovered() && s.family == kSpecx) ImGui::SetTooltip("OnAir has no decoder for this: auto start opens the Spectrum explorer, and Auto record keeps the IQ");
        }
        ImGui::EndTable();
        if (!shown) ImGui::TextDisabled(S.tles.empty() ? "No TLEs: open TLE data above." : "No passes in the next 24 hours.");
    }
    ImGui::EndChild();
    ImGui::SameLine();
    // the sky plot and tracking of the chosen pass (or the current / next one)
    ImGui::BeginChild("##passsky", ImVec2(0, 0));
    const Row* r = nullptr;
    if (S.selSat >= 0 && S.selSat < (int)S.sats.size()) {
        for (const Row& x : S.rows) if (x.sat == S.selSat && std::fabs(x.p.aos - S.selAos) < 1 && x.p.los >= now) r = &x;
        if (!r) r = activeOrNext(S.selSat);
    }
    if (!r) r = activeOrNext(-1);
    if (r) {
        const Sat& s = S.sats[r->sat];
        ImGui::TextUnformatted(s.name.c_str());
        ImGui::TextDisabled("%s - %s, max %.0f deg", timeText(r->p.aos).c_str(), timeText(r->p.los).c_str(), r->p.maxElDeg);
    } else ImGui::TextDisabled("no pass");
    skyPlot(r, std::min(ImGui::GetContentRegionAvail().x, plotW));
    if (r && now >= r->p.aos && now <= r->p.los) {
        dect2::Look l;
        if (lookNow(S.sats[r->sat], now, l))
            ImGui::Text("az %.0f  el %.0f  %.0f km\nDoppler %+.0f Hz", l.azDeg, l.elDeg, l.rangeKm, dect2::dopplerHz(S.sats[r->sat].mhz * 1e6, l.rangeRateKmS));
    } else if (r) {
        const int in = (int)std::max(0.0, r->p.aos - now);
        ImGui::TextDisabled("AOS in %d:%02d:%02d", in / 3600, in / 60 % 60, in % 60);
    }
    ImGui::Separator();
    const bool haveSel = S.selSat >= 0 && S.selSat < (int)S.sats.size();
    ImGui::BeginDisabled(!haveSel);
    std::string tl = haveSel ? "Track " + S.sats[S.selSat].name : std::string("Track (choose a pass)");
    ImGui::Checkbox(tl.c_str(), &S.track);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("During each pass of this satellite, retune about once a second for the Doppler shift");
    ImGui::BeginDisabled(!S.track);
    ImGui::Checkbox("Auto start at AOS, stop at LOS", &S.autoStart);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Switches to the satellite's mode and starts the receiver at AOS (when it is stopped), and stops it at LOS");
    ImGui::Checkbox("Auto record IQ", &S.autoRecord);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Records the radio's IQ during the pass, as Record IQ does (format from the Record IQ setting), into %s/recordings", plat::dataDir().c_str());
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (S.inPass) ImGui::TextColored(pal::okGreen(), "tracking%s", S.weRecord ? ", recording" : "");
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace

// dev: --tle <file> loads TLEs from a file as pasted ones would (tests without the network)
void passesDevTle(const std::string& path) {
    loadState();
    for (const dect2::Tle& t : dect2::parseTleText(readText(path))) S.tles[t.norad] = t;
    buildSats();
}

void passesWindow(App& a) {
    loadState();
    if (!S.sats.empty() && (S.track || a.passesOpen)) compute(a);
    trackTick(a);
    if (a.passesOpen) window(a);
}
