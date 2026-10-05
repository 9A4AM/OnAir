#!/bin/bash
# cap2.sh out.mp4 secs skip -- runs the main build with given args (no --file unless passed), optional xdotool script in $CLICKS
out=$1; tot=$2; skip=$3; shift 3
export ONAIR_W=1920 ONAIR_H=1080 ONAIR_PROMO=1 ONAIR_SRCNAME="HackRF One" HOME=${HOMEDIR:-/tmp/h_cap}
mkdir -p $HOME
Xvfb :78 -screen 0 1920x1080x24 >/dev/null 2>&1 & XP=$!
sleep 1
DISPLAY=:78 /tmp/bmain/dect2 "$@" >/tmp/cap2_app.log 2>&1 & AP=$!
sleep $skip
( [ -n "$CLICKS" ] && DISPLAY=:78 bash -c "$CLICKS" ) &
ffmpeg -y -loglevel error -f x11grab -draw_mouse 0 -framerate 30 -video_size 1920x1080 -i :78 -t $tot -c:v libx264 -preset ultrafast -crf 8 -pix_fmt yuv420p "$out"
kill $AP; sleep 1; kill $XP
