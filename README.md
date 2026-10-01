# DecT2

Open-source DVB-T2 receiver for macOS (HackRF One / HackRF Pro). See [PROJECT_BRIEF.md](PROJECT_BRIEF.md).

**Status: phase 4** — everything from phase 3 plus BB frames -> MPEG transport stream (normal and high-efficiency mode), PSI/SI parsing (service list with names, codecs, bitrates, now/next, network, UTC), file recording and paced UDP/RTP streaming of the whole multiplex or a single service. Decodes the live 522 MHz Sharjah multiplex (10 services) from a HackRF Pro; recordings play in ffmpeg/VLC. Tests: `build/test_sync`, `build/test_l1`, `build/test_pilots`, `build/test_fec`, `build/test_ts`; headless receiver: `build/dect2cli` (`--ts out.ts`, `--udp host:port`, `--service id`).
## HackRF Pro note
With firmware 2026.01.3 the tuned centre of a HackRF Pro is only exact at sample rates <= 10 Msps and at 20 Msps; in between it is offset by (20 - fs)/2 MHz. The app therefore captures at 10 Msps and resamples to the T2 native rate.
Good gain for a weak signal: LNA 32, VGA 20, RF amp on.
