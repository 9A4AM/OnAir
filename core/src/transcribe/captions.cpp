#include "dect2/transcribe/captions.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dect2 {

namespace {

bool isStop(const std::string& s, size_t i, size_t& len) {
    const unsigned char c = (unsigned char)s[i];
    if (c == '.' || c == '!' || c == '?') {
        len = 1;
        // "3.5", "e.g." style dots inside a token are not stops: a stop is followed by space or the end
        return i + 1 >= s.size() || s[i + 1] == ' ' || s[i + 1] == '\n' || s[i + 1] == '"' || s[i + 1] == '!' || s[i + 1] == '?' || s[i + 1] == '.';
    }
    // U+3002, U+FF01, U+FF1F (E3 80 82, EF BC 81, EF BC 9F), U+2026 (E2 80 A6)
    if (i + 2 < s.size()) {
        const unsigned char d = (unsigned char)s[i + 1], e = (unsigned char)s[i + 2];
        if ((c == 0xE3 && d == 0x80 && e == 0x82) || (c == 0xEF && d == 0xBC && (e == 0x81 || e == 0x9F)) || (c == 0xE2 && d == 0x80 && e == 0xA6)) { len = 3; return true; }
    }
    return false;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\n' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\n' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}

// a UTF-8 safe cut position at or before pos
size_t utf8Floor(const std::string& s, size_t pos) {
    while (pos > 0 && pos < s.size() && ((unsigned char)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

void cutLong(const std::string& s, size_t maxChars, std::vector<std::string>& out) {
    std::string rest = trim(s);
    while (rest.size() > maxChars) {
        size_t cut = std::string::npos;
        for (size_t i = maxChars; i > maxChars / 2; i--)
            if (rest[i] == ',' || rest[i] == ';' || rest[i] == ':') { cut = i + 1; break; }
        if (cut == std::string::npos)
            for (size_t i = maxChars; i > maxChars / 2; i--) if (rest[i] == ' ') { cut = i; break; }
        if (cut == std::string::npos) cut = utf8Floor(rest, maxChars);
        out.push_back(trim(rest.substr(0, cut)));
        rest = trim(rest.substr(cut));
    }
    if (!rest.empty()) out.push_back(rest);
}

}  // namespace

std::vector<std::string> splitSentences(const std::string& text, size_t maxChars) {
    std::vector<std::string> out;
    maxChars = std::max<size_t>(maxChars, 20);
    std::string cur;
    for (size_t i = 0; i < text.size();) {
        size_t len = 1;
        if (isStop(text, i, len)) {
            cur.append(text, i, len);
            i += len;
            while (i < text.size() && isStop(text, i, len) && text[i] != '.') { cur.append(text, i, len); i += len; }   // "?!" and the like
            if (!trim(cur).empty()) cutLong(cur, maxChars, out);
            cur.clear();
        } else {
            cur += text[i++];
        }
    }
    if (!trim(cur).empty()) cutLong(cur, maxChars, out);
    return out;
}

bool showBroadcastSubs(CaptionSource s) { return s != CaptionSource::Generated; }
bool showGeneratedSubs(CaptionSource s, bool broadcastPresent) {
    switch (s) {
        case CaptionSource::Broadcast: return false;
        case CaptionSource::Generated: return true;
        case CaptionSource::Both: return true;
        case CaptionSource::Auto: return !broadcastPresent;
    }
    return false;
}

CaptionStyle CaptionStyle::clamped() const {
    CaptionStyle c = *this;
    c.sizePct = std::max(60, std::min(250, c.sizePct));
    c.bgAlpha = std::max(0, std::min(100, c.bgAlpha));
    c.lines = std::max(1, std::min(4, c.lines));
    return c;
}

namespace {
struct StdName { int std; const char* name; };
const StdName kStd[] = {
    {5, "FM broadcast"}, {55, "AM broadcast"}, {2, "DAB / DAB+"}, {10, "DRM"}, {21, "HD Radio"}, {22, "CDR"},
    {0, "DVB-T/T2 TV"}, {6, "DVB-S/S2 TV"}, {40, "DVB-C TV"}, {1, "ATSC TV"}, {3, "ATSC 3.0 TV"}, {4, "ISDB-T TV"}, {7, "DTMB TV"},
    {8, "Analog TV"}, {47, "MAC TV"},
    {25, "Amateur voice (SSB, FM)"}, {53, "QO-100"},
    {9, "DMR"}, {38, "NXDN"}, {50, "P25"}, {45, "dPMR / PMR446"}, {31, "M17"}, {33, "D-STAR"},
};
}  // namespace

bool captionStandardEligible(int standard) {
    for (const auto& s : kStd) if (s.std == standard) return true;
    return false;
}
const char* captionStandardName(int standard) {
    for (const auto& s : kStd) if (s.std == standard) return s.name;
    return "";
}
std::vector<int> captionStandards() {
    std::vector<int> v;
    for (const auto& s : kStd) v.push_back(s.std);
    return v;
}

std::string cueTimestamp(double sec, bool vtt) {
    if (sec < 0) sec = 0;
    const long long ms = (long long)std::llround(sec * 1000.0);
    char b[32];
    snprintf(b, sizeof b, "%02lld:%02lld:%02lld%c%03lld", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, vtt ? '.' : ',', ms % 1000);
    return b;
}

void CaptionTrack::onAudio(size_t n, int rate, int64_t endWallMs) {
    if (!n || rate <= 0) return;
    std::lock_guard<std::mutex> lk(mu_);
    clock_ += (double)n / rate;
    anchorClock_ = clock_;
    anchorWall_ = endWallMs;
}

double CaptionTrack::clock() const {
    std::lock_guard<std::mutex> lk(mu_);
    return clock_;
}

double CaptionTrack::clockAt(int64_t wallMs) const {
    std::lock_guard<std::mutex> lk(mu_);
    return anchorClock_ - (double)(anchorWall_ - wallMs) / 1000.0;
}

void CaptionTrack::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    clock_ = origin_ = anchorClock_ = 0;
    anchorWall_ = 0;
    cues_.clear();
}

void CaptionTrack::markOrigin() {
    std::lock_guard<std::mutex> lk(mu_);
    origin_ = clock_;
}

double CaptionTrack::origin() const {
    std::lock_guard<std::mutex> lk(mu_);
    return origin_;
}

size_t CaptionTrack::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return cues_.size();
}

int CaptionTrack::add(const TranscriptEntry& e) {
    const std::vector<std::string> sent = splitSentences(e.text);
    if (sent.empty()) return 0;
    const std::vector<std::string> tr = e.translation.empty() ? std::vector<std::string>() : splitSentences(e.translation);
    std::lock_guard<std::mutex> lk(mu_);
    const int64_t wall = e.startMs ? e.startMs : e.wall * 1000;
    const double t0 = anchorClock_ - (double)(anchorWall_ - wall) / 1000.0;
    const double dur = std::max(e.durSec, 0.5);
    size_t total = 0;
    for (const auto& s : sent) total += std::max<size_t>(s.size(), 1);
    double at = t0;
    int added = 0;
    for (size_t i = 0; i < sent.size(); i++) {
        CaptionCue c;
        c.text = sent[i];
        c.lang = e.lang;
        const double share = dur * (double)std::max<size_t>(sent[i].size(), 1) / (double)total;
        c.start = at;
        c.end = at + std::max(share, std::min(1.0, dur));
        at += share;
        if (!tr.empty()) {
            if (tr.size() == sent.size()) c.translation = tr[i];
            else {
                const size_t a = i * tr.size() / sent.size(), b = (i + 1) * tr.size() / sent.size();
                for (size_t k = a; k < b; k++) c.translation += (c.translation.empty() ? "" : " ") + tr[k];
            }
        }
        c.shownAt = clock_;
        if (cues_.size() >= kMaxCues) cues_.erase(cues_.begin(), cues_.begin() + (long)(kMaxCues / 4));
        cues_.push_back(std::move(c));
        added++;
    }
    // cues never overlap: a cue ends when the next one starts
    for (size_t i = cues_.size() - (size_t)added; i + 1 < cues_.size(); i++) cues_[i].end = std::min(cues_[i].end, std::max(cues_[i].start + 0.2, cues_[i + 1].start));
    return added;
}

std::vector<CaptionCue> CaptionTrack::cues() const {
    std::lock_guard<std::mutex> lk(mu_);
    return cues_;
}

std::vector<CaptionCue> CaptionTrack::visible(size_t maxLines) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<CaptionCue> v;
    for (auto it = cues_.rbegin(); it != cues_.rend() && v.size() < maxLines; ++it) {
        const double hold = std::max(2.5, std::min(8.0, 1.5 + 0.07 * (double)it->text.size()));
        if (clock_ - it->shownAt > hold) break;
        v.push_back(*it);
    }
    std::reverse(v.begin(), v.end());
    return v;
}

static std::string exportCues(const std::vector<CaptionCue>& all, double from, double to, int translation, bool vtt) {
    std::string o = vtt ? "WEBVTT\n\n" : "";
    int n = 0;
    for (const auto& c : all) {
        if (c.end <= from || (to > 0 && c.start >= to)) continue;
        const double a = std::max(c.start, from) - from, b = (to > 0 ? std::min(c.end, to) : c.end) - from;
        std::string body;
        if (translation == 1 && !c.translation.empty()) body = c.translation;
        else if (translation == 2 && !c.translation.empty()) body = c.text + "\n" + c.translation;
        else body = c.text;
        if (!vtt) o += std::to_string(++n) + "\n";
        o += cueTimestamp(a, vtt) + " --> " + cueTimestamp(std::max(b, a + 0.2), vtt) + "\n" + body + "\n\n";
    }
    return o;
}

std::string CaptionTrack::srt(double from, double to, int translation) const { return exportCues(cues(), from, to, translation, false); }
std::string CaptionTrack::vtt(double from, double to, int translation) const { return exportCues(cues(), from, to, translation, true); }

}  // namespace dect2
