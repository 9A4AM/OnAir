# Voice and audio codecs

OnAir never ships a patented codec. You install the ones you are allowed to use yourself, and OnAir loads them. **Whether you may
install or use them is your own responsibility: follow the patent and radio-listening law where you live.**

Every mode that needs a codec has a small **?** button. It shows whether the library was found and where, the exact file names OnAir
looks for, the exact folders it searches, an **Open codecs folder** button (it makes the folder if it is missing) and **Look again**
(searches again, no restart).

## Which mode needs which codec

| Mode | Codec | Library you provide |
|---|---|---|
| DMR, P25 Phase 1, NXDN, dPMR | AMBE+2 / IMBE (patented) | **mbelib** (DMR: calls are shown, voice is not decoded in this version) |
| D-STAR | AMBE (patented) | **mbelib** |
| FreeDV (HF digital), M17 | codec2 (open) | **codec2** |
| TETRA | its own speech codec | nothing from you: it is not mbelib and not codec2 |
| DAB, DRM, TV, FM | MP2, AAC, H.264 ... | nothing (FFmpeg is inside OnAir) |

**codec2 does not help DMR, P25, NXDN or dPMR.** Putting `codec2.dll` or `libcodec2.dll` in the codecs folder only matters for FreeDV
and M17. If a digital-voice mode says "mbelib not found", you need **mbelib**, a different library.

## File names and search order (the same on every mode)

OnAir tries these file names. Folders are searched in this order; inside each folder every name is tried before the next folder.

1. The `DECT2_MBELIB_PATH` / `DECT2_CODEC2_PATH` environment variable (a full path), if set.
2. The OnAir **codecs folders** (below).
3. The program's own folder (Windows: next to `OnAir.exe`; macOS: inside the app, `Contents/Frameworks`).
4. The system: Windows `PATH`; macOS `/opt/homebrew/lib`, `/usr/local/lib`, `/opt/local/lib`; Linux `/usr/local/lib`, `/usr/lib`, `/usr/lib64`, the multiarch folder.

| Library | Windows | macOS | Linux |
|---|---|---|---|
| mbelib | `mbelib.dll`, `libmbe.dll`, `libmbe-1.dll`, `mbe.dll` | `libmbe.1.3.dylib`, `libmbe.1.dylib`, `libmbe.dylib` | `libmbe.so.1.3`, `libmbe.so.1`, `libmbe.so` |
| codec2 | `codec2.dll`, `libcodec2.dll` | `libcodec2.1.2.dylib` ... `libcodec2.dylib` | `libcodec2.so.1.2` ... `libcodec2.so` (any `libcodec2.so.*`) |

### The OnAir codecs folders

| OS | Next to the program | In your user folder |
|---|---|---|
| Windows | `codecs\` next to `OnAir.exe` | `%LOCALAPPDATA%\OnAir\codecs\` |
| macOS | `codecs/` next to `OnAir.app` | `~/Library/Application Support/OnAir/codecs/` |
| Linux | `codecs/` next to the `dect2` program | `~/.local/share/onair/codecs/` (or `$XDG_DATA_HOME/onair/codecs/`) |

The **?** popup prints the full paths on your computer. A DLL must match OnAir: 64-bit with 64-bit. If it needs other DLLs
(for example `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`), put them in the same folder.

## mbelib (DMR, P25, NXDN, dPMR, D-STAR voice): build it yourself

mbelib decodes AMBE/IMBE, which are patented in some countries (the project's own notice says so). OnAir does not ship, download or
link to binaries of it. Source: <https://github.com/lwvmobile/mbelib>. Check your local law first.

**Windows (MSYS2 / MinGW).** Install MSYS2 (msys2.org), open the "MSYS2 MINGW64" shell, paste:

```
pacman -S --needed git mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake make
git clone https://github.com/lwvmobile/mbelib
cd mbelib && mkdir build && cd build
cmake .. -G "MSYS Makefiles" -DBUILD_SHARED_LIBS=ON && make
```

Copy the resulting `libmbe.dll` (look in the `build` folder) and, if OnAir says it does not load, `libgcc_s_seh-1.dll` and
`libwinpthread-1.dll` from `C:\msys64\mingw64\bin`, into `%LOCALAPPDATA%\OnAir\codecs\`. Press **Look again**.

**macOS.**

```
brew install cmake git
git clone https://github.com/lwvmobile/mbelib
cd mbelib && mkdir build && cd build
cmake .. && make
mkdir -p "$HOME/Library/Application Support/OnAir/codecs"
cp libmbe*.dylib "$HOME/Library/Application Support/OnAir/codecs/"
```

**Linux.**

```
sudo apt install git cmake build-essential     # or your distribution's equivalent
git clone https://github.com/lwvmobile/mbelib
cd mbelib && mkdir build && cd build
cmake .. && make && sudo make install && sudo ldconfig
```

(or copy `libmbe.so*` into `~/.local/share/onair/codecs/`). If the build gives a static library only, add `-DBUILD_SHARED_LIBS=ON`.

Then open the mode and look at its **?** button: it says `mbelib found: <path>`. Encrypted P25/NXDN/DMR calls are only reported as
encrypted; OnAir never decrypts. DMR voice is not decoded in this version of OnAir, even with mbelib installed.

## codec2 (FreeDV, M17)

codec2 is open software (LGPL), but OnAir does not bundle it. It is only for FreeDV and M17.

- **macOS:** `brew install codec2`.
- **Linux:** `sudo apt install libcodec2-1.2` (Ubuntu 22.04: `libcodec2-1.0`), or `sudo dnf install codec2`.
- **Windows:** put `codec2.dll` or `libcodec2.dll` in the codecs folder. FreeDV's installer does not always place it in its `bin` folder: search the FreeDV install folder for `*codec2*.dll`, or in MSYS2 run `pacman -S mingw-w64-x86_64-codec2` and take `libcodec2.dll` from `C:\msys64\mingw64\bin` (with the DLLs it needs).

Open HF digital and pick FreeDV (or M17): the **?** button says `codec2 found: <path>`.

## HDC (HD Radio sound)

HD Radio sound uses HDC, a patented codec. OnAir shows the stations, the data and the pictures, but plays no sound. To hear HD Radio,
use nrsc5 (<https://github.com/theori-io/nrsc5>) with an RTL-SDR or an IQ recording.

## DRA+ (CDR sound)

CDR sound uses DRA+, a patented codec that is not openly described. OnAir lists the services but plays no sound. There is no free
DRA+ decoder to install at this time.

## AVS+, AVS2, AVS3 and DRA (DTMB television in China)

DTMB channels in China send their pictures as AVS+ (stream type 0x42; some as AVS2, 0xD2, or AVS3, 0xD4) and some send their sound as
DRA. These are Chinese patented codecs, and OnAir ships none of them: it plays what the FFmpeg built into it can decode.

- AVS1 in its JiZhun profile: FFmpeg's `cavs` decoder plays it (the macOS and Windows downloads; the Linux packages carry a smaller
  FFmpeg without it).
- AVS+ (the broadcast profile, 0x48): FFmpeg's `cavs` decoder refuses it ("only support JiZhun profile", FFmpeg 8 and 9). OnAir lists
  the service as TV, plays its sound, and says in the player that there is no picture. There is no free AVS+ decoder to install at this
  time.
- AVS2 and AVS3: FFmpeg decodes them only when it is built with libdavs2 or libuavs3d, which the FFmpeg in OnAir is not. Save the stream
  (Outputs: file or UDP) and play it with an FFmpeg build that has them (ffplay).
- DRA sound: FFmpeg has no DRA decoder. The player says "DRA audio: no decoder available"; a second sound track in another format plays.

The player panel has a **?** button next to these messages. A channel that signals a CA system (a CA descriptor, free_CA_mode) but sends
its streams in the clear is not shown as scrambled: OnAir goes by the scrambling bits of the stream packets.

## AMBE (Inmarsat Aero voice)

Aero C channel voice uses an AMBE vocoder. OnAir decodes the P channel data only and does not tune voice channels. Separate programs,
for example JAERO (<https://github.com/jontio/JAERO>), work on C channels.

## AMBE variant (Iridium voice)

OnAir counts Iridium voice frames but does not decode them. It hands the data off instead: **Save for iridium-toolkit** writes a
`.bits` file. Then:

1. Get iridium-toolkit: <https://github.com/muccc/iridium-toolkit>
2. `iridium-parser.py output.bits > output.parsed`
3. Install the `ir77_ambe_decode` program from osmo-ir77 (<http://git.osmocom.org/osmo-ir77>), as the toolkit's README explains, and
   use its `stats-voc.py` or `voc-cluster.py` to play the voice.

## AAC, xHE-AAC and the other FFmpeg codecs

DAB/DAB+ (MP2, AAC), DRM (AAC, xHE-AAC) and the TV modes decode with FFmpeg, which is built into every OnAir download (FFmpeg 8.0).
Nothing to install, with one exception: the SBR and parametric stereo of DAB+ HE-AAC (HE-AAC and HE-AAC v2 stations). FFmpeg cannot apply
them to the 960-sample frames of DAB+ ("SBR with 960 frame length"), so without help those stations play the core only: band-limited, and
HE-AAC v2 in mono. OnAir uses faad2 for them when it is installed (Linux: the `libfaad2` package; macOS: `brew install faad2`; Windows: a
`libfaad2.dll` beside the program). It is found at run time and not shipped; the DAB player panel says which decoder is working and whether SBR
and PS are applied. Some xHE-AAC tools (harmonic SBR, PVC, inter-TES, MPEG Surround 212) are not in FFmpeg yet; DRM stations that use
them stay silent and the DRM view says so.

## Legal note

Some of these codecs are covered by patents in some countries. Whether you install and use them is your choice and your
responsibility; check the rules where you live. OnAir does not ship them, does not download them, and contains no code from them.
