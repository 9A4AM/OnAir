#!/bin/bash
# render_chunks.sh <page> <frames dir> <total frames> [chunk]: renders in chunks, each in a fresh browser, until no frame is missing (a long single run can run the browser out of memory)
PAGE=$1; DIR=$2; NF=$3; CH=${4:-1200}; cd "$(dirname "$0")/.."; mkdir -p "$DIR"
for pass in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
  RANGES=$(python3 - "$DIR" "$NF" "$CH" <<'PY'
import sys,os
d,n,ch=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]); have={int(f[2:7]) for f in os.listdir(d) if f.startswith('f_') and f.endswith('.jpg') and os.path.getsize(os.path.join(d,f))>2000}
miss=[i for i in range(n) if i not in have]
if not miss: print('')
else:
    a=miss[0]; b=a
    for i in miss[1:ch]:
        if i==b+1: b=i
        else: print(f'{a}-{b},',end=''); a=b=i
    print(f'{a}-{b}')
PY
)
  [ -z "$RANGES" ] && { echo "all $NF frames present"; exit 0; }
  echo "pass $pass: $RANGES" | cut -c1-120; FDIR=$DIR PAGE=$PAGE RANGE=$RANGES node render.cjs render 3 2>&1 | grep -vE "^[0-9]+ /" | head -3
done
