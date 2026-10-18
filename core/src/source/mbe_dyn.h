// The user-installed mbelib, loaded at run time (patented voice codecs are never shipped, docs/CODECS.md): the part that the AMBE (D-STAR, NXDN) and IMBE (P25) decoders share.
// Only two functions of mbelib.h (1.3) are needed: mbe_initMbeParms and one frame function per codec. mbe_parms is a plain struct of a few kilobytes at most; it is kept in a
// larger buffer of our own (MbeState), so its layout is never needed.
#pragma once
#include "native_common.h"
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace dect2::mbe {

template <class Frame>
struct Api {
    native::DynLib lib;
    bool ok = false;
    void (DECT2_CALL* init)(void* cur, void* prev, void* prevEnh) = nullptr;
    Frame frame = nullptr;
};

// One per codec (a file-scope object): finds the library once, on first use, and again when asked (the "Look again" button). The Api found stays for the life of the program,
// the decoders hold the one they started with.
template <class Frame>
class Loader {
public:
    explicit Loader(const char* frameSymbol) : sym_(frameSymbol) {}
    const Api<Frame>& api() {
        if (const Api<Frame>* a = api_.load(std::memory_order_acquire)) return *a;
        std::lock_guard<std::mutex> lk(mu_);
        if (!api_.load()) api_.store(scan(), std::memory_order_release);
        return *api_.load();
    }
    bool available(std::string* path) {
        const Api<Frame>& a = api();
        if (a.ok && path) *path = a.lib.path;
        return a.ok;
    }
    bool rescan() {
        if (api().ok) return true;
        std::lock_guard<std::mutex> lk(mu_);
        api_.store(scan(), std::memory_order_release);
        return api_.load()->ok;
    }

private:
    const Api<Frame>* scan() {
        Api<Frame>* r = new Api<Frame>;
        std::vector<std::string> names;
        if (const char* e = getenv("DECT2_MBELIB_PATH")) if (*e) names.push_back(e);
        if (names.empty()) names = native::codecLibNames(DECT2_MBE_STEM, DECT2_MBE_SO, DECT2_MBE_WIN);
        if (r->lib.open(names)) r->ok = r->lib.get(r->init, "mbe_initMbeParms") && r->lib.get(r->frame, sym_);
        return r;
    }
    const char* sym_;
    std::mutex mu_;
    std::atomic<const Api<Frame>*> api_{nullptr};
};

// The state of one decoder: the current, previous and enhanced parameter blocks (16 kB each).
struct State {
    std::vector<double> cur, prev, enh;
    State() : cur(2048, 0.0), prev(2048, 0.0), enh(2048, 0.0) {}
};

} // namespace dect2::mbe
