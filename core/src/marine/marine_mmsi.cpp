#include "dect2/marine/marine_mmsi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace dect2 {
namespace marine {

namespace {
struct Mid { int mid; const char* country; };
// ITU Maritime Identification Digits (List of MIDs, Appendix 43 of the Radio Regulations / ITU Operational Bulletin)
const Mid kMids[] = {
{201,"Albania"},{202,"Andorra"},{203,"Austria"},{204,"Azores (Portugal)"},{205,"Belgium"},{206,"Belarus"},{207,"Bulgaria"},{208,"Vatican"},
{209,"Cyprus"},{210,"Cyprus"},{211,"Germany"},{212,"Cyprus"},{213,"Georgia"},{214,"Moldova"},{215,"Malta"},{216,"Armenia"},{218,"Germany"},
{219,"Denmark"},{220,"Denmark"},{224,"Spain"},{225,"Spain"},{226,"France"},{227,"France"},{228,"France"},{229,"Malta"},{230,"Finland"},
{231,"Faroe Islands (Denmark)"},{232,"United Kingdom"},{233,"United Kingdom"},{234,"United Kingdom"},{235,"United Kingdom"},
{236,"Gibraltar"},{237,"Greece"},{238,"Croatia"},{239,"Greece"},{240,"Greece"},{241,"Greece"},{242,"Morocco"},{243,"Hungary"},
{244,"Netherlands"},{245,"Netherlands"},{246,"Netherlands"},{247,"Italy"},{248,"Malta"},{249,"Malta"},{250,"Ireland"},{251,"Iceland"},
{252,"Liechtenstein"},{253,"Luxembourg"},{254,"Monaco"},{255,"Madeira (Portugal)"},{256,"Malta"},{257,"Norway"},{258,"Norway"},
{259,"Norway"},{261,"Poland"},{262,"Montenegro"},{263,"Portugal"},{264,"Romania"},{265,"Sweden"},{266,"Sweden"},{267,"Slovakia"},
{268,"San Marino"},{269,"Switzerland"},{270,"Czech Republic"},{271,"Turkey"},{272,"Ukraine"},{273,"Russia"},{274,"North Macedonia"},
{275,"Latvia"},{276,"Estonia"},{277,"Lithuania"},{278,"Slovenia"},{279,"Serbia"},
{301,"Anguilla (UK)"},{303,"Alaska (USA)"},{304,"Antigua and Barbuda"},{305,"Antigua and Barbuda"},{306,"Netherlands Antilles"},
{307,"Aruba"},{308,"Bahamas"},{309,"Bahamas"},{310,"Bermuda"},{311,"Bahamas"},{312,"Belize"},{314,"Barbados"},{316,"Canada"},
{319,"Cayman Islands"},{321,"Costa Rica"},{323,"Cuba"},{325,"Dominica"},{327,"Dominican Republic"},{329,"Guadeloupe (France)"},
{330,"Grenada"},{331,"Greenland (Denmark)"},{332,"Guatemala"},{334,"Honduras"},{336,"Haiti"},{338,"United States"},{339,"Jamaica"},
{341,"Saint Kitts and Nevis"},{343,"Saint Lucia"},{345,"Mexico"},{347,"Martinique (France)"},{348,"Montserrat (UK)"},{350,"Nicaragua"},
{351,"Panama"},{352,"Panama"},{353,"Panama"},{354,"Panama"},{355,"Panama"},{356,"Panama"},{357,"Panama"},{358,"Puerto Rico (USA)"},
{359,"El Salvador"},{361,"Saint Pierre and Miquelon (France)"},{362,"Trinidad and Tobago"},{364,"Turks and Caicos Islands (UK)"},
{366,"United States"},{367,"United States"},{368,"United States"},{369,"United States"},{370,"Panama"},{371,"Panama"},{372,"Panama"},
{373,"Panama"},{374,"Panama"},{375,"Saint Vincent and the Grenadines"},{376,"Saint Vincent and the Grenadines"},
{377,"Saint Vincent and the Grenadines"},{378,"British Virgin Islands (UK)"},{379,"US Virgin Islands (USA)"},
{401,"Afghanistan"},{403,"Saudi Arabia"},{405,"Bangladesh"},{408,"Bahrain"},{410,"Bhutan"},{412,"China"},{413,"China"},{414,"China"},
{416,"Taiwan"},{417,"Sri Lanka"},{419,"India"},{422,"Iran"},{423,"Azerbaijan"},{425,"Iraq"},{428,"Israel"},{431,"Japan"},{432,"Japan"},
{434,"Turkmenistan"},{436,"Kazakhstan"},{437,"Uzbekistan"},{438,"Jordan"},{440,"South Korea"},{441,"South Korea"},{443,"Palestine"},
{445,"North Korea"},{447,"Kuwait"},{450,"Lebanon"},{451,"Kyrgyzstan"},{453,"Macao (China)"},{455,"Maldives"},{457,"Mongolia"},
{459,"Nepal"},{461,"Oman"},{463,"Pakistan"},{466,"Qatar"},{468,"Syria"},{470,"United Arab Emirates"},{471,"United Arab Emirates"},
{472,"Tajikistan"},{473,"Yemen"},{475,"Yemen"},{477,"Hong Kong (China)"},{478,"Bosnia and Herzegovina"},
{501,"Adelie Land (France)"},{503,"Australia"},{506,"Myanmar"},{508,"Brunei"},{510,"Micronesia"},{511,"Palau"},{512,"New Zealand"},
{514,"Cambodia"},{515,"Cambodia"},{516,"Christmas Island (Australia)"},{518,"Cook Islands"},{520,"Fiji"},{523,"Cocos Islands (Australia)"},
{525,"Indonesia"},{529,"Kiribati"},{531,"Laos"},{533,"Malaysia"},{536,"Northern Mariana Islands (USA)"},{538,"Marshall Islands"},
{540,"New Caledonia (France)"},{542,"Niue"},{544,"Nauru"},{546,"French Polynesia"},{548,"Philippines"},{553,"Papua New Guinea"},
{555,"Pitcairn Island (UK)"},{557,"Solomon Islands"},{559,"American Samoa (USA)"},{561,"Samoa"},{563,"Singapore"},{564,"Singapore"},
{565,"Singapore"},{566,"Singapore"},{567,"Thailand"},{570,"Tonga"},{572,"Tuvalu"},{574,"Vietnam"},{576,"Vanuatu"},{577,"Vanuatu"},
{578,"Wallis and Futuna (France)"},
{601,"South Africa"},{603,"Angola"},{605,"Algeria"},{607,"Saint Paul and Amsterdam Islands (France)"},{608,"Ascension Island (UK)"},
{609,"Burundi"},{610,"Benin"},{611,"Botswana"},{612,"Central African Republic"},{613,"Cameroon"},{615,"Congo"},{616,"Comoros"},
{617,"Cape Verde"},{618,"Crozet Archipelago (France)"},{619,"Ivory Coast"},{620,"Comoros"},{621,"Djibouti"},{622,"Egypt"},
{624,"Ethiopia"},{625,"Eritrea"},{626,"Gabon"},{627,"Ghana"},{629,"Gambia"},{630,"Guinea-Bissau"},{631,"Equatorial Guinea"},
{632,"Guinea"},{633,"Burkina Faso"},{634,"Kenya"},{635,"Kerguelen Islands (France)"},{636,"Liberia"},{637,"Liberia"},{638,"South Sudan"},
{642,"Libya"},{644,"Lesotho"},{645,"Mauritius"},{647,"Madagascar"},{649,"Mali"},{650,"Mozambique"},{654,"Mauritania"},{655,"Malawi"},
{656,"Niger"},{657,"Nigeria"},{659,"Namibia"},{660,"Reunion (France)"},{661,"Rwanda"},{662,"Sudan"},{663,"Senegal"},{664,"Seychelles"},
{665,"Saint Helena (UK)"},{666,"Somalia"},{667,"Sierra Leone"},{668,"Sao Tome and Principe"},{669,"Eswatini"},{670,"Chad"},{671,"Togo"},
{672,"Tunisia"},{674,"Tanzania"},{675,"Uganda"},{676,"DR Congo"},{677,"Tanzania"},{678,"Zambia"},{679,"Zimbabwe"},
{701,"Argentina"},{710,"Brazil"},{720,"Bolivia"},{725,"Chile"},{730,"Colombia"},{735,"Ecuador"},{740,"Falkland Islands (UK)"},
{745,"Guiana (France)"},{750,"Guyana"},{755,"Paraguay"},{760,"Peru"},{765,"Suriname"},{770,"Uruguay"},{775,"Venezuela"},
};
} // namespace

const char* midCountry(int mid) {
    for (const Mid& m : kMids) if (m.mid == mid) return m.country;
    return "";
}

const char* mmsiKindName(MmsiKind k) {
    switch (k) {
    case MmsiKind::Ship: return "ship";
    case MmsiKind::CoastStation: return "coast station";
    case MmsiKind::Group: return "group";
    case MmsiKind::SarAircraft: return "SAR aircraft";
    case MmsiKind::Handheld: return "handheld VHF";
    case MmsiKind::CraftAssociated: return "craft of a parent ship";
    case MmsiKind::AisSart: return "AIS-SART";
    case MmsiKind::Mob: return "man overboard device";
    case MmsiKind::EpirbAis: return "EPIRB-AIS";
    case MmsiKind::AtoN: return "aid to navigation";
    case MmsiKind::Special: return "special";
    default: return "";
    }
}

MmsiInfo mmsiInfo(const std::string& m) {
    MmsiInfo r;
    if (m.size() != 9) return r;
    for (char c : m) { if (c == '~') r.partial = true; else if (c < '0' || c > '9') return r; }
    auto dig = [&](size_t a, size_t n, int& v) { v = 0; for (size_t i = a; i < a + n; i++) { if (m[i] == '~') return false; v = v * 10 + (m[i] - '0'); } return true; };
    int mid = 0;
    auto setMid = [&](size_t a) { if (dig(a, 3, mid)) { r.mid = mid; r.country = midCountry(mid); } };
    if (m.compare(0, 2, "00") == 0) { r.kind = MmsiKind::CoastStation; setMid(2); }
    else if (m[0] == '0') { r.kind = MmsiKind::Group; setMid(1); }
    else if (m.compare(0, 3, "111") == 0) { r.kind = MmsiKind::SarAircraft; setMid(3); }
    else if (m.compare(0, 2, "99") == 0) { r.kind = MmsiKind::AtoN; setMid(2); }
    else if (m.compare(0, 2, "98") == 0) { r.kind = MmsiKind::CraftAssociated; setMid(2); }
    else if (m.compare(0, 3, "970") == 0) r.kind = MmsiKind::AisSart;
    else if (m.compare(0, 3, "972") == 0) r.kind = MmsiKind::Mob;
    else if (m.compare(0, 3, "974") == 0) r.kind = MmsiKind::EpirbAis;
    else if (m[0] == '8') { r.kind = MmsiKind::Handheld; setMid(1); }
    else if (m[0] >= '2' && m[0] <= '7') { r.kind = MmsiKind::Ship; setMid(0); }
    return r;
}

namespace {
std::string trimS(std::string v) {
    size_t a = 0, b = v.size();
    while (a < b && (v[a] == ' ' || v[a] == '\t' || v[a] == '\r' || v[a] == '"')) a++;
    while (b > a && (v[b - 1] == ' ' || v[b - 1] == '\t' || v[b - 1] == '\r' || v[b - 1] == '"')) b--;
    return v.substr(a, b - a);
}
std::vector<std::string> splitRow(const std::string& line, char d) {
    std::vector<std::string> r;
    std::string cur;
    bool q = false;
    for (char c : line) {
        if (c == '"') q = !q;
        else if (c == d && !q) { r.push_back(trimS(cur)); cur.clear(); }
        else cur += c;
    }
    r.push_back(trimS(cur));
    return r;
}
bool nineDigits(const std::string& s) {
    if (s.size() != 9) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}
std::string lowerS(std::string v) { for (char& c : v) c = (char)tolower((unsigned char)c); return v; }
bool numberOf(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* e = nullptr;
    v = strtod(s.c_str(), &e);
    return e && *e == 0;
}
} // namespace

int ShipDb::importText(const std::string& text) {
    int n = 0;
    std::vector<std::string> lines;
    for (size_t p = 0; p < text.size();) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(p, e - p);
        p = e + 1;
        if (!l.empty() && l[0] == '\xEF') l.erase(0, 3);       // byte order mark
        if (trimS(l).empty() || trimS(l)[0] == '#') continue;
        lines.push_back(l);
    }
    if (lines.empty()) return 0;
    const std::string& f = lines[0];
    const char d = f.find('\t') != std::string::npos ? '\t' : f.find(';') != std::string::npos ? ';' : ',';
    // columns: indexes of mmsi, name, callsign, type, country, station, location, lat, lon
    int col[9] = {0, 1, 2, 3, 4, -1, 5, -1, -1};
    size_t first = 0;
    const auto head = splitRow(lines[0], d);
    if (!nineDigits(trimS(head[0]))) {
        first = 1;
        for (int& c : col) c = -1;
        for (size_t i = 0; i < head.size(); i++) {
            const std::string h = lowerS(head[i]);
            if (h == "mmsi") col[0] = (int)i;
            else if (h == "name" || h == "shipname" || h == "ship name" || h == "vessel") col[1] = (int)i;
            else if (h == "callsign" || h == "call sign" || h == "call") col[2] = (int)i;
            else if (h == "type" || h == "shiptype" || h == "ship type") col[3] = (int)i;
            else if (h == "country" || h == "flag") col[4] = (int)i;
            else if (h == "station" || h == "coast station" || h == "coast") col[5] = (int)i;
            else if (h == "location" || h == "place" || h == "city") col[6] = (int)i;
            else if (h == "lat" || h == "latitude") col[7] = (int)i;
            else if (h == "lon" || h == "lng" || h == "longitude") col[8] = (int)i;
        }
        if (col[0] < 0) return 0;
    }
    std::lock_guard<std::mutex> lk(mu_);
    for (size_t li = first; li < lines.size(); li++) {
        const auto c = splitRow(lines[li], d);
        auto at = [&](int k) { return k >= 0 && (size_t)k < c.size() ? c[(size_t)k] : std::string(); };
        const std::string mmsi = at(col[0]);
        if (!nineDigits(mmsi)) continue;
        ShipRec r;
        auto it = m_.find(mmsi);
        if (it != m_.end()) r = it->second;
        r.mmsi = mmsi;
        auto set = [&](std::string& dst, int k) { const std::string v = at(k); if (!v.empty()) dst = v.substr(0, 60); };
        set(r.name, col[1]); set(r.callsign, col[2]); set(r.type, col[3]); set(r.country, col[4]); set(r.station, col[5]); set(r.location, col[6]);
        double la, lo;
        if (numberOf(at(col[7]), la) && numberOf(at(col[8]), lo) && std::fabs(la) <= 90 && std::fabs(lo) <= 180) { r.hasPos = true; r.lat = la; r.lon = lo; }
        r.source = 1;
        m_[mmsi] = r;
        n++;
    }
    if (n) { ver_++; dirty_ = true; }
    return n;
}

std::string ShipDb::exportTsv() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<const ShipRec*> v;
    for (const auto& kv : m_) v.push_back(&kv.second);
    std::sort(v.begin(), v.end(), [](const ShipRec* a, const ShipRec* b) { return a->mmsi < b->mmsi; });
    std::string o = "mmsi\tname\tcallsign\ttype\tcountry\tstation\tlocation\tlat\tlon\n";
    char b[48];
    for (const ShipRec* r : v) {
        std::string la, lo;
        if (r->hasPos) { snprintf(b, sizeof b, "%.5f", r->lat); la = b; snprintf(b, sizeof b, "%.5f", r->lon); lo = b; }
        auto clean = [](std::string s) { for (char& c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' '; return s; };
        o += r->mmsi + "\t" + clean(r->name) + "\t" + clean(r->callsign) + "\t" + clean(r->type) + "\t" + clean(r->country) + "\t" + clean(r->station) + "\t" + clean(r->location) + "\t" + la + "\t" + lo + "\n";
    }
    return o;
}

void ShipDb::learn(const std::string& mmsi, const std::string& name, const std::string& callsign, const std::string& type) {
    if (!nineDigits(mmsi) || (name.empty() && callsign.empty() && type.empty())) return;
    std::lock_guard<std::mutex> lk(mu_);
    ShipRec& r = m_[mmsi];
    const ShipRec before = r;
    r.mmsi = mmsi;
    auto upd = [&](std::string& dst, const std::string& v) { if (!v.empty() && (r.source == 0 || dst.empty())) dst = v; };
    upd(r.name, name); upd(r.callsign, callsign); upd(r.type, type);
    if (r.country.empty()) r.country = mmsiInfo(mmsi).country;
    if (before.name != r.name || before.callsign != r.callsign || before.type != r.type || before.country != r.country) { ver_++; dirty_ = true; }
}

bool ShipDb::find(const std::string& mmsi, ShipRec& out) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = m_.find(mmsi);
    if (it == m_.end()) return false;
    out = it->second;
    return true;
}

std::string ShipDb::name(const std::string& mmsi) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = m_.find(mmsi);
    if (it == m_.end()) return "";
    const ShipRec& r = it->second;
    std::string n = !r.name.empty() ? r.name : r.station;
    if (!r.location.empty() && !n.empty() && n.find(r.location) == std::string::npos) n += ", " + r.location;
    return n;
}

std::vector<ShipRec> ShipDb::positioned() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ShipRec> v;
    for (const auto& kv : m_) if (kv.second.hasPos) v.push_back(kv.second);
    std::sort(v.begin(), v.end(), [](const ShipRec& a, const ShipRec& b) { return a.mmsi < b.mmsi; });
    return v;
}

size_t ShipDb::size() const { std::lock_guard<std::mutex> lk(mu_); return m_.size(); }
uint64_t ShipDb::version() const { std::lock_guard<std::mutex> lk(mu_); return ver_; }
bool ShipDb::takeDirty() { std::lock_guard<std::mutex> lk(mu_); const bool d = dirty_; dirty_ = false; return d; }
void ShipDb::clear() { std::lock_guard<std::mutex> lk(mu_); m_.clear(); ver_++; dirty_ = true; }
ShipDb& ShipDb::global() { static ShipDb g; return g; }

} // namespace marine
} // namespace dect2
