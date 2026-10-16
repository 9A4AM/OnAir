# Transmit: modes offered and not offered

This list is taken from the tables in `core/src/tx.cpp` (`txModes()` and `txRefusalReason()`). OnAir applies no frequency or power limit; see the Transmit section of the README.

## Modes offered (76)

| Id | Mode | Engine code | Generator |
|---|---|---|---|
| dvbt2 | DVB-T2 | 0 | T2Generator, real PLP with the test transport stream |
| dvbt | DVB-T | 0 | dvbt::Generator, demo programme |
| atsc | ATSC 1.0 (8-VSB) | 3 | atsc::Generator, demo programme |
| dab | DAB / DAB+ | 4 | makeDabSynth, transmission mode I with real audio |
| isdbt | ISDB-T | 6 | ISDB-T layers A and B with the demo programme |
| dvbs | DVB-S / S2 / S2X | 8 | makeDvbsSynth (dvbs_tx.h chain), demo programme |
| dtmb | DTMB | 9 | makeDtmbSynth (dtmb_tx.h chain), demo programme |
| adsb | ADS-B / Mode S | 13 | makeAdsbSynth, Mode S frames of simulated aircraft |
| atsc3 | ATSC 3.0 | 5 | makeAtsc3Synth, in-memory test programme |
| fm | FM broadcast | 7 | FmGenerator, stereo multiplex with RDS |
| atv | Analog TV | 10 | makeAtvSynth, test card and sound carrier |
| dmr | DMR | 11 | makeDmrSynth |
| drm | DRM | 12 | makeDrmSynth |
| sonde | Radiosonde | 15 | makeSondeSynth |
| ais | AIS | 16 | makeAisSynth |
| marine | Marine (NAVTEX / DSC / weather fax) | 17 | makeMarineSynth |
| acars | ACARS | 18 | makeAcarsSynth |
| inmc | Inmarsat-C | 19 | makeInmcSynth |
| aero | Inmarsat Aero | 20 | makeAeroSynth |
| iridium | Iridium | 21 | makeIridiumSynth |
| mesh | Mesh (LoRa) | 22 | makeMeshSynth |
| hdr | HD Radio | 23 | makeHdrSynth |
| cdr | CDR | 24 | makeCdrSynth |
| pager | Pagers (POCSAG / FLEX) | 25 | makePagerSynth, FM on a 25 kHz channel |
| packet | APRS / Packet | 26 | makePacketSynth |
| rtty | RTTY | 27 | makeRttyTxAudio / makeHfdigSynth |
| sstv | SSTV | 27 | makeSstvTx, PPM or test picture with callsign stamp |
| freedv | FreeDV | 27 | makeFreedvTestAudio |
| ft8 | FT8 | 27 | makeFtxTxAudio |
| ft4 | FT4 | 27 | makeFtxTxAudio |
| wspr | WSPR | 27 | makeFtxTxAudio |
| ft2 | FT2 | 27 | makeFtxTxAudio |
| hamdrm | HamDRM | 27 | makeHamdrmTx, file bytes and callsign |
| jt65 | JT65 | 27 | makeFtxTxAudio |
| jt9 | JT9 | 27 | makeFtxTxAudio |
| q65 | Q65 | 27 | makeFtxTxAudio |
| fst4 | FST4 | 27 | makeFtxTxAudio |
| msk144 | MSK144 | 27 | makeFtxTxAudio |
| olivia | Olivia | 27 | makeKbdTx |
| contestia | Contestia | 27 | makeKbdTx |
| mfsk | MFSK | 27 | makeKbdTx |
| thor | THOR | 27 | makeKbdTx |
| dominoex | DominoEX | 27 | makeKbdTx |
| hell | Hellschreiber | 27 | makeKbdTx |
| airband | Airband AM | 28 | makeAirbandSynth, AM voice channels |
| vdl2 | VDL Mode 2 | 29 | makeVdl2Synth |
| vdl4 | VDL Mode 4 | 84 | makeVdl4Synth |
| uat | UAT 978 | 85 | makeUatSynth |
| wisun | Wi-SUN | 86 | makeWisunSynth |
| vorils | VOR / ILS | 32 | makeVorSynth |
| m17 | M17 | 33 | makeM17Synth |
| dstar | D-STAR | 35 | makeDstarSynth |
| cw | CW | 36 | makeCwSynth |
| hfdl | HFDL | 37 | makeHfdlSynth |
| psk | PSK31 | 38 | makePskSynth |
| js8 | JS8Call | 39 | makeJs8Synth |
| nxdn | NXDN | 40 | makeNxdnSynth |
| dvbc | DVB-C | 42 | makeDvbcSynth |
| uplink | Mode S uplink | 45 | makeUplinkSynth |
| orbcomm | Orbcomm | 46 | makeOrbcommSynth |
| dpmr | dPMR | 47 | makeDpmrSynth |
| tetra | TETRA | 48 | makeTetraSynth |
| mac | MAC | 49 | makeMacSynth |
| ale | HF ALE | 51 | makeAleSynth |
| p25 | P25 | 52 | makeP25Synth |
| qo100 | QO-100 | 55 | makeQo100Synth |
| meteor | Meteor-M LRPT | 61 | makeMeteorSynth |
| goes | GOES HRIT | 62 | makeGoesSynth |
| cubesat | CubeSat telemetry | 63 | makeCubesatSynth |
| globalstar | Globalstar | 64 | makeGlobalstarSynth |
| vdes | VDES | 65 | makeVdesSynth |
| hrpt | HRPT / AHRPT | 66 | makeHrptSynth |
| zigbee | Zigbee / 802.15.4 | 67 | makeZigbeeSynth |
| flarm | FLARM | 68 | makeFlarmSynth |
| hfbeacon | HF beacons | 69 | makeHfbeaconSynth |
| dji | DJI drones | 70 | makeDjiSynth |
| loraraw | LoRa (raw) | 73 | makeLoraRawSynth |
| same | NOAA Weather Radio SAME | 78 | makeSameSynth |
| dect | DECT cordless phones | 80 | makeDectSynth |

## Modes not offered

| Engine code | Reason |
|---|---|
| 14 | GNSS is not transmitted: its generator is a satellite-signal simulator (spoofing) |
| 34, 41, 59, 71 | cellular signals are not transmitted: the generators model a network cell for testing the decoder |
| 53 | Bluetooth is not transmitted: it would impersonate a Bluetooth advertiser |
| 56, 72 | Wi-Fi is not transmitted: the generator models access-point beacons for the decoder |
| 58 | Drone Remote ID is not transmitted: it rides on Wi-Fi and Bluetooth advertisements |
| 79 | ANT+ is not transmitted: it would impersonate a sensor network |
| 81 | TETRAPOL is not transmitted: receive only, control-channel system information |
| 30, 76 | this is a receive-only sensor harvester: there is nothing meaningful to transmit |
| 43, 50, 54, 74 | this is an analysis mode: there is nothing to transmit |
| 44 | this is a spectrum analyser: there is nothing to transmit |
| 57 | this is a general listening mode: there is nothing specific to transmit |
| 60 | time signals are not transmitted: the generator imitates broadcast clocks (spoofing) |
| 75 | this is a signal workbench: it is for measurement, not a radio service |
| 77 | Multitone paging is not transmitted: the generator is not the real (unpublished) format, only a detector test |
| any other | this mode has no transmit generator |
