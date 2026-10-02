// Generate a DVB-T baseband IQ recording (cs8, native rate) that carries a real transport stream (video + audio).
// usage: gen_iq in.ts out.cs8 seconds [snr_db]
#include "dect2/dvbt_gen.h"
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace dect2;
int main(int argc, char** argv) {
    if (argc < 4) return 1;
    FILE* in = fopen(argv[1], "rb"); FILE* out = fopen(argv[2], "wb"); double secs = atof(argv[3]); double snr = argc > 4 ? atof(argv[4]) : 30;
    if (!in || !out) return 1;
    dvbt::Params p; p.mode = dvbt::k8K; p.guard = dvbt::kGi8; p.mod = argc > 5 ? atoi(argv[5]) : dvbt::k64Qam; p.crHp = p.crLp = argc > 6 ? atoi(argv[6]) : dvbt::kR23;
    const double fs = 64e6 / 7;
    printf("net bitrate %.0f bit/s\n", dvbt::netBitrate(p, fs));
    dvbt::Generator g(p, [&](uint8_t* pkt) { if (fread(pkt, 1, 188, in) != 188) { rewind(in); fread(pkt, 1, 188, in); } });
    std::mt19937 rng(5); std::normal_distribution<float> nd(0, 1);
    const float nsig = std::pow(10.0, -snr / 20.0) / std::sqrt(2.0), gain = 0.25f;
    std::vector<dvbt::cf32> sym; std::vector<int8_t> buf; size_t total = 0, want = (size_t)(secs * fs);
    while (total < want) {
        g.nextSymbol(sym); buf.clear();
        for (auto v : sym) { for (float x : {v.real(), v.imag()}) { x = (x + nd(rng) * nsig) * gain; buf.push_back((int8_t)std::lround(std::min(127.f, std::max(-128.f, x * 128.f)))); } }
        fwrite(buf.data(), 1, buf.size(), out); total += sym.size();
    }
    fclose(out); return 0;
}
