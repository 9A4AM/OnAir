#!/bin/bash
# every capture -> /tmp/clips7/<name>/f_%05d.jpg (30 fps) and the frame counts for the film (promo/c7n.js); finished clips only
mkdir -p /tmp/clips7; out="window.C7N={"
for f in /tmp/cap027/*.mp4; do n=$(basename $f .mp4); [ "$n" = test ] && continue
  if [ ! -f /tmp/clips7/$n/.done ] || [ $f -nt /tmp/clips7/$n/.done ]; then ffprobe -v error -show_entries format=duration -of csv=p=0 $f >/dev/null 2>&1 || continue
    rm -rf /tmp/clips7/$n; mkdir -p /tmp/clips7/$n; ffmpeg -loglevel error -y -i $f -q:v 3 /tmp/clips7/$n/f_%05d.jpg && touch /tmp/clips7/$n/.done; fi
  c=$(ls /tmp/clips7/$n | grep -c jpg); out="$out\"$n\":$c,"; done
echo "${out%,}};" > /home/user/OnAir/promo/c7n.js; wc -c /home/user/OnAir/promo/c7n.js
