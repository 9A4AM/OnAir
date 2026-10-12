// Time signal stations: the minute frames of the amplitude time codes, the stations' sites and the propagation delay.
//
// Each second of a minute is one symbol. LF stations key the carrier: DCF77 (77.5 kHz, Mainflingen) drops it to 15 % for 100 ms (0) or
// 200 ms (1), not at all in second 59; MSF (60 kHz, Anthorn) switches it off for 100 ms, then for bit A 100 - 200 ms and bit B 200 - 300 ms,
// 500 ms at the minute marker; WWVB (60 kHz, Fort Collins) lowers it 17 dB for 200 (0), 500 (1) or 800 ms (marker); JJY (40 kHz Ohtakadoya,
// 60 kHz Hagane) keeps it high for 800 (0), 500 (1) or 200 ms (marker), low the rest. WWV / WWVH (2.5 - 20 MHz) send the same BCD frame as a
// 100 Hz subcarrier (200 / 500 / 800 ms pulses, none in second 0) with 5 ms ticks at 1000 Hz (WWV) or 1200 Hz (WWVH). CHU (3.33 / 7.85 /
// 14.67 MHz) sends 300 bit/s Bell 103 FSK frames in seconds 31 - 39 (decoded here: the A frames with day of year and time).
//
// DCF77 and MSF announce the minute that begins at the next minute marker; WWVB, JJY and WWV the minute their frame starts. decodeMinute()
// returns the UTC start of the minute the symbols were received in either way.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dect2 {
namespace timesig {

enum Station { Dcf77 = 0, Msf, Wwvb, Jjy40, Jjy60, Wwv, Chu, kNumStations };
const char* stationName(int s);      // "DCF77", "MSF", "WWVB", "JJY 40 kHz", "JJY 60 kHz", "WWV / WWVH", "CHU"
double stationKhz(int s);            // the usual frequency (WWV: 10 MHz, CHU: 7.85 MHz)
bool stationIsHf(int s);             // WWV / WWVH and CHU: AM voice stations with an audio time code
int stationForKhz(double khz, int pref60);   // the station of a frequency (pref60: which of MSF / WWVB / JJY 60 at 60 kHz), -1 none
const char* stationTimeZone(int s);  // the time the frame carries: "CET / CEST", "UK", "UTC", "JST"

// Symbols, one a second. bit 0 is the bit (MSF: bit A), bit 1 MSF bit B.
enum Sym : uint8_t { S0 = 0, S1 = 1, SB = 2, SAB = 3, SMark = 4, SNone = 5, SUnknown = 6 };

struct Fields {
    int year = 0, month = 0, day = 0, doy = 0, hour = 0, minute = 0;   // of the frame's own time (local time for DCF77, MSF, JJY)
    int weekday = -1;               // 1 Monday .. 7 Sunday, -1 not sent
    int utcOffsetMin = 0;           // the frame's time minus UTC
    int dst = -1;                   // summer time in effect: 1, 0, -1 not sent
    int dstCode = -1;               // WWVB / WWV: 0 standard time, 1 DST ends today, 2 begins today, 3 in effect
    bool dstAnnounce = false;       // DCF77 A1, MSF 53B: the change at the end of this hour
    bool leapAnnounce = false;      // a leap second at the end of this month (DCF77 A2) or day (WWVB, WWV, JJY)
    int leapSign = 0;               // JJY: +1 insert, -1 delete
    int dut1 = 99;                  // UT1 - UTC in 0.1 s (99 not sent)
    bool callBit = false;           // DCF77 R: the transmitter runs on its reserve antenna
    bool leapYear = false;          // WWVB
    int64_t utcMinute = 0;          // UTC epoch second of the minute the symbols were received in
};

// Encode the minute that starts at UTC epoch second utcMinute (a multiple of 60): the symbols of its seconds 0 - 59.
struct EncodeOpts { bool leapAnnounce = false; int leapSign = 1; int dut1 = 0; bool callBit = false; };
std::vector<uint8_t> encodeMinute(int station, int64_t utcMinute, const EncodeOpts& o = {});
// Decode the 60 symbols of a received minute (second 0 first). False when a fixed bit, a parity, a range or a marker is wrong.
// yearHint: the year two-digit years are placed near (0: the computer's), and JJY's at minutes 15 and 45 (no year sent)
bool decodeMinute(int station, const uint8_t* sym, Fields& f, int yearHint = 0);
std::string fieldsText(const Fields& f, int station);   // "2026-10-12 14:03 UTC (16:03 CEST)"

// CHU: the A frame of a second 32 - 39 (10 bytes: day of year, hour, minute, second as BCD digits, sent twice), and its parse
std::vector<uint8_t> chuFrameA(int doy, int hour, int minute, int second);
std::vector<uint8_t> chuFrameB(int year);   // second 31: data then its complement (only its shape is checked when decoding)
bool chuParseA(const uint8_t* b, int& doy, int& hour, int& minute, int& second);
bool chuIsB(const uint8_t* b);

// Calendar (proleptic Gregorian, UTC)
int64_t daysFromCivil(int y, int m, int d);
void civilFromDays(int64_t z, int& y, int& m, int& d);
int weekdayOf(int64_t days);         // 1 Monday .. 7 Sunday
bool euSummerTime(int64_t utc);      // the EU rule (last Sunday of March to last Sunday of October, 01:00 UTC)
bool usDstOnDate(int y, int m, int d);   // the US rule: the date is from the second Sunday of March to the day before the first Sunday of November

// The transmitter sites, the great-circle distance and the delay of the path
void stationSite(int s, double& lat, double& lon, bool hawaii = false);
double distanceKm(double lat1, double lon1, double lat2, double lon2);
// Ground wave for LF (distance / c); for HF a sky wave over hops of up to 4000 km off a 300 km layer. Milliseconds.
double propagationMs(int s, double km);

} // namespace timesig
} // namespace dect2
