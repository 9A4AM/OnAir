// The "?" button next to the sound of each mode whose voice or audio needs a codec that OnAir does not ship (docs/CODECS.md). It says whether
// the codec was found and where, what it is, how to install it on this computer, where to put it, a link to the full page, and "Look again".
#include "app.h"
#include "dect2/hfdig_freedv.h"
#include "dect2/dstar_ambe.h"
#include "dect2/nxdn_ambe.h"
#include "dect2/p25_imbe.h"
#include "dect2/updater.h"
#include <string>
#include <vector>

namespace {

constexpr const char* kDocUrl = "https://github.com/adswill/OnAir/blob/main/docs/CODECS.md";

struct Text {
    const char* title;
    const char* what;
    const char* install;   // for this computer
    bool loadable;         // OnAir loads it when installed (otherwise the text points to another program)
};

Text textOf(CodecHelp c) {
    switch (c) {
    case CodecHelp::Codec2:
        return {"codec2 (FreeDV voice)",
                "codec2 is the open voice codec of FreeDV. It is free software, but OnAir does not ship it. When it is installed, OnAir loads it "
                "and plays FreeDV speech.",
#if defined(__APPLE__)
                "Install Homebrew (brew.sh), then run in Terminal:\n  brew install codec2",
#elif defined(_WIN32)
                "Install FreeDV for Windows (freedv.org/download). Copy libcodec2.dll from its bin folder (C:\\Program Files\\FreeDV ...\\bin) "
                "into an OnAir codecs folder below. Use the same kind of Windows (x86_64 or ARM) as OnAir.",
#else
                "Debian / Ubuntu:  sudo apt install libcodec2-1.2\n(Ubuntu 22.04: libcodec2-1.0)\nFedora:  sudo dnf install codec2",
#endif
                true};
    case CodecHelp::DmrAmbe:
        return {"AMBE+2 (DMR voice)",
                "DMR voice uses AMBE+2, a patented vocoder. OnAir finds, counts and logs voice calls, but plays no sound, and it cannot load an "
                "AMBE library yet.",
                "To hear DMR voice, use a separate program such as DSD-FME (github.com/lwvmobile/dsd-fme), which uses the mbelib "
                "library. Check the patent rules where you live first.",
                false};
    case CodecHelp::Hdc:
        return {"HDC (HD Radio sound)",
                "HD Radio sound uses HDC, a patented codec. OnAir shows the stations, the data and the pictures, but plays no sound, and it "
                "cannot load an HDC library.",
                "To hear HD Radio, use the separate program nrsc5 (github.com/theori-io/nrsc5) with an RTL-SDR or an IQ recording.",
                false};
    case CodecHelp::Dra:
        return {"DRA+ (CDR sound)",
                "CDR sound uses DRA+, a patented codec without an open description. OnAir shows the services, but plays no sound.",
                "There is no free DRA+ decoder to install at this time.", false};
    case CodecHelp::AeroAmbe:
        return {"AMBE (Inmarsat Aero voice)",
                "Aero C channel voice uses an AMBE vocoder, which is patented. OnAir decodes the P channel data only; it does not tune voice "
                "channels and cannot load an AMBE library.",
                "There is nothing to install for OnAir. Some separate programs (for example JAERO, github.com/jontio/JAERO) work on C channels.",
                false};
    case CodecHelp::IridiumAmbe:
        return {"AMBE (Iridium voice)",
                "Iridium voice uses an AMBE variant. OnAir counts voice frames but does not decode them. It can save the bursts as an "
                "iridium-toolkit .bits file instead.",
                "Save a .bits file (Save for iridium-toolkit), then use iridium-toolkit (github.com/muccc/iridium-toolkit) with the "
                "ir77_ambe_decode program from osmo-ir77. Their README explains the steps.",
                false};
    case CodecHelp::DstarAmbe:
        return {"mbelib (D-STAR AMBE voice)",
                "D-STAR voice uses AMBE, a patented vocoder. OnAir does not ship it: calls, callsigns, messages and positions are shown without "
                "it. The open mbelib library decodes AMBE; when you install it yourself, OnAir loads it and plays D-STAR voice. Its own notice "
                "says that using it may need a patent licence: check the rules where you live first.",
#if defined(__APPLE__)
                "Homebrew has no formula; build it from github.com/szechyjs/mbelib:\n  brew install cmake\n  (clone the repository)\n"
                "  mkdir build && cd build && cmake .. && make\nthen copy libmbe.dylib into an OnAir codecs folder below.",
#elif defined(_WIN32)
                "Build mbelib (github.com/szechyjs/mbelib) with CMake and Visual Studio, or take mbe.dll / libmbe.dll from a Windows build of "
                "DSD-FME, and copy it into an OnAir codecs folder below. Use the same kind of Windows (x86_64 or ARM) as OnAir.",
#else
                "Most distributions do not package it; build it from github.com/szechyjs/mbelib (needs cmake and a C compiler):\n"
                "  mkdir build && cd build && cmake .. && make && sudo make install",
#endif
                true};
    case CodecHelp::DpmrAmbe:
        return {"mbelib (dPMR AMBE+2 voice)",
                "dPMR voice uses AMBE+2, a patented vocoder. OnAir does not ship it: calls, own and called IDs, colour codes, status and short "
                "messages are shown without it. The open mbelib library decodes AMBE; when you install it yourself, OnAir loads it and plays dPMR "
                "voice. Its own notice says that using it may need a patent licence: check the rules where you live first.",
#if defined(__APPLE__)
                "Homebrew has no formula; build it from github.com/szechyjs/mbelib:\n  brew install cmake\n  (clone the repository)\n"
                "  mkdir build && cd build && cmake .. && make\nthen copy libmbe.dylib into an OnAir codecs folder below.",
#elif defined(_WIN32)
                "Build mbelib (github.com/szechyjs/mbelib) with CMake and Visual Studio, or take mbe.dll / libmbe.dll from a Windows build of "
                "DSD-FME, and copy it into an OnAir codecs folder below. Use the same kind of Windows (x86_64 or ARM) as OnAir.",
#else
                "Most distributions do not package it; build it from github.com/szechyjs/mbelib (needs cmake and a C compiler):\n"
                "  mkdir build && cd build && cmake .. && make && sudo make install",
#endif
                true};
    case CodecHelp::NxdnAmbe:
        return {"mbelib (NXDN AMBE+2 voice)",
                "NXDN voice uses AMBE+2, a patented vocoder. OnAir does not ship it: calls, unit IDs, talkgroups, messages and site information are shown without "
                "it. The open mbelib library decodes AMBE; when you install it yourself, OnAir loads it and plays NXDN voice. Its own notice "
                "says that using it may need a patent licence: check the rules where you live first.",
#if defined(__APPLE__)
                "Homebrew has no formula; build it from github.com/szechyjs/mbelib:\n  brew install cmake\n  (clone the repository)\n"
                "  mkdir build && cd build && cmake .. && make\nthen copy libmbe.dylib into an OnAir codecs folder below.",
#elif defined(_WIN32)
                "Build mbelib (github.com/szechyjs/mbelib) with CMake and Visual Studio, or take mbe.dll / libmbe.dll from a Windows build of "
                "DSD-FME, and copy it into an OnAir codecs folder below. Use the same kind of Windows (x86_64 or ARM) as OnAir.",
#else
                "Most distributions do not package it; build it from github.com/szechyjs/mbelib (needs cmake and a C compiler):\n"
                "  mkdir build && cd build && cmake .. && make && sudo make install",
#endif
                true};
    case CodecHelp::P25Imbe:
        return {"mbelib (P25 IMBE voice)",
                "P25 Phase 1 voice uses IMBE, a patented vocoder. OnAir does not ship it: calls, unit IDs, talkgroups and the trunking system are shown "
                "without it. The open mbelib library decodes IMBE; when you install it yourself, OnAir loads it and plays clear P25 voice. Encrypted "
                "calls are only reported as encrypted: OnAir never decrypts. mbelib's own notice says that using it may need a patent licence, and "
                "listening to radio services is regulated in many countries: check the rules where you live first.",
#if defined(__APPLE__)
                "Homebrew has no formula; build it from github.com/szechyjs/mbelib:\n  brew install cmake\n  (clone the repository)\n"
                "  mkdir build && cd build && cmake .. && make\nthen copy libmbe.dylib into an OnAir codecs folder below.",
#elif defined(_WIN32)
                "Build mbelib (github.com/szechyjs/mbelib) with CMake and Visual Studio, or take mbe.dll / libmbe.dll from a Windows build of "
                "DSD-FME, and copy it into an OnAir codecs folder below. Use the same kind of Windows (x86_64 or ARM) as OnAir.",
#else
                "Most distributions do not package it; build it from github.com/szechyjs/mbelib (needs cmake and a C compiler):\n"
                "  mkdir build && cd build && cmake .. && make && sudo make install",
#endif
                true};
    case CodecHelp::Avs:
        return {"AVS+, AVS2 and AVS3 (TV pictures in China)",
                "DTMB television in China sends its pictures as AVS+ (and some channels as AVS2 or AVS3), Chinese patented video codecs. "
                "OnAir plays them through the FFmpeg built into it and ships no codec of its own. FFmpeg decodes AVS1 in its JiZhun profile, "
                "but not AVS+ (the broadcast profile), and AVS2 and AVS3 only when it was built with libdavs2 or libuavs3d, which the FFmpeg in "
                "OnAir is not. The service list, the sound and the guide work without the picture.",
                "There is no free AVS+ decoder to install at this time. For AVS2 or AVS3, save the stream (Outputs: file or UDP) and play it "
                "with an FFmpeg build that includes libdavs2 or libuavs3d (ffplay). Check the patent rules where you live first.",
                false};
    case CodecHelp::DraTv:
        return {"DRA (TV sound in China)",
                "Some DTMB channels in China send their sound as DRA, a Chinese patented audio codec. FFmpeg has no DRA decoder, so OnAir shows "
                "the picture without that sound; a second sound track in another format (MPEG audio, AAC, AC-3) plays.",
                "There is no free DRA decoder to install at this time.", false};
    }
    return {"", "", "", false};
}

} // namespace

void codecHelpButton(CodecHelp c) {
    const Text t = textOf(c);
    ImGui::PushID((int)c + 7100);
    if (ImGui::SmallButton("?")) ImGui::OpenPopup("codechelp");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How to get the %s codec", t.title);
    if (ImGui::BeginPopup("codechelp")) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
        ImGui::TextUnformatted(t.title);
        ImGui::Separator();
        if (t.loadable) {
            std::string path, ver;
            const bool mbe = c == CodecHelp::DstarAmbe || c == CodecHelp::NxdnAmbe || c == CodecHelp::DpmrAmbe || c == CodecHelp::P25Imbe;
            const bool found = c == CodecHelp::P25Imbe ? dect2::p25ImbeAvailable(&path) : c == CodecHelp::NxdnAmbe || c == CodecHelp::DpmrAmbe ? dect2::nxdnAmbeAvailable(&path) : mbe ? dect2::dstarAmbeAvailable(&path) : dect2::hfdigFreedvLibrary(&path, &ver);
            if (found) ImGui::TextColored(pal::okGreen(), "%s found: %s", mbe ? "mbelib" : "codec2", path.c_str());
            else ImGui::TextColored(pal::warnAmber(), "%s not found%s", mbe ? "mbelib" : "codec2", mbe ? ": calls and data are shown, without sound" : "");
        } else {
            ImGui::TextColored(pal::warnAmber(), "Not decoded in OnAir");
        }
        ImGui::Spacing();
        ImGui::TextUnformatted(t.what);
        ImGui::Spacing();
        ImGui::TextDisabled(t.loadable ? "Install" : "What you can do");
        ImGui::TextUnformatted(t.install);
        if (t.loadable) {
            ImGui::Spacing();
            ImGui::TextDisabled("Where to put it");
#if defined(_WIN32)
            ImGui::TextUnformatted("Into one of these folders (make it if needed):");
#else
            ImGui::TextUnformatted("The package manager's place works. Or copy the library into one of these folders:");
#endif
            for (const auto& d : dect2::codecFolderList()) ImGui::BulletText("%s", d.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Look again")) { if (c == CodecHelp::P25Imbe) dect2::p25ImbeRescan(); else if (c == CodecHelp::NxdnAmbe || c == CodecHelp::DpmrAmbe) dect2::nxdnAmbeRescan(); else if (c == CodecHelp::DstarAmbe) dect2::dstarAmbeRescan(); else dect2::hfdigFreedvRescan(); }
            ImGui::SameLine();
        }
        if (ImGui::Button("Full instructions")) dect2::openUrl(kDocUrl);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kDocUrl);
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
    ImGui::PopID();
}
