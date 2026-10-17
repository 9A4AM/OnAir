# Live transcripts: airband and marine voice as text

> **Off until you turn it on, and no speech model is included.** OnAir downloads one only when you click, or reads a file you put in the models folder.

OnAir can turn what is said on a voice channel into text while you listen, keep it, and tie it to the aircraft on the map. The speech engine
(whisper.cpp, MIT) runs **on your computer**: the audio never leaves it. Open it from **View > Transcripts**. The airband screen shows the last
calls as **live captions**; **ADS-B** shows the calls that name the selected aircraft in a **Voice (radio calls)** section.

## Privacy: read this first

- What is said on public aviation radio is public. A private conversation is not. **Marine VHF, amateur, CB and every other mode are off** until
  you tick them in the Transcripts window (*Settings and speech model*). The airband channels are the only ones on by default, and only once you
  have turned **Transcribe speech** on.
- While transcription runs, a red dot and **Transcribing speech: ...** are shown at the top of the status bar, in every mode.
- The text is kept in your data folder (`transcripts/transcripts.jsonl`) for the number of days you choose (default 7, at most 20 000 entries).
  **Delete all ...** in the window empties it.
- Sharing to the public map is a separate choice (below), off by default.
- You are responsible for following the radio-listening and privacy law where you live.

## Set up

1. **View > Transcripts**, then **Settings and speech model**.
2. Tick **Transcribe speech**.
3. Get a model, one of two ways:
   - **Download speech model:** `tiny.en` (75 MB), `base.en` (142 MB) or `small.en` (466 MB). A confirmation names the file and the address. OnAir asks
     `huggingface.co/ggerganov/whisper.cpp` for that one file, sends nothing else, and checks the file against a SHA-256 it was built with; a file that
     does not match is deleted. Other languages: *Other languages (multilingual models)* has `tiny`, `base` and `small` without `.en`.
   - **Your own file:** press **Open models folder** and put a whisper.cpp `ggml-*.bin` there (the folder is `<data folder>/models`: on macOS
     `~/Library/Application Support/OnAir/models`, on Windows `%LOCALAPPDATA%\OnAir\models`, on Linux `~/.local/share/onair/models`). It appears in the
     model list within a few seconds. What such a file contains is your responsibility.
4. Choose the model and the language. **English** is the default; **Auto-detect** and other languages need a multilingual model.

Which model: `tiny.en` keeps up with real time on a laptop (about 10 times faster than real time on an Apple M-series CPU) and is the right choice for
clear airband speech. `base.en` and `small.en` mistake fewer words and need a stronger computer. The speed shows in the window (*speed ... x real time*).

## What it does

- Per channel, OnAir cuts the audio into transmissions: a transmission starts when the level passes the gate (the airband squelch already gates it),
  keeps 0.35 s in front and 0.7 s behind, and ends when the speaker stops. Clicks shorter than half a second are dropped; a transmission is cut at 27 s.
- Each transmission goes to a queue (at most 6 waiting; **the oldest is dropped** when the speech engine is slower than the radio) and is transcribed on
  a background thread, never in the audio path. While the channels are quiet nothing runs, so it costs no battery. **CPU threads** limits the processor.
- The speech engine is given a hint for aviation (ICAO alphabet, "cleared", "runway", "flight level", airline radio names) or marine speech.
- Mistakes of the engine on silence and noise are filtered: text it itself marks as "no speech", sounds such as `[Music]`, the stock phrases it makes up
  on static ("Thank you.", "Thanks for watching"), a phrase repeated over and over, and more characters than a person can speak in that time.
- **Callsigns** are read from the text: `Lufthansa one two three alpha` becomes `DLH123A`, `Speedbird 4 7 2` `BAW472`, `november one two three alpha bravo`
  `N123AB`, `hotel bravo alpha lima charlie` `HBALC`, and a written `DLH123` as it is. The table has about 150 airline radio names. A call is linked to
  an aircraft on the screen when the callsign or registration matches an ADS-B aircraft; if it matches none now, it still shows up in that aircraft's
  list when the aircraft appears later. The *Speaker* column ("ATC" when the text starts with the callsign, "Aircraft" when it ends with it) is a guess.

## The window

Search the text (all words must be found), filter by frequency, aircraft (callsign, registration or ICAO address) or airport (a code or name in the
channel name or the text). Click a line to copy it. **Export TXT / SRT / JSON / CSV** save the lines shown (SRT times are offsets from the first line;
CSV opens in Excel as UTF-8). **Copy all shown** puts the text on the clipboard.

## Live translation (optional)

With a **multilingual** model (tiny, base or small without `.en`) tick **Translate to English** in the Transcripts settings. Speech that is not English is
transcribed in its own language and then translated to English by the same model (whisper's "translate" task), offline. The Transcripts window and the
aircraft voice log show the original and the English line together; the detected language is stored with each entry, and the TXT, SRT (a second
`[EN]` line per cue), JSON and CSV (columns `language`, `text`, `translation`) exports carry both. Search also looks in the translation.

- It is a second pass over each foreign-language transmission, so it costs about 1.5 times the processor time. English speech is not translated.
  **Performance guard:** when the engine's average speed is already slower than 0.8 x real time, the translation pass is skipped (the original is still
  kept) and the skip is counted.
- English-only models ignore the option.
- **Out of scope:** translation into languages other than English. Nothing is ever sent to a cloud service.

## Sharing to the public map (optional)

In the **Server** menu, once sending to the map is on: **Also share transcripts of aircraft radio calls (text becomes public on the map)**, **off by
default**. Only airband calls the engine was fairly sure of (50 % or better) and that are tied to an aircraft by the spoken callsign go up, a few per
upload, shown for 48 hours on that aircraft's card as messages of type VOICE. Phone numbers and e-mail addresses are removed first. The text can
contain mistakes. Nothing from marine, amateur or other channels is ever sent.

## If something does not work

- **"No speech model yet":** download one or put a file in the models folder. **"cannot load the speech model":** the file is not a whisper.cpp ggml
  model (a Core ML, GGUF-for-llama or PyTorch file will not load), or it is damaged.
- **Nothing is transcribed on airband:** the channel must be in the sample band, the squelch must open on it, and **Airband channels** must be ticked.
  The airband screen shows the live captions; the window shows *Heard, kept, left out*.
- **Many "skipped (too slow)":** the processor cannot keep up with the number of channels; use a smaller model or fewer channels. The queue never
  grows without limit.
- **Other languages come out in English:** the loaded model is English only (`.en`).
- The Windows and Linux packages use no GPU; the build is plain CPU code, portable x86-64 or ARM.

## For developers

`core/src/transcribe`: `segmenter` (voice activity, pre-roll and hang, 16 kHz resampler), `transcriber` (queue, workers, hallucination filters),
`callsign` (phonetic and telephony reader), `transcript` (store, search, TXT/SRT/JSON/CSV, file with retention), `models` (list, pinned SHA-256,
downloader), `whisper_backend` (whisper.cpp behind `SpeechBackend`), `audio_tap` (the sound of the non-airband modes). The app side is
`app/transcribe`. Tests: `test_transcribe` (fake speech backend: segmentation, queue overflow, filters, callsigns, search, exports, files, model list)
and `test_whisper_model`, which needs a model: `DECT2_WHISPER_MODEL=/path/ggml-tiny.en.bin ctest -R whisper_model` checks that silence, noise and a
tone produce no text, and prints the speed. Models are never committed. The vendored sources are in `third_party/whisper` (see its
`README-OnAir.md`); build without them with `-DDECT2_WITH_WHISPER=OFF`.
