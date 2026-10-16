# Per-mode benchmark, 0.2.7 baseline

Made with `bench_modes --seconds 10 --rates both --out ...` (tests/bench/bench_modes.cpp) on a Mac under heavy load from other jobs, so only CPU time is used: **cost** = CPU seconds (all threads) per second of signal, **real-time factor** = 1/cost (how many such streams one core could carry), peak RSS, threads, and the share of the CPU the busiest thread took. The signal is each mode's built-in test signal, fed through the Engine as a file at the rate shown (the mode's default rate, and a second one: twice, or half when that would pass 20 Msps; DVB-T2, DVB-T and ATSC have one fixed rate); the spectrum is off. Items = log lines + FEC blocks, a sanity count (`nothing decoded` rows printed nothing). Method and how to rerun: docs/optimising-a-mode.md.

Failed or timed out: vdl4 @ 4 Msps: failed (139).

Rows 1 to 30 (the most expensive) are in **bold**.

| # | Mode | Rate (Msps) | Cost | Real-time factor | Startup CPU (s) | Peak RSS (MB) | Threads | Busiest thread | Items | Status |
|--:|---|--:|--:|--:|--:|--:|--:|--:|--:|---|
| **1** | **VDL Mode 4 (`vdl4`)** | **2** | **8.9144** | **0.1** | **0.00** | **205** | **26** | **98%** | **1** | **ok** |
| **2** | **DVB-C (`dvbc`)** | **20** | **1.7732** | **0.6** | **0.00** | **623** | **27** | **48%** | **223238** | **ok** |
| **3** | **DVB-C (`dvbc`)** | **10** | **1.1187** | **0.9** | **0.00** | **439** | **27** | **53%** | **219341** | **ok** |
| **4** | **ATSC 1.0 (`atsc`)** | **8** | **1.0694** | **0.9** | **0.19** | **320** | **28** | **42%** | **126311** | **ok** |
| **5** | **ISDB-T (`isdbt`)** | **16** | **0.8746** | **1.1** | **0.07** | **466** | **32** | **49%** | **100928** | **ok** |
| **6** | **ATSC 3.0 (`atsc3`)** | **20** | **0.8352** | **1.2** | **0.00** | **840** | **34** | **36%** | **220** | **ok** |
| **7** | **HFDL (`hfdl`)** | **4** | **0.7679** | **1.3** | **0.00** | **235** | **26** | **93%** | **3** | **ok** |
| **8** | **Drone Remote ID (`rid`)** | **20** | **0.7591** | **1.3** | **0.67** | **374** | **27** | **77%** | **1** | **ok** |
| **9** | **DTMB (`dtmb`)** | **20** | **0.7082** | **1.4** | **0.57** | **358** | **31** | **47%** | **46236** | **ok** |
| **10** | **DTMB (`dtmb`)** | **10** | **0.6710** | **1.5** | **0.35** | **286** | **31** | **40%** | **46221** | **ok** |
| **11** | **ISDB-T (`isdbt`)** | **8** | **0.6692** | **1.5** | **0.06** | **408** | **32** | **43%** | **100928** | **ok** |
| **12** | **DVB-T (`dvbt`)** | **9.143** | **0.6536** | **1.5** | **0.15** | **298** | **31** | **45%** | **144668** | **ok** |
| **13** | **ATSC 3.0 (`atsc3`)** | **10** | **0.6085** | **1.6** | **0.00** | **564** | **33** | **34%** | **215** | **ok** |
| **14** | **HRPT / AHRPT (`hrpt`)** | **12** | **0.6066** | **1.6** | **0.38** | **298** | **27** | **81%** | **2773** | **ok** |
| **15** | **QO-100 (`qo100`)** | **20** | **0.5991** | **1.7** | **0.67** | **352** | **27** | **72%** | **4** | **ok** |
| **16** | **DVB-S/S2 (`dvbs`)** | **20** | **0.5753** | **1.7** | **0.53** | **399** | **34** | **30%** | **1433** | **ok** |
| **17** | **Wi-SUN FAN (`wisun`)** | **20** | **0.5753** | **1.7** | **0.44** | **350** | **28** | **58%** | **1182** | **ok** |
| **18** | **DJI drones (`dji`)** | **20** | **0.5171** | **1.9** | **0.69** | **350** | **27** | **69%** | **34** | **ok** |
| **19** | **Ionospheric sounders (`iono`)** | **20** | **0.5027** | **2.0** | **0.65** | **351** | **27** | **68%** | **1** | **ok** |
| **20** | **DVB-S/S2 (`dvbs`)** | **10** | **0.4942** | **2.0** | **0.20** | **326** | **34** | **33%** | **1452** | **ok** |
| **21** | **MAC (`mac`)** | **20** | **0.4939** | **2.0** | **0.73** | **309** | **27** | **68%** | **19909** | **ok** |
| **22** | **Wi-Fi networks (`wifi`)** | **20** | **0.4586** | **2.2** | **0.58** | **380** | **27** | **64%** | **2492** | **ok** |
| **23** | **DECT cordless phones (`dect`)** | **10** | **0.3990** | **2.5** | **0.36** | **273** | **27** | **79%** | **1** | **ok** |
| **24** | **Wi-SUN FAN (`wisun`)** | **10** | **0.3823** | **2.6** | **0.53** | **278** | **28** | **44%** | **1181** | **ok** |
| **25** | **GNSS (`gnss`)** | **8** | **0.3809** | **2.6** | **0.23** | **276** | **27** | **85%** | **28** | **ok** |
| **26** | **HRPT / AHRPT (`hrpt`)** | **6** | **0.3754** | **2.7** | **0.26** | **248** | **26** | **83%** | **2843** | **ok** |
| **27** | **Analog TV (`atv`)** | **20** | **0.3584** | **2.8** | **0.60** | **365** | **30** | **71%** | **489** | **ok** |
| **28** | **QO-100 (`qo100`)** | **10** | **0.3481** | **2.9** | **0.39** | **278** | **27** | **74%** | **4** | **ok** |
| **29** | **GNSS (`gnss`)** | **4** | **0.3438** | **2.9** | **0.13** | **248** | **26** | **90%** | **28** | **ok** |
| **30** | **Spectrum analyser (`specan`)** | **20** | **0.3109** | **3.2** | **0.69** | **345** | **27** | **50%** | **193409** | **ok** |
| 31 | Zigbee / 802.15.4 (`zigbee`) | 8 | 0.2935 | 3.4 | 0.28 | 259 | 27 | 73% | 3 | ok |
| 32 | LoRaWAN (`lorawan`) | 4 | 0.2850 | 3.5 | 0.19 | 271 | 26 | 85% | 10 | ok |
| 33 | HFDL (`hfdl`) | 2 | 0.2827 | 3.5 | 0.08 | 214 | 26 | 90% | 4 | ok |
| 34 | 5G NR cells (`nr`) | 20 | 0.2705 | 3.7 | 0.70 | 394 | 27 | 26% | 1 | ok |
| 35 | Wi-Fi HaLow (802.11ah) (`halow`) | 8 | 0.2663 | 3.8 | 0.30 | 262 | 27 | 78% | 1749 | ok |
| 36 | Zigbee / 802.15.4 (`zigbee`) | 4 | 0.2564 | 3.9 | 0.23 | 226 | 26 | 80% | 2872 | ok |
| 37 | Analog TV (`atv`) | 10 | 0.2507 | 4.0 | 0.36 | 295 | 30 | 72% | 488 | ok |
| 38 | VDL Mode 2 (`vdl2`) | 4 | 0.2455 | 4.1 | 0.16 | 228 | 26 | 85% | 30 | ok |
| 39 | Cell towers (`cell`) | 20 | 0.2404 | 4.2 | 0.62 | 462 | 27 | 28% | 8 | ok |
| 40 | Ionospheric sounders (`iono`) | 10 | 0.2399 | 4.2 | 0.38 | 280 | 27 | 65% | 1 | ok |
| 41 | DVB-T2 (`t2`) | 9.143 | 0.2390 | 4.2 | 0.32 | 314 | 27 | 75% | 5 | ok |
| 42 | Bluetooth LE (`ble`) | 8 | 0.2354 | 4.2 | 0.24 | 259 | 27 | 68% | 1 | ok |
| 43 | Iridium (`iridium`) | 20 | 0.2331 | 4.3 | 0.62 | 374 | 27 | 27% | 1122 | ok |
| 44 | GOES HRIT (`goes`) | 4.8 | 0.2324 | 4.3 | 0.18 | 230 | 26 | 79% | 566 | ok |
| 45 | LoRa (raw) (`loraraw`) | 4 | 0.2164 | 4.6 | 0.25 | 241 | 26 | 84% | 3 | ok |
| 46 | Spectrum explorer (`specx`) | 20 | 0.2053 | 4.9 | 0.69 | 348 | 27 | 41% | 1 | ok |
| 47 | LoRaWAN (`lorawan`) | 2 | 0.1991 | 5.0 | 0.18 | 251 | 26 | 85% | 10 | ok |
| 48 | ANT+ sensors (`ant`) | 8 | 0.1937 | 5.2 | 0.29 | 257 | 27 | 70% | 1 | ok |
| 49 | LoRa (raw) (`loraraw`) | 2 | 0.1871 | 5.3 | 0.13 | 224 | 26 | 85% | 4 | ok |
| 50 | Globalstar (`globalstar`) | 20 | 0.1825 | 5.5 | 0.66 | 347 | 27 | 37% | 13 | ok |
| 51 | HD Radio (`hdr`) | 4 | 0.1824 | 5.5 | 0.19 | 227 | 26 | 81% | 1 | ok |
| 52 | GOES HRIT (`goes`) | 2.4 | 0.1794 | 5.6 | 0.10 | 209 | 26 | 82% | 552 | ok |
| 53 | DECT cordless phones (`dect`) | 5 | 0.1760 | 5.7 | 0.21 | 232 | 26 | 76% | 1 | ok |
| 54 | Airband (`airband`) | 4 | 0.1759 | 5.7 | 0.20 | 229 | 29 | 82% | 2 | ok |
| 55 | Mode S uplink (`uplink`) | 20 | 0.1735 | 5.8 | 0.65 | 350 | 27 | 32% | 145 | ok |
| 56 | Elektro-L (`elektro`) | 4.8 | 0.1619 | 6.2 | 0.21 | 230 | 26 | 74% | 407 | ok |
| 57 | Elektro-L (`elektro`) | 2.4 | 0.1571 | 6.4 | 0.12 | 211 | 26 | 83% | 407 | ok |
| 58 | Wi-Fi HaLow (802.11ah) (`halow`) | 4 | 0.1536 | 6.5 | 0.16 | 229 | 26 | 78% | 1706 | ok |
| 59 | VDL Mode 2 (`vdl2`) | 2 | 0.1490 | 6.7 | 0.12 | 213 | 26 | 83% | 30 | ok |
| 60 | Bluetooth LE (`ble`) | 4 | 0.1485 | 6.7 | 0.18 | 224 | 26 | 70% | 1 | ok |
| 61 | HF beacons (`hfbeacon`) | 20 | 0.1422 | 7.0 | 0.73 | 350 | 27 | 33% | 1 | ok |
| 62 | 5G NR cells (`nr`) | 10 | 0.1412 | 7.1 | 0.41 | 309 | 27 | 26% | 1 | ok |
| 63 | ANT+ sensors (`ant`) | 4 | 0.1344 | 7.4 | 0.17 | 225 | 26 | 75% | 1 | ok |
| 64 | Spectrum analyser (`specan`) | 10 | 0.1302 | 7.7 | 0.39 | 274 | 27 | 44% | 195313 | ok |
| 65 | DAB (`dab`) | 20 | 0.1284 | 7.8 | 0.66 | 350 | 27 | 34% | 1251 | ok |
| 66 | Iridium (`iridium`) | 10 | 0.1279 | 7.8 | 0.37 | 277 | 27 | 26% | 991 | ok |
| 67 | GSM-R (`gsmr`) | 10 | 0.1234 | 8.1 | 0.29 | 475 | 27 | 29% | 2 | ok |
| 68 | Cell towers (`cell`) | 10 | 0.1210 | 8.3 | 0.36 | 336 | 27 | 27% | 8 | ok |
| 69 | ISM sensors (`ism`) | 4 | 0.1206 | 8.3 | 0.31 | 416 | 26 | 76% | 29 | ok |
| 70 | Spectrum explorer (`specx`) | 10 | 0.1107 | 9.0 | 0.44 | 274 | 27 | 40% | 1 | ok |
| 71 | CDR (`cdr`) | 4 | 0.1097 | 9.1 | 0.17 | 227 | 26 | 74% | 2 | ok |
| 72 | DAB (`dab`) | 10 | 0.0995 | 10.1 | 0.41 | 283 | 27 | 41% | 1251 | ok |
| 73 | Airband (`airband`) | 2 | 0.0989 | 10.1 | 0.14 | 214 | 29 | 78% | 2 | ok |
| 74 | Globalstar (`globalstar`) | 10 | 0.0942 | 10.6 | 0.38 | 276 | 27 | 37% | 13 | ok |
| 75 | CODAR / HF ocean radar (`codar`) | 4 | 0.0883 | 11.3 | 0.17 | 226 | 26 | 66% | 56 | ok |
| 76 | General receiver (`general`) | 4 | 0.0818 | 12.2 | 0.21 | 228 | 26 | 58% | 1 | ok |
| 77 | D-STAR (`dstar`) | 4 | 0.0806 | 12.4 | 0.17 | 223 | 26 | 58% | 1 | ok |
| 78 | FLARM (`flarm`) | 2 | 0.0799 | 12.5 | 0.17 | 213 | 26 | 67% | 36 | ok |
| 79 | MeshCore (`meshcore`) | 4 | 0.0797 | 12.6 | 0.22 | 228 | 26 | 56% | 2 | ok |
| 80 | HF beacons (`hfbeacon`) | 10 | 0.0795 | 12.6 | 0.43 | 274 | 27 | 35% | 1 | ok |
| 81 | Radio astronomy (`astro`) | 5 | 0.0793 | 12.6 | 0.22 | 234 | 26 | 48% | 1 | ok |
| 82 | Inmarsat Aero (`aero`) | 8 | 0.0791 | 12.7 | 0.17 | 266 | 27 | 46% | 382 | ok |
| 83 | M17 (`m17`) | 4 | 0.0781 | 12.8 | 0.19 | 225 | 26 | 62% | 1 | ok |
| 84 | Mode S uplink (`uplink`) | 10 | 0.0738 | 13.6 | 0.45 | 277 | 27 | 36% | 145 | ok |
| 85 | Mesh (LoRa) (`mesh`) | 4 | 0.0726 | 13.8 | 0.19 | 227 | 26 | 64% | 2 | ok |
| 86 | FM (`fm`) | 8 | 0.0715 | 14.0 | 0.29 | 260 | 27 | 46% | 1 | ok |
| 87 | P25 (`p25`) | 4 | 0.0688 | 14.5 | 0.21 | 224 | 26 | 49% | 1 | ok |
| 88 | Inmarsat-C (`inmc`) | 4 | 0.0685 | 14.6 | 0.13 | 228 | 26 | 59% | 3 | ok |
| 89 | GSM-R (`gsmr`) | 5 | 0.0674 | 14.8 | 0.15 | 348 | 26 | 27% | 2 | ok |
| 90 | dPMR (`dpmr`) | 4 | 0.0663 | 15.1 | 0.20 | 226 | 26 | 48% | 1 | ok |
| 91 | ADS-B (`adsb`) | 8 | 0.0653 | 15.3 | 0.28 | 259 | 27 | 39% | 768 | ok |
| 92 | ISM sensors (`ism`) | 2 | 0.0653 | 15.3 | 0.20 | 311 | 26 | 71% | 29 | ok |
| 93 | Orbcomm (`orbcomm`) | 2 | 0.0638 | 15.7 | 0.08 | 209 | 26 | 59% | 801 | ok |
| 94 | ADS-B (`adsb`) | 4 | 0.0634 | 15.8 | 0.18 | 224 | 26 | 53% | 769 | ok |
| 95 | HD Radio (`hdr`) | 2 | 0.0632 | 15.8 | 0.11 | 216 | 26 | 66% | 12 | ok |
| 96 | ACARS (`acars`) | 4 | 0.0626 | 16.0 | 0.21 | 223 | 26 | 60% | 62 | ok |
| 97 | FM (`fm`) | 4 | 0.0620 | 16.1 | 0.20 | 228 | 26 | 53% | 1 | ok |
| 98 | FLARM (`flarm`) | 1 | 0.0614 | 16.3 | 0.11 | 207 | 26 | 68% | 36 | ok |
| 99 | Radiosonde (`sonde`) | 16 | 0.0601 | 16.6 | 0.48 | 389 | 28 | 45% | 6 | ok |
| 100 | VOR / ILS (`vorils`) | 4 | 0.0577 | 17.3 | 0.17 | 224 | 26 | 56% | 1 | ok |
| 101 | MeshCom (`meshcom`) | 4 | 0.0571 | 17.5 | 0.20 | 228 | 26 | 51% | 2 | ok |
| 102 | VOR (`vor`) | 4 | 0.0570 | 17.5 | 0.18 | 225 | 26 | 58% | 1 | ok |
| 103 | NXDN (`nxdn`) | 4 | 0.0568 | 17.6 | 0.19 | 228 | 26 | 45% | 1 | ok |
| 104 | TETRA (`tetra`) | 4 | 0.0562 | 17.8 | 0.18 | 380 | 26 | 33% | 6 | ok |
| 105 | Marine (`marine`) | 4 | 0.0562 | 17.8 | 0.15 | 226 | 26 | 50% | 1 | ok |
| 106 | M17 (`m17`) | 2 | 0.0560 | 17.8 | 0.13 | 207 | 26 | 63% | 1 | ok |
| 107 | ILS (`ils`) | 4 | 0.0552 | 18.1 | 0.17 | 220 | 26 | 58% | 1 | ok |
| 108 | Meteor-M LRPT (`meteor`) | 2 | 0.0552 | 18.1 | 0.14 | 210 | 26 | 57% | 1 | ok |
| 109 | HF digital (`hfdig`) | 4 | 0.0540 | 18.5 | 0.17 | 236 | 26 | 43% | 2 | ok |
| 110 | VDES (`vdes`) | 2 | 0.0538 | 18.6 | 0.12 | 213 | 26 | 57% | 21 | ok |
| 111 | LoRa APRS (`loraaprs`) | 4 | 0.0537 | 18.6 | 0.17 | 231 | 26 | 50% | 2 | ok |
| 112 | FT2 (`ft2`) | 4 | 0.0536 | 18.7 | 0.16 | 230 | 26 | 34% | 2 | ok |
| 113 | NB-IoT cells (`nbiot`) | 4 | 0.0529 | 18.9 | 0.15 | 306 | 26 | 28% | 1 | ok |
| 114 | DRM (`drm`) | 4 | 0.0514 | 19.5 | 0.19 | 244 | 29 | 50% | 177 | ok |
| 115 | General receiver (`general`) | 2 | 0.0507 | 19.7 | 0.15 | 213 | 26 | 58% | 1 | ok |
| 116 | Weather fax (`wefax`) | 4 | 0.0507 | 19.7 | 0.13 | 228 | 26 | 52% | 1 | ok |
| 117 | MeshCore (`meshcore`) | 2 | 0.0497 | 20.1 | 0.17 | 212 | 26 | 56% | 2 | ok |
| 118 | Olivia (`olivia`) | 4 | 0.0486 | 20.6 | 0.21 | 227 | 26 | 40% | 2 | ok |
| 119 | D-STAR (`dstar`) | 2 | 0.0477 | 21.0 | 0.14 | 207 | 26 | 61% | 1 | ok |
| 120 | HF ALE (`ale`) | 4 | 0.0470 | 21.3 | 0.22 | 227 | 26 | 38% | 2 | ok |
| 121 | CODAR / HF ocean radar (`codar`) | 2 | 0.0454 | 22.0 | 0.12 | 209 | 26 | 59% | 57 | ok |
| 122 | Mesh (LoRa) (`mesh`) | 2 | 0.0454 | 22.0 | 0.12 | 211 | 26 | 60% | 2 | ok |
| 123 | FT4 (`ft4`) | 4 | 0.0446 | 22.4 | 0.18 | 238 | 26 | 38% | 2 | ok |
| 124 | Weather fax (`wefax`) | 2 | 0.0441 | 22.7 | 0.10 | 213 | 26 | 51% | 1 | ok |
| 125 | RTTY (`rtty`) | 4 | 0.0440 | 22.7 | 0.18 | 228 | 26 | 37% | 2 | ok |
| 126 | dPMR (`dpmr`) | 2 | 0.0437 | 22.9 | 0.13 | 207 | 26 | 49% | 1 | ok |
| 127 | CubeSat telemetry (`cubesat`) | 4 | 0.0436 | 22.9 | 0.23 | 228 | 26 | 36% | 1 | ok |
| 128 | JS8Call (`js8`) | 4 | 0.0434 | 23.0 | 0.20 | 227 | 26 | 34% | 2 | ok |
| 129 | Inmarsat-C (`inmc`) | 2 | 0.0431 | 23.2 | 0.11 | 212 | 26 | 56% | 3 | ok |
| 130 | P25 (`p25`) | 2 | 0.0426 | 23.5 | 0.15 | 209 | 26 | 51% | 1 | ok |
| 131 | PSK31 (`psk`) | 4 | 0.0423 | 23.6 | 0.18 | 227 | 26 | 37% | 2 | ok |
| 132 | HamDRM (`hamdrm`) | 4 | 0.0423 | 23.7 | 0.18 | 226 | 26 | 40% | 2 | ok |
| 133 | FT8 (`ft8`) | 4 | 0.0417 | 24.0 | 0.20 | 226 | 26 | 39% | 2 | ok |
| 134 | POCSAG (`pocsag`) | 4 | 0.0417 | 24.0 | 0.18 | 227 | 26 | 37% | 2 | ok |
| 135 | Inmarsat Aero (`aero`) | 4 | 0.0416 | 24.1 | 0.10 | 230 | 26 | 44% | 382 | ok |
| 136 | WSPR (`wspr`) | 4 | 0.0410 | 24.4 | 0.18 | 228 | 26 | 39% | 2 | ok |
| 137 | Meshtastic (`meshtastic`) | 4 | 0.0407 | 24.6 | 0.23 | 229 | 26 | 38% | 2 | ok |
| 138 | Marine (`marine`) | 2 | 0.0406 | 24.6 | 0.10 | 212 | 26 | 55% | 1 | ok |
| 139 | FreeDV (`freedv`) | 4 | 0.0401 | 24.9 | 0.16 | 224 | 26 | 46% | 2 | ok |
| 140 | HF digital (`hfdig`) | 2 | 0.0399 | 25.1 | 0.10 | 222 | 26 | 40% | 2 | ok |
| 141 | MeshCom (`meshcom`) | 2 | 0.0394 | 25.4 | 0.13 | 213 | 26 | 49% | 2 | ok |
| 142 | DMR (`dmr`) | 4.8 | 0.0386 | 25.9 | 0.20 | 230 | 26 | 36% | 529 | ok |
| 143 | Hellschreiber (`hell`) | 4 | 0.0376 | 26.6 | 0.19 | 226 | 26 | 40% | 2 | ok |
| 144 | Radio astronomy (`astro`) | 2.5 | 0.0375 | 26.6 | 0.17 | 219 | 26 | 42% | 1 | ok |
| 145 | DominoEX (`dominoex`) | 4 | 0.0372 | 26.9 | 0.16 | 226 | 26 | 43% | 2 | ok |
| 146 | JT65 (`jt65`) | 4 | 0.0367 | 27.2 | 0.18 | 223 | 26 | 42% | 2 | ok |
| 147 | FT2 (`ft2`) | 2 | 0.0363 | 27.5 | 0.12 | 214 | 26 | 37% | 2 | ok |
| 148 | MSK144 (`msk144`) | 4 | 0.0362 | 27.6 | 0.19 | 226 | 26 | 41% | 2 | ok |
| 149 | Meteor-M LRPT (`meteor`) | 1 | 0.0360 | 27.7 | 0.14 | 202 | 26 | 56% | 1 | ok |
| 150 | Orbcomm (`orbcomm`) | 1 | 0.0360 | 27.8 | 0.11 | 206 | 26 | 56% | 779 | ok |
| 151 | JT9 (`jt9`) | 4 | 0.0359 | 27.8 | 0.19 | 228 | 26 | 42% | 2 | ok |
| 152 | Contestia (`contestia`) | 4 | 0.0356 | 28.1 | 0.16 | 222 | 26 | 43% | 2 | ok |
| 153 | DRM (`drm`) | 2 | 0.0351 | 28.5 | 0.13 | 228 | 29 | 50% | 177 | ok |
| 154 | CDR (`cdr`) | 2 | 0.0351 | 28.5 | 0.14 | 222 | 26 | 51% | 157 | ok |
| 155 | CW (`cw`) | 4 | 0.0350 | 28.6 | 0.20 | 226 | 26 | 36% | 2 | ok |
| 156 | Radiosonde (`sonde`) | 8 | 0.0348 | 28.7 | 0.30 | 295 | 28 | 46% | 6 | ok |
| 157 | DSC (`dsc`) | 4 | 0.0347 | 28.9 | 0.16 | 229 | 26 | 38% | 1 | ok |
| 158 | FST4 (`fst4`) | 4 | 0.0345 | 29.0 | 0.18 | 224 | 26 | 42% | 2 | ok |
| 159 | NOAA Weather Radio SAME (`same`) | 4 | 0.0345 | 29.0 | 0.19 | 223 | 26 | 37% | 2 | ok |
| 160 | THOR (`thor`) | 4 | 0.0345 | 29.0 | 0.18 | 220 | 26 | 43% | 2 | ok |
| 161 | NAVTEX (`navtex`) | 4 | 0.0344 | 29.1 | 0.17 | 227 | 26 | 39% | 1 | ok |
| 162 | MFSK (`mfsk`) | 4 | 0.0344 | 29.1 | 0.17 | 226 | 26 | 42% | 2 | ok |
| 163 | FLEX (`flex`) | 4 | 0.0342 | 29.2 | 0.19 | 228 | 26 | 39% | 2 | ok |
| 164 | Q65 (`q65`) | 4 | 0.0341 | 29.3 | 0.18 | 226 | 26 | 42% | 2 | ok |
| 165 | SSTV (`sstv`) | 4 | 0.0340 | 29.4 | 0.18 | 226 | 26 | 42% | 2 | ok |
| 166 | Pagers (`pager`) | 4 | 0.0338 | 29.6 | 0.17 | 222 | 26 | 43% | 2 | ok |
| 167 | APRS / Packet (`packet`) | 4 | 0.0330 | 30.3 | 0.17 | 222 | 26 | 40% | 1 | ok |
| 168 | ACARS (`acars`) | 2 | 0.0326 | 30.7 | 0.16 | 208 | 26 | 53% | 62 | ok |
| 169 | NXDN (`nxdn`) | 2 | 0.0317 | 31.6 | 0.18 | 211 | 26 | 40% | 1 | ok |
| 170 | VDES (`vdes`) | 1 | 0.0311 | 32.1 | 0.12 | 205 | 26 | 53% | 21 | ok |
| 171 | VOR / ILS (`vorils`) | 2 | 0.0302 | 33.1 | 0.13 | 210 | 26 | 50% | 1 | ok |
| 172 | Multitone Paging (`multitone`) | 4 | 0.0298 | 33.5 | 0.19 | 226 | 26 | 40% | 2 | ok |
| 173 | NB-IoT cells (`nbiot`) | 2 | 0.0294 | 34.0 | 0.14 | 245 | 26 | 38% | 1 | ok |
| 174 | ILS (`ils`) | 2 | 0.0290 | 34.5 | 0.14 | 206 | 26 | 49% | 1 | ok |
| 175 | VOR (`vor`) | 2 | 0.0290 | 34.5 | 0.13 | 206 | 26 | 49% | 1 | ok |
| 176 | TETRA (`tetra`) | 2 | 0.0280 | 35.7 | 0.11 | 297 | 26 | 40% | 2 | ok |
| 177 | FT4 (`ft4`) | 2 | 0.0277 | 36.1 | 0.14 | 223 | 26 | 37% | 2 | ok |
| 178 | LoRa APRS (`loraaprs`) | 2 | 0.0271 | 36.9 | 0.17 | 216 | 26 | 42% | 2 | ok |
| 179 | FreeDV (`freedv`) | 2 | 0.0270 | 37.0 | 0.12 | 206 | 26 | 44% | 2 | ok |
| 180 | HF ALE (`ale`) | 2 | 0.0269 | 37.1 | 0.18 | 212 | 26 | 42% | 2 | ok |
| 181 | CubeSat telemetry (`cubesat`) | 2 | 0.0268 | 37.3 | 0.15 | 210 | 26 | 40% | 1 | ok |
| 182 | LBJ (`lbj`) | 0.96 | 0.0264 | 37.8 | 0.09 | 201 | 26 | 54% | 1 | ok |
| 183 | RTTY (`rtty`) | 2 | 0.0256 | 39.0 | 0.16 | 210 | 26 | 43% | 2 | ok |
| 184 | Meshtastic (`meshtastic`) | 2 | 0.0252 | 39.6 | 0.17 | 214 | 26 | 40% | 2 | ok |
| 185 | WSPR (`wspr`) | 2 | 0.0250 | 40.1 | 0.14 | 211 | 26 | 41% | 2 | ok |
| 186 | Contestia (`contestia`) | 2 | 0.0248 | 40.3 | 0.15 | 209 | 26 | 45% | 2 | ok |
| 187 | POCSAG (`pocsag`) | 2 | 0.0248 | 40.3 | 0.14 | 213 | 26 | 41% | 2 | ok |
| 188 | UAT (978 MHz) (`uat`) | 4 | 0.0240 | 41.6 | 0.19 | 223 | 26 | 45% | 1 | ok |
| 189 | JS8Call (`js8`) | 2 | 0.0239 | 41.8 | 0.17 | 214 | 26 | 46% | 2 | ok |
| 190 | PSK31 (`psk`) | 2 | 0.0238 | 42.0 | 0.15 | 211 | 26 | 43% | 2 | ok |
| 191 | DMR (`dmr`) | 2.4 | 0.0237 | 42.2 | 0.11 | 210 | 26 | 41% | 529 | ok |
| 192 | Pagers (`pager`) | 2 | 0.0234 | 42.7 | 0.14 | 211 | 26 | 42% | 2 | ok |
| 193 | Olivia (`olivia`) | 2 | 0.0230 | 43.6 | 0.14 | 208 | 26 | 42% | 2 | ok |
| 194 | SSTV (`sstv`) | 2 | 0.0228 | 43.8 | 0.14 | 209 | 26 | 47% | 2 | ok |
| 195 | HamDRM (`hamdrm`) | 2 | 0.0224 | 44.6 | 0.17 | 207 | 26 | 40% | 2 | ok |
| 196 | AIS (`ais`) | 4 | 0.0220 | 45.4 | 0.21 | 223 | 26 | 47% | 39 | ok |
| 197 | Q65 (`q65`) | 2 | 0.0214 | 46.6 | 0.13 | 209 | 26 | 43% | 2 | ok |
| 198 | THOR (`thor`) | 2 | 0.0214 | 46.8 | 0.12 | 207 | 26 | 43% | 2 | ok |
| 199 | JT9 (`jt9`) | 2 | 0.0214 | 46.8 | 0.13 | 210 | 26 | 44% | 2 | ok |
| 200 | DominoEX (`dominoex`) | 2 | 0.0213 | 46.8 | 0.13 | 209 | 26 | 42% | 2 | ok |
| 201 | JT65 (`jt65`) | 2 | 0.0213 | 47.0 | 0.12 | 211 | 26 | 43% | 2 | ok |
| 202 | FT8 (`ft8`) | 2 | 0.0212 | 47.1 | 0.13 | 209 | 26 | 44% | 2 | ok |
| 203 | MFSK (`mfsk`) | 2 | 0.0211 | 47.3 | 0.13 | 206 | 26 | 44% | 2 | ok |
| 204 | Hellschreiber (`hell`) | 2 | 0.0208 | 48.1 | 0.12 | 205 | 26 | 43% | 2 | ok |
| 205 | MSK144 (`msk144`) | 2 | 0.0207 | 48.3 | 0.12 | 209 | 26 | 44% | 2 | ok |
| 206 | NAVTEX (`navtex`) | 2 | 0.0207 | 48.4 | 0.15 | 209 | 26 | 46% | 1 | ok |
| 207 | APRS / Packet (`packet`) | 2 | 0.0206 | 48.5 | 0.14 | 208 | 26 | 44% | 1 | ok |
| 208 | FLEX (`flex`) | 2 | 0.0205 | 48.8 | 0.15 | 211 | 26 | 45% | 2 | ok |
| 209 | FST4 (`fst4`) | 2 | 0.0205 | 48.8 | 0.13 | 212 | 26 | 43% | 2 | ok |
| 210 | NOAA Weather Radio SAME (`same`) | 2 | 0.0198 | 50.5 | 0.15 | 213 | 26 | 46% | 2 | ok |
| 211 | DSC (`dsc`) | 2 | 0.0195 | 51.3 | 0.14 | 210 | 26 | 47% | 1 | ok |
| 212 | CW (`cw`) | 2 | 0.0190 | 52.6 | 0.13 | 206 | 26 | 46% | 2 | ok |
| 213 | LBJ (`lbj`) | 1.92 | 0.0178 | 56.1 | 0.15 | 213 | 26 | 44% | 1 | ok |
| 214 | Multitone Paging (`multitone`) | 2 | 0.0161 | 62.3 | 0.16 | 211 | 26 | 49% | 2 | ok |
| 215 | Signal workbench (`wb`) | 4 | 0.0153 | 65.2 | 0.19 | 226 | 26 | 56% | 1 | ok |
| 216 | UAT (978 MHz) (`uat`) | 2 | 0.0145 | 68.8 | 0.17 | 207 | 26 | 52% | 1 | ok |
| 217 | AIS (`ais`) | 2 | 0.0145 | 69.2 | 0.13 | 213 | 26 | 52% | 39 | ok |
| 218 | Signal workbench (`wb`) | 2 | 0.0092 | 109.2 | 0.14 | 212 | 26 | 65% | 1 | ok |
| 219 | NDB (`ndb`) | 0.512 | 0.0088 | 113.7 | 0.06 | 192 | 26 | 53% | 1 | ok |
| 220 | NDB (`ndb`) | 0.256 | 0.0076 | 130.9 | 0.05 | 191 | 25 | 53% | 1 | ok |
| 221 | Time signals (`time`) | 0.256 | 0.0071 | 141.6 | 0.00 | 189 | 25 | 71% | 1 | ok |
| 222 | TETRAPOL (`tetrapol`) | 0.25 | 0.0066 | 151.9 | 0.00 | 191 | 25 | 70% | 24 | ok |
| 223 | Datawell Buoy HF Link (`datawell`) | 0.25 | 0.0064 | 155.7 | 0.00 | 189 | 25 | 68% | 1 | ok |
| 224 | Time signals (`time`) | 0.512 | 0.0059 | 168.9 | 0.10 | 196 | 25 | 64% | 1 | ok |
| 225 | TETRAPOL (`tetrapol`) | 0.5 | 0.0051 | 196.3 | 0.09 | 195 | 25 | 64% | 56 | ok |
| 226 | Datawell Buoy HF Link (`datawell`) | 0.5 | 0.0049 | 202.6 | 0.07 | 191 | 26 | 64% | 1 | ok |
| 227 | VDL Mode 4 (`vdl4`) | 4 | 0.0000 | 0.0 | 0.00 | 0 | 0 | 0% | 0 | failed (139) |

## Hot functions of the 15 most expensive modes

macOS `sample` of the receiving process (4 s, idle thread waits left out), self time as a share of the busy samples. Rate as in the table's worst row of the mode; the second row of a mode behaves the same way. Percentages of the top five only; the rest is a long tail.

1. **vdl4** (cost 8.91 at 2 Msps)
   - `dect2::Vdl4Receiver::feed` 99%
   - `cerror_nocancel` 0%
   - `atan2` 0%
2. **dvbc** (cost 1.77 at 20 Msps)
   - `dect2::DvbcReceiver::Impl::process` 31%
   - `dect2::DvbcReceiver::Impl::onSymbol` 24%
   - `dect2::DvbcReceiver::Impl::smooth` 19%
   - `dect2::DvbcReceiver::Impl::lockedDecided` 15%
   - `dect2::dvbt::ConvInterleaver::process` 2%
3. **atsc** (cost 1.07 at 8 Msps)
   - `dect2::atsc::Core::processInput` 27%
   - `dect2::atsc::FieldDecoder::decode` 17%
   - `std::__invoke_result_impl<void, dect2::atsc::Core::processInput` 14%
   - `dect2::atsc::Core::equalize` 14%
   - `dect2::atsc::Core::stageC` 6%
4. **isdbt** (cost 0.87 at 16 Msps)
   - `dect2::dvbt::Viterbi::decode` 25%
   - `dect2::resampleLoop` 18%
   - `dect2::isdbt::Demod::Impl::pushSymbol` 11%
   - `dect2::isdbt::LayerDecoder::processFrame` 8%
   - `dect2::SpectrumAnalyzer::feed` 3%
5. **atsc3** (cost 0.84 at 20 Msps)
   - `dect2::atsc3::resampleExact` 47%
   - `dect2::atsc3::resampleExact` 22%
   - `dect2::atsc3::Atsc3Sync::cutFrame` 6%
   - `dect2::atsc3::receiveSymbol` 4%
   - `_platform_memmove` 3%
6. **hfdl** (cost 0.77 at 4 Msps)
   - `dect2::HfdlReceiver::Impl::push` 75%
   - `dect2::HfdlReceiver::Impl::burst` 9%
   - `dect2::HfdlReceiver::Impl::coarse` 7%
   - `dect2::HfdlReceiver::feed` 2%
   - `dect2::HfdlReceiver::Impl::symSample` 2%
7. **rid** (cost 0.76 at 20 Msps)
   - `dect2::BleReceiver::Impl::input` 30%
   - `dect2::WifiReceiver::Impl::work` 19%
   - `dect2::WifiReceiver::Impl::detect` 15%
   - `dect2::SpectrumAnalyzer::feed` 7%
   - `dect2::WifiReceiver::Impl::input` 7%
8. **dtmb** (cost 0.71 at 20 Msps)
   - `dect2::dtmb::LdpcCode::Decoder::decodeBody` 27%
   - `dect2::dtmb::SrrcResampler::pull` 17%
   - `dect2::dtmb::MixedFft::runBody` 10%
   - `_platform_memmove` 6%
   - `dect2::SpectrumAnalyzer::feed` 5%
9. **dvbt** (cost 0.65 at 9.143 Msps)
   - `dect2::dvbt::Viterbi::decode` 44%
   - `dect2::dvbt::demap` 6%
   - `dect2::dvbt::FecDecoder::process` 5%
   - `dect2::DvbtReceiver::Impl::transform` 4%
   - `dect2::SpectrumAnalyzer::feed` 4%
10. **hrpt** (cost 0.61 at 12 Msps)
   - `dect2::` 35%
   - `dect2::hrpt::Viterbi::push` 22%
   - `dect2::HrptReceiver::Impl::symbol` 9%
   - `dect2::SpectrumAnalyzer::feed` 6%
   - `dect2::meteor::` 5%
11. **qo100** (cost 0.60 at 20 Msps)
   - `dect2::qo100::Decimator::process` 46%
   - `dect2::Qo100Receiver::feed` 26%
   - `dect2::SpectrumAnalyzer::feed` 7%
   - `__read_nocancel` 6%
   - `_platform_memmove` 3%
12. **dvbs** (cost 0.58 at 20 Msps)
   - `dect2::dvbs::S2Rx::Impl::fastRun` 24%
   - `dect2::dvbs::s2RefinePhase` 15%
   - `dect2::dvbs::SymbolTimer::process` 15%
   - `dect2::LdpcCode::decodeFastBody` 11%
   - `dect2::dvbs::PolyResampler::process` 8%
13. **wisun** (cost 0.58 at 20 Msps)
   - `dect2::WisunReceiver::Impl::binSample` 28%
   - `dect2::WisunReceiver::Impl::pfbFrame` 27%
   - `dect2::WisunReceiver::Impl::extend` 14%
   - `dect2::SpectrumAnalyzer::feed` 9%
   - `__read_nocancel` 5%
14. **dji** (cost 0.52 at 20 Msps)
   - `dect2::dji::Resampler::run` 28%
   - `dect2::SpectrumAnalyzer::feed` 16%
   - `dect2::` 14%
   - `dect2::DjiReceiver::Impl::analyzeSignal` 10%
   - `dect2::DjiReceiver::Impl::measure` 8%
15. **iono** (cost 0.50 at 20 Msps)
   - `dect2::iono::Processor::Impl::feed` 54%
   - `dect2::iono::Processor::Impl::frame` 13%
   - `dect2::SpectrumAnalyzer::feed` 11%
   - `__read_nocancel` 7%
   - `std::__nth_element[abi:nqe220106]<std::_ClassicAlgPolicy, std::__less<` 6%
16. **mac** (cost 0.49 at 20 Msps)
   - `no profile` 

Notes: vdl4 is the outlier (a single function, `Vdl4Receiver::feed`, takes 99%; it also crashes at 4 Msps). atsc3 spends 69% in `resampleExact`; dvbt and isdbt spend 25 to 44% in the shared `dvbt::Viterbi::decode`; hfdl 75% in one push function; `SpectrumAnalyzer::feed` shows at 3 to 16% in several modes even though the harness leaves the spectrum off (the receiver's own analyser).

