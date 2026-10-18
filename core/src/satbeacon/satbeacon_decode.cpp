// What the frames of a beacon carry (see satbeacon_decode.h).
#include "dect2/satbeacon/satbeacon_decode.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <map>
#include <set>

namespace dect2 {
namespace sb {

namespace {
std::string hx(const uint8_t* p, size_t n) {
    std::string s;
    char b[4];
    for (size_t i = 0; i < n; i++) { snprintf(b, sizeof b, "%02X", p[i]); s += b; }
    return s;
}
std::string fmt(const char* f, ...) {
    char b[512];
    va_list ap; va_start(ap, f); vsnprintf(b, sizeof b, f, ap); va_end(ap);
    return b;
}
}

void Analyzer::reset() { *this = Analyzer(); }

void Analyzer::addCcsds(const std::vector<uint8_t>& f, double frameSec) {
    Cc& c = cc_;
    c.blocksSeen++;
    std::vector<uint8_t> block(f.begin() + 4, f.end());
    auto process = [&](const std::vector<uint8_t>& blk) {
        std::vector<uint8_t> fr;
        int corrected = 0;
        const bool ok = c.format.decode(blk.data(), blk.size(), fr, corrected);
        if (!ok) { c.failed++; return; }
        c.decoded++; c.corrected += (uint64_t)corrected;
        if (fr.size() > 8) { if (ccsds::crcCcitt(fr.data(), fr.size() - 2) == (uint16_t)(fr[fr.size() - 2] << 8 | fr[fr.size() - 1])) c.crcOk++; else c.crcBad++; }
        ccsds::Header h;
        if (ccsds::parseHeader(fr.data(), fr.size(), h)) {
            c.last = h;
            auto& vc = c.channels[{h.scid, h.vcid}];
            vc.frames++;
            if (vc.lastCount >= 0 && !h.idle()) { const int d = (int)((h.vcCount - (uint32_t)vc.lastCount) & 0xFF); if (d > 1 && d < 128) c.lost += (uint64_t)(d - 1); }
            vc.lastCount = (int)h.vcCount;
        }
        c.lastFrame = fr;
        if (c.inner) c.inner->add(fr, 3, 1, frameSec);
    };
    if (!c.haveFormat) {
        if (c.blocks.size() < 6) c.blocks.push_back(block);
        if (c.blocks.size() >= 4 && ccsds::findFormat(c.blocks, c.format)) {
            c.haveFormat = true;
            c.inner = std::make_shared<Analyzer>();
            for (const auto& blk : c.blocks) process(blk);   // the blocks the layout was found from count as well
        }
        return;
    }
    process(block);
}

void Analyzer::add(const std::vector<uint8_t>& f, int counterByte, int counterMod, double frameSec) {
    if (f.empty()) return;
    if (f.size() > 12 && f[0] == 0x1A && f[1] == 0xCF && f[2] == 0xFC && f[3] == 0x1D) { ccsds_ = true; len_ = (int)f.size(); frameSec_ = frameSec; addCcsds(f, frameSec); n_++; return; }
    ccsds_ = false;
    if ((int)f.size() != len_ || counterByte != ctrByte_) {   // a different frame layout: start again
        reset();
        len_ = (int)f.size(); ctrByte_ = counterByte;
        col_.assign((size_t)len_, Col());
    }
    frameSec_ = frameSec;
    mod_ = counterByte >= 0 && counterMod > 0 ? std::min(counterMod, 256) : 1;
    const int ctr = counterByte >= 0 ? f[(size_t)counterByte] : 0;
    const int p = mod_ > 1 ? ctr % mod_ : 0;
    for (int c = 0; c < len_; c++) {
        Col& k = col_[(size_t)c];
        if (k.pos.size() < (size_t)mod_) k.pos.resize((size_t)mod_);
        const uint8_t v = f[(size_t)c];
        Pos& q = k.pos[(size_t)p];
        if (q.n == 0) q.first = v;
        q.n++; q.last = v; q.mn = std::min(q.mn, v); q.mx = std::max(q.mx, v);
        if (n_ == 0) k.first = v;
        k.mn = std::min(k.mn, v); k.mx = std::max(k.mx, v); k.sum += v;
        if (!k.seen[v]) { k.seen[v] = true; k.distinct++; }
        if (!prev_.empty()) {
            const int d = (v - prev_[(size_t)c]) & 255;
            if (d <= 1) k.steps01++; else k.stepsOther++;
            k.stepSum += d <= 1 ? (uint64_t)d : 0;
        }
        const int cd = (v - ctr) & 255;
        if (k.copyD < 0) k.copyD = cd; else if (k.copyD != cd) k.copyOk = false;
    }
    // the sum check
    int s = 0;
    for (int c = 0; c + 1 < len_; c++) s += f[(size_t)c];
    if ((s & 255) == f[(size_t)len_ - 1]) ckOk_++; else ckBad_++;
    prev_ = f;
    prevCtr_ = ctr;
    n_++;
}

void Analyzer::fill(SbTelemetry& t, double rfHz) const {
    t.fields.clear(); t.info.clear();
    t.checksumOk = ckOk_; t.checksumBad = ckBad_; t.analysedFrames = n_;
    auto info = [&](const char* l, const std::string& v, const std::string& how, int conf) { SbInfo i; i.label = l; i.value = v; i.how = how; i.confidence = conf; t.info.push_back(i); };
    // satellite: from the table of published beacons, by frequency
    {
        size_t nb = 0;
        const SbKnownBeacon* kb = sbKnownBeacons(nb);
        const SbKnownBeacon* best = nullptr;
        double bd = 1e9;
        if (rfHz > 0) for (size_t i = 0; i < nb; i++) { const double d = std::fabs(kb[i].mhz * 1e6 - rfHz); if (d < bd) { bd = d; best = &kb[i]; } }
        if (best && bd < 600e3) {
            info("Satellite", fmt("%s, %.1f E", best->satellite, best->posE),
                 fmt("estimated: the nearest published Ku beacon is %.1f MHz%s%s (%.0f kHz from the measured %.3f MHz; %s, %s). The radio's and the LNB's frequency errors alone can be this large, so this is a guess.",
                     best->mhz, best->pol[0] ? ", " : "", best->pol, bd / 1e3, rfHz / 1e6, best->confirmed ? "operator or regulator figure" : "community list, unconfirmed", best->source), 1);
            info("Orbital slot", fmt("%.1f E", best->posE), "estimated: from the satellite above", 1);
        } else {
            info("Satellite", "not identified", rfHz > 0 ? "no published Ku beacon within 600 kHz of the measured frequency (table of 87 beacons, 110 to 180 E); the frames carry no name that could be read" : "the radio's frequency is not known", 0);
        }
        if (rfHz > 0) info("Beacon frequency", fmt("%.4f MHz", rfHz / 1e6), "measured: the radio's centre plus the carrier's offset", 2);
    }
    if (ccsds_ && cc_.haveFormat && cc_.inner) {
        char b[256];
        info("Frames", fmt("%llu CCSDS codeblocks of %d bytes, %.4f s each", (unsigned long long)cc_.blocksSeen, (int)len_, frameSec_), "the attached sync marker 1ACFFC1D starts every one", 2);
        info("CCSDS layout", cc_.format.describe() + fmt(", transfer frames of %zu bytes", cc_.format.frameBytes()), "found from the first blocks: every Reed-Solomon word decodes and the frames carry a transfer frame header", 2);
        info("Frame health", fmt("%llu decoded, %llu failed, %llu symbols corrected; frame CRC %llu good, %llu bad", (unsigned long long)cc_.decoded, (unsigned long long)cc_.failed, (unsigned long long)cc_.corrected, (unsigned long long)cc_.crcOk, (unsigned long long)cc_.crcBad), "Reed-Solomon decoding and the frame error control field", 2);
        if (cc_.last.version >= 0) {
            snprintf(b, sizeof b, "%s, spacecraft 0x%03X, virtual channel %d, master channel count %d, virtual channel count %u%s", cc_.last.kind(), cc_.last.scid, cc_.last.vcid, cc_.last.mcCount, (unsigned)cc_.last.vcCount, cc_.last.idle() ? ", idle data" : "");
            info("Transfer frame", b, "the header of the last decoded transfer frame (CCSDS 132.0-B / 732.0-B)", 2);
            info("Spacecraft identifier", fmt("0x%03X (%d)", cc_.last.scid, cc_.last.scid), "decoded from the transfer frame header; the assignment of identifiers to spacecraft is published by the SANA registry, not looked up here", 2);
        }
        if (cc_.lost) info("Lost frames", fmt("%llu by the virtual channel counters", (unsigned long long)cc_.lost), "gaps in the virtual channel frame count", 2);
        SbTelemetry inner;
        cc_.inner->fill(inner, 0);
        t.fields = inner.fields;
        for (auto& f : t.fields) f.name = "TM " + f.name;
        for (const auto& i : inner.info) if (i.label == "Date and time of day" || i.label == "Orbit, position, ephemeris" || i.label == "Transponders and channels" || i.label == "Text" || i.label == "On-board clock") t.info.push_back(i);
        t.checksumOk = cc_.crcOk; t.checksumBad = cc_.crcBad;
        return;
    }
    if (n_ < 8) { info("Frames", fmt("%llu analysed", (unsigned long long)n_), "more frames are needed", 0); return; }
    const int L = len_;
    const double major = mod_ > 1 ? mod_ * frameSec_ : 0;
    info("Frames", fmt("%llu of %d bytes, %.4f s each%s", (unsigned long long)n_, L, frameSec_, major > 0 ? fmt(", a major frame of %d frames = %.2f s", mod_, major).c_str() : ""), "measured from the bit rate and the frame length", 2);
    // classify every column
    enum Kind { Const, Fill, Counter, CtrCopy, Clock, Cmut, Sample, Check, Text };
    std::vector<int> kind((size_t)L, Sample);
    std::vector<double> clockHz((size_t)L, 0.0);
    int clockCol = -1;
    const bool sumOk = ckOk_ + ckBad_ > 0 && ckOk_ >= 0.95 * (double)(ckOk_ + ckBad_);
    for (int c = 0; c < L; c++) {
        const Col& k = col_[(size_t)c];
        if (c == ctrByte_) { kind[(size_t)c] = Counter; continue; }
        if (c == L - 1 && sumOk) { kind[(size_t)c] = Check; continue; }
        if (k.mn == k.mx) { kind[(size_t)c] = (k.mn == 0xFB || k.mn == 0xFC) ? Fill : Const; continue; }
        if (ctrByte_ >= 0 && k.copyOk) { kind[(size_t)c] = CtrCopy; continue; }
        const uint64_t st = k.steps01 + k.stepsOther;
        if (st > 0 && k.stepsOther == 0 && k.stepSum >= 8 && k.distinct >= 8) {   // only ever stays or goes up by one: a clock
            const double hz = (double)k.stepSum / ((double)st * frameSec_);
            if (frameSec_ > 0) { kind[(size_t)c] = Clock; clockHz[(size_t)c] = hz; if (clockCol < 0) clockCol = c; continue; }
        }
        // a word that depends on the position in the major frame: within a position it changes by at most 2 counts
        if (mod_ > 1) {
            int good = 0, used = 0;
            for (const Pos& q : k.pos) if (q.n >= 2) { used++; if (q.mx - q.mn <= 2) good++; }
            if (used >= 4 && good >= 0.9 * used) { kind[(size_t)c] = Cmut; continue; }
        }
        kind[(size_t)c] = Sample;
    }
    // printable text: a run of constant printable letters
    for (int c = 0; c < L;) {
        int e = c;
        while (e < L && kind[(size_t)e] == Const && col_[(size_t)e].mn >= 0x20 && col_[(size_t)e].mn < 0x7F) e++;
        if (e - c >= 6) for (int j = c; j < e; j++) kind[(size_t)j] = Text;
        c = std::max(e, c + 1);
    }
    auto addField = [&](const std::string& name, const std::string& val, const std::string& unit, const std::string& raw, const std::string& how, int a, int b, int conf) {
        SbField f; f.name = name; f.value = val; f.unit = unit; f.raw = raw; f.how = how; f.firstByte = a; f.lastByte = b; f.confidence = conf;
        t.fields.push_back(f);
    };
    const uint8_t* last = prev_.data();
    int textRuns = 0;
    for (int c = 0; c < L;) {
        const int kd = kind[(size_t)c];
        // runs of the same kind (for constants, only while the neighbours are constants too)
        int e = c + 1;
        if (kd == Const || kd == Fill || kd == Text) while (e < L && kind[(size_t)e] == kd && (kd != Fill || col_[(size_t)e].mn == col_[(size_t)c].mn)) e++;
        const std::string pos = e - c == 1 ? fmt("byte %d", c) : fmt("bytes %d-%d", c, e - 1);
        switch (kd) {
        case Const:
            if (c == 0 && e >= 3) {
                addField("Sync word", hx(last, 3), "", hx(last, 3), "decoded: the 24-bit IRIG 106 minor frame sync word FAF320 when it is that", c, 2, hx(last, 3) == "FAF320" ? 2 : 0);
                if (e > 3) addField("Frame type", hx(last + 3, (size_t)e - 3), "", hx(last + 3, (size_t)e - 3), "constant in every frame; the meaning is the maker's", 3, e - 1, 0);
            } else addField("Constant", hx(last + c, (size_t)(e - c)), "", hx(last + c, (size_t)(e - c)), "the same in every frame (an identification or configuration word: meaning unknown)", c, e - 1, 0);
            break;
        case Fill: addField("Idle fill", fmt("%02X x%d", last[c], e - c), "", hx(last + c, (size_t)(e - c)), "the same value in every frame, FB or FC: padding", c, e - 1, 0); break;
        case Text: {
            std::string s(prev_.begin() + c, prev_.begin() + e);
            addField("Text", s, "", hx(last + c, (size_t)(e - c)), "printable and constant: ASCII text", c, e - 1, 1);
            textRuns++;
            break;
        }
        case Counter:
            addField("Minor frame counter", fmt("%d", last[c]), "", hx(last + c, 1), fmt("decoded: counts up by one every frame, 0 to %d (a major frame of %d frames)", mod_ - 1, mod_), c, c, 2);
            break;
        case CtrCopy:
            addField("Copy of the counter", fmt("%d", last[c]), "", hx(last + c, 1), fmt("equals the frame counter %s%d in every frame", col_[(size_t)c].copyD ? "plus " : "", col_[(size_t)c].copyD), c, c, 1);
            break;
        case Clock: {
            const double hz = clockHz[(size_t)c];
            const bool sec = std::fabs(hz - 1.0) < 0.03;
            addField(sec ? "Clock, seconds counter" : "Slow counter", fmt("%d", last[c]), sec ? "s (8 bit, wraps at 256)" : "counts", hx(last + c, 1),
                     fmt("estimated: goes up by one at %.4f per second and never otherwise%s", hz, sec ? ", so it counts seconds (the start value is not a date)" : ""), c, c, 1);
            break;
        }
        case Check:
            addField("Checksum", hx(last + c, 1), "", hx(last + c, 1), fmt("decoded: the sum of the other %d bytes modulo 256; true in %llu of %llu frames", L - 1, (unsigned long long)ckOk_, (unsigned long long)(ckOk_ + ckBad_)), c, c, 2);
            break;
        case Cmut: {
            // the values by position of the first column of the run
            std::string vals;
            const Col& k = col_[(size_t)c];
            for (size_t p = 0; p < k.pos.size() && p < 32; p++) { if (p) vals += ' '; vals += k.pos[p].n ? hx(&k.pos[p].last, 1) : "--"; }
            addField("Commutated word", e - c == 1 ? vals : fmt("%d words", e - c), "", e - c == 1 ? vals : "", fmt("depends on the frame's position in the major frame (a value per counter step); the %s shown by position", e - c == 1 ? "values" : "table is in the CSV"), c, e - 1, 0);
            break;
        }
        default: {
            const Col& k = col_[(size_t)c];
            const double mean = k.sum / (double)n_;
            addField("Sample", fmt("%d (last), mean %.1f, range %d to %d", last[c], mean, k.mn, k.mx), "raw counts", hx(last + c, 1), "changes from frame to frame: a measured value or a variable word; units are the maker's", c, c, 0);
            break;
        }
        }
        c = e;
    }
    // summary lines
    info("Frame format", sumOk || ctrByte_ >= 0 ? fmt("%d bytes%s%s", L, ctrByte_ >= 0 ? fmt(", counter in byte %d (0 to %d)", ctrByte_, mod_ - 1).c_str() : "", sumOk ? ", checksum in the last byte" : "") : fmt("%d bytes", L), "learned from the frames", 2);
    if (ckOk_ + ckBad_ > 0) info("Frame health", fmt("%llu of %llu frames pass the checksum", (unsigned long long)ckOk_, (unsigned long long)(ckOk_ + ckBad_)), sumOk ? "the last byte is the sum of the others" : "no checksum found", sumOk ? 2 : 0);
    if (clockCol >= 0) {
        const bool sec = std::fabs(clockHz[(size_t)clockCol] - 1.0) < 0.03;
        info("On-board clock", fmt("%d s counter (byte %d), running for %.0f s here", prev_[(size_t)clockCol], clockCol, n_ * frameSec_), sec ? "estimated: byte counts at 1.000 per second and never otherwise; it wraps at 256 and carries no date" : "estimated: a slow counter", 1);
    }
    info("Date and time of day", "not found", "no word counts like a calendar or a time of day; only the counter above", 0);
    info("Orbit, position, ephemeris", "not found", "no group of words that moves smoothly like an orbit; the frames carry raw spacecraft telemetry", 0);
    info("Transponders and channels", "not found", "no frequency or channel table (no words matching a transponder plan)", 0);
    info("Text", textRuns ? "found" : "none", textRuns ? "see the table" : "no run of constant printable characters", textRuns ? 1 : 0);
}

} // namespace sb

// ---------------------------------------------------------------- export

namespace {
std::string csvq(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += "\"\""; else o += c; }
    return o + "\"";
}
std::string jsq(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o + "\"";
}
const char* confName(int c) { return c >= 2 ? "decoded" : (c == 1 ? "estimated" : "raw"); }
}

std::string sbExportCsv(const SbTelemetry& t) {
    std::string o = "section,name,value,unit,raw_hex,first_byte,last_byte,confidence,how\n";
    for (const auto& i : t.info) o += "summary," + csvq(i.label) + "," + csvq(i.value) + ",,,,," + confName(i.confidence) + "," + csvq(i.how) + "\n";
    for (const auto& f : t.fields) o += "field," + csvq(f.name) + "," + csvq(f.value) + "," + csvq(f.unit) + "," + f.raw + "," + std::to_string(f.firstByte) + "," + std::to_string(f.lastByte) + "," + confName(f.confidence) + "," + csvq(f.how) + "\n";
    for (const auto& fr : t.frames) {
        std::string h;
        char b[4];
        for (uint8_t x : fr.bytes) { snprintf(b, sizeof b, "%02X", x); h += b; }
        o += "frame,frame " + std::to_string(fr.id) + "," + (fr.counter >= 0 ? std::to_string(fr.counter) : "") + ",," + h + ",0," + std::to_string(fr.bytes.size() ? fr.bytes.size() - 1 : 0) + ",decoded,\n";
    }
    return o;
}

std::string sbExportJson(const SbTelemetry& t) {
    char b[160];
    std::string o = "{\n  \"carrier\": {";
    snprintf(b, sizeof b, "\"offset_hz\": %.2f, \"cn0_dbhz\": %.1f, \"level_dbfs\": %.1f, \"drift_hz_per_min\": %.2f", t.cfoHz, (double)t.snrDb, (double)t.levelDb, t.driftHzPerMin);
    o += b;
    if (t.centreHz > 0) { snprintf(b, sizeof b, ", \"frequency_hz\": %.1f", t.centreHz + t.cfoHz); o += b; }
    snprintf(b, sizeof b, "},\n  \"telemetry\": {\"subcarrier_hz\": %.1f, \"bit_rate\": %.3f, \"coding\": \"%s\", \"frame_bits\": %d, \"sync\": ", t.subcarrierHz, t.baud, t.coding ? "bi-phase" : "NRZ", t.frameBits);
    o += b + jsq(t.syncName) + ", \"frames\": " + std::to_string(t.framesTotal) + ", \"checksum_ok\": " + std::to_string(t.checksumOk) + ", \"checksum_bad\": " + std::to_string(t.checksumBad) + "},\n  \"summary\": [";
    for (size_t i = 0; i < t.info.size(); i++) o += std::string(i ? "," : "") + "\n    {\"label\": " + jsq(t.info[i].label) + ", \"value\": " + jsq(t.info[i].value) + ", \"confidence\": \"" + confName(t.info[i].confidence) + "\", \"how\": " + jsq(t.info[i].how) + "}";
    o += "\n  ],\n  \"fields\": [";
    for (size_t i = 0; i < t.fields.size(); i++) {
        const auto& f = t.fields[i];
        o += std::string(i ? "," : "") + "\n    {\"name\": " + jsq(f.name) + ", \"value\": " + jsq(f.value) + ", \"unit\": " + jsq(f.unit) + ", \"raw\": \"" + f.raw + "\", \"first_byte\": " + std::to_string(f.firstByte) + ", \"last_byte\": " + std::to_string(f.lastByte) + ", \"confidence\": \"" + confName(f.confidence) + "\", \"how\": " + jsq(f.how) + "}";
    }
    o += "\n  ],\n  \"frames\": [";
    for (size_t i = 0; i < t.frames.size(); i++) {
        std::string h;
        for (uint8_t x : t.frames[i].bytes) { snprintf(b, sizeof b, "%02X", x); h += b; }
        o += std::string(i ? "," : "") + "\n    {\"id\": " + std::to_string(t.frames[i].id) + ", \"counter\": " + std::to_string(t.frames[i].counter) + ", \"hex\": \"" + h + "\"}";
    }
    o += "\n  ]\n}\n";
    return o;
}

} // namespace dect2
