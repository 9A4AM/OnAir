// Wi-Fi HaLow (IEEE 802.11ah, S1G) pieces of the network monitor, receive only: the OFDM layout of the 1 / 2 / 4 MHz channels, the preamble
// fields, the SIG field (CRC-4), the BCC and scrambler of the data field, and the S1G beacon (frame control, SA, timestamp, compressed SSID,
// information elements: SSID, RSN, S1G Operation, Beacon Compatibility), plus the EU 863 - 868 MHz and US 902 - 928 MHz channel plans.
//
// Written from the published description of the standard (IEEE 802.11-2020 clause 23, 9.3.4, 9.4.2) and checked only against its own test
// signal (halow_gen.h): no live 802.11ah recording was available. Where the author was not certain of a detail (the 1 MHz and 4 MHz LTF tone
// values, the exact bit positions inside the SIG fields, the 1 MHz LTF1 length) the choice is marked "unverified" in halow_proto.cpp.
// Everything is sampled at 4 Msps on one 128-point grid of 31.25 kHz tones: a 1 MHz channel is the middle 32 of them, 2 MHz the middle 64,
// 4 MHz the middle 128, so one FFT size serves all three widths.
#pragma once
#include "ring.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dect2::halow {

constexpr double kRate = 4e6;            // the one internal sample rate
constexpr int kFft = 128;                // 4 Msps / 31.25 kHz
constexpr int kGi = 32;                  // 8 us guard interval
constexpr int kSym = kFft + kGi;         // 40 us
constexpr int kLag = 32;                 // the STF repeats every 8 us (32 samples)
enum Width { W1 = 0, W2 = 1, W4 = 2, kNumWidths = 3 };
inline int widthMhz(int w) { return 1 << w; }

// Tones of a channel width (indices k from -64 .. 63, FFT bin (k + 128) % 128)
struct Layout {
    std::vector<int> dataK;              // 0 = a tone that does not exist in this copy (dropped at the band edge)
    std::vector<int> pilotK;
};
struct WidthInfo {
    int w = 0;
    int usedLo = 0, usedHi = 0;          // the used tones run -usedHi .. -usedLo and usedLo .. usedHi
    int nUsed = 0;                       // tones carrying something (data + pilots)
    int ncbps = 0;                       // coded bits per symbol (BPSK: the data tones)
    int ncol = 0, nrow = 0;              // interleaver
    int sigNcbps = 0, sigNcol = 0, sigNrow = 0;   // the same for the SIG field's symbols (the 2 MHz layout at 2 and 4 MHz)
    Layout data;                         // the data field
    std::vector<Layout> sig;             // the SIG field: one layout, or the 2 MHz layout in both halves of the 4 MHz channel
    int sigSyms = 0;                     // OFDM symbols of the SIG field: 6 (1 MHz), 2
    int sigInfoBits = 0;                 // 36, 52, 52 with the CRC and the tail
    int stfSamples = 0;                  // 640 at 1 MHz (160 us), 320
    int ltfSyms = 0;                     // 1 MHz: 4 symbols of GI + LTS; 2 and 4 MHz: a double GI and two LTS
};
const WidthInfo& info(int w);
inline int bin(int k) { return (k + kFft) % kFft; }

// Preamble spectra on the 128-bin grid
void stfSpectrum(int w, cf32* x);        // 128 bins
void ltfSpectrum(int w, float* l);       // 128 bins: +-1 on the used tones, 0 elsewhere
float stfScale(int w);                   // time-domain scale giving unit power
float symScale(int w);                   // time-domain scale of the LTF and of every data symbol: 1 / sqrt(nUsed)
void ltfTime(int w, cf32* x);            // the 128 samples of one LTS
void stfTime(int w, cf32* x);            // the 128 samples (four periods) of the STF

// bits, coding
uint8_t crc4(const uint8_t* bits, size_t n);                     // init ones, result inverted; bits in transmit order
std::vector<uint8_t> scramblerSeq(uint8_t seed, size_t n);       // the x^7 + x^4 + 1 sequence from a 7 bit state
std::vector<uint8_t> scramblerFrom(const uint8_t* first7, size_t n);   // the sequence that started with these 7 bits
int pilotPolarity(int n);                                        // +1 / -1, the 127 long sequence
std::vector<uint8_t> convEncode(const std::vector<uint8_t>& bits);     // rate 1/2, K = 7, 133 / 171 octal
std::vector<uint8_t> viterbi(const std::vector<float>& llr);           // llr > 0: bit 1; llr.size() / 2 bits out
inline int interleave(int ncol, int nrow, int k) { return nrow * (k % ncol) + k / ncol; }   // coded bit k of a symbol -> its position

// SIG field
struct Sig {
    int w = 0;                           // channel width the PPDU was sent in
    int mcs = 0;                         // 0 (BPSK 1/2) or 10 (BPSK 1/2, 2x repetition, 1 MHz only) are decoded
    int len = 0;                         // PSDU bytes
    bool agg = false;
    int nsts = 0;                        // spatial streams - 1
    bool sgi = false;
    int id = 0;                          // partial AID (2 / 4 MHz)
    bool ul = false, stbc = false;
};
std::vector<uint8_t> sigBits(const Sig& s);                      // with CRC-4 and tail
bool parseSig(const uint8_t* bits, int w, Sig& s);               // false: CRC or field check failed

// The PPDU
int dataBitsPerSym(int w, int mcs);                              // NDBPS
int dataSymbols(int w, int mcs, int len);
size_t ppduSamples(int w, int mcs, int len);                     // at 4 Msps
std::vector<cf32> buildPpdu(const std::vector<uint8_t>& psdu, int w, int mcs, uint8_t seed = 0x5D, bool agg = false);
// Which symbol (counted from the start of the SIG field) carries the pilots' polarity n
inline int pilotIndex(int symInPpdu) { return symInPpdu; }

// ---------------------------------------------------------------- MAC
enum Security { Open = 0, Wpa2Psk, Wpa3Sae, Owe, Wpa2Ent, RsnOther, Secured };
const char* securityName(int s);

struct BssSpec {
    uint8_t bssid[6] = {};
    std::string ssid;
    bool fullSsid = true;                // the SSID element in the beacon body (else only its 32-bit compressed form)
    bool compressedSsid = true;
    int bwInd = 0;                       // beacon bandwidth indication: 0 1 MHz, 1 2 MHz, 2 4 MHz
    int security = Wpa2Psk;
    int opClass = 6;                     // S1G Operation: operating class, primary channel number, channel centre frequency number, width
    int primary = 1;
    int ccf = 1;
    int opWidthMhz = 1;
    int intervalTu = 100;
    uint32_t timestamp = 0;
    uint8_t changeSeq = 0;
    bool ano = false;
};
void appendFcs(std::vector<uint8_t>& f);
bool fcsOk(const uint8_t* p, size_t n);                          // n includes the 4 byte FCS
std::vector<uint8_t> makeS1gBeacon(const BssSpec& b);            // FCS appended
std::vector<uint8_t> makeProbeResponse(const BssSpec& b, const uint8_t* client);   // a management frame; FCS appended
std::vector<uint8_t> makeDataFrame(const uint8_t bssid[6], const uint8_t sta[6], size_t payload);

struct Frame {
    int type = -1, subtype = -1;
    bool s1gBeacon = false, probeResp = false, mgmtBeacon = false;
    uint8_t bssid[6] = {};
    bool hasSsid = false;
    std::string ssid;
    bool hasComp = false;
    uint32_t compSsid = 0;
    int bwInd = -1;                      // from the frame control of an S1G beacon
    bool secBit = false;
    int security = Open;
    bool hasRsn = false;
    bool hasOp = false;
    int opClass = 0, primary = 0, ccf = 0, opWidthMhz = 0;
    int intervalTu = 0;
    uint32_t timestamp = 0;
    std::string summary;
};
bool parseFrame(const uint8_t* p, size_t n, Frame& f);           // n excludes the FCS (the caller checks it)
std::string macText(const uint8_t a[6]);
std::string compText(uint32_t c);                                // "c:1A2B3C4D"

// Channel plans. The S1G channel centre frequency = start + 0.5 MHz x number
struct Chan { int ccf; double mhz; int widthMhz; };
double chanMhz(int opClass, int ccf);                            // 0 when the class is not known
std::vector<Chan> euChannels(int widthMhz);                      // 863 - 868 MHz
std::vector<Chan> usChannels(int widthMhz);                      // 902 - 928 MHz
int nearestChannel(double mhz, int widthMhz, bool eu, double* chMhz = nullptr);   // its S1G channel number, 0 none within 1 MHz
bool inHalowBand(double mhz);

} // namespace dect2::halow
