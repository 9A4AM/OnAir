# core/src/bandplan: Band plans

`bandplan.cpp`: the model (bands, channels, channel and frequency both ways), validation, reading and writing CSV and JSON, the built-in plans and the registry of the user's plans (atomic files in the settings folder). The built-in data is `data/bandplans/builtin.json`, compiled into `kBuiltinBandPlans` by CMake (`generated/bandplans_data.inc`). The file format and the countries are described in docs/band-plans.md; the interface is in app/bandplan.

Mode folders (core/src/<mode>/) hold one decoder each; this folder holds code shared by all of them. See ARCHITECTURE.md.
