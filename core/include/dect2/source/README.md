# core/include/dect2/source: Radios, drivers and capture files

Public headers (#include "dect2/source/<name>.h") for this topic. One source_<radio>.cpp per supported radio, the native USB drivers (hackrf_usb, rx888_usb), Hamlib rig control, the antenna rotator, IQ file playback and recording, gain control and the frequency converter.

Mode folders (core/src/<mode>/) hold one decoder each; this folder holds code shared by all of them. See ARCHITECTURE.md.
