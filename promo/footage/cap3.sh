#!/bin/bash
# cap3.sh out.mp4 secs skip [app args] : synthetic/any source, mouse parked, optional CLICKS
out=$1; tot=$2; skip=$3; shift 3
export ONAIR_W=1920 ONAIR_H=1080 ONAIR_PROMO=1 ONAIR_SRCNAME="HackRF One" ONAIR_SPACE=${SPACE:-0.3} ONAIR_WFAUTO=1 ONAIR_PACE=${PACE:-0.25} HOME=${HOMEDIR:-/tmp/h_cap3}
mkdir -p $HOME; echo '{"wizardDone":true}' > /dev/null
Xvfb :79 -screen 0 1920x1080x24 >/dev/null 2>&1 & XP=$!
sleep 1; DISPLAY=:79 xdotool mousemove 1915 1075 2>/dev/null
DISPLAY=:79 /tmp/b2/dect2 "$@" >/tmp/cap3_app.log 2>&1 & AP=$!
sleep 2; DISPLAY=:79 xdotool mousemove 1915 1075 2>/dev/null
sleep $skip
( [ -n "$CLICKS" ] && DISPLAY=:79 bash -c "$CLICKS" ) &
ffmpeg -y -loglevel error -f x11grab -draw_mouse 0 -framerate 30 -video_size 1920x1080 -i :79 -t $tot -c:v libx264 -preset ultrafast -crf 8 -pix_fmt yuv420p "$out"
kill $AP; sleep 1; kill $XP
