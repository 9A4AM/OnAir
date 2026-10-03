// TS file -> DVB-T 8K 64-QAM 2/3 GI 1/8 baseband at 64/7 Msps, written as interleaved float32 IQ
#include "dect2/dvbt_gen.h"
#include <cstdio>
#include <vector>
using namespace dect2;
int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "rb"); FILE* o = fopen(argv[2], "wb"); double secs = atof(argv[3]);
    dvbt::Params p; p.mode = dvbt::k8K; p.guard = 2; p.mod = dvbt::k64Qam; p.crHp = p.crLp = dvbt::kR23;
    dvbt::Generator g(p, [&](uint8_t* pk) { if (fread(pk, 1, 188, f) != 188) { rewind(f); fread(pk, 1, 188, f); } });
    std::vector<dvbt::cf32> s; size_t n = 0, tot = (size_t)(secs * 64e6 / 7);
    while (n < tot) { s.clear(); g.nextSymbol(s); fwrite(s.data(), sizeof(dvbt::cf32), s.size(), o); n += s.size(); }
    fclose(o); return 0;
}
