#!/bin/bash
cd /tmp/cap024
C(){ out=$1; secs=$2; skip=$3; shift 3; ./cap4.sh $out.mp4 $secs $skip "$@"; echo "done $out"; }
TAB2='sleep 6; python3 /tmp/cap024/clicktab.py 2'
for m in mac hdr acars tetra cell pager ble astro packet hfdl dvbc ais sonde wifi ism cubesat rid nr time; do PRE="$TAB2" C $m 9 14 --mode $m --autostart; done
C specan 9 12 --mode specan --autostart
PRE="$TAB2" C meteor_pic 9 35 --mode meteor --autostart
PRE="$TAB2" C wefax 9 40 --mode wefax --autostart
PRE="$TAB2" C sstv 9 45 --mode sstv --autostart
PRE="$TAB2" C ft8 9 25 --mode ft8 --autostart
for l in de es fr lt zh; do C lang_$l 4 8 --mode acars --autostart --lang $l; done
C light 9 10 --mode dvbs --autostart --light
C map 9 15 --mode adsb --autostart --map
C second 9 12 --mode fm --autostart --second "DAB / DAB+" 220.352
ACTIONS='sleep 1.5; xdotool mousemove 150 184 click 1; sleep .5; for ch in l o r a; do xdotool type --delay 250 $ch; done; sleep 1.5; xdotool key ctrl+a BackSpace; sleep .4; for ch in s a t; do xdotool type --delay 250 $ch; done; sleep 2' C search 10 8 --mode meteor --autostart
echo ALLDONE
