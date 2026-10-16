# core/include/dect2/results: Results and telemetry output

Public headers (#include "dect2/results/<name>.h") for this topic. The results (de)serialiser and the generated field ladder (results_io, results_fields), the telemetry JSON (rx_json) and the engine's results glue. The Android bridge (outside this repo) reads these headers.

Mode folders (core/src/<mode>/) hold one decoder each; this folder holds code shared by all of them. See ARCHITECTURE.md.
