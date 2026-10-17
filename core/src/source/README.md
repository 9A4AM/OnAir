# core/src/source: Radios, drivers and capture files

Sources for this topic. One source_<radio>.cpp per supported radio, the native USB drivers (hackrf_usb, rx888_usb), Hamlib rig control, the antenna rotator, IQ file playback and recording, gain control and the frequency converter.

Mode folders (core/src/<mode>/) hold one decoder each; this folder holds code shared by all of them. See ARCHITECTURE.md.

<<<<<<< HEAD
## Airspy Mini at 10 Msps (opt-in)

An Airspy Mini lists 6 and 3 Msps (an R2 lists 10 and 2.5). The Mini's radio settings have "10 Msps (unofficial)" (key `mini10`, off by
default); when on, `source_airspy.cpp` adds 10 Msps to the rate it may pick and asks libairspy for it. The firmware decides: a refusal is
reported with libairspy's error code and name and the Mini falls back to the fastest listed rate. With the setting off nothing changes.
Tests: `tests/source/test_native.cpp` (fake libairspy: rate choice with and without the opt-in, refused rate, refused listed rate).

=======
`source_limesdr.cpp` drives LimeSDR boards through libLimeSuite (loaded at run time); `lime_board.h` holds the board facts it uses (name from the device string, Mini rate list, input by frequency, gain mapping, error hints) as plain functions that tests/source/test_limesdr_mini.cpp checks.
>>>>>>> limesdr-mini
