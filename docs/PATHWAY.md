
## 2026-10-18 FPV video (analog)
New mode fpv (family 101, active 102, std 103): channel plans, scan, FM video picture via the analog TV decoder, 6.0/6.5 MHz sound, snapshot/recording. Synthetic tests only.

## 2026-10-18 Auto subtitles (auto-subs branch)
Captions from speech for the sound of the heard receiver (FM/AM/DAB/DRM/HD Radio, TV audio, amateur and digital voice): core/src/transcribe/captions.cpp (sentence cues on the audio clock, SRT/VTT, TV source choice broadcast/generated/both/auto, style, eligible modes), music-vs-speech rhythm test in the segmenter, a second in-memory Transcriber for captions, caption window (radio) / picture overlay (TV), settings in Transcripts, .srt/.vtt next to audio recordings. Tested with the fake backend and synthetic audio; no screenshot or live run yet.
