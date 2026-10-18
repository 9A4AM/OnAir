#!/bin/bash
export DECT2_WINSIZE=1920x1080 HOME=/tmp/h_cap7_shared
Xvfb :87 -screen 0 1920x1080x24 >/dev/null 2>&1 & XP=$!; sleep 1
DISPLAY=:87 /tmp/a027/dect2 --mode dji --autostart >/dev/null 2>&1 & AP=$!
sleep 3; DISPLAY=:87 bash -c "w=\$(xdotool search --pid $AP | head -1); xdotool windowmove \$w 0 0 windowsize \$w 1920 1080"
sleep 9; DISPLAY=:87 ffmpeg -loglevel error -y -f x11grab -video_size 1920x1080 -i :87 -frames:v 1 /tmp/cap027/init0.png
DISPLAY=:87 xdotool mousemove 1108 642; sleep .3; DISPLAY=:87 xdotool mousemove 1111 642; sleep .2; DISPLAY=:87 xdotool click 1; sleep 2
DISPLAY=:87 ffmpeg -loglevel error -y -f x11grab -video_size 1920x1080 -i :87 -frames:v 1 /tmp/cap027/init1.png
kill $AP; sleep 3; kill $XP
