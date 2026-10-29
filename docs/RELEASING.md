# Releases

[README](../README.md) · [Building and running](BUILDING.md) · [The launcher](LAUNCHER.md) ·
[Recompilation notes](RECOMPILATION.md)

A release carries the sources, the launcher and as much of the toolchain as may be
redistributed — and no game data. `.github/workflows/build.yml` builds it; the game itself is
never built there, because it needs the user's own disc.

| Event | What happens |
| --- | --- |
| pull request, push to `main` | compile check on macOS (arm64) and Windows (x64), both running `ctest`, plus `tools/check_publish.py` |
| tag `v*` | the same, then the Windows tool archive and a launcher reconfigured to pin its URL; every zip is attached to that tag's release (created if it does not exist yet) |
| manual run | the same as a compile check; `-f bundle=true` also writes the tool archive and exercises the launcher's download of it, but that launcher is left unpinned — a branch has no release to point at — so it uses installed tools |

There is one Windows job, not two: a release run needs exactly what a compile check does, so
the archive and the pin are extra steps in the same job rather than a second build of SDL2, the
launcher and the tests.

```sh
gh workflow run build.yml -f bundle=true     # exercise the bundle without a release
```

## The assets

| Asset | Holds |
| --- | --- |
| `NarutoLauncher-macOS-arm64-v<version>.zip` | the `.app`, with the game's sources and build tools inside, plus the notices |
| `NarutoLauncher-Windows-x64-v<version>.zip` | the launcher and the sources |
| `NarutoLauncher-tools-windows-x64-v<version>.zip` | the toolchain the launcher downloads on first use |

The tag also names the version: the workflow passes it as `NLA_VERSION`, which becomes the
bundle's `CFBundleShortVersionString`, marks the copy of the sources a macOS bundle lays down,
and, compiled into the launcher, is the version it compares against the newest release. A build
from a branch says `0.0.0` and never offers to update itself. The tool archive is written
before the launcher is configured, so its URL, digest and size are pinned into the build, and
the configure step also records the commit and its date alongside it. A lean Windows zip is
uploaded as a workflow artifact on every run.

Windows has no prebuilt SDL2 for every target, so a launcher build needs a prefix first.
`launcher/packaging/build-sdl2.ps1` builds one from source into
`%LOCALAPPDATA%\gcn-recomp\SDL2-<version>-<tag>`, the same prefix gcn-recomp builds and the
launcher's CMake looks for. It also runs outside Windows (any CMake, Ninja and compiler), which
is how CI's use of it is checked.

## The Windows release

This is the release that asks the player for nothing: everything needed to compile the game
from their own disc is either inside it or fetched once from the project's own release.

A Windows release is two files, because the toolchain dwarfs the launcher: the launcher is a
few megabytes, the tools are about 190 MB, and only a player who has to build the game needs
the second one.

```powershell
# the tool archive the launcher downloads, then the launcher that pins it
powershell -ExecutionPolicy Bypass -File launcher\packaging\build-release.ps1 -SkipBuild -Zip -Split -Version 1.0.1
powershell -ExecutionPolicy Bypass -File launcher\packaging\build-release.ps1 -Lean -SkipBuild -Zip -Version 1.0.1
```

Both forms carry the game's sources: the release folder is `naruto\` and its siblings (sources,
minus the extracted disc, the build and the saves), `tools\extract_disc.py`, `covers\` if the
machine had any, the launcher, and the notices. That is the project layout the launcher looks
for, so it finds the project without being told and a player only ever supplies a disc image.
`-Lean` ships that and no toolchain; a full release adds `tools\cmake`, `tools\ninja`,
`tools\mingw64`, `tools\python`, `tools\sdl2` and `tools\nodtool`.

`-Split` writes `launcher/dist/NarutoLauncher-tools-windows-x64-v<Version>.zip` and prints its
SHA-256 and size. Pass those three to CMake when the launcher is configured, so the build can
only ever fetch what it was built against:

```powershell
cmake -S launcher -B launcher/build-release -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DNLA_VERSION=1.0.1 `
  -DNLA_TOOLS_URL=https://github.com/<owner>/<repo>/releases/download/v1.0.1/NarutoLauncher-tools-windows-x64-v1.0.1.zip `
  -DNLA_TOOLS_SHA256=<hex> -DNLA_TOOLS_BYTES=<bytes>
```

`-Zip` without `-Split` still writes the one-folder release,
`launcher/dist/NarutoLauncher-Windows-x64-v<Version>.zip`, with the tools inside it — what to
hand someone who wants everything at once and no download on first use. That folder is about
670 MB before compression, almost all of it MinGW-w64: the script drops what a build never
reaches (the Fortran and Objective-C frontends, gdb, doxygen, MinGW's own CMake, ctest and
cpack, cppcheck, and the info/locale/man tree), which is a further 370 MB.

`-Version` is the tag without its `v`, and it names the archives. Both archives are counted
against the tree and the script fails if the two disagree, then prints the SHA-256 for the
release notes. They are written with the `tar` that ships with Windows (bsdtar) rather than
`Compress-Archive`, which left a 4 KB archive and no error after minutes on this tree.

The launcher ships no game data: the extracted tree is created inside the release folder on
first use, and `launcher/build/`, `launcher/build-release/` and `launcher/dist/` are gitignored.
The workflow fails the build if the release ever carries any game's `rom/`, `build/` or
`saves/`.

## The macOS release

A macOS release needs a compiler on the machine — the one thing it cannot carry, because the SDK
it needs is licensed with Xcode and may not be redistributed. Apple's Command Line Tools provide
it (`xcode-select --install`: free, no account, one click, about 2 GB), and the launcher offers
to start Apple's own installer, then waits for the compiler and carries on by itself. An Xcode
installation — including a beta — brings the same compiler and SDK and counts as well. Nothing
else has to be installed: no Homebrew, no `pkg-config`, no checkout, because the game's CMake
prefers the bundled SDL2's CMake package over `pkg-config`.

`NarutoLauncher-macOS-arm64-v<version>.zip` is otherwise the whole game's worth of software: the
`.app` carries the launcher (SDL2 linked statically), the game's sources in
`Contents/Resources/project`, and the tools that build it in `Contents/Resources/tools` — CMake,
Ninja, the static SDL2 the game links, and nodtool. `Contents/Resources` is read only in
practice, and a signed bundle must not be written into, so the launcher copies the sources to
`~/Library/Application Support/NarutoLauncher/project` on first use and builds there; a version
marker means an updated launcher replaces its own sources and leaves the extracted disc, the
build and the saves alone.

What is inside is kept small: CMake ships as the single `cmake` binary thinned to arm64 plus its
module tree, about 18 MB where the full Kitware `.app` is 180 MB, and the whole zip is about
14 MB.

Two caveats of packaging without an Apple identity: the bundle is arm64 only, and it is signed
ad hoc, exactly like a local build — an unsigned arm64 bundle will not start, and notarisation
would need an identity this project does not have — so the download needs right-click → Open the
first time.

The sources are the same tree the repository publishes: `rom/`, `build/` and `saves/` are
excluded when the bundle is staged, so the app never carries anything derived from a disc.

## Publishing

What ships is the project's own work: the recompilation project, the steering tables, the
launcher and the tools, plus each game's `analysis/symbols.txt` — addresses, sizes and names,
which is a map and not the game's contents. No `main.dol`, no disc file, no image and no build
output is published, and neither is any game's `saves/` folder (a memory card) or `covers/` (the
publisher's box art). A binary built from this additionally carries
[THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) and `licenses/`, which is a licence
obligation, not an option.

`tools/check_publish.py` is the proof, and it is meant to run before every push:

```sh
tools/check_publish.py     # exit 0: nothing derived from the disc is in the publish set
```

It lists what `git add .` would publish and fails on any path from the disc, any disc-image or
disc-asset extension (`.iso`, `.ciso`, `.rvz`, `.wia`, `.gcz`, `.wbfs`, `.tgc`, `.nfs`, `.h4m`,
`.trk`, `.bnr`, `.dol`), anything under `covers/`, any published file over 2 MiB, any binary
that is not an image, and any file whose contents are either pinned `main.dol` under whatever
name. It reads tracked files as well as untracked ones, because `.gitignore` does not un-track a
file that was added before its rule existed. Before `git init` it still works: it points an
empty throwaway repository at the tree so `.gitignore` answers, instead of trusting a copy of its
rules.

The extracted tree is not needed to publish and can be deleted; `tools/setup.sh` rebuilds it
from your own disc image.
