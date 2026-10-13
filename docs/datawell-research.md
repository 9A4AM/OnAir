# Datawell Waverider buoy HF link: what is public, what was found (research note, 2026-10-13)

Sources (all fetched and read; downloaded into a scratch directory, nothing executed):
- sigidwiki.com/wiki/Datawell_Buoy_HF_Link and its 5 audio samples (File:buoy-29525.mp3, Datawell_transmittertype_2FSK.mp3, Datawell_transmittertype_4FSK.mp3, two "WRONG" combinations).
- Datawell Waverider Reference Manual DWR-MkIII (cdip.ucsd.edu/m/documents/_downloads/ce99796d384cd0194a745160409da9ba/mk3.pdf), section 5.7 "Data format".
- Datawell Waverider Transmission Protocol for the Mk4 buoys, 16 Jan 2019 (datawell.nl/wp-content/uploads/2022/09/datawell_specification_mk4_dwtp.pdf).
- Datawell RX-C4 receiver manual, 2023 (datawell.nl/wp-content/uploads/2023/04/datawell_manual_rx-c4CE_2023-04-5.pdf), specifications and appendix B.
- Datawell Library Manual (libdatawell) and the DWR4 manual (cdip mk4.pdf): no over-the-air coding in them.

Public
- Channels 25.5 - 35.5 MHz (45 MHz on request), USB, about 50 km; listed channels (sigidwiki): 35.3875, 35.2875, 34.7375, 31.9275, 31.2290, 30.4750, 29.9250, 29.5250 MHz.
- Link rate 81.92 bit/s with 2FSK (HXV: DWR-MkIII, DWR-G, WR-SG) and 163.84 bit/s with 4FSK (HVA: DWR4 /ACM) (RX-C4 specifications): both are 81.92 symbols per second.
- HXV real-time format (reference manual 5.7): 64-bit vectors, 1.28 Hz: cyclic 16 | heave 12 | north 12 | west 12 | parity 12; displacements 12-bit sign-magnitude, cm;
  WR-SG: two vertical samples and a dummy 101010101010; 18 vectors = a block (sync 0x7FFF, system word number + 12-bit word, 4 x 4 spectrum words), 16 blocks = a full system file
  (Hrms, fz, PSDmax, Tr, Tw, battery, accelerometer offsets, latitude, longitude, orientation, inclination) and a 64 band directional spectrum (f, direction, spread, m2, n2, K, relative PSD).
- HXV text lines as printed by the RX-C4 (appendix B): examples used as test vectors.
- DWTP (HVA, after the receiver): hexadecimal vector A of 31 characters = 9 bytes real time data (2 samples of heave / north / west, 12-bit hyperbolic sine, a = 0.001, b = 457, NaN -2048),
  3 bytes packet data (0x7E framed, 0x7D escaped), messages with a CRC-4 (x^4 + x + 1) and ids 0xF20 ... 0xFE3. The formulas of the sine coding are pictures in the PDF (x = a b sinh(i / b) fits the stated range).

Found (not published): the HXV parity. The manual says "a Galois code table encodes 63 bits". Brute force over all generator polynomials and bit selections, on the 24 vectors of the sigidwiki 2FSK sample
and the 3 example lines of the RX-C4 manual, gives exactly one answer for all 27 vectors: BCH(63,51), generator x^12 + x^10 + x^8 + x^5 + x^4 + x^3 + 1 (octal 12471), codeword = the 64 vector bits without
bit 27 (the least significant heave bit, counting from the first bit sent). It repairs up to 2 bit errors. Tones of the sample: 2FSK 1436 / 1565 Hz in the audio (129 Hz apart), 4FSK about 78 Hz apart.

Not public, so not decoded
- The HVA (4FSK) over-the-air coding: "BCH / Reed-Solomon concatenated" (RX-C4). The sigidwiki 4FSK sample shows 128-bit frames (one per 0.78 s), no BCH(63,51) windows, nothing else could be derived.
  The DWTP layer above it is only reachable after the receiver's error correction, so it is not implemented.
- The HXV spectrum words of the real 29.525 MHz recording (a noisy WR-SG signal: displacements decode, vectors are lost, the 14 s do not reach a sync word).

Checked on real audio: the sigidwiki 2FSK sample decodes 20 clean vectors, system word 4 = 25.00 C, spectrum points n = 16 and 17; the buoy-29525 sample decodes WR-SG vectors (0xAAA dummy) with 3 of 11 lost.
Everything else on synthetic signals built from the documented layout.

Legal: receive only, nothing is transmitted. Datawell manuals: 28.0 - 29.7 MHz is reserved for amateur radio, a radio permit is needed in territorial waters. Whether you may receive and keep the telemetry depends on your country.
