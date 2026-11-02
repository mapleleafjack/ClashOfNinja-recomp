#!/usr/bin/env bash
# Checks a Linux release the way a player receives it: unpack the tarball the release
# publishes, then confirm the folder inside is the one that runs.
#
#   bash launcher/packaging/check-release-linux.sh \
#       launcher/dist/NarutoLauncher-Linux-x64-v1.0.1.tar.xz --version 1.0.1
#
#   # or a folder that has not been packed yet
#   bash launcher/packaging/check-release-linux.sh \
#       --dir "launcher/dist/NarutoLauncher Linux 0.0.0" --version 0.0.0
#
# Everything is read from the extracted copy rather than the build tree: a file that only
# exists in the build tree, an executable bit GNU tar did not carry, or a tarball whose name
# the update check would not recognise is exactly what a download would get wrong. CI runs
# this after packaging, on every run, so a release cannot ship a folder nobody started.
#
# Needs binutils (readelf) for the dynamic-link check, and a system font for --render --
# fonts-dejavu-core on Debian/Ubuntu, which is what a player's own desktop has. No display:
# the render is drawn in software.
set -euo pipefail

fail() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

say() {
  printf '  %s\n' "$*"
}

usage() {
  awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "${BASH_SOURCE[0]}"
}

version=
archive=
dir=
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) version=${2:-}; shift 2 ;;
    --dir) dir=${2:-}; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    -*) fail "unknown option: $1" ;;
    *) archive=$1; shift ;;
  esac
done

[[ -n "$version" ]] || fail "--version is required"
[[ -n "$archive" || -n "$dir" ]] || fail "pass the release tarball, or --dir for an unpacked folder"
[[ -n "$archive" && -n "$dir" ]] && fail "pass the tarball or --dir, not both"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

stage=
if [[ -n "$archive" ]]; then
  [[ -f "$archive" ]] || fail "no archive at $archive"

  # update.cpp picks the asset to download by name: the platform, the version and the
  # architecture have to be in it, and it has to end in the type this platform unpacks.
  want_archive="NarutoLauncher-Linux-x64-v$version.tar.xz"
  got_archive=$(basename -- "$archive")
  [[ "$got_archive" == "$want_archive" ]] ||
    fail "the asset is named '$got_archive', and an update is only offered for '$want_archive'"

  mkdir -p "$work/unpacked"
  tar -xJf "$archive" -C "$work/unpacked"

  # The download is one folder named for the release, so extracting it does not scatter
  # files through the player's Downloads folder.
  top=$(ls -A "$work/unpacked" || true)
  count=$(printf '%s\n' "$top" | grep -c . || true)
  [[ "$count" -eq 1 ]] || fail "the archive should hold one folder, and it holds: $(printf '%s ' $top)"
  [[ "$top" == "NarutoLauncher Linux $version" ]] ||
    fail "the archive's folder is '$top', not 'NarutoLauncher Linux $version'"
  stage="$work/unpacked/$top"

  # What the launcher looks for beside itself, and what the licence obligation carries.
  for want in NarutoLauncher mods.json LICENSE THIRD-PARTY-NOTICES.md licenses \
              gcn-recomp/CMakeLists.txt tools/extract_disc.py tools/setup.sh; do
    [[ -e "$stage/$want" ]] || fail "the archive is missing $want"
  done
  say "the archive holds 'NarutoLauncher Linux $version' with the launcher, the recompiler, the notices and the tools"
else
  stage=$dir
  [[ -d "$stage" ]] || fail "no folder at $stage"
fi

exe="$stage/NarutoLauncher"
[[ -f "$exe" ]] || fail "the folder has no NarutoLauncher"
# A tar keeps the mode; a zip, or an unpacker that dropped it, leaves this unset and the
# launcher cannot be started at all.
[[ -x "$exe" ]] || fail "NarutoLauncher is not executable, so a player could not start it"

# The ELF header, read directly: one less thing a checking machine has to have installed
# than a full binutils, and what it says is what the loader acts on.
elf=($(od -An -tu1 -N20 "$exe"))
[[ "${elf[0]} ${elf[1]} ${elf[2]} ${elf[3]}" == "127 69 76 70" ]] ||
  fail "NarutoLauncher is not an ELF binary"
[[ "${elf[4]}" -eq 2 ]] || fail "the launcher is not a 64-bit ELF (class ${elf[4]})"
machine=$((elf[18] + 256 * elf[19]))
[[ "$machine" -eq 62 ]] ||
  fail "the launcher is built for ELF machine $machine, and the release is x86-64 (62)"

command -v readelf >/dev/null || fail "readelf (binutils) is needed for the dynamic-link check"
readelf -d "$exe" > "$work/readelf.txt"
if grep -qi 'NEEDED.*SDL' "$work/readelf.txt"; then
  cat "$work/readelf.txt" >&2
  fail "the launcher links SDL2 dynamically, so the download would need SDL2 installed"
fi
say "x86-64, with SDL2 linked statically"

# The version compiled into the launcher is what the update check compares against the
# newest release; a folder built at the wrong one offers a player an update to itself.
got_version=$(cd "$work" && "$exe" --version 2>/dev/null || true)
[[ "$got_version" == "$version" ]] ||
  fail "the launcher reports version '${got_version:-nothing}' and the release is '$version', so its update check would compare the wrong version"
say "the launcher reports version $got_version"

# A player has no checkout to fall back on: without the static SDL2 the game build needs
# pkg-config sdl2, which SteamOS does not have and cannot install.
[[ -f "$stage/tools/sdl2/lib/cmake/SDL2/SDL2Config.cmake" ]] ||
  fail "the release carries no static SDL2 at tools/sdl2, so the first game build would need SDL2 installed"

if [[ -d "$stage/tools/cmake" ]]; then
  # A self-contained release: the toolchain is what makes the first build work offline.
  for want in tools/cmake/bin/cmake tools/ninja/ninja; do
    [[ -e "$stage/$want" ]] || fail "the release carries a toolchain but not $want, which a first build needs"
  done
  say "the folder carries the toolchain and the static SDL2"
else
  say "no toolchain in the folder: the launcher downloads the pinned tools archive on first use"
fi

# Nothing derived from a disc may be published, and the folder is where a build tree's
# leftovers would ride along: a copy of the launcher that has been run writes its projects,
# its box art and its saves beside itself.
for bad in project config saves covers naruto naruto2 gnt3 gnt4 narutoeu; do
  [[ -e "$stage/$bad" ]] && fail "the release must not carry $bad"
done
if find "$stage" -type f \( -iname '*.iso' -o -iname '*.ciso' -o -iname '*.rvz' \
     -o -iname '*.wia' -o -iname '*.gcz' -o -iname '*.wbfs' -o -iname '*.tgc' \
     -o -iname '*.nfs' -o -iname '*.dol' -o -iname '*.trk' -o -iname '*.h4m' \) | grep -q .; then
  fail "the release carries a disc image or a DOL"
fi
say "nothing derived from a disc is in the folder"

# The launcher itself, from the extracted copy: a missing library, a resource it only found
# in the build tree, or a binary that will not start shows up here and nowhere else.
runs="$work/runs"
mkdir -p "$runs"

if ! ( cd "$runs" && "$exe" --make-project "$runs/project" ) > "$runs/make-project.log" 2>&1; then
  cat "$runs/make-project.log" >&2
  fail "--make-project failed, so the launcher does not run from the folder"
fi
for game in naruto naruto2 gnt3 gnt4 narutoeu; do
  [[ -f "$runs/project/$game/CMakeLists.txt" ]] ||
    fail "the launcher did not write the $game project"
done
[[ -f "$runs/project/gcn-recomp/CMakeLists.txt" ]] ||
  fail "the launcher did not copy the recompiler into the project it wrote"
say "--make-project wrote all five games and the recompiler"

if ! ( cd "$runs" && "$exe" --render "$runs/frame.bmp" main ) > "$runs/render.log" 2>&1; then
  cat "$runs/render.log" >&2
  fail "--render failed, so the launcher cannot draw a frame"
fi
[[ -s "$runs/frame.bmp" ]] || fail "--render wrote no frame"
header=$(head -c 2 "$runs/frame.bmp")
[[ "$header" == "BM" ]] || fail "--render did not write a bitmap"
# Width and height, little-endian, at 18 and 22 bytes into a bitmap header. A frame that is
# zero pixels across is the failure a render can report as success.
width=$(od -An -tu4 -j18 -N4 "$runs/frame.bmp" | tr -d ' ')
height=$(od -An -tu4 -j22 -N4 "$runs/frame.bmp" | tr -d ' ')
[[ "$width" -ge 320 && "$height" -ge 240 ]] ||
  fail "--render drew a ${width}x${height} frame, which is not a window"
say "--render drew a ${width}x${height} frame"

printf '\n%s\n' "the Linux release works as downloaded"
