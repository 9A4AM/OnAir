# Pass scheduler

The Passes window (View > Passes) can take satellite passes by itself: it starts the receiver, tunes it, follows the Doppler shift and records each pass without you. It is for weather satellites and other passes you list in the window.

## Turning it on

1. Open **Passes**, set the **Receiver position** (the same position as the ADS-B antenna position) and load the satellite elements (TLE).
2. Tick the satellite groups you care about and set the **min el** slider (passes that never rise this high are ignored).
3. Tick **Schedule passes by itself**. The next 24 hours appear as a strip: the passes the scheduler takes on the top lane (amber when a pass is trimmed), the ones it skips below. Hover a pass for its times and maximum elevation. In the pass table a pass is marked as skipped when the SDR is reserved, the pass is too short, or a higher priority pass overlaps.

## How it chooses

One SDR serves one pass at a time.

- **Priority** (0 to 9 per satellite group, higher wins). When two passes overlap, the higher priority wins; a tie goes to the pass with the higher maximum elevation, then to the earlier rise.
- A lower priority pass is **trimmed** to the part the SDR is free for when that part is still worth keeping. A slot shorter than two minutes is not taken.
- The receiver is brought up 20 seconds before a slot starts, to tune and start the decoder.
- **Keep the SDR free every day** reserves a daily window (from and to hours) that the scheduler never uses, for your own listening.
- A receiver you started yourself is not interrupted: that time is treated as busy and the passes it overlaps are skipped or trimmed.

The settings (groups shown, priorities, the daily reservation, minimum elevation) are remembered.

Status: checked with the planning logic in unit tests and with synthetic passes; not yet run unattended against real satellites for days.
