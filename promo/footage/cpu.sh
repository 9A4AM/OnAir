#!/bin/bash
APPB=$1; shift
export ONAIR_W=1920 ONAIR_H=1080 ONAIR_PACE=${P:-1} HOME=/tmp/h_cpu_$$; mkdir -p $HOME
Xvfb :81 -screen 0 1920x1080x24 >/dev/null 2>&1 & XP=$!; sleep 1
DISPLAY=:81 $APPB "$@" >/dev/null 2>&1 & AP=$!
sleep 15; T0=$(awk '{print $14+$15}' /proc/$AP/stat); sleep 20; T1=$(awk '{print $14+$15}' /proc/$AP/stat)
echo "cpu% $(( (T1-T0) * 100 / 100 / 20 ))"
kill $AP; sleep 1; kill $XP
