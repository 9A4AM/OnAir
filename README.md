# DecT2

Open-source DVB-T2 receiver for macOS (HackRF One / HackRF Pro). See [PROJECT_BRIEF.md](PROJECT_BRIEF.md).

**Status: phase 5** — everything from phase 4 plus a live player: click a service to watch it (H.264/HEVC/MPEG-2 via FFmpeg, hardware decode with VideoToolbox when possible; MPEG audio/AAC/AC-3 through CoreAudio; audio-clock A/V sync; DVB subtitles). Decodes the live 522 MHz Sharjah multiplex from a HackRF Pro. Not done: teletext, deinterlacing, GPU colour conversion.
## HackRF Pro note
With firmware 2026.01.3 the tuned centre of a HackRF Pro is only exact at sample rates <= 10 Msps and at 20 Msps; in between it is offset by (20 - fs)/2 MHz. The app therefore captures at 10 Msps and resamples to the T2 native rate.
Good gain for a weak signal: LNA 32, VGA 20, RF amp on.

## Offline testing without a HackRF

`recordings/dubai_522MHz_10Msps_60s.cs8` (not in git, 1.2 GB) is a 60 s capture of the 522 MHz mux
(`hackrf_transfer -f 522000000 -s 10000000 -l 32 -g 20 -a 1`). Use it with the app's "IQ recording file" source
(10 Msps, cs8) or headless:

    build/dect2cli --file recordings/dubai_522MHz_10Msps_60s.cs8 --rate 10 --format cs8 --secs 60 --play 1600
