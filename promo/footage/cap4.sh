#!/bin/bash
# cap4.sh out.mp4 secs skip [app args]: the unmodified main build under Xvfb at 1920x1080, mouse parked, optional ACTIONS (xdotool) during the take
out=$1; tot=$2; skip=$3; shift 3
export DECT2_WINSIZE=1920x1080 HOME=${HOMEDIR:-/tmp/h_cap4_$$}; rm -rf $HOME; mkdir -p $HOME
Xvfb :84 -screen 0 1920x1080x24 >/dev/null 2>&1 & XP=$!
sleep 1; DISPLAY=:84 xdotool mousemove 1915 1075 2>/dev/null
DISPLAY=:84 /tmp/a024/dect2 "$@" >/tmp/cap024/app_$(basename $out .mp4).log 2>&1 & AP=$!
sleep 3; DISPLAY=:84 bash -c 'w=$(xdotool search --pid '$AP' | head -1); [ -n "$w" ] && xdotool windowmove $w 0 0 windowsize $w 1920 1080' 2>/dev/null
DISPLAY=:84 xdotool mousemove 1915 1075
[ -n "$PRE" ] && DISPLAY=:84 bash -c "$PRE"
sleep $skip
( [ -n "$ACTIONS" ] && DISPLAY=:84 bash -c "$ACTIONS" ) &
ffmpeg -y -loglevel error -f x11grab -draw_mouse ${MOUSE:-0} -framerate 30 -video_size 1920x1080 -i :84 -t $tot -c:v libx264 -preset ultrafast -crf 10 -pix_fmt yuv420p "$out"
kill $AP; sleep 1; kill $XP; sleep 1
