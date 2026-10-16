# core/src/halow: Wi-Fi HaLow (IEEE 802.11ah, S1G) network monitor

Receive only. Passive: nothing is transmitted, probed, joined or decrypted; the security type is what a beacon announces. Engine mode std 72
(`halowTuning()`, telemetry `HalowTelemetry` in `halow_tel.h`), UI in `app/halow/halow_ui.cpp`, tests in `tests/halow/`.

## What is received

| | |
|---|---|
| Channel widths | 1, 2, 4, 8, 16 MHz (31.25 kHz tones, 40 us symbols, 8 us or 4 us guard interval) |
| Preambles | S1G_1M (1 MHz), S1G_SHORT and S1G_LONG (2 MHz and up; told apart by the rotation of the second SIG symbol) |
| Modulation / coding | BPSK, QPSK, 16/64/256-QAM; BCC rate 1/2, 2/3, 3/4, 5/6; MCS 0 - 9 and MCS 10 (1 MHz BPSK 1/2 with 2x repetition); 1 or 2 BCC encoders |
| Valid combinations | by the whole-bits-per-symbol rule, so 2 MHz MCS 9 only with 3 streams, MCS 10 only at 1 MHz (`mcsValid`) |
| Streams | one spatial stream is decoded. SIG fields with 2 - 4 streams, STBC, LDPC, A-MPDU or NDP are detected from the SIG field, counted and logged ("PPDU" lines) with width, MCS, streams, SNR; their data is not read |
| Beamformed single stream | S1G_LONG frames: the channel is taken from the D-LTF |
| Frames | S1G beacon (full and short), management beacon, probe request / response, data (addresses by To / From DS), control and management names. FCS (CRC-32) must pass. Elements: SSID, compressed SSID, RSN (security), S1G Operation (widths, primary width, class, channel, basic MCS), S1G Capabilities (long preamble), TIM (DTIM), Country, power constraint, supported rates, RAW, TWT (presence) |
| Regions (`halow_chan.h`) | US/CA 902 - 928 (1/2/4/8/16), EU 863 - 868 (1/2), JP 916.5 - 927.5, KR 917.5 - 923.5, SG 866 - 869 and 920 - 925, AU and NZ 915 - 928, CN 755 - 779 and 779 - 787, IN 865 - 867, TW 920 - 928 (1/2/4/8 where the band holds them) |

## Sample rate

The radio's rate decides the widest channel. 1 MHz from 2 Msps, 2 MHz from 2 Msps (4 recommended), 4 MHz from 4 Msps (5), 8 MHz from 8 Msps (10),
16 MHz from 16 Msps (20). Below that the width is not tried and the app says so (`maxWidthMhz`, `minRateHz`, `recommendedRateHz` in `halow_phy.h`).
The receiver runs one chain per tone grid, so a narrow signal is not looked at through a wide noise band: 1 / 2 / 4 MHz on a 4 Msps grid, 8 MHz on
8 Msps, 16 MHz on 16 Msps (the 16 MHz channel is two 8 MHz halves around +-128 tones, FFT size 512).

CPU: one core handles Auto 1 - 4 MHz (4 Msps) at about a tenth of its time; Auto, all widths at 20 Msps runs three chains and takes most of a core when the
band is full of traffic (measured on a laptop: 0.7 - 1.0 of real time with a PPDU every 3 ms). Choosing one width runs only the chain that carries it.

## Files

| file | job |
|---|---|
| `halow_phy.cpp` | tone layouts of the five widths, STF / LTF, MCS table, valid combinations, data bits per symbol |
| `halow_code.cpp` | CRC-4, scrambler, BCC + Viterbi, puncturing, interleaver (both permutations), constellations and soft demapper |
| `halow_sig.cpp` | SIG fields of the three preambles (CRC-4, reserved bits, bandwidth field, MCS / stream check) |
| `halow_ppdu.cpp` | PPDU builder for the test signal |
| `halow_demod.cpp` | FFT window, pilot tracking, equalisation, soft bits, data field decoding |
| `halow_chain.cpp` | one receive chain: resampling, STF detector, timing from the LTF, offset, channel, SIG, data |
| `halow_rx.cpp` | mixer, chains, network table, frame log, results file |
| `halow_mac.cpp` | MAC frames and elements |
| `halow_chan.cpp` | channel plans, operating classes |
| `halow_gen.cpp` | test signal: every width / MCS / preamble, offsets, clock error, off-centre, noise, echo, mid-frame start |

## How it was checked, and what is not independent

* Independent of this code: the published S1G data rates (every width at one stream, 0.3 Mbit/s at 1 MHz up to 78 Mbit/s at 16 MHz, 86.7 / 346.7 Mbit/s with
  short GI and 4 streams) fix the tone counts, bits per symbol and the valid MCS; the 127-bit 802.11 scrambler sequence; the 802.11 QAM and puncturing definitions;
  the 31.25 kHz / 40 us / 80 us / 560 us timings. All pass in `test_halow_phy`.
* Loopback (generator to receiver, same author): `test_halow_sweep_narrow` and `test_halow_sweep_wide` cover every valid width x MCS x preamble x guard interval x
  three impairment sets (clean; +30 kHz carrier offset, off-centre signal, +40 ppm; -30 kHz, -40 ppm, a 2 us echo at -10 dB, the stream starting mid-PPDU),
  `test_halow_modes` the rest (sensitivity per width, radio rates 2 - 25 Msps, manual width, reported-only PPDUs, beamformed, invalid combinations, 511-byte PSDUs,
  a random mixture, noise and a tone). This proves the receiver reads what the generator writes, not that a real 802.11ah radio sends exactly this.
* No live capture and no copy of the standard were available. These are written from memory and public descriptions and are UNVERIFIED against real signals:
  the 4 / 8 / 16 MHz LTF sequences (4 MHz as remembered, 8 and 16 MHz an extension of it), the STF phase rotations (not applied), the exact SIG bit positions
  (S1G_1M follows the TGah documents; the short / long layout and the long preamble's extra symbols D-STF, D-LTF, SIG-B are the author's best reading), pilot values,
  the 2-encoder parsing at 16 MHz MCS 7 - 9, the 1 MHz LTF1 length, and the operating class numbers other than US 1 - 5 and EU 6 - 7 (frequencies = start + 0.5 MHz x channel
  number are certain; which channels each regulator allows come from public summaries). If a real capture disagrees, these are the first places to look.

## Not supported

* LDPC coding (the 802.11 parity-check matrices of Annex F were not available to check): the frame is counted as "LDPC".
* More than one spatial stream, STBC: one RF channel is one antenna; the streams cannot be separated. Counted from the SIG field.
* The duplicate formats S1G_DUP_1M / S1G_DUP_2M (a 1 MHz PPDU repeated across a wider channel, used for some beacons and control frames): the preamble of the copies is not matched to the channel layout, so they are not found.
* A-MPDU (the delimiters are not parsed), NDP contents, MU-MIMO (S1G_LONG with group ID), S1G short-MAC-header control frames, TWT / RAW scheduling details beyond presence.
* Decryption of any kind.
