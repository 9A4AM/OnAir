// Wi-Fi HaLow (802.11ah) pieces (see halow_proto.h).
#include "dect2/halow/halow_proto.h"
#include "dect2/dsp/fftutil.h"
#include "dect2/wifi/wifi_proto.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dect2::halow {

namespace {

// 11a / 11ac 20 MHz long training sequence, k = -26 .. 26 (the 2 MHz LTS is this with the tones +-27, +-28 added)
const int kLtfNeg[26] = {1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, 1};
const int kLtfPos[26] = {1, -1, -1, 1, 1, -1, 1, -1, 1, -1, -1, -1, -1, -1, 1, 1, -1, -1, 1, -1, 1, -1, 1, 1, 1, 1};

int ltf2(int k) {   // -28 .. 28, 0 at DC
    if (k == 0 || k < -28 || k > 28) return 0;
    if (k == -28 || k == -27) return 1;
    if (k == 27 || k == 28) return -1;
    return k < 0 ? kLtfNeg[k + 26] : kLtfPos[k - 1];
}

// 11a short training sequence on the tones that are multiples of 4 (k = -24 .. 24); the unit is (1 + j) up to the scale
int stf2(int k) {
    switch (k) {
        case -24: return 1;  case -20: return -1; case -16: return 1;  case -12: return -1; case -8: return -1; case -4: return 1;
        case 4: return -1;   case 8: return -1;   case 12: return 1;   case 16: return 1;   case 20: return 1;  case 24: return 1;
        default: return 0;
    }
}

std::vector<int> range(int lo, int hi) { std::vector<int> v; for (int k = lo; k <= hi; k++) v.push_back(k); return v; }

WidthInfo makeInfo(int w) {
    WidthInfo wi;
    wi.w = w;
    auto build = [](int hi, int lo, const std::vector<int>& pil, Layout& lay) {
        lay.pilotK = pil;
        std::sort(lay.pilotK.begin(), lay.pilotK.end());
        for (int k = -hi; k <= hi; k++) {
            if (std::abs(k) < lo) continue;
            if (std::find(pil.begin(), pil.end(), k) != pil.end()) continue;
            lay.dataK.push_back(k);
        }
    };
    if (w == W1) {
        wi.usedLo = 1; wi.usedHi = 13;
        build(13, 1, {-7, 7}, wi.data);
        wi.ncbps = 24; wi.ncol = 6; wi.nrow = 4;
        wi.sig.push_back(wi.data);
        wi.sigNcbps = 24; wi.sigNcol = 6; wi.sigNrow = 4;
        wi.sigSyms = 6; wi.sigInfoBits = 36; wi.stfSamples = 640; wi.ltfSyms = 4;
    } else if (w == W2) {
        wi.usedLo = 1; wi.usedHi = 28;
        build(28, 1, {-21, -7, 7, 21}, wi.data);
        wi.ncbps = 52; wi.ncol = 13; wi.nrow = 4;
        wi.sig.push_back(wi.data);
        wi.sigNcbps = 52; wi.sigNcol = 13; wi.sigNrow = 4;
        wi.sigSyms = 2; wi.sigInfoBits = 52; wi.stfSamples = 320; wi.ltfSyms = 2;
    } else {
        wi.usedLo = 2; wi.usedHi = 58;
        build(58, 2, {-53, -25, -11, 11, 25, 53}, wi.data);
        wi.ncbps = 108; wi.ncol = 18; wi.nrow = 6;
        Layout base;
        build(28, 1, {-21, -7, 7, 21}, base);
        for (int shift : {-32, 32}) {   // the 2 MHz SIG in both halves; the tones that would fall outside +-58 are dropped
            Layout l;
            for (int k : base.dataK) l.dataK.push_back(std::abs(k + shift) <= 58 ? k + shift : 0);
            for (int k : base.pilotK) l.pilotK.push_back(k + shift);
            wi.sig.push_back(l);
        }
        wi.sigNcbps = 52; wi.sigNcol = 13; wi.sigNrow = 4;
        wi.sigSyms = 2; wi.sigInfoBits = 52; wi.stfSamples = 320; wi.ltfSyms = 2;
    }
    wi.nUsed = (int)wi.data.dataK.size() + (int)wi.data.pilotK.size();
    return wi;
}

Fft& fft128() { static thread_local Fft f(kFft); return f; }

int parity(uint32_t v) { v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return (int)(v & 1); }

void putLe(std::vector<uint8_t>& v, uint32_t x, int n) { for (int i = 0; i < n; i++) v.push_back((uint8_t)(x >> (8 * i))); }

} // namespace

const WidthInfo& info(int w) {
    static const WidthInfo t[3] = {makeInfo(W1), makeInfo(W2), makeInfo(W4)};
    return t[w < 0 ? 0 : w > 2 ? 2 : w];
}

// ---------------------------------------------------------------- preamble

void stfSpectrum(int w, cf32* x) {
    for (int i = 0; i < kFft; i++) x[i] = cf32(0, 0);
    auto put = [&](int k, int v) { if (v) x[bin(k)] = cf32((float)v, (float)v); };
    if (w == W1) {
        for (int k : {-12, -8, -4, 4, 8, 12}) put(k, stf2(k));
    } else if (w == W2) {
        for (int k = -24; k <= 24; k += 4) put(k, stf2(k));
    } else {
        for (int k = -56; k <= 56; k += 4) {
            if (k == 0 || std::abs(k) == 32) continue;
            put(k, stf2(k < 0 ? k + 32 : k - 32));
        }
    }
}

void ltfSpectrum(int w, float* l) {
    for (int i = 0; i < kFft; i++) l[i] = 0;
    if (w == W1) {   // unverified: the 2 MHz sequence on the 26 tones
        for (int k = -13; k <= 13; k++) if (k) l[bin(k)] = (float)ltf2(k);
    } else if (w == W2) {
        for (int k = -28; k <= 28; k++) if (k) l[bin(k)] = (float)ltf2(k);
    } else {         // unverified: the 2 MHz sequence in both halves, other tones a fixed pattern
        for (int k = -58; k <= 58; k++) {
            if (std::abs(k) < 2) continue;
            const int p = k < 0 ? k + 32 : k - 32;
            l[bin(k)] = (float)(p != 0 && std::abs(p) <= 28 ? ltf2(p) : (((k * k + 3 * k) & 4) ? -1 : 1));
        }
    }
}

float stfScale(int w) {
    cf32 x[kFft];
    stfSpectrum(w, x);
    double e = 0;
    for (const cf32& v : x) e += std::norm(v);
    return (float)(1.0 / std::sqrt(e));
}

float symScale(int w) { return (float)(1.0 / std::sqrt((double)info(w).nUsed)); }

void ltfTime(int w, cf32* x) {
    float l[kFft];
    ltfSpectrum(w, l);
    for (int i = 0; i < kFft; i++) x[i] = cf32(l[i], 0);
    fft128().inverse(x);
    const float g = symScale(w);
    for (int i = 0; i < kFft; i++) x[i] *= g;
}

void stfTime(int w, cf32* x) {
    stfSpectrum(w, x);
    fft128().inverse(x);
    const float g = stfScale(w);
    for (int i = 0; i < kFft; i++) x[i] *= g;
}

// ---------------------------------------------------------------- bits and coding

uint8_t crc4(const uint8_t* bits, size_t n) {
    uint8_t c = 0xF;
    for (size_t i = 0; i < n; i++) {
        const uint8_t fb = (uint8_t)(((c >> 3) & 1) ^ (bits[i] & 1));
        c = (uint8_t)((c << 1) & 0xF);
        if (fb) c ^= 0x3;
    }
    return (uint8_t)(~c & 0xF);
}

std::vector<uint8_t> scramblerSeq(uint8_t seed, size_t n) {
    std::vector<uint8_t> o(n);
    uint8_t h[7];
    for (int i = 0; i < 7; i++) h[i] = (uint8_t)(seed >> i & 1);   // h[i] = the output i + 1 places back
    for (size_t i = 0; i < n; i++) {
        const uint8_t b = h[3] ^ h[6];
        o[i] = b;
        for (int j = 6; j > 0; j--) h[j] = h[j - 1];
        h[0] = b;
    }
    return o;
}

std::vector<uint8_t> scramblerFrom(const uint8_t* first7, size_t n) {
    std::vector<uint8_t> o(n);
    uint8_t h[7];
    for (int i = 0; i < 7; i++) h[i] = first7[6 - i];
    for (size_t i = 0; i < n; i++) {
        if (i < 7) { o[i] = first7[i]; continue; }
        const uint8_t b = h[3] ^ h[6];
        o[i] = b;
        for (int j = 6; j > 0; j--) h[j] = h[j - 1];
        h[0] = b;
    }
    return o;
}

int pilotPolarity(int n) {
    static const std::vector<uint8_t> s = scramblerSeq(0x7F, 127);
    return s[(size_t)(n % 127)] ? -1 : 1;
}

std::vector<uint8_t> convEncode(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> out;
    out.reserve(bits.size() * 2);
    uint32_t reg = 0;
    for (uint8_t b : bits) {
        reg = (reg >> 1) | (uint32_t)(b & 1) << 6;
        out.push_back((uint8_t)parity(reg & 0x5B));
        out.push_back((uint8_t)parity(reg & 0x79));
    }
    return out;
}

std::vector<uint8_t> viterbi(const std::vector<float>& llr) {
    const size_t n = llr.size() / 2;
    std::vector<uint8_t> dec(n * 64);
    float pm[64], nm[64];
    for (int s = 0; s < 64; s++) pm[s] = s == 0 ? 0.f : -1e9f;
    static const struct Tab {
        uint8_t o0[2][64], o1[2][64];   // output bits for [input][state]
        Tab() { for (int b = 0; b < 2; b++) for (int s = 0; s < 64; s++) { const uint32_t r = (uint32_t)(b << 6 | s); o0[b][s] = (uint8_t)parity(r & 0x5B); o1[b][s] = (uint8_t)parity(r & 0x79); } }
    } tab;
    for (size_t i = 0; i < n; i++) {
        const float l0 = llr[2 * i], l1 = llr[2 * i + 1];
        for (int ns = 0; ns < 64; ns++) {
            const int b = ns >> 5;
            float best = -1e30f; int bd = 0;
            for (int d = 0; d < 2; d++) {
                const int s = (ns & 0x1F) << 1 | d;
                const float m = pm[s] + (tab.o0[b][s] ? l0 : -l0) + (tab.o1[b][s] ? l1 : -l1);
                if (m > best) { best = m; bd = d; }
            }
            nm[ns] = best;
            dec[i * 64 + (size_t)ns] = (uint8_t)bd;
        }
        float mx = nm[0];
        for (int s = 1; s < 64; s++) mx = std::max(mx, nm[s]);
        for (int s = 0; s < 64; s++) pm[s] = nm[s] - mx;
    }
    std::vector<uint8_t> out(n);
    int s = 0;   // the tail brings the encoder back to state 0
    for (size_t i = n; i-- > 0;) {
        out[i] = (uint8_t)(s >> 5);
        s = (s & 0x1F) << 1 | dec[i * 64 + (size_t)s];
    }
    return out;
}

// ---------------------------------------------------------------- SIG

namespace {
void putBits(std::vector<uint8_t>& v, size_t at, uint32_t x, int n) { for (int i = 0; i < n; i++) v[at + (size_t)i] = (uint8_t)(x >> i & 1); }
uint32_t getBits(const uint8_t* v, size_t at, int n) { uint32_t x = 0; for (int i = 0; i < n; i++) x |= (uint32_t)(v[at + (size_t)i] & 1) << i; return x; }
}

// Bit positions follow the published field order as the author remembers it; unverified against a live signal.
std::vector<uint8_t> sigBits(const Sig& s) {
    const WidthInfo& wi = info(s.w);
    std::vector<uint8_t> b((size_t)wi.sigInfoBits, 0);
    size_t infoLen;
    if (s.w == W1) {
        putBits(b, 0, (uint32_t)s.nsts, 2); putBits(b, 2, s.sgi, 1); putBits(b, 3, 0, 1); putBits(b, 4, (uint32_t)s.mcs, 4);
        putBits(b, 8, s.agg, 1); putBits(b, 9, (uint32_t)s.len, 9); putBits(b, 20, 1, 1);
        infoLen = 26;
    } else {
        putBits(b, 0, s.stbc, 1); putBits(b, 1, s.ul, 1); putBits(b, 2, s.w == W4 ? 1 : 0, 2); putBits(b, 4, (uint32_t)s.nsts, 2);
        putBits(b, 6, (uint32_t)s.id, 9); putBits(b, 16, s.sgi, 1); putBits(b, 19, (uint32_t)s.mcs, 4); putBits(b, 24, s.agg, 1);
        putBits(b, 25, (uint32_t)s.len, 9); putBits(b, 36, 1, 1);
        infoLen = 42;
    }
    const uint8_t c = crc4(b.data(), infoLen);
    for (int i = 0; i < 4; i++) b[infoLen + (size_t)i] = (uint8_t)(c >> (3 - i) & 1);
    return b;   // the six tail bits stay 0
}

bool parseSig(const uint8_t* bits, int w, Sig& s) {
    const WidthInfo& wi = info(w);
    const size_t infoLen = w == W1 ? 26 : 42;
    const uint8_t c = crc4(bits, infoLen);
    uint8_t r = 0;
    for (int i = 0; i < 4; i++) r = (uint8_t)(r << 1 | (bits[infoLen + (size_t)i] & 1));
    if (r != c) return false;
    for (size_t i = infoLen + 4; i < (size_t)wi.sigInfoBits; i++) if (bits[i] & 1) return false;   // the tail
    s = Sig();
    s.w = w;
    if (w == W1) {
        s.nsts = (int)getBits(bits, 0, 2); s.sgi = getBits(bits, 2, 1); s.mcs = (int)getBits(bits, 4, 4);
        s.agg = getBits(bits, 8, 1); s.len = (int)getBits(bits, 9, 9);
    } else {
        s.stbc = getBits(bits, 0, 1); s.ul = getBits(bits, 1, 1);
        if ((int)getBits(bits, 2, 2) != (w == W4 ? 1 : 0)) return false;   // the bandwidth field disagrees with where it was heard
        s.nsts = (int)getBits(bits, 4, 2); s.id = (int)getBits(bits, 6, 9); s.sgi = getBits(bits, 16, 1);
        s.mcs = (int)getBits(bits, 19, 4); s.agg = getBits(bits, 24, 1); s.len = (int)getBits(bits, 25, 9);
    }
    return true;
}

// ---------------------------------------------------------------- PPDU

int dataBitsPerSym(int w, int mcs) {
    if (w == W1) return mcs == 10 ? 6 : mcs == 0 ? 12 : 0;
    return mcs == 0 ? info(w).ncbps / 2 : 0;
}

int dataSymbols(int w, int mcs, int len) {
    const int nd = dataBitsPerSym(w, mcs);
    if (nd <= 0) return 0;
    const int bits = 8 + 8 * len + 6;
    return (bits + nd - 1) / nd;
}

size_t ppduSamples(int w, int mcs, int len) {
    const WidthInfo& wi = info(w);
    const size_t ltf = w == W1 ? (size_t)wi.ltfSyms * kSym : (size_t)(kFft / 2 + 2 * kFft);
    return (size_t)wi.stfSamples + ltf + (size_t)(wi.sigSyms + dataSymbols(w, mcs, len)) * kSym;
}

namespace {

void addSymbol(std::vector<cf32>& out, cf32* X, float g) {
    fft128().inverse(X);
    for (int i = kFft - kGi; i < kFft; i++) out.push_back(X[i] * g);
    for (int i = 0; i < kFft; i++) out.push_back(X[i] * g);
}

// BPSK symbol from one block of interleaved coded bits (per copy of the layout)
void fillSymbol(cf32* X, const std::vector<Layout>& lays, const std::vector<uint8_t>& bits, int polarityIdx) {
    for (int i = 0; i < kFft; i++) X[i] = cf32(0, 0);
    const int pol = pilotPolarity(polarityIdx);
    for (const Layout& l : lays) {
        for (size_t i = 0; i < l.dataK.size(); i++) if (l.dataK[i]) X[bin(l.dataK[i])] = cf32(bits[i] ? 1.f : -1.f, 0);
        for (size_t i = 0; i < l.pilotK.size(); i++) X[bin(l.pilotK[i])] = cf32((float)((i % 4 == 3 ? -1 : 1) * pol), 0);
    }
}

// info bits -> coded bits -> symbols of interleaved bits (rep: every 12 coded bits sent twice in a 24 bit symbol, 1 MHz MCS 10 and its SIG)
std::vector<std::vector<uint8_t>> codeToSymbols(const std::vector<uint8_t>& infoBits, int ncbps, int ncol, int nrow, bool rep) {
    const std::vector<uint8_t> coded = convEncode(infoBits);
    const size_t blk = rep ? (size_t)ncbps / 2 : (size_t)ncbps;
    std::vector<std::vector<uint8_t>> syms;
    for (size_t at = 0; at < coded.size(); at += blk) {
        std::vector<uint8_t> raw;
        for (size_t i = 0; i < blk; i++) raw.push_back(at + i < coded.size() ? coded[at + i] : 0);
        if (rep) { const std::vector<uint8_t> c = raw; raw.insert(raw.end(), c.begin(), c.end()); }
        std::vector<uint8_t> il((size_t)ncbps);
        for (int k = 0; k < ncbps; k++) il[(size_t)interleave(ncol, nrow, k)] = raw[(size_t)k];
        syms.push_back(std::move(il));
    }
    return syms;
}

} // namespace

std::vector<cf32> buildPpdu(const std::vector<uint8_t>& psdu, int w, int mcs, uint8_t seed, bool agg) {
    const WidthInfo& wi = info(w);
    std::vector<cf32> out;
    out.reserve(ppduSamples(w, mcs, (int)psdu.size()));
    cf32 X[kFft];
    // STF
    stfTime(w, X);
    for (int i = 0; i < wi.stfSamples; i++) out.push_back(X[i % kFft]);
    // LTF1
    cf32 L[kFft];
    ltfTime(w, L);
    if (w == W1) {
        for (int s = 0; s < wi.ltfSyms; s++) { for (int i = kFft - kGi; i < kFft; i++) out.push_back(L[i]); for (int i = 0; i < kFft; i++) out.push_back(L[i]); }
    } else {
        for (int i = kFft / 2; i < kFft; i++) out.push_back(L[i]);
        for (int r = 0; r < 2; r++) for (int i = 0; i < kFft; i++) out.push_back(L[i]);
    }
    // SIG
    Sig sg; sg.w = w; sg.mcs = mcs; sg.len = (int)psdu.size(); sg.agg = agg;
    const bool sigRep = w == W1;
    const auto sigSyms = codeToSymbols(sigBits(sg), wi.sigNcbps, wi.sigNcol, wi.sigNrow, sigRep);
    const float g = symScale(w);
    int symIdx = 0;
    for (const auto& bs : sigSyms) { fillSymbol(X, wi.sig, bs, symIdx++); addSymbol(out, X, g); }
    // data: SERVICE (8), PSDU, tail (6), pad; scrambled; the tail bits cleared again after scrambling
    const int nd = dataBitsPerSym(w, mcs);
    if (nd > 0) {
        const int nsym = dataSymbols(w, mcs, (int)psdu.size());
        std::vector<uint8_t> bits((size_t)nsym * (size_t)nd, 0);
        for (size_t i = 0; i < psdu.size(); i++) for (int b = 0; b < 8; b++) bits[8 + i * 8 + (size_t)b] = (uint8_t)(psdu[i] >> b & 1);
        const std::vector<uint8_t> sc = scramblerSeq(seed ? seed : 0x5D, bits.size());
        for (size_t i = 0; i < bits.size(); i++) bits[i] ^= sc[i];
        for (size_t i = 0; i < 6; i++) bits[8 + psdu.size() * 8 + i] = 0;
        const auto syms = codeToSymbols(bits, wi.ncbps, wi.ncol, wi.nrow, w == W1 && mcs == 10);
        for (const auto& bs : syms) { fillSymbol(X, {wi.data}, bs, symIdx++); addSymbol(out, X, g); }
    }
    return out;
}

// ---------------------------------------------------------------- MAC

const char* securityName(int s) {
    switch (s) {
        case Open: return "Open";
        case Wpa2Psk: return "WPA2-PSK";
        case Wpa3Sae: return "WPA3-SAE";
        case Owe: return "OWE";
        case Wpa2Ent: return "WPA2-Enterprise";
        case RsnOther: return "RSN";
        case Secured: return "Secured";
        default: return "?";
    }
}

void appendFcs(std::vector<uint8_t>& f) { putLe(f, wifi::crc32(f.data(), f.size()), 4); }

bool fcsOk(const uint8_t* p, size_t n) {
    if (n < 6) return false;
    const uint32_t c = wifi::crc32(p, n - 4);
    const uint32_t r = (uint32_t)p[n - 4] | (uint32_t)p[n - 3] << 8 | (uint32_t)p[n - 2] << 16 | (uint32_t)p[n - 1] << 24;
    return c == r;
}

namespace {

void addIe(std::vector<uint8_t>& v, int id, const std::vector<uint8_t>& body) {
    v.push_back((uint8_t)id);
    v.push_back((uint8_t)body.size());
    v.insert(v.end(), body.begin(), body.end());
}

void addBodyIes(std::vector<uint8_t>& v, const BssSpec& b, bool withSsid, bool withCompat) {
    if (withSsid) addIe(v, 0, std::vector<uint8_t>(b.ssid.begin(), b.ssid.end()));
    if (withCompat) {   // S1G Beacon Compatibility: capabilities, beacon interval, TSF completion
        std::vector<uint8_t> c;
        putLe(c, 0x0001, 2); putLe(c, (uint32_t)b.intervalTu, 2); putLe(c, 0, 4);
        addIe(v, 213, c);
    }
    if (b.security != Open && b.security != Secured) {
        std::vector<uint8_t> r;
        putLe(r, 1, 2);                              // version
        r.insert(r.end(), {0x00, 0x0F, 0xAC, 0x04}); // group cipher CCMP
        putLe(r, 1, 2); r.insert(r.end(), {0x00, 0x0F, 0xAC, 0x04});   // pairwise: CCMP
        const uint8_t akm = b.security == Wpa3Sae ? 8 : b.security == Owe ? 18 : b.security == Wpa2Ent ? 1 : 2;
        putLe(r, 1, 2); r.insert(r.end(), {0x00, 0x0F, 0xAC, akm});
        putLe(r, 0x00C0, 2);                         // capabilities (management frame protection)
        addIe(v, 48, r);
    }
    std::vector<uint8_t> cap(15, 0);                 // S1G Capabilities: nothing the monitor reads
    cap[0] = 0x2B;
    addIe(v, 217, cap);
    std::vector<uint8_t> op;                         // S1G Operation: width, class, primary channel, centre frequency number, MCS / NSS set
    const int code = b.opWidthMhz >= 16 ? 15 : b.opWidthMhz >= 8 ? 7 : b.opWidthMhz >= 4 ? 3 : b.opWidthMhz >= 2 ? 1 : 0;
    op.push_back((uint8_t)(code << 1 | (b.opWidthMhz == 1 ? 1 : 0)));
    op.push_back((uint8_t)b.opClass); op.push_back((uint8_t)b.primary); op.push_back((uint8_t)b.ccf);
    putLe(op, 0xFFFC, 2);
    addIe(v, 232, op);
}

} // namespace

std::vector<uint8_t> makeS1gBeacon(const BssSpec& b) {
    std::vector<uint8_t> f;
    f.push_back(0x1C);   // version 0, type 3 (extension), subtype 1 (S1G beacon)
    f.push_back((uint8_t)((b.compressedSsid ? 2 : 0) | (b.ano ? 4 : 0) | (b.bwInd & 7) << 3 | (b.security != Open ? 0x40 : 0)));
    putLe(f, 0, 2);      // duration
    f.insert(f.end(), b.bssid, b.bssid + 6);
    putLe(f, b.timestamp, 4);
    f.push_back(b.changeSeq);
    if (b.compressedSsid) putLe(f, wifi::crc32(reinterpret_cast<const uint8_t*>(b.ssid.data()), b.ssid.size()), 4);
    if (b.ano) f.push_back(0);
    addBodyIes(f, b, b.fullSsid, true);
    appendFcs(f);
    return f;
}

std::vector<uint8_t> makeProbeResponse(const BssSpec& b, const uint8_t* client) {
    std::vector<uint8_t> f;
    f.push_back(0x50); f.push_back(0x00);   // management, subtype 5
    putLe(f, 0, 2);
    static const uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    f.insert(f.end(), client ? client : bc, (client ? client : bc) + 6);
    f.insert(f.end(), b.bssid, b.bssid + 6);
    f.insert(f.end(), b.bssid, b.bssid + 6);
    putLe(f, 0, 2);
    putLe(f, b.timestamp, 4); putLe(f, 0, 4);
    putLe(f, (uint32_t)b.intervalTu, 2);
    putLe(f, b.security != Open ? 0x0011 : 0x0001, 2);
    addBodyIes(f, b, true, false);
    appendFcs(f);
    return f;
}

std::vector<uint8_t> makeDataFrame(const uint8_t bssid[6], const uint8_t sta[6], size_t payload) {
    std::vector<uint8_t> f = {0x08, 0x01, 0x00, 0x00};   // data, to DS
    f.insert(f.end(), bssid, bssid + 6);
    f.insert(f.end(), sta, sta + 6);
    f.insert(f.end(), bssid, bssid + 6);
    putLe(f, 0, 2);
    for (size_t i = 0; i < payload; i++) f.push_back((uint8_t)(i * 29 + 11));
    appendFcs(f);
    return f;
}

std::string macText(const uint8_t a[6]) { return wifi::macText(a); }
std::string compText(uint32_t c) { char b[16]; snprintf(b, sizeof b, "c:%08X", c); return b; }

namespace {

void parseIes(const uint8_t* p, size_t n, Frame& f) {
    size_t i = 0;
    int akmBest = Open;
    while (i + 2 <= n) {
        const int id = p[i], len = p[i + 1];
        if (i + 2 + (size_t)len > n) break;
        const uint8_t* b = p + i + 2;
        if (id == 0) { f.hasSsid = true; f.ssid.assign(reinterpret_cast<const char*>(b), (size_t)len); }
        else if (id == 213 && len >= 4) { f.intervalTu = b[2] | b[3] << 8; }
        else if (id == 232 && len >= 4) {
            f.hasOp = true;
            const int code = b[0] >> 1 & 0xF;
            f.opWidthMhz = code >= 15 ? 16 : code >= 7 ? 8 : code >= 3 ? 4 : code >= 1 ? 2 : 1;
            f.opClass = b[1]; f.primary = b[2]; f.ccf = b[3];
        } else if (id == 48 && len >= 8) {
            f.hasRsn = true;
            size_t o = 2 + 4;
            auto u16 = [&](size_t at) { return at + 2 <= (size_t)len ? b[at] | b[at + 1] << 8 : 0; };
            const int pc = u16(o); o += 2 + 4 * (size_t)pc;
            const int ac = o + 2 <= (size_t)len ? u16(o) : 0; o += 2;
            int best = RsnOther;
            for (int s = 0; s < ac && o + 4 <= (size_t)len; s++, o += 4) {
                if (b[o] != 0x00 || b[o + 1] != 0x0F || b[o + 2] != 0xAC) continue;
                const int t = b[o + 3];
                const int k = (t == 8 || t == 9 || t == 24) ? Wpa3Sae : t == 18 ? Owe : (t == 2 || t == 4 || t == 6) ? Wpa2Psk : (t == 1 || t == 3 || t == 5 || t == 11 || t == 12) ? Wpa2Ent : RsnOther;
                const int rank[] = {0, 3, 5, 2, 4, 1, 0};   // Open, Wpa2Psk, Wpa3Sae, Owe, Wpa2Ent, RsnOther
                if (best == RsnOther || rank[k] > rank[best]) best = k;
            }
            akmBest = best;
        }
        i += 2 + (size_t)len;
    }
    if (f.hasRsn) f.security = akmBest;
    else f.security = f.secBit ? Secured : Open;
}

} // namespace

bool parseFrame(const uint8_t* p, size_t n, Frame& f) {
    f = Frame();
    if (n < 2) return false;
    f.type = p[0] >> 2 & 3;
    f.subtype = p[0] >> 4;
    if (f.type == 3 && f.subtype == 1) {   // S1G beacon
        if (n < 15) return false;
        f.s1gBeacon = true;
        f.secBit = p[1] >> 6 & 1;
        f.bwInd = p[1] >> 3 & 7;
        memcpy(f.bssid, p + 4, 6);
        f.timestamp = (uint32_t)p[10] | (uint32_t)p[11] << 8 | (uint32_t)p[12] << 16 | (uint32_t)p[13] << 24;
        size_t o = 15;
        if (p[1] & 1) o += 3;
        if (p[1] & 2) {
            if (o + 4 > n) return false;
            f.hasComp = true;
            f.compSsid = (uint32_t)p[o] | (uint32_t)p[o + 1] << 8 | (uint32_t)p[o + 2] << 16 | (uint32_t)p[o + 3] << 24;
            o += 4;
        }
        if (p[1] & 4) o += 1;
        if (o > n) return false;
        parseIes(p + o, n - o, f);
        char b[96];
        snprintf(b, sizeof b, "S1G beacon%s%s", f.hasSsid ? " " : "", f.hasSsid ? (f.ssid.empty() ? "(hidden)" : f.ssid.c_str()) : "");
        f.summary = b;
        return true;
    }
    if (f.type == 0 && (f.subtype == 5 || f.subtype == 8)) {   // probe response, beacon: 24 byte header, timestamp 8, interval 2, capability 2
        if (n < 36) return false;
        f.probeResp = f.subtype == 5; f.mgmtBeacon = f.subtype == 8;
        memcpy(f.bssid, p + 16, 6);
        f.timestamp = (uint32_t)p[24] | (uint32_t)p[25] << 8 | (uint32_t)p[26] << 16 | (uint32_t)p[27] << 24;
        f.intervalTu = p[32] | p[33] << 8;
        f.secBit = (p[34] | p[35] << 8) & 0x10;
        parseIes(p + 36, n - 36, f);
        f.summary = f.probeResp ? "probe response" : "beacon";
        if (f.hasSsid) { f.summary += " "; f.summary += f.ssid.empty() ? "(hidden)" : f.ssid; }
        return true;
    }
    static const char* mg[] = {"assoc req", "assoc resp", "reassoc req", "reassoc resp", "probe req", "probe resp", "timing adv", "", "beacon", "ATIM", "disassoc", "auth", "deauth", "action", "action no-ack", ""};
    char b[48];
    if (f.type == 0) snprintf(b, sizeof b, "%s", f.subtype < 16 && mg[f.subtype][0] ? mg[f.subtype] : "management");
    else if (f.type == 1) snprintf(b, sizeof b, "control %d", f.subtype);
    else if (f.type == 2) snprintf(b, sizeof b, "data");
    else snprintf(b, sizeof b, "S1G extension %d", f.subtype);
    f.summary = b;
    if (f.type == 2 && n >= 22) memcpy(f.bssid, p + 4, 6);
    return true;
}

// ---------------------------------------------------------------- channels

double chanMhz(int opClass, int ccf) {
    if (ccf <= 0) return 0;
    if (opClass == 6 || opClass == 7) return 863.0 + 0.5 * ccf;
    if (opClass >= 1 && opClass <= 5) return 902.0 + 0.5 * ccf;
    return 0;
}

std::vector<Chan> euChannels(int widthMhz) {
    std::vector<Chan> v;
    if (widthMhz == 1) for (int c = 1; c <= 9; c += 2) v.push_back({c, 863.0 + 0.5 * c, 1});
    else if (widthMhz == 2) for (int c : {2, 6}) v.push_back({c, 863.0 + 0.5 * c, 2});
    return v;
}

std::vector<Chan> usChannels(int widthMhz) {
    std::vector<Chan> v;
    if (widthMhz == 1) for (int c = 1; c <= 51; c += 2) v.push_back({c, 902.0 + 0.5 * c, 1});
    else if (widthMhz == 2) for (int c = 2; c <= 50; c += 4) v.push_back({c, 902.0 + 0.5 * c, 2});
    else if (widthMhz == 4) for (int c = 4; c <= 44; c += 8) v.push_back({c, 902.0 + 0.5 * c, 4});
    return v;
}

int nearestChannel(double mhz, int widthMhz, bool eu, double* chMhz) {
    const std::vector<Chan> v = eu ? euChannels(widthMhz) : usChannels(widthMhz);
    int best = 0; double bd = 1.0;
    for (const Chan& c : v) if (std::fabs(c.mhz - mhz) < bd) { bd = std::fabs(c.mhz - mhz); best = c.ccf; if (chMhz) *chMhz = c.mhz; }
    return best;
}

bool inHalowBand(double mhz) { return (mhz >= 863.0 && mhz <= 868.0) || (mhz >= 902.0 && mhz <= 928.0); }

} // namespace dect2::halow
