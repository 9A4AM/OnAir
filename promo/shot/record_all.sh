#!/bin/bash
# record the UI clips (wall clock, 3000x1800) into $S/raw/*.mp4; the receiver runs in real time, so frame rates are modest (clips are blended up to 30 fps in the video)
H=$S/onair_headless
$H --scale 2 --fps 6 --file $S/demo16.cs8 --filerate 9142857.14 --play 1 --fakeepg --warm 10 --frames 228 --tab Player --at 90:Guide --at 126:Overview --at 168:TS --at 192:Outputs --at 210:Signalling --out $S/raw/tv.mp4 2>&1 | grep -v ALSA
$H --scale 2 --fps 6 --warm 20 --frames 150 --tab Overview --at 84:Sync --at 108:Signalling --at 132:"Frame map" --out $S/raw/t2.mp4 2>&1 | grep -v ALSA
$H --scale 2 --fps 6 --dvbt --snr 27 --warm 15 --frames 60 --tab Overview --out $S/raw/dvbt.mp4 2>&1 | grep -v ALSA
$H --scale 2 --fps 6 --snr 22 --echo 7 --echodelay 500 --warm 15 --frames 96 --tab Channel --at 60:Overview --out $S/raw/echo.mp4 2>&1 | grep -v ALSA
$H --scale 2 --fps 6 --snr 9 --warm 12 --frames 36 --tab Overview --out $S/raw/noisy.mp4 2>&1 | grep -v ALSA
$H --scale 2 --fps 6 --warm 6 --frames 30 --tab Antenna --out $S/raw/ant.mp4 2>&1 | grep -v ALSA
