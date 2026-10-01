# DecT2

Open-source DVB-T2 receiver for macOS (HackRF One / HackRF Pro). See [PROJECT_BRIEF.md](PROJECT_BRIEF.md).

**Status: phase 2** — P1/GI/CFO sync, pilot-aided channel estimation (P2 + scattered pilots), L1-pre and L1-post decoding (LDPC, BCH, CRC-32), equalised constellations, channel / impulse-response / SNR-per-carrier views. The signal generator is a real T2 transmitter model (pilots + L1) used for tests; its L1 encoders, pilot maps and frequency interleaver were checked bit-for-bit against the open-source gr-dvbt2 transmitter. Tests: `build/test_sync`, `build/test_l1`, `build/test_pilots`.