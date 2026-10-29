#!/bin/sh
# One-time setup: take the user's own disc image, extract it into naruto/rom/disc, build
# the game, and leave the image deletable. Nothing from the disc is committed or shipped.
#
#   tools/setup.sh "/path/to/your/disc image.iso"      # extract and build
#   tools/setup.sh --run "/path/to/image.iso"          # ...then start the game
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
    echo "usage: $0 [--run] <your disc image>" >&2
    echo "  extracts it to $project/naruto/rom/disc, then builds the game" >&2
    echo "  the image can then be deleted" >&2
    exit 2
fi

image=$1
[ -f "$image" ] || { echo "error: no such file: $image" >&2; exit 1; }

if ! command -v python3 >/dev/null 2>&1; then
    echo "error: python3 was not found. On macOS it comes with Apple's command line tools:" >&2
    echo "  xcode-select --install" >&2
    exit 1
fi

python3 "$here/extract_disc.py" "$image"

if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
    set -- -S "$project/naruto" -B "$project/naruto/build" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
    # The SDL2 a release carries is the one the game links; without it a configure falls
    # back to pkg-config and needs SDL2 installed.
    if [ -n "$tools" ] && [ -f "$tools/sdl2/lib/cmake/SDL2/SDL2Config.cmake" ]; then
        set -- "$@" "-DGCN_SDL2_PREFIX=$tools/sdl2"
    fi
    echo
    echo "building..."
    cmake "$@" >/dev/null
    ninja -C "$project/naruto/build"
    echo
    echo "run: cd $project/naruto && ./build/naruto"
    if [ "$run" -eq 1 ]; then
        cd "$project/naruto"
        exec ./build/naruto "--controls=$project/naruto/controls.txt"
    fi
else
    echo
    echo "cmake and ninja were not found; run this from a release, or install them." >&2
fi
