#!/bin/sh
# The whole pipeline on one disc image, on the machine that has the disc: writes the game
# projects, extracts the disc and checks it is the one the build expects, builds the game,
# and leaves it running for a while. CI has no disc, so this is the check to run by hand
# before cutting a release -- and the only check that reaches the disc at all.
#
#   tools/acceptance.sh "/path/to/your disc image.iso"       # naruto, the default
#   tools/acceptance.sh "/path/to/image.rvz" gnt4            # any format nodtool reads
#
# NLA_ACCEPTANCE_SECONDS=60   how long the game is left running (default 20)
# NLA_LAUNCHER=<path>         the launcher binary, when it is not where a build puts it
# NARUTO_TOOLS=<dir>          tools to put on PATH first, as tools/setup.sh takes it
#
# Reports one line per step and exits non-zero at the first that failed, so what broke is
# the last line that says ok. Everything a step printed is kept in the log beside it.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project=$(dirname -- "$here")
seconds=${NLA_ACCEPTANCE_SECONDS:-20}

if [ $# -lt 1 ]; then
    echo "usage: $0 <your disc image> [game]" >&2
    echo "  game: naruto (default), naruto2, gnt3, gnt4, narutoeu" >&2
    exit 2
fi
image=$1
game=${2:-naruto}
work="$project/acceptance"
log="$work/log.txt"

mkdir -p "$work"
: > "$log"

fail() {
    echo "FAIL $1" >&2
    echo "  everything the step printed is in $log" >&2
    exit 1
}

ok() {
    echo "ok   $1"
}

# The tools a release carries come first, the same way setup.sh finds them.
tools=${NARUTO_TOOLS-}
if [ -z "$tools" ] && [ -d "$project/tools/cmake/bin" ]; then
    tools=$project/tools
fi
if [ -n "$tools" ] && [ -d "$tools" ]; then
    PATH="$tools/cmake/bin:$tools/ninja:$tools/nodtool:$PATH"
    export PATH
fi

# ---- the disc image -------------------------------------------------------------------
[ -f "$image" ] || fail "no disc image at $image"
ok "disc image: $image ($(wc -c < "$image" | tr -d ' ') bytes)"

# ---- the launcher --------------------------------------------------------------------
launcher=${NLA_LAUNCHER-}
if [ -z "$launcher" ]; then
    for candidate in \
        "$project/launcher/build/NarutoLauncher.app/Contents/MacOS/NarutoLauncher" \
        "$project/launcher/build/NarutoLauncher" \
        "$project/launcher/build/NarutoLauncher.exe"
    do
        [ -x "$candidate" ] && launcher=$candidate && break
    done
fi
[ -n "$launcher" ] && [ -x "$launcher" ] || {
    echo "error: no launcher found. Build one first:" >&2
    echo "  cmake -S launcher -B launcher/build -G Ninja && ninja -C launcher/build" >&2
    echo "  ...or point NLA_LAUNCHER at one." >&2
    exit 2
}
ok "launcher: $launcher"

# ---- the game projects ----------------------------------------------------------------
cmake_lists="$project/$game/CMakeLists.txt"
if [ ! -f "$cmake_lists" ]; then
    "$launcher" --make-project "$project" >>"$log" 2>&1 || fail "writing the game projects"
fi
[ -f "$cmake_lists" ] || fail "the $game project was not written"
ok "the $game project is in place"

# ---- extraction, against the disc the build pins --------------------------------------
disc_id=$(sed -n 's/.*DISC_ID[[:space:]]\{1,\}\([A-Za-z0-9]\{4,8\}\).*/\1/p' "$cmake_lists" | head -n 1)
dol_sha1=$(sed -n 's/.*DOL_SHA1[[:space:]]\{1,\}\([0-9a-fA-F]\{40\}\).*/\1/p' "$cmake_lists" | head -n 1)
[ -n "$disc_id" ] && [ -n "$dol_sha1" ] || fail "$cmake_lists names no DISC_ID and DOL_SHA1"

disc="$project/$game/rom/disc"
rm -rf "$disc"
if ! python3 "$here/extract_disc.py" "$image" "$disc" \
        --disc-id "$disc_id" --dol-sha1 "$dol_sha1" >>"$log" 2>&1; then
    fail "extracting $game from the disc (the image is not ${disc_id}, or main.dol is not the supported one)"
fi
[ -f "$disc/sys/main.dol" ] || fail "the extracted tree has no sys/main.dol"
files=$(find "$disc" -type f | wc -l | tr -d ' ')
ok "extracted $game: $files files, main.dol matches ${dol_sha1}"

# ---- the build ------------------------------------------------------------------------
build="$project/$game/build"
rm -rf "$build"
configure="-S $project/$game -B $build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo"
if [ -n "$tools" ] && [ -f "$tools/sdl2/lib/cmake/SDL2/SDL2Config.cmake" ]; then
    configure="$configure -DGCN_SDL2_PREFIX=$tools/sdl2"
fi
# shellcheck disable=SC2086
cmake $configure >>"$log" 2>&1 || fail "configuring the build"
ninja -C "$build" >>"$log" 2>&1 || fail "building the game"
binary="$build/$game"
[ -x "$binary" ] || fail "the build wrote no $game at $binary"
ok "built $binary"

# ---- the game itself ------------------------------------------------------------------
# The build makes a headless harness beside the game, which boots the recompiled disc,
# runs the guest for a fixed number of seconds and exits on its own -- so a crash is an
# exit code rather than something to watch a window for. The windowed game is started
# instead when a build has no harness, and then staying up for the whole window is the
# pass, because nothing closes it.
bench="$build/${game}_bench"
if [ -x "$bench" ]; then
    if ! ( cd "$project/$game" && "$bench" "--seconds=$seconds" ) >"$work/game.txt" 2>&1; then
        cat "$work/game.txt" >>"$log"
        fail "the game did not run for ${seconds}s (see $work/game.txt)"
    fi
    cat "$work/game.txt" >>"$log"
    ok "the game booted and ran for ${seconds}s"
else
    ( cd "$project/$game" && "./build/$game" ) >"$work/game.txt" 2>&1 &
    pid=$!
    waited=0
    while [ "$waited" -lt "$seconds" ]; do
        if ! kill -0 "$pid" 2>/dev/null; then
            wait "$pid" || true
            cat "$work/game.txt" >>"$log"
            fail "the game exited on its own after ${waited}s; its output is in $work/game.txt"
        fi
        sleep 1
        waited=$((waited + 1))
    done
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    cat "$work/game.txt" >>"$log"
    ok "the game ran for ${seconds}s without exiting"
fi

echo
echo "PASS the $game path works on this machine: disc, extraction, build and boot"
