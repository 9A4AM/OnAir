# OnAir promo video

`OnAir_promo.mp4` (1080p30, 54 s) is made from real captures of the app's own UI.

* `shot/` builds a headless Linux version of `app/main.mm`: `make_headless.py` swaps the Metal/Cocoa parts for a software
  rasteriser (`swrender.h`) and runs the real UI and receiver code. `gen_iq.cpp` writes a DVB-T IQ recording that carries a real
  H.264/MPEG audio transport stream, which the app then decodes and plays. `record_all.sh` records the clips.
* `scenes.js` + `common_head.js` compose the video on a canvas (`render.cjs` renders frames with Playwright); `audio.py`
  synthesises the score. Needs the clips extracted to `clips/<name>/%04d.jpg` (`ffmpeg -i raw/<name>.mp4 -q:v 4 clips/<name>/%04d.jpg`).
* The footage uses the app's built-in synthetic DVB-T2 / DVB-T signal and a generated test video, not an over-the-air capture.
