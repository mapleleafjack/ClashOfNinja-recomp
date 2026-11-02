#!/usr/bin/env bash
# Checks a macOS release the way a player receives it: unzip the archive the release
# publishes, then confirm the bundle inside is the one that runs.
#
#   bash launcher/packaging/check-release-macos.sh \
#       launcher/dist/NarutoLauncher-macOS-arm64-v1.0.1.zip --version 1.0.1
#
#   # or a bundle that has not been packaged yet
#   bash launcher/packaging/check-release-macos.sh \
#       --app launcher/build-ci/NarutoLauncher.app --version 0.0.0
#
# Everything is read from the extracted copy rather than the build tree: a file that only
# exists in the build tree, or a signature the archive did not carry, is exactly what a
# download would get wrong. CI runs this after packaging, on every run, so a release cannot
# ship a bundle nobody started.
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
app=
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) version=${2:-}; shift 2 ;;
    --app) app=${2:-}; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    -*) fail "unknown option: $1" ;;
    *) archive=$1; shift ;;
  esac
done

[[ -n "$version" ]] || fail "--version is required"
[[ -n "$archive" || -n "$app" ]] || fail "pass the release archive, or --app for an unpacked bundle"
[[ -n "$archive" && -n "$app" ]] && fail "pass the archive or --app, not both"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

stage=
if [[ -n "$archive" ]]; then
  [[ -f "$archive" ]] || fail "no archive at $archive"
  ditto -x -k "$archive" "$work/unpacked"

  # The download is one folder named for the release, so extracting it does not scatter
  # files through the player's Downloads folder.
  top=$(ls -A "$work/unpacked" | grep -v -e '^\.DS_Store$' -e '^__MACOSX$' || true)
  count=$(printf '%s\n' "$top" | grep -c . || true)
  [[ "$count" -eq 1 ]] || fail "the archive should hold one folder, and it holds: $(printf '%s ' $top)"
  [[ "$top" == "Naruto Launcher $version" ]] || fail "the archive's folder is '$top', not 'Naruto Launcher $version'"
  stage="$work/unpacked/$top"

  # What the README the release ships tells a player to find.
  for want in "Naruto Launcher.app" LICENSE THIRD-PARTY-NOTICES.md README.txt licenses; do
    [[ -e "$stage/$want" ]] || fail "the archive is missing $want"
  done
  say "the archive holds 'Naruto Launcher $version' with the bundle, the notices and the README"

  bundle="$stage/Naruto Launcher.app"
else
  bundle=$app
  [[ -d "$bundle" ]] || fail "no bundle at $bundle"
fi

plist="$bundle/Contents/Info.plist"
exe="$bundle/Contents/MacOS/NarutoLauncher"
res="$bundle/Contents/Resources"
[[ -f "$plist" ]] || fail "the bundle has no Info.plist"
[[ -x "$exe" ]] || fail "the bundle has no executable at Contents/MacOS/NarutoLauncher"

plist_read() {  # plist_read <key>
  /usr/libexec/PlistBuddy -c "Print :$1" "$plist" 2>/dev/null || true
}

got_version=$(plist_read CFBundleShortVersionString)
# CFBundleShortVersionString is three numbers, so a pre-release tag - v1.7.0-rc.1 - shows in
# the bundle as its numeric part, and the launcher's own --version carries the suffix.
core=${version%%-*}
[[ "$got_version" == "$version" || "$got_version" == "$core" ]] ||
  fail "the bundle says version '$got_version' and the release is '$version'"

got_exe=$(plist_read CFBundleExecutable)
[[ "$got_exe" == "NarutoLauncher" ]] ||
  fail "Info.plist names '$got_exe' as the executable"

identifier=$(plist_read CFBundleIdentifier)
[[ -n "$identifier" ]] || fail "Info.plist names no bundle identifier"

icon=$(plist_read CFBundleIconFile)
[[ -n "$icon" ]] || fail "Info.plist names no icon"
[[ -f "$res/$icon" ]] || fail "Info.plist names '$icon' and the bundle does not carry it"
say "Info.plist: $identifier, version $version, icon $icon"

# Apple silicon refuses to start a bundle whose seal is broken, and the archive is where a
# seal breaks: a file written after the signature, or a zip that drops its metadata.
codesign --verify --deep --strict "$bundle" ||
  fail "the bundle's ad-hoc signature does not verify, so it would not start"
say "the ad-hoc signature verifies"

archs=$(lipo -archs "$exe")
[[ "$archs" == "arm64" ]] || fail "the bundle is built for '$archs', and the release is arm64 only"

otool -L "$exe" > "$work/otool.txt"
if grep -qi sdl "$work/otool.txt"; then
  cat "$work/otool.txt" >&2
  fail "the bundle links SDL2 dynamically, so the download would need SDL2 installed"
fi
say "arm64, with SDL2 linked statically"

# Everything a first click needs, since a player has no checkout and no Homebrew to fall
# back on: the tools that build the game and the recompiler the projects add.
for want in mods.json \
            gcn-recomp/CMakeLists.txt \
            tools/extract_disc.py tools/setup.sh \
            tools/cmake/bin/cmake tools/ninja/ninja \
            tools/dtk/dtk tools/nodtool/nodtool \
            tools/sdl2/lib/cmake/SDL2/SDL2Config.cmake; do
  [[ -e "$res/$want" ]] || fail "the bundle is missing $want, which a first build needs"
done
say "the bundle carries the recompiler, the tools and the static SDL2"

# Nothing derived from a disc may be published, and the bundle is where a build tree's
# leftovers would ride along: a copy of the launcher that has been run writes its projects,
# its box art and its saves beside itself, which is inside the bundle.
for dir in "$res" "$bundle/Contents/MacOS"; do
  for bad in project covers saves naruto naruto2 gnt3 gnt4 narutoeu; do
    [[ -e "$dir/$bad" ]] && fail "the release must not carry ${dir#"$bundle"/}/$bad"
  done
done
if find "$bundle" -type f \( -iname '*.iso' -o -iname '*.ciso' -o -iname '*.rvz' \
     -o -iname '*.wia' -o -iname '*.gcz' -o -iname '*.wbfs' -o -iname '*.tgc' \
     -o -iname '*.nfs' -o -iname '*.dol' \) | grep -q .; then
  fail "the release carries a disc image or a DOL"
fi
say "nothing derived from a disc is in the bundle"

# The launcher itself, from the extracted copy: a missing framework, a resource it only
# found in the build tree, or a bundle that will not start shows up here and nowhere else.
runs="$work/runs"
mkdir -p "$runs"

if ! "$exe" --make-project "$runs/project" > "$runs/make-project.log" 2>&1; then
  cat "$runs/make-project.log" >&2
  fail "--make-project failed, so the launcher does not run from the bundle"
fi
for game in naruto naruto2 gnt3 gnt4 narutoeu; do
  [[ -f "$runs/project/$game/CMakeLists.txt" ]] ||
    fail "the launcher did not write the $game project"
done
[[ -f "$runs/project/gcn-recomp/CMakeLists.txt" ]] ||
  fail "the launcher did not copy the recompiler into the project it wrote"
say "--make-project wrote all five games and the recompiler"

if ! "$exe" --render "$runs/frame.bmp" main > "$runs/render.log" 2>&1; then
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

printf '\n%s\n' "the macOS release works as downloaded"
