# core/src/audio: Sound and video in/out, codecs

Sources for this topic. Audio input and output (CoreAudio / miniaudio back ends), video input, the AC-4 decoder, FLAC and MP3 encoders, the built-in vocoder glue and the shared ham audio front end (ham_listen, hf_demod). Codecs are installed by the user: see docs/CODECS.md.

Mode folders (core/src/<mode>/) hold one decoder each; this folder holds code shared by all of them. See ARCHITECTURE.md.
