# Licences of the libraries inside the OnAir packages

OnAir is GPL-3.0-or-later (file LICENSE; source: https://github.com/adswill/OnAir). The full list of third-party code, data and credits is THIRD_PARTY.md; the licence texts are in LICENSES/.

## FFmpeg (libavcodec, libavformat, libavutil, libswscale, libswresample)

- Version 8.0 (https://ffmpeg.org/releases/ffmpeg-8.0.tar.xz, unmodified), licence LGPL-2.1-or-later (LICENSES/LGPL-2.1.txt).
- Built by tools/package/build_ffmpeg_static.sh, **without** --enable-gpl, --enable-version3 and --enable-nonfree, and with no external libraries (no x264, x265 and so on). The script stops if the configuration says otherwise, and tools/package/check_lgpl_bundle.sh fails the package build if the FFmpeg inside is a GPL build or a GPL-only library is bundled.
- Linux (.deb and portable), macOS (.dmg) and Windows (zip, installer): linked statically into the program. The exact configure line is in the macOS app at OnAir.app/Contents/Resources/licenses/ffmpeg/configuration.txt.
- **Source offer / rebuild:** download the FFmpeg source above, run `tools/package/build_ffmpeg_static.sh <folder>` (Windows: tools/package/cross_deps_windows.sh), then build OnAir from its public source with `-DDECT2_FFMPEG_PREFIX=<folder>`; the relinked program replaces the shipped one. OnAir as a whole is GPL-3.0-or-later with its source public, so this satisfies the LGPL relinking terms.
- The patent licences of the video and audio formats (H.264, H.265, AAC, AC-3 ...) are the user's matter, as with FFmpeg everywhere.
- Homebrew's and MSYS2's FFmpeg packages are configured with --enable-gpl and are never bundled.

Other bundled libraries (Hamlib, codec2, libusb, libhackrf, GLFW, the radio vendor libraries): see THIRD_PARTY.md; each package also carries the licence files of the libraries it contains.
