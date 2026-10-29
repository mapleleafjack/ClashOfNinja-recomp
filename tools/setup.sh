#!/bin/sh
# One-time setup: take the user's own disc image, extract it into <game>/rom/disc, and
# leave the image deletable. Nothing from the disc is committed or shipped.
#
#   tools/setup.sh <your disc image> [game]
#
# `game` is the folder the game is built in: naruto (Clash of Ninja, the default) or
# naruto2 (Clash of Ninja 2). The image may be any format nodtool reads (ISO, RVZ, WIA,
# GCZ, WBFS, CISO, TGC, NFS).
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project=$(dirname -- "$here")

if [ $# -lt 1 ]; then
    echo "usage: $0 <your disc image> [game]" >&2
    echo "  game: naruto (Clash of Ninja, default) or naruto2 (Clash of Ninja 2)" >&2
    echo "  extracts it to $project/<game>/rom/disc; the image can then be deleted" >&2
    exit 2
fi

image=$1
game=${2:-naruto}
cmake_lists=$project/$game/CMakeLists.txt
[ -f "$cmake_lists" ] || { echo "error: no game project at $cmake_lists" >&2; exit 1; }

# The build pins the disc it reads; take the disc ID and main.dol hash from there so
# there is one place to change them.
disc_id=$(sed -n 's/.*DISC_ID[[:space:]]\{1,\}\([A-Za-z0-9]\{4,8\}\).*/\1/p' "$cmake_lists" | head -n 1)
dol_sha1=$(sed -n 's/.*DOL_SHA1[[:space:]]\{1,\}\([0-9a-fA-F]\{40\}\).*/\1/p' "$cmake_lists" | head -n 1)
[ -n "$disc_id" ] && [ -n "$dol_sha1" ] || {
    echo "error: $cmake_lists does not name a DISC_ID and a DOL_SHA1" >&2
    exit 1
}

python3 "$here/extract_disc.py" "$image" "$project/$game/rom/disc" \
    --disc-id "$disc_id" --dol-sha1 "$dol_sha1"

if command -v cmake >/dev/null 2>&1; then
    echo
    echo "building..."
    cmake -S "$project/$game" -B "$project/$game/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
    ninja -C "$project/$game/build"
    echo
    echo "run: $project/$game/build/$game"
fi
