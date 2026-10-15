#!/bin/bash
# Build RTL-SDR Blog librtlsdr 1.3.6 as a dylib for bundling into OnAir.app (stock Homebrew librtlsdr cannot drive the V4 HF upconverter).
# Usage: build_rtlsdr_blog_macos.sh <output directory>
set -euo pipefail
OUT=${1:?output directory}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
T=rtl-sdr-blog-1.3.6.tar.gz
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
tarball=
for f in "$REPO/build-windows-deps/src/$T" "$REPO/tools/package/cache/$T"; do [ -f "$f" ] && tarball=$f && break; done
if [ -z "$tarball" ]; then
  tarball=$W/$T
  curl -fsSL -o "$tarball" https://github.com/rtlsdrblog/rtl-sdr-blog/archive/refs/tags/v1.3.6.tar.gz
fi
mkdir "$W/src" && tar xf "$tarball" -C "$W/src" --strip-components=1
sed -i '' 's/^set(VERSION_INFO_PATCH_VERSION git)/set(VERSION_INFO_PATCH_VERSION 6)/' "$W/src/CMakeLists.txt"
cmake -S "$W/src" -B "$W/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$W/inst" -DVERSION=0.6.6 -DDETACH_KERNEL_DRIVER=ON -DINSTALL_UDEV_RULES=OFF
cmake --build "$W/build" -j"$(sysctl -n hw.ncpu)" --target rtlsdr
lib=$(find "$W/build/src" -name 'librtlsdr.*.dylib' -type f | head -1)
[ -n "$lib" ] || { echo "Blog librtlsdr dylib not built"; exit 1; }
mkdir -p "$OUT"
base=$(basename "$lib")
cp "$lib" "$OUT/$base"
for n in librtlsdr.0.dylib librtlsdr.dylib; do [ "$n" = "$base" ] || ln -sf "$base" "$OUT/$n"; done
echo "RTL-SDR Blog librtlsdr -> $OUT/$base"
