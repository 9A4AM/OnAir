# core/src/source: Radios, drivers and capture files

Sources for this topic. One source_<radio>.cpp per supported radio, the native USB drivers (hackrf_usb, rx888_usb), Hamlib rig control, the antenna rotator, IQ file playback and recording, gain control and the frequency converter.

Mode folders (core/src/<mode>/) hold one decoder each; this folder holds code shared by all of them. See ARCHITECTURE.md.

`source_limesdr.cpp` drives LimeSDR boards through libLimeSuite (loaded at run time); `lime_board.h` holds the board facts it uses (name from the device string, Mini rate list, input by frequency, gain mapping, error hints) as plain functions that tests/source/test_limesdr_mini.cpp checks.
