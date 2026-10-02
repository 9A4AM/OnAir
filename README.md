<p align="center"><img src="assets/onair_1024.png" width="160" alt="OnAir logo"></p>

# OnAir

Open-source DVB-T2, DVB-T and ATSC (8-VSB) receiver for macOS (HackRF One / HackRF Pro natively, and other radios through SoapySDR). See [PROJECT_BRIEF.md](PROJECT_BRIEF.md).

**Status:** receives DVB-T2 (T2-Base/Lite SISO; MISO, inter-frame time interleaving and sub-sliced PLPs are detected and reported but not decoded) and DVB-T (2K/8K, all guard intervals, QPSK/16-QAM/64-QAM, all code rates; hierarchical modulation not decoded), with automatic standard detection. Live player (FFmpeg, VideoToolbox, CoreAudio, DVB subtitles, teletext, programme guide), channel scanner, AGC and gain helper, multipath detector, GPU (Metal) LDPC decoding, TS output to file/UDP. Verified on a live 522 MHz Sharjah DVB-T2 multiplex and against GNU Radio reference encoders; DVB-T has been tested against generated signals only, not yet against a real transmitter.

**ATSC 1.0 (8-VSB, 6 MHz)** is implemented too (switch next to the tuner settings): pilot and symbol-clock recovery, field sync, an adaptive equaliser (plain plus decision feedback, handles echoes up to about -1.5 dB at 8 us), trellis decoding, Reed-Solomon, channel names and numbers from PSIP, scanner presets for the US channel plan. Its channel coding is bit-exact against GNU Radio's gr-dtv and the receiver is tested on generated signals with carrier and clock offsets, noise and echoes. It has **not** been tried on a real ATSC transmission yet (the author is in a DVB-T2 country); a 6 MHz capture at 8-10 Msps would be welcome. Not done: ATSC programme guide (EIT/ETT), a 0 dB echo cannot be acquired. See [docs/ATSC_HANDOFF.md](docs/ATSC_HANDOFF.md).
## Supported radios
The HackRF has its own native driver. Every other radio goes through [SoapySDR](https://github.com/pothosware/SoapySDR) (build with SoapySDR installed: `brew install soapysdr`, or `apt install libsoapysdr-dev`; then install the SoapySDR module for your radio, for example `soapyairspy`, `soapysdrplay3`, `soapyrtlsdr`, `soapyplutosdr`, `soapybladerf`, `soapyuhd`, `limesuite`). Radios that SoapySDR finds appear in the source list next to the HackRF; the gain control becomes one overall gain slider.

What a radio can decode depends on its sample rate (a channel needs about 1 Msps per MHz of width):

| Radio | Max rate | 8 MHz DVB-T / T2 / ATSC |
|---|---|---|
| HackRF One / Pro | 20 Msps | yes (tested) |
| Airspy R2 / Mini | 10 / 6 Msps | R2: should work (untested); Mini: up to 5-6 MHz channels |
| SDRplay RSP1A / RSPdx / RSPduo | about 10 Msps | should work (untested) |
| PlutoSDR, BladeRF, LimeSDR, USRP | 20+ Msps | should work (untested) |
| RTL-SDR | about 2.4-3 Msps | no, only narrow signals such as 1.7 MHz DVB-T2-Lite |
| Airspy HF+ and other HF radios | under 1 Msps | no |

Only the HackRF has been tested on real hardware; the SoapySDR path is tested through a fake radio (`tests/soapy_testdriver.cpp`) that streams the synthetic signal.

## HackRF Pro note
With firmware 2026.01.3 the tuned centre of a HackRF Pro is only exact at sample rates <= 10 Msps and at 20 Msps; in between it is offset by (20 - fs)/2 MHz. The app therefore captures at 10 Msps and resamples to the T2 native rate.
Good gain for a weak signal: LNA 32, VGA 20, RF amp on.

## Offline testing without a HackRF

`recordings/dubai_522MHz_10Msps_60s.cs8` (not in git, 1.2 GB) is a 60 s capture of the 522 MHz mux
(`hackrf_transfer -f 522000000 -s 10000000 -l 32 -g 20 -a 1`). Use it with the app's "IQ recording file" source
(10 Msps, cs8) or headless:

    build/dect2cli --file recordings/dubai_522MHz_10Msps_60s.cs8 --rate 10 --format cs8 --secs 60 --play 1600
