// AMBE through the user-installed mbelib (see dstar_ambe.h); the loader is shared with the other voice decoders (source/mbe_dyn.h).
#include "dect2/dstar/dstar_ambe.h"
#include "dect2/dstar/dstar_proto.h"
#include "../source/mbe_dyn.h"
#include <cstring>

namespace dect2 {

namespace {
using FrameFn = void (DECT2_CALL*)(short* aout, int* errs, int* errs2, char* errStr, char fr[4][24], char d[49], void* cur, void* prev, void* prevEnh, int uvq);
mbe::Loader<FrameFn> gMbe("mbe_processAmbe3600x2400Frame");
} // namespace

bool dstarAmbeAvailable(std::string* path) { return gMbe.available(path); }
bool dstarAmbeRescan() { return gMbe.rescan(); }

struct DstarAmbe::Impl {
    const mbe::Api<FrameFn>* a = nullptr;
    mbe::State s;
};

DstarAmbe::DstarAmbe() : p_(std::make_unique<Impl>()) {
    const mbe::Api<FrameFn>& a = gMbe.api();
    if (!a.ok) return;
    p_->a = &a;
    a.init(p_->s.cur.data(), p_->s.prev.data(), p_->s.enh.data());
}
DstarAmbe::~DstarAmbe() = default;
bool DstarAmbe::ok() const { return p_->a != nullptr; }

void DstarAmbe::decode(const uint8_t bits[72], int16_t pcm[160]) {
    if (!p_->a) { memset(pcm, 0, 160 * sizeof(int16_t)); return; }
    char fr[4][24], d[49] = {}, err[64] = {};
    dstar::ambeFrame(bits, fr);
    int e1 = 0, e2 = 0;
    short out[160] = {};
    p_->a->frame(out, &e1, &e2, err, fr, d, p_->s.cur.data(), p_->s.prev.data(), p_->s.enh.data(), 3);
    for (int i = 0; i < 160; i++) pcm[i] = out[i];
}

} // namespace dect2
