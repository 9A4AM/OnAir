// Live transcription without a speech engine: the callsign reader (phonetic and telephony cases), the segmenter, the transcriber with a fake
// backend (queue overflow, hallucination filters, linking), the store (search, filters, exports, file and retention) and the model list.
#include "dect2/transcribe/callsign.h"
#include "dect2/transcribe/models.h"
#include "dect2/transcribe/segmenter.h"
#include "dect2/transcribe/transcriber.h"
#include "dect2/transcribe/transcript.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

using namespace dect2;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static std::string firstKey(const std::string& t) {
    const auto h = findCallsigns(t);
    return h.empty() ? std::string() : h[0].key;
}

static void callsigns() {
    CHECK(firstKey("Lufthansa one two three alpha bravo, descend flight level eight zero") == "DLH123AB", "got %s", firstKey("Lufthansa one two three alpha bravo").c_str());
    CHECK(firstKey("Lufthansa 123") == "DLH123", "digits");
    CHECK(firstKey("Hello, this is Lufthansa 1-2-3.") == "DLH123", "hyphenated digits");
    CHECK(firstKey("lufthansa one two tree") == "DLH123", "tree");
    CHECK(firstKey("Speedbird 4 7 2, contact tower") == "BAW472", "speedbird");
    CHECK(firstKey("Speedbird niner fife") == "BAW95", "niner fife");
    CHECK(firstKey("Wizz Air four zero one") == "WZZ401", "two word name");
    CHECK(firstKey("Emirates two zero one heavy") == "UAE201", "heavy");
    CHECK(firstKey("Ryanair twenty three") == "RYR23", "tens");
    CHECK(firstKey("Ryanair zero zero seven") == "RYR7", "leading zeros");
    CHECK(firstKey("DLH123 cleared to land") == "DLH123", "icao glued");
    CHECK(firstKey("DLH 123 cleared") == "DLH123", "icao spaced");
    CHECK(firstKey("Delta one two three") == "DAL123", "delta flight");
    auto h = findCallsigns("november one two three alpha bravo cleared for takeoff");
    CHECK(h.size() == 1 && h[0].kind == CallsignHit::Reg && h[0].key == "N123AB", "n-number: %s", h.empty() ? "none" : h[0].key.c_str());
    h = findCallsigns("November 4 5 6 Charlie, wind calm");
    CHECK(h.size() == 1 && h[0].key == "N456C", "n-number digits: %s", h.empty() ? "none" : h[0].key.c_str());
    h = findCallsigns("N123AB cleared to land");
    CHECK(h.size() == 1 && h[0].key == "N123AB", "n-number written");
    h = findCallsigns("hotel bravo alpha lima charlie, taxi to holding point");
    CHECK(h.size() == 1 && h[0].kind == CallsignHit::Reg && h[0].key == "HBALC", "hb reg: %s", h.empty() ? "none" : h[0].key.c_str());
    h = findCallsigns("taxi via alpha bravo charlie, hold short runway two five left");
    CHECK(h.empty(), "taxiways are not callsigns (%zu)", h.size());
    h = findCallsigns("Lufthansa one two three, Speedbird four five six, behind");
    CHECK(h.size() == 2 && h[0].key == "DLH123" && h[1].key == "BAW456", "two callsigns");
    CHECK(findCallsigns("a lot of traffic today").empty(), "'a lot' is not LOT");
    CHECK(findCallsigns("squawk four five six seven").empty(), "squawk is no callsign");
    CHECK(telephonyToIcao("speedbird") == "BAW" && telephonyToIcao("nothing") == "", "table");
    CHECK(telephonyCount() > 100, "table size %zu", telephonyCount());
    CHECK(guessSpeaker(findCallsigns("Lufthansa one two three, descend flight level eight zero")) == "ATC", "atc");
    CHECK(guessSpeaker(findCallsigns("Descending flight level eight zero, Lufthansa one two three")) == "Aircraft", "aircraft");
}

// 8 kHz audio: `sec` seconds of a speech-like tone (changing pitch, 4 Hz amplitude changes) or silence, plus a little noise
static std::vector<float> audio(double sec, float amp, unsigned seed = 1, int rate = 8000) {
    std::vector<float> v((size_t)(sec * rate));
    std::mt19937 g(seed);
    std::normal_distribution<float> nd(0.f, 1.f);
    for (size_t i = 0; i < v.size(); i++) {
        const double t = (double)i / rate;
        const double env = 0.55 + 0.45 * std::sin(2 * M_PI * 4 * t);
        v[i] = amp * (float)(env * (std::sin(2 * M_PI * (180 + 40 * std::sin(2 * M_PI * 0.7 * t)) * t) + 0.5 * std::sin(2 * M_PI * 1200 * t))) + 0.0005f * nd(g);
    }
    return v;
}

static void segmenter() {
    VoiceSegmenter s(8000);
    std::vector<VoiceSegment> got;
    auto out = [&](VoiceSegment&& g) { got.push_back(std::move(g)); };
    int64_t now = 1000000;
    auto feed = [&](const std::vector<float>& a) {
        for (size_t i = 0; i < a.size(); i += 160) {
            const size_t n = std::min<size_t>(160, a.size() - i);
            now += 20;
            s.push(a.data() + i, n, now, out);
        }
    };
    feed(audio(2.0, 0.f));                       // quiet
    CHECK(got.empty() && !s.active(), "silence is no transmission");
    feed(audio(1.5, 0.3f));                      // a transmission of 1.5 s
    CHECK(s.active() && got.empty(), "going on");
    feed(audio(2.0, 0.f));                       // hang passes
    CHECK(got.size() == 1 && !s.active(), "one transmission (%zu)", got.size());
    if (!got.empty()) {
        const double sec = got[0].pcm16k.size() / 16000.0;
        CHECK(sec > 1.6 && sec < 2.6, "16 kHz length %.2f s (pre-roll and tail included)", sec);
        CHECK(got[0].durSec > 1.3 && got[0].durSec < 1.7, "voiced %.2f s", got[0].durSec);
        double p = 0;
        for (float x : got[0].pcm16k) p += x * x;
        const double db = 10 * std::log10(p / got[0].pcm16k.size() + 1e-12);
        CHECK(db > -30 && db < -12, "normalised level %.1f dBFS", db);
        CHECK(got[0].startMs > 1000000 + 1500 && got[0].startMs < 1000000 + 4500, "start time %lld", (long long)(got[0].startMs - 1000000));
    }
    got.clear();
    feed(audio(0.2, 0.3f)); feed(audio(2.0, 0.f));   // a click shorter than the minimum
    CHECK(got.empty(), "a click is dropped");
    feed(audio(1.0, 0.3f)); feed(audio(0.3, 0.f)); feed(audio(1.0, 0.3f)); feed(audio(2.0, 0.f));   // a pause shorter than the hang
    CHECK(got.size() == 1, "a short pause does not split (%zu)", got.size());
    got.clear();
    feed(audio(40.0, 0.3f)); feed(audio(2.0, 0.f));
    CHECK(got.size() >= 2 && got[0].cut, "a very long transmission is cut (%zu)", got.size());
    // resampler: a 1 kHz tone at 8 kHz keeps its level at 16 kHz
    std::vector<float> tone(8000);
    for (size_t i = 0; i < tone.size(); i++) tone[i] = 0.5f * (float)std::sin(2 * M_PI * 1000 * i / 8000.0);
    const auto up = resampleMono(tone.data(), tone.size(), 8000, 16000);
    CHECK(up.size() == 16000, "upsampled length %zu", up.size());
    double p = 0;
    for (size_t i = 100; i < up.size() - 100; i++) p += up[i] * up[i];
    CHECK(std::fabs(std::sqrt(p / (up.size() - 200)) - 0.3536) < 0.01, "tone amplitude kept");
    std::vector<float> t48(48000);
    for (size_t i = 0; i < t48.size(); i++) t48[i] = 0.5f * (float)std::sin(2 * M_PI * 1000 * i / 48000.0);
    const auto dn = resampleMono(t48.data(), t48.size(), 48000, 16000);
    CHECK(dn.size() == 16000, "48k to 16k length %zu", dn.size());
}

struct Fake : SpeechBackend {
    std::atomic<int> calls{0};
    std::atomic<int> delayMs{0};
    std::string text = "Lufthansa one two three descend flight level eight zero";
    SpeechResult res;
    bool multi = false;
    std::string lang = "de", translated = "Lufthansa one two three descend to flight level eight zero";
    std::atomic<int> translateCalls{0};
    std::string lastLang;
    bool englishOnly() const override { return !multi; }
    Fake() { res.ok = true; res.confidence = 0.9f; res.avgLogprob = -0.3f; res.noSpeechProb = 0.05f; }
    SpeechResult transcribe(const float*, size_t, const SpeechOptions& o) override {
        calls++;
        lastLang = o.language;
        if (o.translate) translateCalls++;
        if (delayMs) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs.load()));
        SpeechResult r = res;
        r.text = o.translate ? translated : text;
        r.lang = lang;
        return r;
    }
};

static void filters() {
    FilterConfig c;
    SpeechResult r; r.ok = true; r.avgLogprob = -0.3f; r.noSpeechProb = 0.1f;
    auto run = [&](std::string t, SpeechResult rr = SpeechResult{}, double dur = 3.0) {
        if (rr.text.empty() && rr.avgLogprob == 0) rr = r;
        const std::string why = filterHallucination(t, rr, dur, c);
        return std::make_pair(why, t);
    };
    CHECK(run("Thank you.").first == "junk", "thank you dropped");
    CHECK(run("Thanks for watching!").first == "junk", "thanks for watching");
    CHECK(run("[BLANK_AUDIO]").first == "junk", "blank audio");
    CHECK(run(" (music) ").first == "junk", "music");
    CHECK(run("").first == "empty", "empty");
    CHECK(run("Roger, thank you.").first.empty(), "a real thank you stays");
    CHECK(run("Roger.").first.empty(), "roger stays");
    CHECK(run("one one one").first.empty(), "numbers repeated stay");
    CHECK(run("go go go go go go go go").first == "repeat", "a word looped");
    CHECK(run("I'm going to be a good one. I'm going to be a good one. I'm going to be a good one. I'm going to be a good one.").first == "repeat", "a phrase looped");
    auto p = run("Lufthansa one two three descend. Lufthansa one two three descend. Lufthansa one two three descend. Cleared to land runway two five.", SpeechResult{}, 9.0);
    CHECK(p.first.empty() && p.second.find("Cleared") != std::string::npos && p.second.size() < 90, "a loop inside text is cut: %s", p.second.c_str());
    SpeechResult nospeech = r; nospeech.noSpeechProb = 0.8f; nospeech.avgLogprob = -1.2f;
    CHECK(run("hello there", nospeech).first == "no speech", "no speech");
    SpeechResult bad = r; bad.avgLogprob = -1.8f;
    CHECK(run("hello there", bad).first == "low confidence", "low confidence");
    SpeechResult okSpeech = r; okSpeech.noSpeechProb = 0.8f; okSpeech.avgLogprob = -0.4f;
    CHECK(run("Speedbird four five six", okSpeech).first.empty(), "no-speech alone, with good words, stays");
    CHECK(run(std::string(300, 'a') + " b", r, 2.0).first == "too fast", "too many characters for the time");
}

static void transcriber() {
    TranscriptStore store;
    Transcriber tr(store);
    auto fake = std::make_shared<Fake>();
    TranscribeConfig cfg;
    cfg.enabled = true;
    cfg.queueMax = 3;
    tr.configure(cfg);
    tr.setBackend(fake);
    tr.setSynchronous(true);
    std::string seen;
    tr.setOnEntry([&](const TranscriptEntry& e) { seen = e.text; });
    tr.setResolver([](const CallsignHit& h) { return h.key == "DLH123" ? std::string("3C6444") : std::string(); });
    SourceInfo src;
    src.key = 7; src.freqHz = 118.7e6; src.mode = "airband"; src.channel = "118.700 Tower";
    int64_t now = 1800000000000LL;
    auto feedAll = [&](const std::vector<float>& a, int key = 7) {
        SourceInfo s = src; s.key = key;
        for (size_t i = 0; i < a.size(); i += 160) { now += 20; tr.feed(s, a.data() + i, std::min<size_t>(160, a.size() - i), 8000, now); }
    };
    feedAll(audio(2.0, 0.3f)); feedAll(audio(2.0, 0.f));
    TranscriberStats st = tr.stats();
    CHECK(fake->calls == 1 && st.kept == 1 && store.size() == 1, "one transmission, one entry (calls %d kept %llu)", fake->calls.load(), (unsigned long long)st.kept);
    const auto all = store.find({});
    if (!all.empty()) {
        CHECK(all[0].callsign == "DLH123" && all[0].aircraft == "3C6444" && all[0].speaker == "ATC", "linked: %s / %s / %s", all[0].callsign.c_str(), all[0].aircraft.c_str(), all[0].speaker.c_str());
        CHECK(all[0].channel == "118.700 Tower" && std::fabs(all[0].freqHz - 118.7e6) < 1 && all[0].turn == 1, "channel data");
        CHECK(all[0].wall == (1800000000000LL + 40) / 1000 - 4 || std::llabs(all[0].wall - 1800000000LL) < 10, "time %lld", (long long)all[0].wall);
    }
    CHECK(seen.find("Lufthansa") != std::string::npos, "callback");
    CHECK(store.forAircraft("DLH123", "").size() == 1 && store.forAircraft("dlh0123", "").size() == 1 && store.forAircraft("BAW1", "").empty(), "forAircraft");
    // silence and noise: nothing is sent to the engine
    const int before = fake->calls;
    feedAll(audio(6.0, 0.f, 3));
    CHECK(fake->calls == before, "silence costs nothing");
    // the engine says "Thank you." for a noisy transmission: dropped
    fake->text = "Thank you.";
    feedAll(audio(1.5, 0.3f)); feedAll(audio(2.0, 0.f));
    CHECK(store.size() == 1 && tr.stats().filtered == 1 && tr.stats().lastFilter == "junk", "junk filtered (%zu, %s)", store.size(), tr.stats().lastFilter.c_str());
    // a disabled transcriber takes nothing
    cfg.enabled = false; tr.configure(cfg);
    const int c2 = fake->calls;
    feedAll(audio(1.5, 0.3f)); feedAll(audio(2.0, 0.f));
    CHECK(fake->calls == c2, "disabled");
    // threaded: the queue is bounded and the oldest is dropped
    Transcriber t2(store);
    auto slow = std::make_shared<Fake>();
    slow->delayMs = 150;
    slow->text = "Speedbird four five six";
    cfg.enabled = true; cfg.queueMax = 2;
    t2.configure(cfg);
    t2.setBackend(slow);
    for (int k = 0; k < 8; k++) {   // eight transmissions on eight channels within a moment
        SourceInfo s = src; s.key = 100 + k; s.channel = "ch" + std::to_string(k);
        const auto a = audio(1.2, 0.3f, (unsigned)k + 5);
        const auto z = audio(1.2, 0.f);
        for (size_t i = 0; i < a.size(); i += 160) { now += 20; t2.feed(s, a.data() + i, std::min<size_t>(160, a.size() - i), 8000, now); }
        for (size_t i = 0; i < z.size(); i += 160) { now += 20; t2.feed(s, z.data() + i, std::min<size_t>(160, z.size() - i), 8000, now); }
    }
    t2.waitIdle();
    const TranscriberStats s2 = t2.stats();
    CHECK(s2.segments == 8, "8 cut (%llu)", (unsigned long long)s2.segments);
    CHECK(s2.dropped >= 4, "the oldest are dropped when the engine is slow (%llu)", (unsigned long long)s2.dropped);
    CHECK(s2.done + s2.dropped == 8, "every one done or dropped (%llu + %llu)", (unsigned long long)s2.done, (unsigned long long)s2.dropped);
    CHECK(!t2.active(), "idle at the end");
}

static void translation() {
    TranscriptStore store;
    Transcriber tr(store);
    auto fake = std::make_shared<Fake>();
    fake->multi = true;
    fake->text = "Guten Tag Lufthansa eins zwei drei Landung frei";
    TranscribeConfig cfg;
    cfg.enabled = true; cfg.language = "auto"; cfg.translate = true;
    tr.configure(cfg); tr.setBackend(fake); tr.setSynchronous(true);
    SourceInfo src; src.key = 1; src.mode = "audio"; src.channel = "test"; src.domain = "general";
    int64_t now = 1800000000000LL;
    auto feedAll = [&](const std::vector<float>& a) {
        for (size_t i = 0; i < a.size(); i += 160) { now += 20; tr.feed(src, a.data() + i, std::min<size_t>(160, a.size() - i), 8000, now); }
    };
    feedAll(audio(2.0, 0.3f)); feedAll(audio(2.0, 0.f));
    CHECK(fake->calls == 2 && fake->translateCalls == 1, "two passes (%d, %d)", fake->calls.load(), fake->translateCalls.load());
    CHECK(fake->lastLang == "de", "second pass uses the detected language (%s)", fake->lastLang.c_str());
    auto all = store.find({});
    CHECK(all.size() == 1 && all[0].lang == "de" && all[0].text.find("Guten") == 0 && all[0].translation.find("descend") != std::string::npos, "both fields stored");
    CHECK(tr.stats().translated == 1, "counted");
    if (!all.empty()) {
        TranscriptEntry back;
        CHECK(transcriptFromJson(transcriptToJson(all[0]), back) && back.translation == all[0].translation && back.lang == "de", "json round trip");
        CHECK(transcriptsSrt(all).find("[EN] Lufthansa") != std::string::npos && transcriptsSrt(all).find("Guten") != std::string::npos, "SRT has both");
        CHECK(transcriptsTxt(all).find("de > EN") != std::string::npos, "TXT has both");
        CHECK(transcriptsCsv(all).find("language,text,translation") != std::string::npos && transcriptsCsv(all).find(",de,") != std::string::npos, "CSV has both");
        TranscriptFilter f; f.query = "descend";
        CHECK(store.find(f).size() == 1, "search finds the translation");
    }
    // English speech: no second pass
    fake->lang = "en"; fake->calls = 0; fake->translateCalls = 0;
    feedAll(audio(2.0, 0.3f)); feedAll(audio(2.0, 0.f));
    CHECK(fake->calls == 1 && fake->translateCalls == 0, "English is not translated");
    // English-only model: translate is ignored
    fake->multi = false; fake->lang = "de"; fake->calls = 0;
    feedAll(audio(2.0, 0.3f)); feedAll(audio(2.0, 0.f));
    CHECK(fake->calls == 1, "English-only model: one pass");
    // performance guard
    fake->multi = true; fake->calls = 0; fake->translateCalls = 0;
    cfg.translateMaxRtf = -1; tr.configure(cfg);
    feedAll(audio(2.0, 0.3f)); feedAll(audio(2.0, 0.f));
    CHECK(fake->translateCalls == 0 && tr.stats().translateSkipped >= 1, "too slow: translation skipped");
    // translate off
    cfg.translateMaxRtf = 0.8; cfg.translate = false; tr.configure(cfg); fake->calls = 0;
    feedAll(audio(2.0, 0.3f)); feedAll(audio(2.0, 0.f));
    CHECK(fake->calls == 1, "off: one pass");
}

static void store() {
    namespace fs = std::filesystem;
    TranscriptStore s;
    const int64_t t0 = (int64_t)time(nullptr) - 600;
    auto mk = [&](int64_t dt, double f, const char* ch, const char* text, const char* cs, const char* reg = "") {
        TranscriptEntry e; e.wall = t0 + dt; e.freqHz = f; e.mode = "airband"; e.channel = ch; e.text = text; e.callsign = cs; e.reg = reg; e.durSec = 3; e.confidence = 0.8f; return e;
    };
    s.add(mk(0, 118.7e6, "118.700 Tower EYVI", "Lufthansa one two three, runway two zero cleared to land", "DLH123"));
    s.add(mk(60, 118.7e6, "118.700 Tower EYVI", "Cleared to land two zero, Lufthansa one two three", "DLH123"));
    s.add(mk(120, 121.9e6, "121.900 Ground", "Speedbird four five six, taxi to holding point, \"alpha\", line 2", "BAW456"));
    s.add(mk(180, 121.9e6, "121.900 Ground", "november one two three alpha bravo, push approved", "", "N123AB"));
    CHECK(s.size() == 4, "size");
    TranscriptFilter f;
    f.query = "cleared land";
    CHECK(s.find(f).size() == 2, "search words");
    f = {}; f.query = "SPEEDBIRD";
    CHECK(s.find(f).size() == 1, "search case");
    f = {}; f.freqHz = 121.9e6;
    CHECK(s.find(f).size() == 2, "frequency");
    f = {}; f.aircraft = "dlh123";
    CHECK(s.find(f).size() == 2, "aircraft by callsign");
    f = {}; f.aircraft = "N123AB";
    CHECK(s.find(f).size() == 1, "aircraft by registration");
    f = {}; f.airport = "eyvi";
    CHECK(s.find(f).size() == 2, "airport");
    f = {}; f.from = t0 + 100;
    CHECK(s.find(f).size() == 2, "time range");
    CHECK(s.find({}, 1).size() == 1 && s.find({}, 1)[0].callsign.empty(), "newest first, limit");
    const auto v = s.find({});
    const std::string txt = transcriptsTxt(v), srt = transcriptsSrt(v), js = transcriptsJson(v), csv = transcriptsCsv(v);
    CHECK(txt.find("Z  118.700 Tower EYVI  ATC") == std::string::npos && txt.find("118.700 Tower EYVI  DLH123\nLufthansa") != std::string::npos, "txt: %s", txt.substr(0, 120).c_str());
    CHECK(srt.rfind("1\n00:00:00,000 --> 00:00:03,000\n[118.700 Tower EYVI] Lufthansa", 0) == 0, "srt first cue: %s", srt.substr(0, 90).c_str());
    CHECK(srt.find("2\n00:01:00,000 --> 00:01:03,000") != std::string::npos && srt.find("4\n00:03:00,000") != std::string::npos, "srt offsets");
    CHECK(js.front() == '[' && js.find("\\\"alpha\\\"") != std::string::npos, "json escapes");
    CHECK(csv.rfind("time_utc,frequency_mhz", 0) == 0 && csv.find("\"Speedbird four five six, taxi to holding point, \"\"alpha\"\", line 2\"") != std::string::npos, "csv quoting");
    // file: persistence, reload, retention
    const fs::path dir = fs::temp_directory_path() / "onair_transcribe_test";
    fs::remove_all(dir);
    const std::string path = (dir / "t.jsonl").string();
    {
        TranscriptStore a;
        std::string err;
        CHECK(a.attachFile(path, &err), "attach: %s", err.c_str());
        a.add(mk(0, 118.7e6, "ch \"q\" \\ x", "line one\nline two \xC3\xA9", "DLH1"));
        TranscriptEntry old = mk(0, 118.7e6, "old", "ancient", "");
        old.wall = (int64_t)time(nullptr) - 30 * 86400;
        a.add(old);
    }
    {
        TranscriptStore b;
        b.setLimits(100, 7);
        CHECK(b.attachFile(path), "attach again");
        CHECK(b.size() == 1, "retention drops the old one on load (%zu)", b.size());
        const auto r = b.find({});
        CHECK(!r.empty() && r[0].text == "line one\nline two \xC3\xA9" && r[0].channel == "ch \"q\" \\ x" && r[0].callsign == "DLH1", "round trip");
        for (int i = 0; i < 130; i++) b.add(mk(i, 118.7e6, "x", "n", ""));
        CHECK(b.size() <= 110, "trimmed to the limit in steps (%zu)", b.size());
        b.prune((int64_t)time(nullptr) + 40 * 86400);
        CHECK(b.size() == 0, "prune by age");
    }
    {
        TranscriptStore c;
        c.attachFile(path);
        CHECK(c.size() == 0, "the file followed the prune (%zu)", c.size());
    }
    fs::remove_all(dir);
    TranscriptEntry e, e2;
    e.wall = 5; e.text = "a\tb \"c\" \x01"; e.channel = "z";
    CHECK(transcriptFromJson(transcriptToJson(e), e2) && e2.text == e.text, "control characters survive");
    CHECK(!transcriptFromJson("not json", e2) && !transcriptFromJson("{\"t\":1}", e2), "bad lines");
}

static void models() {
    const auto& m = speechModels();
    CHECK(m.size() == 6, "models");
    for (const auto& x : m) {
        CHECK(strlen(x.sha256) == 64 && x.bytes > 70000000ull, "%s entry", x.id);
        CHECK(speechModelEnglishOnly(x.file) == !x.multilingual, "%s language flag", x.id);
        CHECK(speechModelUrl(x).rfind("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-", 0) == 0, "url");
    }
    CHECK(findSpeechModel("tiny.en") && !findSpeechModel("huge"), "find");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "onair_models_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    CHECK(!speechModelInstalled(dir.string(), *findSpeechModel("tiny.en")), "not installed");
    { std::ofstream(dir / "ggml-tiny.en.bin") << "short"; }
    CHECK(!speechModelInstalled(dir.string(), *findSpeechModel("tiny.en")), "wrong size is not installed");
    { std::ofstream(dir / "ggml-mine.bin") << "x"; std::ofstream(dir / "notes.txt") << "x"; }
    const auto u = userSpeechModels(dir.string());
    CHECK(u.size() == 1 && u[0] == "ggml-mine.bin", "user files (%zu)", u.size());
    fs::remove_all(dir);
}

int main() {
    callsigns();
    segmenter();
    filters();
    transcriber();
    translation();
    store();
    models();
    if (fails) { printf("%d failures\n", fails); return 1; }
    printf("transcribe: all ok\n");
    return 0;
}
