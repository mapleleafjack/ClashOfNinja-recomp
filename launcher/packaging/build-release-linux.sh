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
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
launcher=$(dirname "$here")
project=$(dirname "$launcher")

version=0.0.0
out="$launcher/dist"
build_dir="$launcher/build-release"
sdl2_prefix=/opt/sdl2
tools_dir=""
split=0
lean=0
skip_build=0

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
    -h|--help) awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' \
                 "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

exe="$build_dir/NarutoLauncher"
stage="$out/NarutoLauncher Linux $version"
archive="$out/NarutoLauncher-Linux-x64-v$version.tar.xz"
tools_archive="$out/NarutoLauncher-tools.tar.xz"

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
