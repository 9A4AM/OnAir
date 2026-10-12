#!/bin/bash
# every capture -> /tmp/clips4/<name>/f_%05d.jpg (30 fps), and the frame counts for the film (promo/c4n.js)
mkdir -p /tmp/clips4; out="window.C4N={"
for f in /tmp/cap024/*.mp4; do n=$(basename $f .mp4); [ "$n" = test ] && continue; mkdir -p /tmp/clips4/$n; rm -f /tmp/clips4/$n/*.jpg
  ffmpeg -loglevel error -y -i $f -q:v 3 /tmp/clips4/$n/f_%05d.jpg; c=$(ls /tmp/clips4/$n | wc -l); out="$out\"$n\":$c,"; done
echo "${out%,}};" > /home/user/OnAir/promo/c4n.js; cat /home/user/OnAir/promo/c4n.js
