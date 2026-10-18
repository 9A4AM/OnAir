# FPV video (analog)

The mode **FPV video (analog)** (TV group) receives the analog video links of drones and FPV goggles: composite video (PAL 625 lines or NTSC 525 lines)
frequency modulated onto a carrier about 17 MHz wide, with a sound subcarrier. It is receive only. The picture decoder is the one of the Analog TV mode in its
FM video setting; this mode adds the channel plans, the level of every channel, a scan, the sound and the recording.

## Channels

| Band | Channels 1 - 8 (MHz) |
|---|---|
| A | 5865 5845 5825 5805 5785 5765 5745 5725 |
| B | 5733 5752 5771 5790 5809 5828 5847 5866 |
| E | 5705 5685 5665 5645 5885 5905 5925 5945 |
| F (Fat Shark) | 5740 5760 5780 5800 5820 5840 5860 5880 |
| R (Raceband) | 5658 5695 5732 5769 5806 5843 5880 5917 |
| L (Low race) | 5362 5399 5436 5473 5510 5547 5584 5621 |
| 1.2 GHz | 1080 1120 1160 1200 1240 1280 1320 1360 |

Pick the band and channel in the tuner, click a cell of the **Channels** screen, or type the frequency. The receiver takes the channel of the plans nearest to the radio's
centre (within 4 MHz) down to baseband itself, so a few MHz of tuning error do not matter. Bands share frequencies (R5 5806 MHz and A4 5805 MHz are the same channel).

## What it shows

- **Video**: the live picture with the system, the colour system and the channel; **Snapshot** saves a PNG, **Record** saves up to 5 pictures a second as numbered
  PNG files in a new folder under `<data folder>/fpv/`. The on-screen display of the flight controller (battery, timer, RSSI) is part of the picture; it is shown, not decoded.
- **Channels**: the 7 x 8 grid with the level of each channel over the noise floor (bars), and **Scan 5.8 GHz** / **Scan 1.2 GHz**. A scan tunes the radio over the channels (about
  9 MHz apart, 0.7 s each) and measures every channel that lies inside the capture; at the end it tunes to the strongest transmitter (or goes back, when "Tune to the strongest" is off).
- **Spectrum**: the capture with the channels inside it marked.
- **Sound**: the sound subcarrier (6.0 or 6.5 MHz, found by itself or set) is FM demodulated and played; mute and volume are the program's.

## What the numbers mean

The level of a channel is the power within +-5 MHz of its centre. The noise floor is learnt from the lower part of the spectrum and falls to the quietest capture seen (it rises only by 0.2 dB a second),
so scan once with nothing transmitting nearby, or start on an empty channel, for the levels over the noise to be right. A channel counts as occupied at 8 dB over the noise.

## Needs

A radio that gives 16 Msps or more (a HackRF at 20 Msps). A 20 Msps capture holds one channel with a few MHz to spare; two 5.8 GHz channels are never in it together except
neighbouring bands' duplicates. Tuning to 5.8 GHz needs a radio that goes there (HackRF, or a PlutoSDR with the extended range).

## Test signal and tests

The built-in test signal is an FM video transmitter on the tuned channel: PAL or NTSC test card or colour bars, a 1 kHz tone on a 6.5 or 6.0 MHz subcarrier, noise, an offset
and an AC-coupled transmitter (`core/include/dect2/fpv/fpv_gen.h`). `tests/fpv` runs it through the receiver: PAL and NTSC lock, the sound tone, an off-centre channel, an empty channel and a scan.
Checked against synthetic signals only, not yet against a real transmitter.

## Credits

Written clean-room: the channel plans are the transmitter makers' nominal frequencies; the picture decoder is the project's own analog TV decoder. See THIRD_PARTY.md.
