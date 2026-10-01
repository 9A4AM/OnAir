# DecT2

Open-source DVB-T2 receiver for macOS (HackRF One / HackRF Pro). See [PROJECT_BRIEF.md](PROJECT_BRIEF.md).

**Status: phase 5** — everything from phase 4 plus a live player: click a service to watch it (H.264/HEVC/MPEG-2 via FFmpeg, hardware decode with VideoToolbox when possible; MPEG audio/AAC/AC-3 through CoreAudio; audio-clock A/V sync; DVB subtitles). Decodes the live 522 MHz Sharjah multiplex from a HackRF Pro. Not done: teletext, deinterlacing, GPU colour conversion.
## HackRF Pro note
With firmware 2026.01.3 the tuned centre of a HackRF Pro is only exact at sample rates <= 10 Msps and at 20 Msps; in between it is offset by (20 - fs)/2 MHz. The app therefore captures at 10 Msps and resamples to the T2 native rate.
Good gain for a weak signal: LNA 32, VGA 20, RF amp on.
