// AMBE through the user-installed mbelib (see nxdn_ambe.h); the loader is shared with the other voice decoders (source/mbe_dyn.h).
#include "dect2/nxdn/nxdn_ambe.h"
#include "dect2/nxdn/nxdn_proto.h"
#include "../source/mbe_dyn.h"
#include <cstring>

namespace dect2 {

namespace {
using FrameFn = void (DECT2_CALL*)(short* aout, int* errs, int* errs2, char* errStr, char fr[4][24], char d[49], void* cur, void* prev, void* prevEnh, int uvq);
mbe::Loader<FrameFn> gMbe("mbe_processAmbe3600x2450Frame");
} // namespace

bool nxdnAmbeAvailable(std::string* path) { return gMbe.available(path); }
bool nxdnAmbeRescan() { return gMbe.rescan(); }

struct NxdnAmbe::Impl {
    const mbe::Api<FrameFn>* a = nullptr;
    mbe::State s;
};

NxdnAmbe::NxdnAmbe() : p_(std::make_unique<Impl>()) {
    const mbe::Api<FrameFn>& a = gMbe.api();
    if (!a.ok) return;
    p_->a = &a;
    a.init(p_->s.cur.data(), p_->s.prev.data(), p_->s.enh.data());
}
NxdnAmbe::~NxdnAmbe() = default;
bool NxdnAmbe::ok() const { return p_->a != nullptr; }

void NxdnAmbe::decode(const uint8_t bits[72], int16_t pcm[160]) {
    if (!p_->a) { memset(pcm, 0, 160 * sizeof(int16_t)); return; }
    char fr[4][24], d[49] = {}, err[64] = {};
    nxdn::ambeFrame(bits, fr);
    int e1 = 0, e2 = 0;
    short out[160] = {};
    p_->a->frame(out, &e1, &e2, err, fr, d, p_->s.cur.data(), p_->s.prev.data(), p_->s.enh.data(), 3);
    for (int i = 0; i < 160; i++) pcm[i] = out[i];
}

} // namespace dect2
