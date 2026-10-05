#!/bin/bash
# usage: cap.sh out.mp4 seconds_total skip_seconds [app args...]
out=$1; tot=$2; skip=$3; shift 3
export ONAIR_PACE=${PACE:-0.25} ONAIR_W=1920 ONAIR_H=1080 ONAIR_PROMO=1 ONAIR_SRCNAME="HackRF One"
Xvfb :77 -screen 0 1920x1080x24 >/dev/null 2>&1 & XP=$!
sleep 1
DISPLAY=:77 ${APP:-/tmp/bmain/dect2} --file ${IQ:-/tmp/onair_demo_10Msps.cs8} --fakeepg --freq 600 "$@" >/tmp/cap_app.log 2>&1 & AP=$!
sleep $skip
ffmpeg -y -loglevel error -f x11grab -draw_mouse 0 -framerate 30 -video_size 1920x1080 -i :77 -t $((tot)) -c:v libx264 -preset ultrafast -crf 8 -pix_fmt yuv420p "$out"
kill $AP; sleep 1; kill $XP
