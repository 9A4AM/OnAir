# OnAir promo video

`OnAir_promo.mp4` (1080p30, 66 s). Every scene is drawn on a canvas in the app's own palette (accent blue, navy waterfall ramp,
accent-coloured constellations on near-black plots); nothing imitates the app's windows.

* `scenes.js` + `common_head.js` + `index.html`: the scenes. `node render.cjs render 4` renders frames with Playwright
  (`PAGE=index.html FDIR=frames`), then `ffmpeg -framerate 30 -i frames/f_%05d.jpg -i music.wav ...`.
* `music.py`: the score. Composed as MIDI stems (piano, pads, strings, bass, vibraphone, drums, glockenspiel), rendered with
  FluidSynth (FluidR3 GM soundfont), levelled and mixed with reverb, sidechain and compression. 120 BPM, one bar = 2 s, so every
  scene boundary falls on a bar line. Needs `fluidsynth`, `fluid-soundfont-gm`, `mido`, `scipy`.
