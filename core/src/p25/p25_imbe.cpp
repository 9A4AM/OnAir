// IMBE through the user-installed mbelib (see p25_imbe.h); the loader is shared with the other voice decoders (source/mbe_dyn.h).
#include "dect2/p25/p25_imbe.h"
#include "../source/mbe_dyn.h"
#include <cstring>

namespace dect2 {

namespace {
using FrameFn = void (DECT2_CALL*)(short* aout, int* errs, int* errs2, char* errStr, char fr[8][23], char d[88], void* cur, void* prev, void* prevEnh, int uvq);
mbe::Loader<FrameFn> gMbe("mbe_processImbe7200x4400Frame");
} // namespace

bool p25ImbeAvailable(std::string* path) { return gMbe.available(path); }
bool p25ImbeRescan() { return gMbe.rescan(); }

struct P25Imbe::Impl {
    const mbe::Api<FrameFn>* a = nullptr;
    mbe::State s;
};

P25Imbe::P25Imbe() : p_(std::make_unique<Impl>()) {
    const mbe::Api<FrameFn>& a = gMbe.api();
    if (!a.ok) return;
    p_->a = &a;
    a.init(p_->s.cur.data(), p_->s.prev.data(), p_->s.enh.data());
}
P25Imbe::~P25Imbe() = default;
bool P25Imbe::ok() const { return p_->a != nullptr; }

void P25Imbe::decode(const char frIn[8][23], int16_t pcm[160]) {
    if (!p_->a) { memset(pcm, 0, 160 * sizeof(int16_t)); return; }
    char fr[8][23], d[88] = {}, err[64] = {};
    memcpy(fr, frIn, sizeof fr);
    int e1 = 0, e2 = 0;
    short out[160] = {};
    p_->a->frame(out, &e1, &e2, err, fr, d, p_->s.cur.data(), p_->s.prev.data(), p_->s.enh.data(), 3);
    for (int i = 0; i < 160; i++) pcm[i] = out[i];
}

} // namespace dect2
