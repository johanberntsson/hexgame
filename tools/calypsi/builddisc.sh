#!/bin/sh
# Build the game's D81.
#
#   builddisc.sh <image.d81> <program.prg> [resource ...]
#
# The program is written first and under the name the ROM autoboots, and every
# resource after it as an ordinary PRG. c1541 names each file on the disk after
# the host file's basename, which is what the opens in src/hexgame.c expect --
# so a resource renamed here has to be renamed there too.
#
# The image is always made from scratch: c1541 will happily write a second copy
# of a file that is already on the disk, and a stale first copy is the one the
# ROM would find.
set -e

if [ $# -lt 2 ]; then
  echo "usage: $0 <image.d81> <program.prg> [resource ...]" >&2
  exit 2
fi

IMAGE=$1
PROGRAM=$2
shift 2

mkdir -p "$(dirname "$IMAGE")"
rm -f "$IMAGE"

c1541 -format hexgame,sk d81 "$IMAGE" >/dev/null
c1541 "$IMAGE" -write "$PROGRAM" autoboot.c65 >/dev/null

for resource in "$@"; do
  c1541 "$IMAGE" -write "$resource" >/dev/null
done

echo "disc: $IMAGE"
c1541 "$IMAGE" -list
