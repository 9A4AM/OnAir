#!/bin/bash
# build the headless UI recorder (needs libhackrf/ffmpeg dev packages and a built libdect2core.a in $S/build)
set -e; cd "$(dirname "$0")"; R=$(readlink -f ../..); S=${S:?scratch dir with build/ and obj/}
python3 make_headless.py
g++ -O2 -std=c++20 -fcx-limited-range -I$R/core/include -I$R/third_party/imgui -I$R/third_party/implot -I$R/app -I. app_headless.cpp $S/obj/*.o $S/build/libdect2core.a $(pkg-config --libs libhackrf libavformat libavcodec libavutil libswscale libswresample) -lpthread -o $S/onair_headless
