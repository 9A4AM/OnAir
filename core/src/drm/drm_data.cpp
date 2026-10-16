// DRM data services: packet reassembly -> MSC data groups -> MOT objects (slide show, broadcast website, EPG) and TPEG frames, see drm_data.h.
#include "dect2/drm_data.h"
#include "dect2/dab.h"
#include "dect2/dab_pad.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace dect2 {

const char* drmAppName(int domain, int app) {
    if (domain != 0) return domain == 1 ? "DRM domain application" : "application of another domain";
    switch (app) {
    case 0x001: return "dynamic labels";
    case 0x002: return "MOT slide show";
    case 0x003: return "MOT broadcast website";
    case 0x004: return "TPEG";
    case 0x005: return "DGPS";
    case 0x006: return "TMC";
    case 0x007: return "EPG";
    case 0x008: return "DAB Java";
    case 0x009: return "DMB";
    case 0x00A: return "IPDC services";
    case 0x00B: return "voice applications";
    case 0x00C: return "middleware";
    case 0x00D: return "filecasting";
    case 0x44A: return "Journaline";
    }
    return app < 0 ? "unknown application" : "unlisted application";
}

namespace {
const size_t kMaxObjects = 96;
const size_t kTextBytes = 16384;

bool validUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = (uint8_t)s[i];
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size()) return false;
        for (size_t j = 1; j < n; j++) if (((uint8_t)s[i + j] >> 6) != 2) return false;
        i += n;
    }
    return true;
}

// Text of a received object for display: control characters out, ISO 8859-1 turned into UTF-8 when the bytes are not UTF-8
std::string displayText(const std::vector<uint8_t>& b) {
    std::string raw((const char*)b.data(), std::min(b.size(), kTextBytes));
    std::string out;
    const bool u8 = validUtf8(raw);
    for (unsigned char c : raw) {
        if (c == '\r') continue;
        if (c < 0x20 && c != '\n' && c != '\t') { out += ' '; continue; }
        if (c >= 0x80 && !u8) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); continue; }
        out += (char)c;
    }
    return out;
}

bool looksText(const std::vector<uint8_t>& b) {
    const size_t n = std::min<size_t>(b.size(), 512);
    if (n < 4) return false;
    size_t odd = 0;
    for (size_t i = 0; i < n; i++) {
        const uint8_t c = b[i];
        if (c == 0) return false;
        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') odd++;
    }
    return odd * 50 < n;
}

bool endsWith(const std::string& s, const char* e) {
    const std::string x(e);
    if (s.size() < x.size()) return false;
    for (size_t i = 0; i < x.size(); i++) if (std::tolower((unsigned char)s[s.size() - x.size() + i]) != x[i]) return false;
    return true;
}

uint64_t fnv(const std::vector<uint8_t>& b) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t c : b) h = (h ^ c) * 1099511628211ull;
    return h | 1;
}

// The payload of an MSC data group (EN 300 401 clause 5.3.3.1): after the headers, before the CRC; nullptr when the group is damaged
const uint8_t* groupPayload(const uint8_t* d, size_t n, size_t& len) {
    if (n < 3) return nullptr;
    const bool ext = d[0] & 0x80, crc = d[0] & 0x40, seg = d[0] & 0x20, ua = d[0] & 0x10;
    if (crc) {
        if (n < 5 || dab::crc16(d, (int)n - 2) != (uint16_t)((d[n - 2] << 8) | d[n - 1])) return nullptr;
        n -= 2;
    }
    size_t p = 2 + (ext ? 2 : 0) + (seg ? 2 : 0);
    if (ua) { if (p + 1 > n) return nullptr; p += 1 + (d[p] & 0x0F); }
    if (p > n) return nullptr;
    len = n - p;
    return d + p;
}
} // namespace

DrmDataService::DrmDataService() : mot_(24, 1u << 20) {
    mot_.setSink([this](const MotObject& m) { motObject(m); });
}

void DrmDataService::configure(const drm::SdcApp& a) {
    const int app = a.dabUserApp();
    const int key = (int)((a.shortId << 28) ^ (a.streamId << 24) ^ (a.packetId << 20) ^ ((a.packet ? 1 : 0) << 19) ^ ((a.dataUnit ? 1 : 0) << 18) ^ (a.packetLen << 10) ^ (a.domain << 8) ^ ((app + 1) & 0x7FF));
    if (key == key_) return;
    key_ = key;
    const uint64_t seq = view_.seq + 1;
    view_ = DrmDataView();
    view_.seq = seq;
    mot_.reset();
    demux_ = DrmPacketDemux();
    nextOrder_ = 1; slideHash_ = 0; fresh_.clear();
    DrmDataView& v = view_;
    v.shortId = a.shortId; v.domain = a.domain; v.userApp = app;
    v.appName = drmAppName(a.domain, app);
    v.packet = a.packet; v.dataUnit = a.dataUnit; v.streamId = a.streamId; v.packetId = a.packetId; v.packetLen = a.packetLen;
    v.journaline = a.domain == 0 && app == 0x44A;
    char b[160];
    if (!a.packet) {
        v.modeText = "stream mode data, not decoded";
    } else {
        snprintf(b, sizeof b, "packet mode, %s, packet id %d, %d byte packets", a.dataUnit ? "data units" : "single packets", a.packetId, a.packetLen);
        v.modeText = b;
        if (a.packetLen > 0) demux_.configure(a.packetLen, a.packetId);
    }
    const bool mot = a.domain == 0 && (app == 0x002 || app == 0x003 || app == 0x007);
    const bool tpeg = a.domain == 0 && app == 0x004;
    v.decoded = a.packet && a.dataUnit && a.packetLen > 0 && (mot || tpeg || v.journaline);
    if (a.packet && !a.dataUnit && (mot || tpeg)) v.modeText += " (single packets: not decoded)";
}

void DrmDataService::feed(const uint8_t* d, size_t n) {
    if (!view_.packet || !view_.dataUnit || view_.packetLen <= 0 || view_.journaline) return;
    const int app = view_.userApp;
    if (view_.domain != 0 || (app != 0x002 && app != 0x003 && app != 0x007 && app != 0x004)) return;
    demux_.feed(d, n, [this](const uint8_t* g, size_t m) { unit(g, m); });
    view_.seq++;
}

void DrmDataService::unit(const uint8_t* g, size_t n) {
    view_.unitBytes += n;
    if (view_.userApp == 0x004) {                                // TPEG: transport frames, a sync byte 0xFF, a 16 bit length, the frame
        size_t len = 0;
        const uint8_t* p = groupPayload(g, n, len);
        if (!p) { view_.mot.groupsBad++; return; }
        view_.mot.groups++;
        view_.tpegBytes += len;
        for (size_t i = 0; i + 3 <= len;) {
            if (p[i] != 0xFF) { i++; continue; }
            const size_t fl = ((size_t)p[i + 1] << 8) | p[i + 2];
            if (i + 3 + fl > len || fl == 0) { i++; continue; }
            view_.tpegFrames++;
            i += 3 + fl;
        }
        return;
    }
    mot_.dataGroup(g, (int)n);
}

void DrmDataService::motObject(const MotObject& m) {
    DrmDataView& v = view_;
    char nb[64];
    DrmObjectInfo o;
    o.tid = m.tid;
    snprintf(nb, sizeof nb, "object %u", (unsigned)m.tid);
    o.name = m.name.empty() ? nb : m.name;
    o.type = motContentTypeName(m.contentType, m.subtype);
    o.size = m.body.size();
    o.received = 1;
    const bool xml = m.body.size() > 5 && std::string((const char*)m.body.data(), 5) == "<?xml";
    o.text = m.contentType == 1 || (m.contentType != 2 && looksText(m.body)) ||
             endsWith(o.name, ".html") || endsWith(o.name, ".htm") || endsWith(o.name, ".txt") || endsWith(o.name, ".css") || endsWith(o.name, ".xml");
    if (o.text) o.content = displayText(m.body);
    if (v.userApp == 0x007) o.note = xml ? "XML in the clear" : m.body.empty() ? "" : "binary coded, not decoded";
    if (v.userApp == 0x002 || m.contentType == 2) {
        if (!m.category.empty()) o.note += (o.note.empty() ? "" : ", ") + std::string("category ") + m.category;
        if (m.hasTrigger) o.note += (o.note.empty() ? "" : ", ") + std::string("trigger ") + motTimeText(m.trigger);
    }
    // the same name again is the carousel round: count it, replace the content when it changed
    for (DrmObjectInfo& e : v.objects) {
        if (e.name == o.name && e.type == o.type) {
            e.received++; e.tid = o.tid;
            if (e.size != o.size || e.content != o.content) { e.size = o.size; e.content = o.content; e.note = o.note; e.order = nextOrder_++; }
            goto picture;
        }
    }
    o.order = nextOrder_++;
    if (v.objects.size() >= kMaxObjects) v.objects.erase(v.objects.begin());
    v.objects.push_back(std::move(o));
picture:
    if (m.contentType == 2 && (v.userApp == 0x002 || v.userApp == 0x003)) {
        const uint64_t h = fnv(m.body) ^ (uint64_t)m.slideId;
        if (v.userApp == 0x002) {
            if (h == slideHash_) return;                           // the same picture again under a new transport id
            DrmSlide s;
            if (!decodeSlideImage(m.body.data(), m.body.size(), m.subtype, s.w, s.h, s.rgba)) return;
            slideHash_ = h;
            s.seq = v.slide.seq + 1;
            s.name = m.name; s.caption = m.caption; s.category = m.category; s.clickUrl = m.clickUrl; s.altUrl = m.altUrl;
            s.categoryId = m.categoryId; s.slideId = m.slideId;
            s.type = motContentTypeName(m.contentType, m.subtype);
            s.bytes = m.body.size();
            s.trigger = m.hasTrigger ? motTimeText(m.trigger) : "";
            v.slide = s;
            if (fresh_.size() >= 16) fresh_.pop_front();
            fresh_.push_back(std::move(s));
        }
    }
}

bool DrmDataService::takeSlide(DrmSlide& out) {
    if (fresh_.empty()) return false;
    out = std::move(fresh_.front());
    fresh_.pop_front();
    return true;
}

void DrmDataService::fill(DrmDataView& v) const {
    v = view_;
    v.packets = demux_.packets; v.badPackets = demux_.badPackets; v.lostPackets = demux_.lostPackets; v.orphanPackets = demux_.orphanPackets;
    v.emptyPackets = demux_.emptyPackets; v.otherIdPackets = demux_.otherIdPackets; v.unitsOk = demux_.unitsOk; v.unitsDropped = demux_.unitsDropped;
    const MotStats& s = mot_.stats();
    if (view_.userApp != 0x004) v.mot = s;
    v.directory = mot_.directory();
    for (MotObject& m : v.directory) m.body.clear();
}

} // namespace dect2
