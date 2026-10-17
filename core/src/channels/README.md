# core/src/channels: Channel tables and lookup

Sources for this topic. The channel database, channel search, locating a wideband channel inside the sample band, and the multi-channel TV row.

Mode folders (core/src/<mode>/) hold one decoder each; this folder holds code shared by all of them. See ARCHITECTURE.md.

## Memories

`memory_bank.cpp` (the model: add, update, duplicates, search, sort, the recall order), `memory_io.cpp` (JSON and CSV files, imports, the atomic save) and `memory_json.*` (a small JSON reader). A memory is a frequency with the mode and receiver settings it was stored with; the Memory panel in app/memory uses it. The file format is described in app/memory/README.md.
