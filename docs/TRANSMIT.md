# Transmit

**You are responsible.** OnAir limits nothing: not the frequency, not the power. What you type goes to the radio as typed (the radio's own range is shown as information only). Transmitting needs a licence or permission where you are, and a radio without an antenna, an attenuator or a dummy load can disturb others. Transmit is meant for test signals into a dummy load or a shielded setup, and for your own use. The first time you start, a one-time notice has to be accepted with "I understand".

## How to transmit, step by step

Switch the RX / TX slider at the top left to **TX**. The transmit panel fills the window. Nothing is sent until you press the big Start button.

**The header.** A big status word: **Idle**, **Preparing** (the radio is being opened and the first samples made), **Transmitting** (with a pulsing red dot and the elapsed time), **Test (no RF)** or **Carrier tune**. Next to it is the one primary button: **START TRANSMITTING** or **STOP**. Space starts and stops, Esc stops (not while you are typing in a text box). Errors and notes appear as boxes under the header, never as popups (except the one-time notice).

**Step 1, Signal.** A searchable list of every signal OnAir can send, grouped like the receiver's mode list, with a short description and an icon per group. Click one to choose it. The **Recent** buttons above the list bring back one of the last signals with its frequency and level.

**Step 2, Settings.**
- **Test (dry run, no RF)**: runs the generator at the radio's sample rate into nothing. No radio is opened or claimed, nothing is sent, and the spectrum and levels in step 3 show what would go out. Use it to try a signal and its settings first.
- **Transmit radio**: only radios with a transmit path are listed. If there is none, receive-only radios are named with the reason. A radio a receiver is using is refused in plain words unless it is genuinely full duplex.
- **Frequency (MHz)**: any value. The radio's specified range is shown next to the field; a value outside it gets a note, not a block.
- **Output level (dB)**: a slider over the radio's own TX gain scale, with the radio's stage names under it (HackRF: TX VGA, and the +14 dB amplifier as its own switch). Start low.
- **Offset, repeat and timing**: an offset added only to the frequency sent to the radio, "repeat until stopped" or one pass of N seconds.
- The signal's own options (message text, callsign, bandwidth, modulation, test pattern, input file, microphone / camera choice) sit under the signal's name, with sensible defaults. Empty fields send the mode's built-in demo signal.
- While a transmission runs, the frequency and the level can still be changed: **Apply frequency and level now** retunes the running radio (a radio that cannot change while sending says so and carries on as before).

**Step 3, Preview and send.** A live spectrum and waterfall of what will be sent (made in the background a moment after each change) or, while running, of the IQ really handed to the radio. Under it: **Peak** and **RMS** meters in dBFS, a **Clipping** lamp (red when the limiter has to work: lower the level), the **Sample rate** the radio will use and the estimated **Occupied bandwidth** (99% of the power), and an IQ scatter. **Carrier tune (hold)** sends an unmodulated carrier while the button is held, for adjusting drive, filter and SWR. **STOP ALL** stops the transmission, the test, the carrier and any pending start and gives the radio back. The log below lists what was sent and what happened.

**Pop out.** The small button in the top right corner moves the whole panel into a window of its own.

**Level safety.** Every block passes a soft limiter just before the radio: samples up to 0.9 of full scale pass untouched, above that they are rounded smoothly towards 1.0 and never exceed it, and a NaN or infinity becomes zero. The HackRF's 8-bit conversion clips at plus or minus 127. The limiter is not a power limit: it only keeps the generated signal inside the converter's range.

**Map upload** stays suspended while transmitting.

## Modes offered

This list is taken from the tables in `core/src/tx/tx.cpp` (`txModes()`).

### Modes offered (99)

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
| eczas | e-Czas 225 kHz | 87 | makeEczasSynth |
| fpv | FPV video (analog) | 103 | makeFpvSynth |
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
| gnss | GNSS | 14 | makeGnssSynth |
| ism | ISM sensors | 30 | makeIsmSynth |
| cell | Cellular (LTE) | 34 | makeCellSynth |
| gsmr | GSM-R | 41 | makeGsmrSynth |
| specx | Explorer | 43 | makeSpecxSynth |
| specan | Spectrum analyser | 44 | makeSpecanSynth |
| astro | Radio astronomy | 50 | makeAstroSynth |
| ble | Bluetooth LE | 53 | makeBleSynth |
| iono | Ionospheric sounders | 54 | makeIonoSynth |
| wifi | Wi-Fi | 56 | makeWifiSynth |
| general | General receiver | 57 | makeGeneralSynth |
| rid | Drone Remote ID | 58 | makeRidSynth |
| nr | 5G NR | 59 | makeNrSynth |
| timesig | Time signals | 60 | makeTimeSynth |
| nbiot | NB-IoT | 71 | makeNbiotSynth |
| halow | Wi-Fi HaLow | 72 | makeHalowSynth |
| codar | CODAR | 74 | makeCodarSynth |
| datawell | Datawell | 76 | makeDatawellSynth |
| multitone | Multitone paging | 77 | makeMultitoneSynth |
| ant | ANT+ | 79 | makeAntSynth |
| tetrapol | TETRAPOL | 81 | makeTetrapolSynth |
| ndb | NDB beacons | 31 | makeNdbSynth |
| elektro | Elektro-L | 82 | makeElektroSynth |

### Modes without a generator

Nothing is refused: OnAir blocks no mode, frequency or power. A mode can only be missing when it has no signal generator (for example the signal workbench, engine code 75); the panel then says "this mode has no transmit generator". Limits of the radio (no transmit path, a sample rate too low for the mode) are reported as they come.
