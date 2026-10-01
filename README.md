# DecT2

Open-source DVB-T2 receiver for macOS (HackRF One / HackRF Pro). See [PROJECT_BRIEF.md](PROJECT_BRIEF.md).

**Status: phase 3** — everything from phase 2 plus full PLP decoding: time/cell de-interleaving, rotated-QAM demapping, layered NEON LDPC, BCH correction, BB-frame headers (normal and high-efficiency mode). Decodes the live 522 MHz Dubai mux (32K ext, PP8, 64-QAM 2/3 rotated) in real time from a HackRF Pro. Tests: `build/test_sync`, `build/test_l1`, `build/test_pilots`, `build/test_fec`; headless receiver: `build/dect2cli`.
## HackRF Pro note
With firmware 2026.01.3 the tuned centre of a HackRF Pro is only exact at sample rates <= 10 Msps and at 20 Msps; in between it is offset by (20 - fs)/2 MHz. The app therefore captures at 10 Msps and resamples to the T2 native rate.
Good gain for a weak signal: LNA 32, VGA 20, RF amp on.
