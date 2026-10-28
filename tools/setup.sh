#!/bin/sh
# One-time setup: take the user's own disc image, extract it into naruto/rom/disc, and
# leave the image deletable. Nothing from the disc is committed or shipped.
#
#   tools/setup.sh "/path/to/your/disc image.iso"
#
# The image may be any format nodtool reads (ISO, RVZ, WIA, GCZ, WBFS, CISO, TGC, NFS).
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project=$(dirname -- "$here")

if [ $# -lt 1 ]; then
    echo "usage: $0 <your disc image>" >&2
    echo "  extracts it to $project/naruto/rom/disc; the image can then be deleted" >&2
    exit 2
fi

image=$1
[ -f "$image" ] || { echo "error: no such file: $image" >&2; exit 1; }

python3 "$here/extract_disc.py" "$image"

if command -v cmake >/dev/null 2>&1; then
    echo
    echo "building..."
    cmake -S "$project/naruto" -B "$project/naruto/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
    ninja -C "$project/naruto/build"
    echo
    echo "run: $project/naruto/build/naruto"
fi
