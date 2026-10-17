# tests/source

Tests of radios and drivers: native drivers with the fake radio (native_fakes.cpp), Soapy (soapy_testdriver.cpp), Hamlib, RX888, gain, IQ recording and SigMF.

Same topic folder as core/src/<topic>/; see ARCHITECTURE.md. Mode folders hold one decoder mode each.

`test_limesdr_mini.cpp`: the LimeSDR Mini (discovery by name, rates, input by frequency, gain, calibration, error texts) against the fake LimeSuite in native_fakes.cpp (`FAKE_LIME_MODEL` picks the board).
