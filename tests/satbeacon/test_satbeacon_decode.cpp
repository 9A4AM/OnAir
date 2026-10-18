// What the frames of a beacon carry: the golden decode of 64 frames of the recorded beacon (the clock, the counter, the checksum, the constant, commutated and changing
// words, nothing invented: no date, orbit or text), the estimate of the satellite from the measured frequency, the export, and the generator's frames.
#include "dect2/satbeacon/satbeacon_decode.h"
#include "dect2/satbeacon/satbeacon_gen.h"
#include "real_frames.h"
#include <cstdio>
#include <cstring>
#include <string>
using namespace dect2;
#include "../common/test_util.h"

static std::vector<uint8_t> unhex(const char* s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; s[i] && s[i + 1]; i += 2) { unsigned x; sscanf(s + i, "%2x", &x); v.push_back((uint8_t)x); }
    return v;
}
static const SbField* field(const SbTelemetry& t, const char* name, int firstByte) {
    for (const auto& f : t.fields) if (f.name == name && f.firstByte == firstByte) return &f;
    return nullptr;
}
static const SbInfo* info(const SbTelemetry& t, const char* label) {
    for (const auto& i : t.info) if (i.label == label) return &i;
    return nullptr;
}

int main() {
    const double frameSec = 2048.0 / 4800.0;
    // the recorded frames
    {
        sb::Analyzer a;
        for (int i = 0; i < 64; i++) a.add(unhex(kSbRealFrames[i]), 4, 32, frameSec);
        SbTelemetry t;
        a.fill(t, 11705.4861e6);
        CHECK(t.analysedFrames == 64 && t.checksumOk == 64 && t.checksumBad == 0, "checksum holds for %llu of 64", (unsigned long long)t.checksumOk);
        const SbField* f = field(t, "Sync word", 0);
        CHECK(f && f->value == "FAF320" && f->confidence == 2, "sync word");
        f = field(t, "Minor frame counter", 4);
        CHECK(f && f->confidence == 2 && f->how.find("0 to 31") != std::string::npos, "counter");
        f = field(t, "Clock, seconds counter", 15);
        CHECK(f && f->confidence == 1 && f->how.find("counts seconds") != std::string::npos, "the seconds counter in byte 15: %s", f ? f->how.c_str() : "missing");
        f = field(t, "Copy of the counter", 122);
        CHECK(f != nullptr, "byte 122 copies the counter");
        f = field(t, "Checksum", 255);
        CHECK(f && f->confidence == 2, "checksum field");
        f = field(t, "Constant", 5);
        CHECK(f && f->value == "00001EA32B" && f->confidence == 0, "constant block bytes 5-9");
        f = field(t, "Idle fill", 59);
        CHECK(f && f->value.find("FB") == 0, "idle fill");
        f = field(t, "Commutated word", 10);
        CHECK(f && f->value.rfind("80 08 66 80", 0) == 0, "commutated word byte 10: %s", f ? f->value.c_str() : "missing");
        f = field(t, "Sample", 200);
        CHECK(f && f->confidence == 0 && f->unit == "raw counts", "sample byte 200");
        // nothing invented
        CHECK(info(t, "Date and time of day") && info(t, "Date and time of day")->value == "not found", "no date");
        CHECK(info(t, "Orbit, position, ephemeris") && info(t, "Orbit, position, ephemeris")->value == "not found", "no orbit");
        CHECK(info(t, "Transponders and channels") && info(t, "Transponders and channels")->value == "not found", "no transponders");
        CHECK(info(t, "Text") && info(t, "Text")->value == "none", "no text");
        const SbInfo* s = info(t, "Satellite");
        CHECK(s && s->confidence == 1 && s->value.find("BSAT-4b") == 0 && s->how.rfind("estimated", 0) == 0, "satellite estimated from the frequency: %s", s ? s->value.c_str() : "missing");
        CHECK(info(t, "Frame health") && info(t, "Frame health")->value.find("64 of 64") == 0, "frame health");
        const std::string csv = sbExportCsv(t), js = sbExportJson(t);
        CHECK(csv.find("field,\"Sync word\",\"FAF320\"") != std::string::npos, "csv has the sync word");
        CHECK(js.find("\"label\": \"Satellite\"") != std::string::npos && js.find("\"confidence\": \"estimated\"") != std::string::npos, "json has the estimate");
        printf("  %zu fields, %zu summary lines\n", t.fields.size(), t.info.size());
    }
    // no frequency: no satellite is named
    {
        sb::Analyzer a;
        for (int i = 0; i < 64; i++) a.add(unhex(kSbRealFrames[i]), 4, 32, frameSec);
        SbTelemetry t;
        a.fill(t, 0);
        CHECK(info(t, "Satellite") && info(t, "Satellite")->value == "not identified", "without a frequency no satellite");
        a.fill(t, 9000e6);
        CHECK(info(t, "Satellite") && info(t, "Satellite")->value == "not identified", "no beacon near 9 GHz");
    }
    // the generator's frames carry what the decoder finds
    {
        sb::Analyzer a;
        for (uint32_t n = 0; n < 96; n++) a.add(satbeaconFrameBytes(1, n), 4, 32, frameSec);
        SbTelemetry t;
        a.fill(t, 0);
        CHECK(t.checksumOk == 96, "generator checksum");
        const SbField* f = field(t, "Clock, seconds counter", 15);
        CHECK(f && f->how.find("counts seconds") != std::string::npos, "generator clock: %s", f ? f->how.c_str() : "missing");
        CHECK(field(t, "Copy of the counter", 122) != nullptr, "generator counter copy");
        CHECK(field(t, "Sync word", 0) != nullptr, "generator sync");
    }
    // too few frames: no fields, said so
    {
        sb::Analyzer a;
        for (int i = 0; i < 3; i++) a.add(unhex(kSbRealFrames[i]), 4, 32, frameSec);
        SbTelemetry t;
        a.fill(t, 0);
        CHECK(t.fields.empty(), "no fields from three frames");
    }
    return testSummary("satbeacon decode");
}
