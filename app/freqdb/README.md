# app/freqdb

The Frequencies panel and the Tuner's "Known frequencies" drop-down. The data and the search are in core (`core/include/dect2/channels/freq_db.h`, `data/frequencies/*.tsv`); this folder is the screen. Tuning goes through the Memory panel's recall (`app/memory/memory_options.h`), so a mode switch applies the mode's tuning as a recalled memory does. See `docs/frequency-db.md`.
