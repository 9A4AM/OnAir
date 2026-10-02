<p align="center"><img src="assets/onair_1024.png" width="160" alt="OnAir logo"></p>

# OnAir

Open-source DVB-T2 and DVB-T receiver for macOS (HackRF One / HackRF Pro). See [PROJECT_BRIEF.md](PROJECT_BRIEF.md).

**Status:** receives DVB-T2 (T2-Base/Lite SISO; MISO, inter-frame time interleaving and sub-sliced PLPs are detected and reported but not decoded) and DVB-T (2K/8K, all guard intervals, QPSK/16-QAM/64-QAM, all code rates; hierarchical modulation not decoded), with automatic standard detection. Live player (FFmpeg, VideoToolbox, CoreAudio, DVB subtitles, teletext, programme guide), channel scanner, AGC and gain helper, multipath detector, GPU (Metal) LDPC decoding, TS output to file/UDP. Verified on a live 522 MHz Sharjah DVB-T2 multiplex and against GNU Radio reference encoders; DVB-T has been tested against generated signals only, not yet against a real transmitter.
## HackRF Pro note
With firmware 2026.01.3 the tuned centre of a HackRF Pro is only exact at sample rates <= 10 Msps and at 20 Msps; in between it is offset by (20 - fs)/2 MHz. The app therefore captures at 10 Msps and resamples to the T2 native rate.
Good gain for a weak signal: LNA 32, VGA 20, RF amp on.

## Offline testing without a HackRF

`recordings/dubai_522MHz_10Msps_60s.cs8` (not in git, 1.2 GB) is a 60 s capture of the 522 MHz mux
(`hackrf_transfer -f 522000000 -s 10000000 -l 32 -g 20 -a 1`). Use it with the app's "IQ recording file" source
(10 Msps, cs8) or headless:

    build/dect2cli --file recordings/dubai_522MHz_10Msps_60s.cs8 --rate 10 --format cs8 --secs 60 --play 1600
