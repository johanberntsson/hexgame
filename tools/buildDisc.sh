#!/bin/sh

set -e
DISCNAME="hexgame.d81"

cat c65bin/c65toc64wrapper.prg bin/hexgame.c64 > bin/autoboot.c65

mkdir -p res
for filename in img-src/*.png; do
  echo $filename
  #python3 tools/png2fci.py -vr $filename res/$(basename $filename .png).fci
  #python3 tools/png2fci.py -v0rc $filename res/$(basename $filename .png).fci
  python3 tools/png2fci.py -v0r $filename res/$(basename $filename .png).fci
done

mkdir -p disc
c1541 -format hexgame,sk d81 disc/$DISCNAME

for filename in res/*; do
  c1541 disc/$DISCNAME -write $filename
done

c1541 disc/$DISCNAME -write bin/autoboot.c65
