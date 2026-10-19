// Loader for user-installed speech codecs (see core/include/dect2/codec/speech_codec.h and docs/CODECS.md).
#include "dect2/codec/speech_codec.h"

#include <filesystem>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace dect2::codec {

namespace {

constexpr const char* kDocUrl = "https://github.com/adswill/OnAir/blob/main/docs/CODECS.md";
constexpr size_t kMaxSamplesPerFrame = 16000;   // a bit over one second at 16 kHz: no coded frame is longer

struct Api {
    void* (*open)() = nullptr;
    void (*close)(void*) = nullptr;
    int (*rate)() = nullptr;
    int (*decode)(void*, const uint8_t*, size_t, int16_t*, size_t) = nullptr;
};

void* openLibrary(const std::string& path) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (n <= 0) return nullptr;
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), n);
    return reinterpret_cast<void*>(LoadLibraryW(w.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void closeLibrary(void* lib) {
    if (!lib) return;
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(lib));
#else
    dlclose(lib);
#endif
}

void* symbol(void* lib, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

// Decoder over a plug-in: owns the library handle and the codec state.
class PluginCodec final : public SpeechCodec {
public:
    PluginCodec(Api api, void* state, void* lib) : api_(api), state_(state), lib_(lib), rate_(api.rate()) {}
    ~PluginCodec() override {
        if (state_) api_.close(state_);
        closeLibrary(lib_);
    }
    int sampleRate() const override { return rate_; }
    std::vector<int16_t> decode(const std::vector<uint8_t>& coded) override {
        std::vector<int16_t> pcm(kMaxSamplesPerFrame);
        const int n = api_.decode(state_, coded.data(), coded.size(), pcm.data(), pcm.size());
        if (n <= 0) return {};
        pcm.resize(static_cast<size_t>(n));
        return pcm;
    }
private:
    Api api_;
    void* state_;
    void* lib_;
    int rate_;
};

std::unique_ptr<SpeechCodec> openSharedLibrary(const std::string& path, std::string& err) {
    void* lib = openLibrary(path);
    if (!lib) { err = "the file would not load"; return nullptr; }
    Api a;
    a.open = reinterpret_cast<decltype(a.open)>(symbol(lib, "onair_speech_open"));
    a.close = reinterpret_cast<decltype(a.close)>(symbol(lib, "onair_speech_close"));
    a.rate = reinterpret_cast<decltype(a.rate)>(symbol(lib, "onair_speech_sample_rate"));
    a.decode = reinterpret_cast<decltype(a.decode)>(symbol(lib, "onair_speech_decode"));
    if (!a.open || !a.close || !a.rate || !a.decode) { closeLibrary(lib); err = "it does not have the codec functions"; return nullptr; }
    void* state = a.open();
    if (!state) { closeLibrary(lib); err = "its decoder would not start"; return nullptr; }
    return std::make_unique<PluginCodec>(a, state, lib);
}

bool validId(const std::string& id) {
    if (id.empty()) return false;
    for (const char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    return true;
}

} // namespace

std::string speechCodecNotice() {
    return "Legal notice. OnAir does not include, download or distribute this speech codec. You install it yourself, and you are "
           "responsible for it: for following the laws where you live on receiving and listening to radio transmissions, and for the "
           "licence and patent terms of the codec. Install only a codec you are allowed to use.";
}

std::string speechCodecFileName(const std::string& id) {
#ifdef _WIN32
    const char* ext = ".dll";
#elif defined(__APPLE__)
    const char* ext = ".dylib";
#else
    const char* ext = ".so";
#endif
    return "onair-codec-" + id + ext;
}

CodecLookup loadSpeechCodec(const std::string& id, const std::string& folder, bool noticeAccepted,
                            std::unique_ptr<SpeechCodec>& out, const CodecOpener& opener) {
    CodecLookup r;
    out.reset();
    if (!validId(id)) {
        r.status = CodecStatus::LoadFailed;
        r.message = "Codec id \"" + id + "\" is not valid.";
        return r;
    }
    const std::string file = speechCodecFileName(id);
    const std::filesystem::path path = std::filesystem::path(folder) / file;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        r.status = CodecStatus::NotInstalled;
        r.message = "Codec not installed: this mode needs the " + id + " speech codec. OnAir does not include it. Install it yourself "
                    "(see " + std::string(kDocUrl) + ") and put the plug-in file " + file + " in " + folder + ".";
        return r;
    }
    r.path = path.string();
    if (!noticeAccepted) {
        r.status = CodecStatus::NoticeRequired;
        r.message = "Accept the codec notice to load the " + id + " speech codec.";
        return r;
    }
    std::string err;
    std::unique_ptr<SpeechCodec> codec = opener ? opener(r.path, err) : openSharedLibrary(r.path, err);
    if (!codec) {
        r.status = CodecStatus::LoadFailed;
        r.message = "The " + id + " codec at " + r.path + " could not be loaded: " + err + ".";
        return r;
    }
    out = std::move(codec);
    r.status = CodecStatus::Ready;
    r.message = "Codec loaded: " + id + ".";
    return r;
}

} // namespace dect2::codec
