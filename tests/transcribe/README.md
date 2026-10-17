# tests/transcribe: Live transcripts tests

test_transcribe runs without a speech engine (a fake backend): segmenter, queue overflow, hallucination filters, callsign reader, transcript store, exports, model list. test_whisper_model needs a model file (DECT2_WHISPER_MODEL=...) and is skipped without it. See docs/TRANSCRIBE.md.
