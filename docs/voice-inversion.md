# Voice inversion

A stage for listening to analog voice (airband, NFM, WFM, SSB, the General receiver) that undoes simple frequency-inversion scramblers. **This is not encryption**: the scrambler only mirrors the audio spectrum, and anybody can undo it with a mixer and a lowpass filter. Encrypted traffic is never touched, digital voice modes and data modes do not get the button.

Open it with the wave button next to the audio cleanup button. It is **off** by default and asks you to accept a notice once (stored as `vi.accepted`): it may be illegal to listen to private communications in your country, you are responsible, frequency inversion is not encryption.

- **Simple**: f becomes carrier - f. Presets 2.55 / 2.7 / 3.0 / 3.3 / 3.4 kHz or any value.
- **Split-band**: the band lo..hi is cut at the split frequency and each half is mirrored inside its own range.
- **Search**: listens for 5 seconds, then tries the carrier (or split) values on that audio and scores how speech-like each result is (energy in the voice band, spectral tilt, the share around the first formant, no hum, no energy above the voice band, 4 Hz syllable modulation). The best one is set. The split search is coarser than the carrier search (the score has a plateau); fine-tune by ear.

Where it sits: `AudioOut::write()` (`core/src/audio/audioout.cpp`), before the audio cleanup chain. The decoders, clips and transcripts get the raw sound; "Record audio" saves the raw sound unless "Record the cleaned sound" is ticked in the cleanup pop-up. Settings are per mode (`vi.<mode>.<field>`). Latency is the filter delay (about 1 ms), nothing is computed while it is off, nothing allocates or locks in the audio callback. Time or segment scrambling is not implemented.

Code: `core/include/dect2/audio/voice_inversion.h`, `core/src/audio/voice_inversion.cpp` (clean-room: a phase-accumulator mixer with Butterworth and Linkwitz-Riley biquads), `app/panels/voice_inversion_ui.cpp`, `tests/audio/test_voice_inversion.cpp`.
