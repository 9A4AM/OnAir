# Third-party code, data and credits

OnAir is released under the **GNU General Public License v3.0 or later** (see [LICENSE](LICENSE)). It stands on the work of many people. This file lists the third-party and derived code, libraries, data and specifications behind it, what each licence asks of us, and where we are not sure. The same list is in the program (Help, Credits and licences), on the website and in the wiki. If you wrote something we use and you are not named correctly, please open an issue and we will fix it at once.

How to read the "How" column: **vendored** means the source is in this repository and compiled into OnAir; **ported / re-implemented / read** means OnAir's own code was written from that project's source or documentation (file headers name the project); **linked** means a library OnAir is built against; **loaded at run time** means the user installs it and OnAir finds it, it is not shipped.

## The list

| Name | Author | Licence | Where used | How | What it needs / status |
|---|---|---|---|---|---|
| Dear ImGui | Omar Cornut and contributors | MIT | app/ and third_party/imgui (user interface) | vendored, compiled in | Keep the copyright notice and licence text (third_party/imgui/LICENSE.txt, reproduced in THIRD_PARTY.md). GPL-3 compatible. |
| stb_truetype, stb_rect_pack, stb_textedit | Sean Barrett and contributors | MIT or public domain | third_party/imgui/imstb_*.h (fonts and text input) | vendored, compiled in | Notice kept in the file headers. GPL-3 compatible. |
| miniaudio | David Reid | Public domain or MIT-0 | third_party/miniaudio (sound output and input) | vendored, compiled in | No attribution required (credited anyway). GPL-3 compatible. |
| codec2 | David Rowe and contributors | LGPL-2.1 | third_party/codec2 (FreeDV and M17 speech) | vendored, built as a separate shared library, loaded at run time | Licence text and source shipped, library replaceable (docs/CODECS.md). LGPL-2.1 may be combined with GPL-3. |
| DRA decoder (dradec) | maliwen2015, from ffmpeg_cavs_dra | MIT (as published there) | third_party/dra (CDR digital radio audio) | vendored, compiled in | MIT notice added to the file. UNKNOWN: the file carries no earlier copyright and the code behind it (a DRA reference decoder, shipped inside an FFmpeg patch) has no stated origin. See the conflicts section. |
| uat_fec (Reed-Solomon for UAT) | Oliver Jowett (dump978); Phil Karn KA9Q (RS library) | GPL-2.0-or-later (Jowett); LGPL (Karn) | third_party/uat_fec, core/src/uat/uat_decode.c, uat_fec_bridge.c (978 MHz UAT) | vendored, compiled in | Headers kept. "Or later" lets it be combined into GPL-3.0-or-later. Licence text in LICENSES/GPL-2.0.txt. |
| SDDC_FX3.img (RX888 MkII firmware) | Oscar Steila ik1xpv (ExtIO_sddc); renardspark (SDDC_Driver); Cypress FX3 SDK parts | MIT (SDDC projects); Cypress/Infineon SDK terms for its parts | third_party/sddc_fx3, copied to the codecs folder | binary firmware, loaded into the radio's RAM at run time | MIT text shipped next to it. UNKNOWN: the Cypress SDK terms of the binary were not checked. Not linked into the program. |
| nrsc5 | Theori, Clayton Smith and contributors | GPL-3.0-or-later | core/src/hdr (HD Radio) | ported (adapted), compiled in | Same licence family; source offered by this repository. File headers name it. |
| etisnoop | CSP Innovazione nelle ICT, Matthias P. Braendli and contributors | GPL-3.0-or-later | core/src/dab/dab_fig*.cpp (DAB service information) | ported, compiled in | Copyright lines kept in dab_fig.cpp. GPL-3+ compatible. |
| gr-dvbt2 | gr-dvbt2 authors | GPL-3.0 | core/src/t2/*_tables.cpp (DVB-T2 pilot, FEC and mapping tables) | values dumped from the transmitter, compiled in | Data taken from a GPL-3 program; file headers name it. Compatible. |
| e-Czas | sp5wwp | GPL-3.0 | core/src/eczas (PCSK225 time signal) | ported, compiled in | Compatible; headers name it. |
| WSJT-X | Joe Taylor K1JT, Steve Franke K9AN, Bill Somerville G4WJS, Nico Palermo IV3NWV and the WSJT Development Group | GPL-3.0 | core/src/hfdig (FT8, FT4, FST4, JT9, JT65, Q65, MSK144), core/src/js8 (LDPC tables, message packing) | read and re-implemented; LDPC and QRA tables copied; compiled in | Compatible. Table files carry the credit in their headers. |
| WSJT-X Improved | Uwe Risse DG2YCB and contributors | GPL-3.0 | core/src/hfdig (FT2 mode) | read and re-implemented | Compatible. |
| JS8Call | Jordan Sherer KN4CRD and contributors | GPL-3.0 | core/src/js8 (JS8 modes) | read and re-implemented; compiled in | Compatible. |
| JTEncode | Jason Milldrum NT7S, Etherkit | GPL-3.0 | core/src/hfdig (WSPR cross-check) | read as a cross-check | Compatible. |
| fldigi | W1HKJ, Dave Freese and contributors (Olivia modem: Pawel Jalocha SP9VRC) | GPL-3.0 | core/src/hfdig/hfdig_kbd*.cpp (PSK, MFSK, DominoEX, Olivia, Contestia) | re-implemented; tables generated from it; compiled in | Compatible. Headers name it. |
| rtl_433 | Benjamin Larsson and contributors | GPL-2.0 (files say "or later") | core/src/ism (433/868/915 MHz sensors) | re-written from its device decoders, compiled in | The files read carry "version 2 or at your option any later version", which is GPL-3 compatible. Verify per file if more devices are ported. Not GPL-2.0-only as far as seen. |
| gr-lora_sdr | Joachim Tapparel (EPFL) and contributors | GPL-3.0 | core/src/mesh (LoRa physical layer) | facts and tables read, re-implemented | Compatible. |
| LoRa_APRS_iGate | Ricardo Guzman (richonguzman) | GPL-3.0 | core/src/mesh/mesh_proto_aprs.cpp (LoRa APRS framing) | read, re-implemented | Compatible. |
| Meshtastic | Meshtastic contributors | GPL-3.0 | core/src/mesh (packet and channel formats) | protocol read, re-implemented | Compatible. |
| MeshCore | MeshCore contributors | MIT | core/src/mesh (MeshCore packets) | protocol read, re-implemented | MIT; credited. GPL-3 compatible. |
| MeshCom firmware | icssw-org | MIT | core/src/mesh (MeshCom over LoRa APRS) | protocol read, re-implemented | MIT; credited. Compatible. |
| acarsdec | Thierry Leconte F4FXL | GNU Library GPL 2 (msk.c header) | core/src/acars (MSK demodulator, CRC repair) | method read, re-written | LGPL-2.0 text allows conversion to the ordinary GPL, but only 2.0 is named. POSSIBLE CONFLICT with GPL-3, see the conflicts section. |
| libacars | Tomasz Lemiech (szpajder) | MIT | core/src/acars, core/src/aero (ARINC 622 layout) | protocol read, re-implemented | MIT; credited. Compatible. |
| dumphfdl, dumpvdl2 | Tomasz Lemiech (szpajder) | GPL-3.0 | core/src/hfdl, core/src/vdl2 | protocol facts read, not copied | Compatible. |
| JAERO | Jonti Olds | MIT | core/src/aero (Inmarsat Aero) | constants and structure read, re-implemented | MIT; credited. Compatible. |
| iridium-toolkit | muccc (Chaos Computer Club Munich) | No licence file in the repository (not stated) | core/src/iridium (frame formats, BCH) | facts (bit layouts, polynomials) read, no code copied | UNKNOWN licence. Only facts of the air format were used; credited. |
| gr-iridium | muccc | GPL-3.0-or-later | core/src/iridium (symbol mapping) | facts read | Compatible. |
| multimon-ng | Elias Oenal, Thomas Sailer and contributors | GPL-2.0 | core/src/pager (POCSAG capcode and alphanumeric rules) | rules read, re-written | Project is GPL-2.0 and pocsag.c says "version 2 or any later version", so compatible. Only formulas and field layouts were used. |
| dump1090 and readsb | Salvatore Sanfilippo, Oliver Jowett, Matthew Kirkwood and contributors | GPL-2.0-or-later / GPL-3.0 | core/src/net (Beast and SBS feed formats) | formats read, re-implemented | Compatible (formats). |
| welle.io | Albrecht Lohofener and contributors | GPL-2.0-or-later | core/src/common/text_charset.cpp (EBU Latin table) | table generated from it | "Or later": compatible. |
| ODR-PadEnc | Matthias P. Braendli, Opendigitalradio | GPL-3.0-or-later | core/src/common/text_charset.cpp (cross-check) | table checked against it | Compatible. |
| FFmpeg (AAC tables) | FFmpeg developers | LGPL-2.1-or-later | core/src/drm/drm_aactab.cpp (AAC window and scale-factor values) | data copied by a script | LGPL-2.1+ data, standard's values. Notice named in the file. Compatible. |
| glibc iso_6937 | GNU C Library authors | LGPL-2.1-or-later | core/src/common/text_charset.cpp (ISO 6937) | table generated from it | Compatible. |
| SatDump and DSD-FME | Aang23 and contributors; Louis Wilson lwvmobile | GPL-3.0 | satellite and digital-voice formats | behaviour compared only; DMR voice points users to DSD-FME | No code taken. |
| DJI DroneID research | Schiller et al. (NDSS 2023); proto17/dji_droneid | Paper; MIT | core/src/dji | frame structure written from the public description | MIT; credited. The AGPL RUB-SysSec receiver was not used. |
| SDRplay API headers | SDRplay Limited | Proprietary (headers); library not shipped | core/src/source/sdrplay_api_min.h | struct layouts written out for run-time loading | UNCLEAR: the layouts follow the official headers, whose licence was not checked. The library itself is the user's download. |
| FFmpeg libraries | FFmpeg developers | LGPL-2.1-or-later (built without --enable-gpl) | video and audio decoding | linked (static LGPL build in packages) | Source offer and licence text must ship with packages; the build script (tools/package/build_ffmpeg_static.sh) uses no GPL options. |
| GLFW | Marcus Geelnard, Camilla Löwy and contributors | zlib | window and input | linked | No notice needed in binaries; credited. |
| libusb | libusb contributors | LGPL-2.1-or-later | USB radios | linked or loaded | Licence text must ship with packages. |
| libhackrf | Great Scott Gadgets, Michael Ossmann, Jared Boone | BSD-3-Clause | HackRF | linked | BSD notice must be reproduced in binary packages (THIRD_PARTY.md does so in short). |
| Hamlib | The Hamlib group | LGPL-2.1-or-later | CAT control | linked | Notice in packaging/licenses/hamlib. |
| SoapySDR | Josh Blum, Pothosware | BSL-1.0 | radio abstraction | linked or loaded | No binary notice needed. |
| Radio vendor libraries | librtlsdr, libairspy, libbladeRF, libLimeSuite, libiio, UHD, Fobos, librfnm and others | GPL, BSD, LGPL or MIT (each its own) | native radio drivers | loaded at run time from the user's install, not shipped | Not distributed with OnAir. Run-time loading by a GPL program is not a licence issue for a separate library. |
| SDRplay API | SDRplay Limited | Proprietary | RSP radios | loaded at run time, user installs it | Not shipped; its licence forbids redistribution. |
| LAME | The LAME project | LGPL-2.0-or-later | MP3 recording | loaded at run time, user installs it | Not shipped. |
| faad2 | Ahead Software, Nero and contributors | GPL-2.0-or-later | DAB+ HE-AAC (SBR, PS) | loaded at run time, user installs it | Not shipped. |
| mbelib | szechyjs and contributors | Not verified (patented vocoders) | DMR, P25, NXDN, D-STAR voice | loaded at run time, user installs it | Not shipped, by design (docs/CODECS.md). |
| RADE (librade) | David Rowe and contributors | BSD-2-Clause | FreeDV RADE | loaded at run time, optional | Not shipped. |
| MapLibre GL JS 5.7.3 | MapLibre contributors | BSD-3-Clause | web/map (live map page) | loaded by the visitor's browser from unpkg | Not bundled in this repository. BSD notice is in the library's own files. |
| Wrangler and Cloudflare Workers | Cloudflare | Apache-2.0 / MIT | web/map and web/discord (development tooling and hosting) | dev dependency, not shipped | Not shipped to users. |
| OpenStreetMap tiles and data | OpenStreetMap contributors | ODbL 1.0 (data), tile usage policy | map background in the app and on the live map | downloaded at run time | Attribution "(c) OpenStreetMap contributors" must be visible; follow the tile usage policy (cached tiles, User-Agent). |
| adsbdb | Matthew Jackson (mrjackwills) | MIT (service code); data from public sources | aircraft type, airline and route lookups on the live map | web service, queried by the map worker | Credit the service. |
| Planespotters photos | Planespotters.net and the individual photographers | Photographers keep their rights, shown with credit and link | aircraft photo on the live map card | fetched in the visitor's browser after a click | Credit and link are shown with each photo; nothing is stored. |
| CelesTrak TLE data | T.S. Kelso, CelesTrak | Public data, free use with credit | satellite pass prediction | downloaded at run time | Credit. |
| OurAirports navaids | David Megginson | Public domain | VOR station list | user downloads the CSV, nothing bundled | None. |
| OpenAIP | OpenAIP | CC BY-NC (stated in the app text) | VOR station list alternative | user exports a file, nothing bundled | Non-commercial data: the user fetches it themselves under their own terms. Nothing is distributed by OnAir. |
| OpenCelliD | Unwired Labs | CC BY-SA 4.0 | cell tower lookups | downloaded by the user | Attribution and share-alike apply to data the user keeps. |
| Dream DRM sample recordings | The Dream project | Not stated | tests/data/drm (recorded cells, not shipped in packages) | test fixtures derived from the recordings | UNKNOWN redistribution terms for the recordings. Used only by the test suite. |
| ETSI, ITU, ICAO, ARINC, IEEE, ATSC, ARIB and other standards | their standards bodies | Specifications, not code | every mode | tables and formulas implemented from the published text | No licence applies to the facts; no standard text is reproduced beyond constants. |

Links: [Dear ImGui](https://github.com/ocornut/imgui); [stb_truetype, stb_rect_pack, stb_textedit](https://github.com/nothings/stb); [miniaudio](https://miniaud.io); [codec2](https://github.com/drowe67/codec2); [DRA decoder (dradec)](https://github.com/maliwen2015/ffmpeg_cavs_dra); [uat_fec (Reed-Solomon for UAT)](https://github.com/mutability/dump978); [SDDC_FX3.img (RX888 MkII firmware)](https://github.com/renardspark/SDDC_Driver); [nrsc5](https://github.com/theori-io/nrsc5); [etisnoop](https://github.com/Opendigitalradio/etisnoop); [gr-dvbt2](https://github.com/drmpeg/gr-dvbt2); [e-Czas](https://github.com/sp5wwp/e-Czas); [WSJT-X](https://sourceforge.net/projects/wsjt/); [WSJT-X Improved](https://sourceforge.net/projects/wsjt-x-improved/); [JS8Call](https://github.com/JS8Call-improved/JS8Call-improved); [JTEncode](https://github.com/etherkit/JTEncode); [fldigi](https://github.com/w1hkj/fldigi); [rtl_433](https://github.com/merbanan/rtl_433); [gr-lora_sdr](https://github.com/tapparelj/gr-lora_sdr); [LoRa_APRS_iGate](https://github.com/richonguzman/LoRa_APRS_iGate); [Meshtastic](https://github.com/meshtastic/firmware); [MeshCore](https://github.com/meshcore-dev/MeshCore); [MeshCom firmware](https://github.com/icssw-org/MeshCom-Firmware); [acarsdec](https://github.com/TLeconte/acarsdec); [libacars](https://github.com/szpajder/libacars); [dumphfdl, dumpvdl2](https://github.com/szpajder/dumphfdl); [JAERO](https://github.com/jontio/JAERO); [iridium-toolkit](https://github.com/muccc/iridium-toolkit); [gr-iridium](https://github.com/muccc/gr-iridium); [multimon-ng](https://github.com/EliasOenal/multimon-ng); [dump1090 and readsb](https://github.com/wiedehopf/readsb); [welle.io](https://github.com/AlbrechtL/welle.io); [ODR-PadEnc](https://github.com/Opendigitalradio/ODR-PadEnc); [FFmpeg (AAC tables)](https://ffmpeg.org); [glibc iso_6937](https://www.gnu.org/software/libc/); [SatDump and DSD-FME](https://github.com/SatDump/SatDump); [DJI DroneID research](https://github.com/proto17/dji_droneid); [SDRplay API headers](https://www.sdrplay.com/api/); [FFmpeg libraries](https://ffmpeg.org); [GLFW](https://www.glfw.org); [libusb](https://libusb.info); [libhackrf](https://github.com/greatscottgadgets/hackrf); [Hamlib](https://hamlib.github.io); [SoapySDR](https://github.com/pothosware/SoapySDR); [Radio vendor libraries](https://osmocom.org/projects/rtl-sdr); [SDRplay API](https://www.sdrplay.com/api/); [LAME](https://lame.sourceforge.io); [faad2](https://github.com/knik0/faad2); [mbelib](https://github.com/szechyjs/mbelib); [RADE (librade)](https://github.com/drowe67/radae); [MapLibre GL JS 5.7.3](https://maplibre.org); [Wrangler and Cloudflare Workers](https://github.com/cloudflare/workers-sdk); [OpenStreetMap tiles and data](https://www.openstreetmap.org/copyright); [adsbdb](https://www.adsbdb.com); [Planespotters photos](https://www.planespotters.net); [CelesTrak TLE data](https://celestrak.org); [OurAirports navaids](https://ourairports.com/data/); [OpenAIP](https://www.openaip.net); [OpenCelliD](https://opencellid.org); [Dream DRM sample recordings](https://sourceforge.net/projects/drm/); [ETSI, ITU, ICAO, ARINC, IEEE, ATSC, ARIB and other standards](https://www.etsi.org/standards)

## Licence audit: open points, stated plainly

Nothing found is GPL-2.0-only code or a non-commercial licence inside the OnAir binary. These points are not settled and are listed so nobody has to guess:

1. **third_party/dra/dradec.c (DRA decoder).** Published by maliwen2015 under MIT, but the file had no copyright notice and the decoder is shipped there inside an FFmpeg patch without saying where it came from. MIT text and attribution are now next to the file. The original author and licence of the DRA reference decoder are unknown; if the owner of that code does not allow redistribution under MIT, the file must be replaced or removed (the CDR mode would lose its audio).
2. **third_party/sddc_fx3/SDDC_FX3.img (RX888 firmware binary).** The SDDC projects (ik1xpv ExtIO_sddc, renardspark SDDC_Driver) are MIT and the licence text now ships with the image, but the binary contains Cypress/Infineon FX3 SDK parts whose redistribution terms were not checked. It is separate data that is loaded into the radio, not linked into OnAir, but the terms should be confirmed.
3. **acarsdec (core/src/acars).** Its `msk.c` is under the *GNU Library GPL version 2* only. OnAir's ACARS code was written from the method (comments say "derived from"), not copied line by line, but if any code was taken the LGPL-2.0 text lets a copy be converted only to the GPL, and it does not name version 3. Treat as possible conflict until the owner confirms no code was copied; the safe fix is to re-derive the MSK stage from the maths and the standard.
4. **iridium-toolkit (core/src/iridium).** The repository has no licence file. Only facts of the air interface (bit layouts, BCH polynomials) were used, which are not copyrightable, but the licence status is unknown.
5. **SDRplay API (core/src/source/sdrplay_api_min.h).** The struct layouts follow SDRplay's proprietary headers so OnAir can load the library at run time. The header file's licence was not checked. The SDRplay library itself is never shipped.
6. **tests/data/drm** holds cells recorded from the Dream project's sample recordings. Terms for those recordings are not stated. They are only test inputs and are not in the packages.
7. **Packaging notices.** Release packages must carry the licence texts and notices of the bundled libraries (FFmpeg LGPL and the source offer, libusb LGPL, libhackrf BSD-3, GLFW zlib, Hamlib LGPL, codec2 LGPL). codec2 and Hamlib notices are already installed; the others should be checked per package by whoever builds it.
8. **rtl_433 and multimon-ng** are GPL-2.0 projects; the files read carry "version 2 or any later version", so they are compatible. If a future port reads a file without "or later", stop and check it first.
9. **Rust workspace.** The crates under rust/ use no outside crates (only each other), so there is no crate licence to list.
10. **OpenAIP data (CC BY-NC)** and **OpenCelliD (CC BY-SA)** are named in the app but never bundled; the user downloads them under their own terms.

## Notices that must be kept

### Dear ImGui (MIT)

```
The MIT License (MIT)

Copyright (c) 2014-2025 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### SDDC firmware and driver (MIT)

Copyright (c) 2017-2020 Oscar Steila ik1xpv (ExtIO_sddc); SDDC_Driver by renardspark. The full MIT text is in `third_party/sddc_fx3/LICENSE-SDDC_Driver.txt`.

### DRA decoder (MIT)

Copyright (c) 2021 maliwen2015 (ffmpeg_cavs_dra). The full text is in `third_party/dra/LICENSE-ffmpeg_cavs_dra.txt`. See the open points above.

### miniaudio (public domain or MIT-0)

Copyright 2026 David Reid. No attribution is required; it is given here anyway.

### stb libraries (MIT or public domain)

Sean Barrett and contributors; the notices are in the headers `third_party/imgui/imstb_*.h`.

### codec2 (LGPL-2.1)

Copyright David Rowe and the codec2 contributors. The licence text is `third_party/codec2/COPYING` (also `LICENSES/LGPL-2.1.txt`). The sources of the release OnAir ships are in `third_party/codec2/src`; replace the shared library with your own build as described in [docs/CODECS.md](docs/CODECS.md).

### dump978 / uat_fec (GPL-2.0-or-later, LGPL)

Copyright 2015 Oliver Jowett; the Reed-Solomon routines are Copyright 2003 Phil Karn, KA9Q (LGPL). The headers stay in `third_party/uat_fec/*` and `core/src/uat/uat_decode.c`; the GPL-2.0 text is `LICENSES/GPL-2.0.txt`. As "version 2 or (at your option) any later version", it is combined here under GPL-3.0-or-later.

### etisnoop (GPL-3.0-or-later)

Copyright (C) 2014 CSP Innovazione nelle ICT s.c.a r.l., (C) 2015 Data Path, (C) 2018 Matthias P. Braendli. Kept in `core/src/dab/dab_fig.cpp`.

### libhackrf (BSD-3-Clause)

Copyright (c) Great Scott Gadgets, Michael Ossmann, Jared Boone and others. Redistribution in binary form must reproduce the copyright notice, the list of conditions and the disclaimer of the BSD 3-clause licence (https://github.com/greatscottgadgets/hackrf/blob/master/COPYING); the neither-name-endorse clause also applies. This is that notice.

### GLFW (zlib)

Copyright (c) 2002-2006 Marcus Geelnard, (c) 2006-2019 Camilla Loewy. The zlib licence needs no notice in binaries.

### MapLibre GL JS (BSD-3-Clause)

Loaded in the visitor's browser from unpkg; not part of this repository.

### OpenStreetMap

Map data (c) OpenStreetMap contributors, available under the Open Database Licence (https://www.openstreetmap.org/copyright). The program and the live map show the attribution.

## Changing this list

Edit `tools/credits_data.tsv`, then run `python3 tools/gen_credits.py`. It rewrites this file, `docs/credits-for-website.md` and the table shown in the program (`app/panels/credits_data.h`). `tests/app/test_credits.cpp` fails when a field is empty.
