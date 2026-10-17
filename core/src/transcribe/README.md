# core/src/transcribe: Live transcripts

Live transcripts: speech to text for voice channels (segmenter, speech-engine queue and filters, callsign reader, transcript store and exports, model list and download, audio tap). Pure rules plus one back end (whisper.cpp, third_party/whisper); no model is shipped. See docs/TRANSCRIBE.md.

Mode folders (core/src/<mode>/) hold one decoder each; this folder holds code shared by the voice modes. See ARCHITECTURE.md.
