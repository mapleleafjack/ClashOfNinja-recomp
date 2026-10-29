#!/bin/sh
# One-time setup: take the user's own disc image, extract it into <game>/rom/disc, build
# the game, and leave the image deletable. Nothing from the disc is committed or shipped.
#
#   tools/setup.sh "/path/to/your/disc image.iso"      # extract and build
#   tools/setup.sh --run "/path/to/image.iso"          # ...then start the game
#
# `game` is the folder the game is built in: naruto (Clash of Ninja, the default) or
# naruto2 (Clash of Ninja 2), given after the image:
#
#   tools/setup.sh --run "/path/to/image.iso" naruto2
#
# The image may be any format nodtool reads (ISO, RVZ, WIA, GCZ, WBFS, CISO, TGC, NFS).
#
# A macOS release keeps the sources, CMake, Ninja, nodtool and SDL2 inside the .app. Run
# this from the copy in the bundle: the sources are copied out of it first, because a
# signed bundle must not be written into, and the bundled tools are then used - so a
# terminal build needs no Homebrew. Set NARUTO_TOOLS to point at another tool folder, or
# NARUTO_HOME to copy the sources somewhere other than ~/Naruto.
set -eu

run=0
if [ "${1-}" = "--run" ]; then
    run=1
    shift
fi

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project=$(dirname -- "$here")
tools=${NARUTO_TOOLS-}

case "$project" in
*/Contents/Resources/project)
    bundle=$(dirname -- "$project")
    target=${NARUTO_HOME:-$HOME/Naruto}
    if [ ! -f "$target/naruto/CMakeLists.txt" ]; then
        echo "copying the game's sources out of the app to $target"
        mkdir -p "$target"
        cp -R "$project/." "$target/"
    fi
    project=$target
    tools=$bundle/tools
    ;;
esac

# Tools shipped with a release come first, wherever they are: tools/ beside the project in
# a checkout, or Contents/Resources/tools above it in a bundle.
if [ -z "$tools" ] && [ -d "$(dirname -- "$project")/tools" ]; then
    tools=$(dirname -- "$project")/tools
fi
if [ -n "$tools" ] && [ -d "$tools" ]; then
    PATH="$tools/cmake/bin:$tools/ninja:$tools/nodtool:$PATH"
    export PATH
else
    tools=
fi

if [ $# -lt 1 ]; then
    echo "usage: $0 [--run] <your disc image> [game]" >&2
    echo "  game: naruto (Clash of Ninja, default) or naruto2 (Clash of Ninja 2)" >&2
    echo "  extracts it to $project/<game>/rom/disc, then builds the game" >&2
    echo "  the image can then be deleted" >&2
    exit 2
fi

image=$1
game=${2:-naruto}
cmake_lists=$project/$game/CMakeLists.txt
[ -f "$cmake_lists" ] || { echo "error: no game project at $cmake_lists" >&2; exit 1; }

if ! command -v python3 >/dev/null 2>&1; then
    echo "error: python3 was not found. On macOS it comes with Apple's command line tools:" >&2
    echo "  xcode-select --install" >&2
    exit 1
fi

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

if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
    set -- -S "$project/$game" -B "$project/$game/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
    # The SDL2 a release carries is the one the game links; without it a configure falls
    # back to pkg-config and needs SDL2 installed.
    if [ -n "$tools" ] && [ -f "$tools/sdl2/lib/cmake/SDL2/SDL2Config.cmake" ]; then
        set -- "$@" "-DGCN_SDL2_PREFIX=$tools/sdl2"
    fi
    echo
    echo "building..."
    cmake "$@" >/dev/null
    ninja -C "$project/$game/build"
    echo
    echo "run: $project/$game/build/$game"
    if [ "$run" -eq 1 ]; then
        cd "$project/$game"
        if [ -f controls.txt ]; then
            exec "./build/$game" "--controls=$project/$game/controls.txt"
        fi
        exec "./build/$game"
    fi
else
    echo
    echo "cmake and ninja were not found; run this from a release, or install them." >&2
fi
