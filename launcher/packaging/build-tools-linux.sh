#!/bin/bash
# Assembles the Linux build-tools archive the launcher downloads for itself.
#
# Windows ships a MinGW-w64 toolchain and macOS leans on Apple's command line tools. A
# Linux machine has neither guarantee: SteamOS, which a Steam Deck runs, is immutable and
# ships no C++ compiler, and a distribution's gcc is not relocatable. So the compiler here
# is Zig, which is one relocatable directory and targets glibc with `zig cc`; CMake, Ninja
# and a static SDL2 come from the same pinned releases the other platforms use.
#
#   launcher/packaging/build-tools-linux.sh /tmp/tools NarutoLauncher-tools.tar.xz
#
# The archive holds tools/, so it unpacks beside the launcher, which is where the launcher
# looks (see build_search_path in src/tools.cpp). Its SHA-256 and size are what a release
# pins with NLA_TOOLS_SHA256 and NLA_TOOLS_BYTES.
set -euo pipefail

out=${1:?usage: build-tools-linux.sh <staging dir> <archive>}
archive=${2:?usage: build-tools-linux.sh <staging dir> <archive>}
here=$(cd "$(dirname "$0")" && pwd)

# The launcher unpacks the archive into its own folder, so the top-level directory has to
# be the one it then looks in, tools/.
if [ "$(basename "$out")" != tools ]; then
    echo "error: the staging directory must be named tools, not $(basename "$out")" >&2
    exit 1
fi

# Pinned, all of them: the archive is checked against a digest, so what it carries must be
# the same bytes every time it is built.
cmake_version=3.30.5
ninja_version=1.12.1
zig_version=0.13.0
sdl2_version=2.30.9

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fetch() {  # fetch <url> <output>
    echo "fetching $(basename "$2")"
    curl -fsSL --retry 3 -o "$2" "$1"
}

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
for pair in "cc:cc" "gcc:cc" "c++:c++" "g++:c++" "ar:ar"; do
    name=${pair%%:*}
    mode=${pair##*:}
    cat > "$out/bin/$name" <<WRAPPER
#!/bin/sh
# Written by launcher/packaging/build-tools-linux.sh. Zig is the toolchain the archive
# carries, so this stands in for the system's $name.
exec "\$(dirname "\$0")/../zig/zig" $mode "\$@"
WRAPPER
    chmod +x "$out/bin/$name"
done

# SDL2, static and with a CMake config, so the game links it from the archive and needs
# neither the distribution's SDL2 nor pkg-config. Built with the host compiler: it is a
# library that ships ready-made, not something the player's machine compiles.
#
# Which backends end up in it depends on which headers are installed here, and SDL leaves
# a backend out silently when it finds none -- an archive with no X11 or ALSA would give a
# player a game with no window and no sound, and nothing would say why. So the headers are
# required up front.
missing=
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

fetch "https://github.com/libsdl-org/SDL/releases/download/release-${sdl2_version}/SDL2-${sdl2_version}.tar.gz" \
      "$work/sdl2.tar.gz"
tar -xzf "$work/sdl2.tar.gz" -C "$work"
cmake -S "$work/SDL2-${sdl2_version}" -B "$work/sdl2-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$out/sdl2" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF >/dev/null
cmake --build "$work/sdl2-build" --target install >/dev/null

# SDL2 loads each backend by name at run time rather than linking it, so the soname it
# would load is what proves the backend is in the library. Written to a file first: piping
# into `grep -q` would stop `strings` early, and with pipefail that failure would look like
# a missing backend.
strings "$out/sdl2/lib/libSDL2.a" > "$work/sdl2-strings.txt"
for soname in libX11.so.6 libasound.so.2 libudev.so.1; do
    if ! grep -qx "$soname" "$work/sdl2-strings.txt"; then
        echo "error: the SDL2 in the archive would not load $soname, so that backend is not" >&2
        echo "       in it. Install the matching development package and build again." >&2
        exit 1
    fi
done

# The tools the launcher needs beside itself: extract_disc.py is the marker it looks for,
# and setup.sh is what the terminal path runs.
for script in extract_disc.py setup.sh; do
    if [ ! -f "$out/$script" ]; then
        cp "$here/../../tools/$script" "$out/$script"
    fi
done

echo "assembling $archive"
tar -cJf "$archive" -C "$(dirname "$out")" "$(basename "$out")"
echo
echo "$(basename "$archive"): $(stat -c%s "$archive") bytes"
echo "sha256: $(sha256sum "$archive" | cut -d' ' -f1)"
echo
echo "pin these when configuring the launcher:"
echo "  -DNLA_TOOLS_URL=<release url>"
echo "  -DNLA_TOOLS_SHA256=$(sha256sum "$archive" | cut -d' ' -f1)"
echo "  -DNLA_TOOLS_BYTES=$(stat -c%s "$archive")"
