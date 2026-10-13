# Airspy

Tested with an Airspy R2 on Linux (ATSC).

```sh
# debian / ubuntu
sudo apt install libairspy0 airspy
sudo usermod -aG plugdev $USER   # log out and back in
# replug the radio, then check it shows up
airspy_info
```

- R2 runs at 10 Msps for TV, Mini not tried yet
- gain is the linearity gain, 0 to 21
- 10 Msps needs a decent CPU, a 2-core laptop couldn't keep up with live ATSC

# Fobos SDR (RigExpert)

Not tested on a radio yet: the driver was written against RigExpert's library source and checked with a stand-in for the library
(`tests/native_fakes.cpp`). Reports are welcome.

OnAir drives the Fobos SDR through RigExpert's **agile** library, [libfobos-sdr-agile](https://github.com/rigexpert/libfobos-sdr-agile)
(`fobos_sdr.dll`, `libfobos_sdr.so`). That library only sees radios that run the agile firmware: hardware rev. 4 has it from the factory,
rev. 2 and 3 need the firmware from the library's Releases page. A Fobos SDR with the stock firmware is not listed.

The library is not shipped with OnAir:

- Windows: copy the 64-bit `fobos_sdr.dll` next to `OnAir.exe` (it uses the `libusb-1.0.dll` that is already there). The radio needs the
  WinUSB driver, as for RigExpert's own programs.
- Linux: build and install libfobos-sdr-agile (`cmake`, `make`, `sudo make install`, which also installs its udev rules), then
  `sudo ldconfig`.

`onair.log` (Windows) or the terminal says which library was loaded, how many radios it lists, and the hardware and firmware revision
of the one that was opened.

- RF input: 50 MHz to 6 GHz, at 8, 10, 12.5, 16, 20, 25, 32, 40 or 50 Msps (rev. 4: also 58 and 64); OnAir takes the smallest rate that is
  fast enough for the mode
- gain: one slider from 0 to 95 dB. The LNA has three steps (0, +16, +33 dB) and follows the slider; the rest is the VGA in 2 dB steps.
  The radio settings can fix the LNA step
- HF1 / HF2 inputs: below 50 MHz the radio samples one of them directly (radio settings: HF1, HF2 or off). The converter then runs at
  its fastest rate and OnAir shifts, filters and decimates to the rate the mode asks for; about 70 % of that rate is clean. The range
  ends at half the converter's rate (25 MHz at 50 Msps, 32 MHz on rev. 4), so 25 to 50 MHz is covered by neither input. The gain slider
  does nothing here: the HF inputs have no amplifier
- the fast converter rates need a USB 3 port; on HF, a lower "HF converter rate" in the radio settings takes less of the computer
- an external 10 MHz reference can be chosen in the radio settings
- SoapySDR: a SoapyFobosSDR module built with Visual Studio (PothosSDR) cannot be loaded by OnAir's Windows build, which is built with
  MinGW; the native driver needs no SoapySDR

