#!/bin/bash
# the new screens and features, each opened with the app's own dev flags
cd /tmp/cap027
C(){ out=$1; secs=$2; skip=$3; shift 3; ./cap7.sh $out.mp4 $secs $skip "$@"; echo "done $out"; }
C f_touch 8 12 --touch --dvb --autostart
C f_touchkeypad 8 12 --touchsheet keypad --dvb --autostart
C f_touchmodes 8 8 --touchsheet modes --dvb --autostart
C f_tx 8 6 --tx
C f_txdry 8 12 --txdry dvbt
C f_images 8 55 --mode hrpt --autostart --images
C f_images2 8 40 --mode elektro --autostart --images
C f_packs 8 6 --packs --dvb
C f_passive 8 14 --passive-radar
C f_rfi 8 14 --rfi --mode specan --autostart
C f_memory 8 6 --memory --dvb --autostart
C f_passes 8 6 --passes --mode meteor --autostart
C f_df 8 10 --df --mode fm --autostart
C f_audio 8 8 --audioclean adv --mode airband --autostart
C f_second 8 14 --mode fm --autostart --second "DAB / DAB+" 220.352
C f_rx2 8 14 --rx2 --mode adsb --autostart
C f_scanner 8 12 --scanner --dvb --autostart
C f_hamlib 8 6 --hamlib --dvb
C f_hc 8 12 --hc --mode dvbs --autostart
C f_light 8 12 --light --mode ft8 --autostart
C f_wb 8 8 --mode wb
C f_map 8 20 --mode adsb --autostart --map
C f_p25 8 14 --mode p25 --autostart
C f_dvb 8 14 --dvb --autostart
echo ALLDONE_B
