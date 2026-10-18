#!/bin/bash
# receiver CPU (dect2cli's own load meter: % of real time = CPU cores busy), the same recording through two builds, two runs each: speed_027.sh <old dect2cli> <new dect2cli>
OLD=$1; NEW=$2
run(){ b=$1; shift; timeout 200 $b "$@" 2>&1 | grep -oE "receiver [0-9]+% of real time" | tail -1 | grep -oE "[0-9]+"; }
for r in 1 2; do
 for tag in old new; do b=$OLD; [ $tag = new ] && b=$NEW
  echo "dvbt $tag $(run $b --file /tmp/onair_demo_10Msps.cs8 --rate 10 --format cs8 --secs 24)"
  echo "isdbt $tag $(run $b --standard isdbt --file /tmp/bench/isdbt.cs8 --rate 10 --format cs8 --secs 24)"
  echo "dab $tag $(run $b --standard dab --file /tmp/bench/dab.cs8 --rate 2.048 --format cs8 --secs 24)"
  echo "atsc $tag $(run $b --synthetic --atsc --secs 24)"
 done
done
