// PSK31 receiver against generated signals: four BPSK31 signals across the passband at once (one drifting, one with a clock error),
// a tuning error, two signals 70 Hz apart, a weak signal in noise, a fast drift and a large clock error, PSK63, QPSK31 and QPSK63,
// a picked frequency (the focus), the chat log lines, and noise only (no text).
#include "dect2/psk_gen.h"
#include "dect2/psk_rx.h"
#include "dect2/psk_tel.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace dect2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

constexpr double kRate = 250000;
constexpr double kOff = -20000;   // the dial sits 20 kHz below the centre, as with the radio

static PskTelemetry run(const std::vector<PskGenSignal>& sigs, double secs, int mode = 0, double cfo = 0, double focusHz = 1e9, uint32_t seed = 3) {
    PskGenConfig c;
    c.rate = kRate; c.dialOffsetHz = kOff; c.cfoHz = cfo; c.signals = sigs; c.seed = seed;
    PskGenerator g(c);
    PskReceiver r;
    r.configure(kRate);
    r.setSignalOffset(kOff);
    r.setMode(mode);
    PskTelemetry res;
    std::vector<cf32> x(25000);
    uint64_t seq = 0;
    for (double t = 0; t < secs; t += 0.1) {
        g.generate(x.data(), x.size());
        if (focusHz < 1e8 && t > 0.5 && t < 0.65) r.setFocusHz(focusHz);
        r.feed(x.data(), x.size());
        PskTelemetry tt;
        if (r.telemetry(tt, seq)) { seq = tt.seq; res = std::move(tt); }
    }
    return res;
}

static PskGenSignal sig(double hz, const std::string& text, double snr, int mode = 0) {
    PskGenSignal s;
    s.offsetHz = hz; s.text = text; s.snrDb = snr; s.mode = mode; s.repeat = false;
    return s;
}

static const PskSignal* near(const PskTelemetry& t, double hz, double tol = 20) {
    const PskSignal* b = nullptr;
    for (const auto& s : t.signals) if (std::fabs(s.offsetHz - hz) <= tol && (!b || s.chars > b->chars)) b = &s;
    return b;
}

static void expect(const char* name, const PskTelemetry& t, double hz, const std::string& want, double tol = 20) {
    const PskSignal* s = near(t, hz, tol);
    CHECK(s, "%s: no signal at %.0f Hz (%zu signals)", name, hz, t.signals.size());
    if (!s) return;
    const bool ok = s->text.find(want) != std::string::npos;
    printf("  %-14s %7.1f Hz %5.1f dB afc %+5.1f %s \"%s\"\n", name, s->offsetHz, s->snrDb, s->afcHz, ok ? "ok  " : "MISS", s->text.c_str());
    CHECK(ok, "%s: \"%s\" not in \"%s\"", name, want.c_str(), s->text.c_str());
}

int main() {
    {   // the band: four signals, one drifting, one with a clock error
        std::vector<PskGenSignal> v = {
            sig(700, "CQ CQ CQ DE LY2ABC LY2ABC KO24 PSE K\n", 14),
            sig(1050, "DL1XYZ DE G4ABC UR RST 599 NAME JOHN\n", 12),
            sig(1500, "CQ TEST DE OK1XX OK1XX TEST\n", 11),
            sig(2200, "SP5AB DE EA3XYZ TNX FER QSO 73 SK\n", 13)};
        v[1].driftHzPerMin = 4; v[1].startSec = 1.2;
        v[2].clockPpm = 100; v[2].startSec = 0.7;
        v[3].startSec = 2.0;
        const PskTelemetry t = run(v, 17);
        expect("band 700", t, 700, "DE LY2ABC LY2ABC KO24");
        expect("band 1050", t, 1050, "UR RST 599 NAME JOHN");
        expect("band 1500", t, 1500, "DE OK1XX OK1XX");
        expect("band 2200", t, 2200, "TNX FER QSO 73");
        const PskSignal* s = near(t, 700);
        if (s) {
            CHECK(!s->lines.empty() && s->lines.front().text.find("CQ CQ CQ") != std::string::npos, "chat log line: %s", s->lines.empty() ? "-" : s->lines.front().text.c_str());
            bool call = false;
            for (const auto& c : s->calls) if (c == "LY2ABC") call = true;
            CHECK(call, "callsign LY2ABC found");
        }
        CHECK(t.state == 2 || t.state == 1, "state %d", t.state);
        CHECK(t.blocksOk > 100, "chars %llu", (unsigned long long)t.blocksOk);
        CHECK(!t.spectrum.empty(), "spectrum");
    }
    {   // a tuning error of +37 Hz: everything moves, AFC follows
        const PskTelemetry t = run({sig(1200, "LY2ABC DE DL1XYZ GM ES TNX\n", 12)}, 10, 0, 37);
        expect("cfo +37", t, 1237, "LY2ABC DE DL1XYZ");
    }
    {   // two signals 70 Hz apart
        const PskTelemetry t = run({sig(1000, "QRL? DE F5ABC F5ABC\n", 14), sig(1070, "UR 599 599 BK DE I2XYZ\n", 14)}, 10);
        expect("close 1000", t, 1000, "DE F5ABC");
        expect("close 1070", t, 1070, "599 BK DE I2XYZ");
    }
    {   // weak: 9 dB in the 31 Hz bandwidth (-11 dB in 3 kHz)
        const PskTelemetry t = run({sig(1400, "CQ CQ DE LY3QRP LY3QRP K\n", 9)}, 12, 0, 0, 1e9, 9);
        expect("weak", t, 1400, "LY3QRP");
    }
    {   // fast drift (12 Hz a minute) and a 300 ppm symbol clock
        PskGenSignal s = sig(1800, "DRIFTY DE OH2XX OH2XX TEST TEST\n", 13);
        s.driftHzPerMin = 12; s.clockPpm = 300;
        const PskTelemetry t = run({s}, 13);
        expect("drift + clock", t, 1800 + 12.0 * 6.5 / 60, "DE OH2XX OH2XX", 25);
    }
    {   // PSK63
        const PskTelemetry t = run({sig(900, "PSK63 TEST DE HB9ABC HB9ABC QRZ?\n", 13, 1), sig(2000, "CQ DE YU1XYZ YU1XYZ K\n", 13, 1)}, 8, 1);
        expect("psk63 900", t, 900, "DE HB9ABC HB9ABC");
        expect("psk63 2000", t, 2000, "CQ DE YU1XYZ");
        CHECK(t.mode == 1, "mode %d", t.mode);
    }
    {   // QPSK31 and QPSK63 (the K = 5 code)
        const PskTelemetry t = run({sig(1300, "QPSK31 DE ES1ABC ES1ABC PSE K\n", 15, 2)}, 13, 2);
        expect("qpsk31", t, 1300, "DE ES1ABC ES1ABC");
        const PskTelemetry t2 = run({sig(1600, "QPSK63 DE YL2XYZ YL2XYZ PSE K\n", 16, 3)}, 8, 3);
        expect("qpsk63", t2, 1600, "DE YL2XYZ YL2XYZ");
    }
    {   // the focus: a picked frequency becomes the signal of the big view
        const PskTelemetry t = run({sig(800, "AAA DE SM5ABC\n", 14), sig(2500, "BBB DE LA1XYZ LA1XYZ\n", 14)}, 9, 0, 0, 2504);
        const PskSignal* f = pskFocus(t);
        CHECK(f && std::fabs(f->offsetHz - 2500) < 15, "focus %.0f", f ? f->offsetHz : -1);
        if (f) CHECK(f->text.find("LA1XYZ") != std::string::npos, "focus text \"%s\"", f->text.c_str());
    }
    {   // noise only: no signals that make text
        const PskTelemetry t = run({}, 12);
        uint64_t chars = 0;
        for (const auto& s : t.signals) chars += s.chars;
        printf("  noise only: %zu signals, %llu characters\n", t.signals.size(), (unsigned long long)chars);
        CHECK(chars < 3, "noise made %llu characters", (unsigned long long)chars);
    }
    printf(fails ? "psk rx: %d FAILED\n" : "psk rx: all passed\n", fails);
    return fails ? 1 : 0;
}
