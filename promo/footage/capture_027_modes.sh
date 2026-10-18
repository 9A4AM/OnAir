#!/bin/bash
cd /tmp/cap027
C(){ out=$1; secs=$2; skip=$3; shift 3; ./cap7.sh $out.mp4 $secs $skip "$@"; echo "done $out"; }
C n_dji 8 12 --mode dji --autostart
C n_fpv 8 12 --mode fpv --autostart
C n_hrpt 8 12 --mode hrpt --autostart
C n_wisun 8 12 --mode wisun --autostart
C o_dvb 3 9 --dvb --autostart
C o_atsc 3 9 --atsc --autostart
C o_atsc3 3 9 --atsc3 --autostart
C o_isdbt 3 9 --isdbt --autostart
C o_dvbs 3 9 --mode dvbs --autostart
C o_dtmb 3 9 --mode dtmb --autostart
C o_dvbc 3 9 --mode dvbc --autostart
C o_atv 3 9 --mode atv --autostart
C o_mac 3 9 --mode mac --autostart
C o_dab 3 9 --dab --autostart
C o_fm 3 9 --fm --autostart
C o_drm 3 9 --mode drm --autostart
C o_hdr 3 9 --mode hdr --autostart
C o_cdr 3 9 --mode cdr --autostart
C o_adsb 3 9 --mode adsb --autostart
C o_uplink 3 9 --mode uplink --autostart
C o_acars 3 9 --mode acars --autostart
C o_vdl2 3 9 --mode vdl2 --autostart
C o_hfdl 3 9 --mode hfdl --autostart
C o_airband 3 9 --mode airband --autostart
C o_vor 3 9 --mode vor --autostart
C o_ils 3 9 --mode ils --autostart
C o_rid 3 9 --mode rid --autostart
C o_ais 3 9 --mode ais --autostart
C o_navtex 3 9 --mode navtex --autostart
C o_dsc 3 9 --mode dsc --autostart
C o_wefax 3 9 --mode wefax --autostart
C o_gnss 3 9 --mode gnss --autostart
C o_inmc 3 9 --mode inmc --autostart
C o_aero 3 9 --mode aero --autostart
C o_iridium 3 9 --mode iridium --autostart
C o_meteor 3 9 --mode meteor --autostart
C o_goes 3 9 --mode goes --autostart
C o_orbcomm 3 9 --mode orbcomm --autostart
C o_qo100 3 9 --mode qo100 --autostart
C o_cubesat 3 9 --mode cubesat --autostart
C o_dmr 3 9 --mode dmr --autostart
C o_nxdn 3 9 --mode nxdn --autostart
C o_p25 3 9 --mode p25 --autostart
C o_dpmr 3 9 --mode dpmr --autostart
C o_tetra 3 9 --mode tetra --autostart
C o_cell 3 9 --mode cell --autostart
C o_nr 3 9 --mode nr --autostart
C o_gsmr 3 9 --mode gsmr --autostart
C o_pocsag 3 9 --mode pocsag --autostart
C o_flex 3 9 --mode flex --autostart
C o_ism 3 9 --mode ism --autostart
C o_lorawan 3 9 --mode lorawan --autostart
C o_meshtastic 3 9 --mode meshtastic --autostart
C o_meshcore 3 9 --mode meshcore --autostart
C o_ble 3 9 --mode ble --autostart
C o_wifi 3 9 --mode wifi --autostart
C o_sonde 3 9 --mode sonde --autostart
C o_time 3 9 --mode time --autostart
C o_iono 3 9 --mode iono --autostart
C o_astro 3 9 --mode astro --autostart
C o_ft8 3 9 --mode ft8 --autostart
C o_ft4 3 9 --mode ft4 --autostart
C o_ft2 3 9 --mode ft2 --autostart
C o_wspr 3 9 --mode wspr --autostart
C o_psk 3 9 --mode psk --autostart
C o_js8 3 9 --mode js8 --autostart
C o_rtty 3 9 --mode rtty --autostart
C o_cw 3 9 --mode cw --autostart
C o_sstv 3 9 --mode sstv --autostart
C o_hamdrm 3 9 --mode hamdrm --autostart
C o_freedv 3 9 --mode freedv --autostart
C o_m17 3 9 --mode m17 --autostart
C o_dstar 3 9 --mode dstar --autostart
C o_packet 3 9 --mode packet --autostart
C o_loraaprs 3 9 --mode loraaprs --autostart
C o_meshcom 3 9 --mode meshcom --autostart
C o_ale 3 9 --mode ale --autostart
C o_general 3 9 --mode general --autostart
C o_specx 3 9 --mode specx --autostart
C o_specan 3 9 --mode specan --autostart
./batch_b.sh > batch_b.log 2>&1
echo ALLDONE > run_all.done
