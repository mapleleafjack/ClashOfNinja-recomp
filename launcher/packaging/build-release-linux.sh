#!/usr/bin/env bash
# Assembles the Linux release, which is what build-release.ps1 does for Windows and the
# macOS bundle: a folder holding the launcher and everything a game build needs that a
# machine may not have, and a tarball of it. A Linux player has no launcher store to
# install from, and SteamOS has a read-only root and no package manager to add to, so the
# folder is the installer.
#
# The tarball name is not cosmetic: update.cpp matches a release asset against the
# platform's own archive type, and a Linux copy unpacks with GNU tar, which reads only
# tar archives.
#
#   bash launcher/packaging/build-release-linux.sh --version 1.0.1 \
#       --build-dir launcher/build-release --sdl2-prefix /opt/sdl2
#
#   # with a toolchain to carry (cmake, ninja, python, dtk, nodtool, a compiler), split so
#   # the launcher stays small and the tools can be pinned for the download on first use:
#   bash launcher/packaging/build-release-linux.sh --version 1.0.1 --tools /opt/linux-tools --split
#
#   # the tool archive on its own, built from pinned releases, which needs no launcher build
#   # and is what a release publishes beside the tarball above:
#   bash launcher/packaging/build-release-linux.sh --version 1.0.1 --out dist --tools-only
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
launcher=$(dirname "$here")
project=$(dirname "$launcher")

# The toolchain a Linux player has no guarantee of. Windows ships a MinGW-w64 toolchain and
# macOS leans on Apple's command line tools; SteamOS, which a Steam Deck runs, is immutable
# and ships no C++ compiler, and a distribution's gcc is not relocatable. So the compiler
# here is Zig, which is one relocatable directory and targets glibc with `zig cc`; CMake,
# Ninja and a static SDL2 come from the same pinned releases the other platforms use.
#
# Pinned, all of them: the archive is checked against a digest, so what it carries must be
# the same bytes every time it is built.
cmake_version=3.30.5
ninja_version=1.12.1
zig_version=0.13.0
sdl2_version=2.30.9

fetch() {  # fetch <url> <output>
  echo "fetching $(basename "$2")"
  curl -fsSL --retry 3 -o "$2" "$1"
}

# SDL leaves a backend out silently when it finds no headers for it, and a game with no
# window and no sound says nothing about why -- so the headers are required up front.
require_sdl2_headers() {
  local missing=
  for header in X11/Xlib.h alsa/asoundlib.h pulse/pulseaudio.h libudev.h GL/gl.h; do
    [ -f "/usr/include/$header" ] || missing="$missing $header"
  done
  if [ -n "$missing" ]; then
    echo "error: SDL2 would be built without these backends, and the game would need them:" >&2
    echo "      $missing" >&2
    echo "  install the development packages, on Debian/Ubuntu:" >&2
    echo "    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev" >&2
    echo "    libxss-dev libxkbcommon-dev libasound2-dev libpulse-dev libudev-dev libgl-dev" >&2
    exit 1
  fi
}

# SDL2 loads each backend by name at run time rather than linking it, so the soname it would
# load is what proves the backend is in the library. The list goes to a file first: piping
# into `grep -q` would stop `strings` early, and with pipefail that failure would look like a
# missing backend.
verify_sdl2_backends() {  # verify_sdl2_backends <prefix> <work>
  local prefix=$1 work=$2
  [[ -f "$prefix/lib/libSDL2.a" ]] || {
    echo "error: no static SDL2 at $prefix/lib/libSDL2.a" >&2; exit 1; }
  strings "$prefix/lib/libSDL2.a" > "$work/sdl2-strings.txt"
  for soname in libX11.so.6 libasound.so.2 libudev.so.1; do
    if ! grep -qx "$soname" "$work/sdl2-strings.txt"; then
      echo "error: the SDL2 in the archive would not load $soname, so that backend is not" >&2
      echo "       in it. Install the matching development package and build again." >&2
      exit 1
    fi
  done
}

# Everything a game build shells out to, written into a tools/ directory -- the shape the
# launcher unpacks beside itself (see build_search_path in src/tools.cpp).
build_pinned_tools() {  # build_pinned_tools <tools dir> <work>
  local out=$1 work=$2
  mkdir -p "$out/cmake/bin" "$out/ninja" "$out/bin"

  # CMake, from the standalone build Kitware publishes. Only the binary and its module tree
  # are kept: the documentation, man pages and ctest/ccmake are not run by a configure.
  fetch "https://github.com/Kitware/CMake/releases/download/v${cmake_version}/cmake-${cmake_version}-linux-x86_64.tar.gz" \
        "$work/cmake.tar.gz"
  mkdir -p "$work/cmake"
  tar -xzf "$work/cmake.tar.gz" -C "$work/cmake" --strip-components=1
  cp "$work/cmake/bin/cmake" "$out/cmake/bin/"
  cp -R "$work/cmake/share" "$out/cmake/share"

  # Ninja, a single static binary.
  fetch "https://github.com/ninja-build/ninja/releases/download/v${ninja_version}/ninja-linux.zip" \
        "$work/ninja.zip"
  ( cd "$out/ninja" && unzip -oq "$work/ninja.zip" )
  chmod +x "$out/ninja/ninja"

  # Zig, as the C and C++ compiler. The whole directory is kept: `zig cc` finds its libc and
  # its lib directory relative to itself, so it only works unpacked in place.
  fetch "https://ziglang.org/download/${zig_version}/zig-linux-x86_64-${zig_version}.tar.xz" \
        "$work/zig.tar.xz"
  tar -xJf "$work/zig.tar.xz" -C "$work"
  mv "$work/zig-linux-x86_64-${zig_version}" "$out/zig"

  # The names CMake looks for on PATH. They are wrappers because CMAKE_C_COMPILER has to be
  # one program: `zig` with `cc` as its first argument cannot be expressed there. `ar` is a
  # wrapper too -- zig carries a linker and an archiver, and a system without binutils (which
  # is what SteamOS is) would otherwise fail while CMake links its first static library.
  local pair name mode
  for pair in "cc:cc" "gcc:cc" "c++:c++" "g++:c++" "ar:ar"; do
    name=${pair%%:*}
    mode=${pair##*:}
    cat > "$out/bin/$name" <<WRAPPER
#!/bin/sh
# Written by launcher/packaging/build-release-linux.sh. Zig is the toolchain the archive
# carries, so this stands in for the system's $name.
exec "\$(dirname "\$0")/../zig/zig" $mode "\$@"
WRAPPER
    chmod +x "$out/bin/$name"
  done

  # SDL2, static and with a CMake config, so the game links it from the archive and needs
  # neither the distribution's SDL2 nor pkg-config. Built with the host compiler: it is a
  # library that ships ready-made, not something the player's machine compiles.
  require_sdl2_headers
  fetch "https://github.com/libsdl-org/SDL/releases/download/release-${sdl2_version}/SDL2-${sdl2_version}.tar.gz" \
        "$work/sdl2.tar.gz"
  tar -xzf "$work/sdl2.tar.gz" -C "$work"
  cmake -S "$work/SDL2-${sdl2_version}" -B "$work/sdl2-build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$out/sdl2" \
      -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF >/dev/null
  cmake --build "$work/sdl2-build" --target install >/dev/null
  verify_sdl2_backends "$out/sdl2" "$work"

  # The tools the launcher needs beside itself: extract_disc.py is the marker it looks for,
  # and setup.sh is what the terminal path runs.
  local script
  for script in extract_disc.py setup.sh; do
    [ -f "$out/$script" ] || cp "$here/../../tools/$script" "$out/$script"
  done
}

version=0.0.0
out="$launcher/dist"
build_dir="$launcher/build-release"
sdl2_prefix=/opt/sdl2
tools_dir=""
split=0
lean=0
skip_build=0
tools_only=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) version=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --build-dir) build_dir=$2; shift 2 ;;
    --sdl2-prefix) sdl2_prefix=$2; shift 2 ;;
    --tools) tools_dir=$2; shift 2 ;;
    --split) split=1; shift ;;
    --lean) lean=1; shift ;;
    --skip-build) skip_build=1; shift ;;
    --tools-only) tools_only=1; shift ;;
    -h|--help) awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' \
                 "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

exe="$build_dir/NarutoLauncher"
stage="$out/NarutoLauncher Linux $version"
archive="$out/NarutoLauncher-Linux-x64-v$version.tar.xz"
tools_archive="$out/NarutoLauncher-tools-linux-x64-v$version.tar.xz"

if [[ $tools_only -eq 1 ]]; then
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
  mkdir -p "$work/tools" "$out"
  build_pinned_tools "$work/tools" "$work"
  echo "packing $tools_archive"
  rm -f "$tools_archive"
  tar -cJf "$tools_archive" -C "$work" tools
  echo "  $(stat -c %s "$tools_archive") bytes"
  echo "  sha256 $(sha256sum "$tools_archive" | cut -d' ' -f1)"
  echo
  echo "pin it when configuring the launcher:"
  echo "  -DNLA_TOOLS_URL=<release download url> \\"
  echo "  -DNLA_TOOLS_SHA256=$(sha256sum "$tools_archive" | cut -d' ' -f1) \\"
  echo "  -DNLA_TOOLS_BYTES=$(stat -c %s "$tools_archive")"
  exit 0
fi

if [[ $skip_build -eq 0 ]]; then
  echo "building the launcher into $build_dir"
  cmake_args=(-S "$launcher" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release
              -DNLA_VERSION="$version")
  if [[ -n "${SDL2_DIR:-}" ]]; then cmake_args+=(-DSDL2_DIR="$SDL2_DIR"); fi
  cmake "${cmake_args[@]}"
  ninja -C "$build_dir" NarutoLauncher
fi
[[ -x "$exe" ]] || { echo "no launcher at $exe (pass --build-dir)" >&2; exit 1; }

# The recompiler and extract_disc.py are what shipped_sources() looks for to decide a
# folder can set up a game, so they travel beside the launcher on Linux.
for required in "$project/gcn-recomp/CMakeLists.txt" "$project/tools/extract_disc.py" \
                "$project/tools/setup.sh" "$launcher/mods.json"; do
  [[ -e "$required" ]] || { echo "missing $required" >&2; exit 1; }
done

rm -rf "$stage"
mkdir -p "$stage/tools" "$out"

cp "$exe" "$stage/NarutoLauncher"
cp "$launcher/mods.json" "$stage/"
cp -a "$project/gcn-recomp" "$stage/gcn-recomp"
cp "$project/tools/extract_disc.py" "$project/tools/setup.sh" "$stage/tools/"

# A licence obligation, not an option.
cp "$project/LICENSE" "$project/THIRD-PARTY-NOTICES.md" "$stage/"
cp -a "$project/licenses" "$stage/licenses"

# Clean out build output the recompiler may carry from a local build: the shipped copy is
# source only, and a stale generated tree would be a disc-derived artefact.
rm -rf "$stage/gcn-recomp/build" "$stage/gcn-recomp/gen"

if [[ $lean -eq 0 ]]; then
  # The game links SDL2 statically from this prefix, which is what lets it build on a
  # machine with no SDL2 development package -- SteamOS has none and cannot install one.
  if [[ -f "$sdl2_prefix/lib/cmake/SDL2/SDL2Config.cmake" ]]; then
    cp -a "$sdl2_prefix" "$stage/tools/sdl2"
  else
    echo "warning: no SDL2 CMake package at $sdl2_prefix; the game will need pkg-config sdl2" >&2
  fi

  if [[ -n "$tools_dir" ]]; then
    # Everything the build shells out to. tools.cpp searches these subdirectories in this
    # order before PATH, so a carried copy wins over whatever the machine has.
    for name in cmake ninja python nodtool dtk toolchain gcc bin; do
      if [[ -e "$tools_dir/$name" ]]; then cp -a "$tools_dir/$name" "$stage/tools/$name"; fi
    done
  fi
fi

# Nothing derived from a disc may be published: not a game project, not its extracted
# tree, not a build, not a memory card, not the publisher's box art.
for bad in "$stage"/*/rom "$stage"/*/build "$stage"/*/saves "$stage/covers" \
           "$stage/naruto" "$stage/naruto2" "$stage/gnt3" "$stage/gnt4" "$stage/narutoeu"; do
  if [[ -e "$bad" ]]; then
    echo "refusing to package $bad: disc-derived" >&2
    exit 1
  fi
done
if find "$stage" -type f \( -iname '*.iso' -o -iname '*.ciso' -o -iname '*.rvz' \
     -o -iname '*.wia' -o -iname '*.gcz' -o -iname '*.wbfs' -o -iname '*.tgc' \
     -o -iname '*.nfs' -o -iname '*.dol' -o -iname '*.trk' -o -iname '*.h4m' \) | grep -q .; then
  echo "refusing to package disc files" >&2
  exit 1
fi

echo "packing $archive"
rm -f "$archive"
tar -cJf "$archive" -C "$out" "NarutoLauncher Linux $version"
echo "  $(stat -c %s "$archive") bytes"
echo "  sha256 $(sha256sum "$archive" | cut -d' ' -f1)"

if [[ $split -eq 1 ]]; then
  # The launcher unpacks this into its own folder, so the archive's root is tools/.
  # extract_disc.py and setup.sh stay in the launcher's tarball as well: they are the
  # marker that makes a folder able to set up a game, and it has to work before the
  # tools arrive.
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
  mkdir -p "$work/tools"
  cp -a "$stage/tools/." "$work/tools/"
  echo "packing $tools_archive"
  rm -f "$tools_archive"
  tar -cJf "$tools_archive" -C "$work" tools
  echo "  $(stat -c %s "$tools_archive") bytes"
  echo "  sha256 $(sha256sum "$tools_archive" | cut -d' ' -f1)"
  echo
  echo "pin it when configuring the launcher:"
  echo "  -DNLA_TOOLS_URL=<release download url> \\"
  echo "  -DNLA_TOOLS_SHA256=$(sha256sum "$tools_archive" | cut -d' ' -f1) \\"
  echo "  -DNLA_TOOLS_BYTES=$(stat -c %s "$tools_archive")"
fi

echo "done: $stage"
