# Naruto: Clash of Ninja (GNREDA) — static recompilation for macOS

Native Apple Silicon build of the GameCube game via
[gcn-recomp](https://github.com/ethannicholas/gcn-recomp), using
[decomp-toolkit](https://github.com/encounter/decomp-toolkit) for analysis and function
boundaries. Not an emulator: the PowerPC code is translated to C and linked against a
runtime that implements the GameCube hardware.

## Layout

| Path | What it is |
| --- | --- |
| `tools/extract_disc.py` | one-time extraction of the user's own disc image into `naruto/rom/disc` |
| `tools/setup.sh` | that, then the build |
| `tools/check_publish.py` | proves nothing derived from the disc is in what would be published |
| `LICENSE`, `THIRD-PARTY-NOTICES.md` | this project's MIT licence, and the licences of what it is built on |
| `launcher/` | desktop launcher for Windows, macOS and Linux: one C++ application — extract the disc, build, play |
| `naruto/rom/disc/` | the disc, extracted: `sys/main.dol`, `sys/fst.bin`, `files/…` and a `disc.manifest` of disc offsets. Derived from the user's disc; not committed |
| `config/GNREDA/`, `build/GNREDA/` | dtk project config and split output, when you generate them. Not committed — both come from the disc, see "Analysis" |
| `naruto/` | the recompilation project (the thing that builds and runs) |
| `naruto/analysis/symbols.txt` | dtk symbols, with the SDK functions dtk missed named by hand |
| `naruto/recomp/*.txt` | steering tables: which functions to replace at runtime, etc. |
| `naruto/build/naruto` | the app |
| `naruto/build/naruto_bench` | headless benchmark |

## Setup (once, with your own disc)

The game needs a disc image, and none may be committed or shipped. Point the setup at
yours — any format [nodtool](https://github.com/encounter/nod) reads, which is ISO, CISO,
RVZ, WIA, GCZ, WBFS, TGC and NFS:

```sh
cargo install --locked nodtool          # once, if your image is not a plain ISO/CISO
tools/setup.sh "/path/to/your disc image"
```

That writes `naruto/rom/disc/`, then reads it back through the manifest and compares every
file with the image, refusing to continue if `main.dol` is not the pinned build. **The
image is not needed after that:**

```sh
rm "/path/to/your disc image"
```

`naruto/rom/disc/disc.manifest` is what makes that possible: it records the disc offset
each extracted file came from, and the runtime serves the console's DVD reads from it
(see "How the disc reaches the game" below).

## Build and run

```sh
cd naruto
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
./build/naruto            # or: ./build/naruto_bench --seconds=10
```

`cmake` picks up `rom/disc` on its own; `-DGAME_ISO=/path/to/image.iso` still works, as
does putting an `.iso`/`.ciso` in `rom/` instead of the tree.

Under MinGW-w64 the game is linked with `-static -static-libgcc -static-libstdc++`, so the
built `naruto.exe` carries the C++ runtime: it starts on its own, with no toolchain DLLs
beside it or on `PATH`.

## Launcher (Windows, macOS, Linux)

`launcher/` is one C++ application that builds on all three. Its window is a single
**Launch the game** button: it asks for your disc image, extracts and builds as needed, then
plays. It draws its own interface — no widget toolkit, one executable per platform — and
needs only SDL2, the same library the game's runtime uses.

```sh
# macOS: xcode-select --install, then
brew install cmake ninja sdl2

# Debian/Ubuntu
sudo apt install cmake ninja-build g++ libsdl2-dev

# build and run
cmake -S launcher -B launcher/build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C launcher/build NarutoLauncher
launcher/build/NarutoLauncher              # macOS: launcher/build/NarutoLauncher.app
```

On Windows, CMake finds the SDL2 that `gcn-recomp` already builds under
`%LOCALAPPDATA%\gcn-recomp`; pass `-DSDL2_DIR=<the folder holding SDL2Config.cmake>` to
use another. A release of the launcher is a single executable — SDL2 is linked in
statically — so nothing else has to be installed for it to start.

What it does:

- **Launch the game** — the window is one call to action. It asks for the disc image when
  the game data is missing, builds the game when there is no binary yet, and otherwise
  starts it. While it works, the step and a progress bar replace the button, and Activity
  shows the output; when the game runs the button becomes Stop.
- **Game disc** — reads ISO and CISO itself, including the FST offset shift that NKit and
  rebuilt images use. Other formats (RVZ, WIA, GCZ, WBFS, TGC, NFS) are converted with
  `nodtool` first. It checks `main.dol`'s SHA-1 before writing anything and puts the new
  tree in place only when the whole extraction succeeded; the extractor writes the same
  bytes, and the same `disc.manifest`, as `tools/extract_disc.py`.
- **Build** — CMake and Ninja with their output streamed into Activity, a progress bar and
  Cancel. Cancel ends the whole build tree: a job object on Windows, the process group
  elsewhere, so compilers the build started go too.
- **Play** — the same `--scale`, `--no-music`, `--no-input-log` and `--controls` arguments
  as the command line. Stop sends `SIGINT` on macOS and Linux, which the runtime answers by
  dumping its GX FIFO, then escalates if the game ignores it.
- **Updates** — the launcher asks GitHub for the newest release when it starts, and
  offers it when that release is newer than the one it was built from. "Update now"
  downloads the zip for this system, unpacks it and hands over to a small helper: the
  launcher exits, the helper puts the new version in place of the old one and starts it.
  Until then nothing is touched. Settings names the version, has *Check now* and can turn
  the check off; a version answered with *Cancel* is not asked about again, but stays
  behind the **Update** button in the header. A build made from a branch rather than a
  tag hides itself from this, and an install that cannot be written to falls back to the
  release page. The check is the only network request the launcher makes, unless the
  project source is updated from git as below.
- **Game source** — an update replaces the launcher only; the game itself is compiled
  from the project folder, so releases carry no `naruto/` source. When that folder is a
  git checkout, Settings names its revision and **Update project** runs
  `git pull --ff-only --tags --prune` in it, then offers Rebuild. It refuses a folder with
  uncommitted changes, so nothing local is merged, rebased or lost, and the disc, the
  build output and the saves inside the tree are untouched. The launcher also records the
  commit it was built from; when the checkout is older than that, Settings says so instead
  of letting an unmatched pair fail later in the build.
- **Settings** — everything that is not part of launching the game: the project folder,
  a replacement disc image, the play defaults (window scale, music, input log), the saves
  and controls files, the build tools with a Rebuild action, and a Desktop or menu
  shortcut: a `.lnk` on Windows, a symlink to the `.app` on macOS, a `.desktop` file on
  Linux.
- **Tools** — a `tools/` folder beside the executable comes first, then PATH and the
  platform's usual install locations (winget packages and Program Files on Windows,
  Homebrew on macOS, `/usr/local` and `~/.cargo/bin` everywhere). On Windows, when the
  build tools are nowhere to be found, the launcher offers to fetch them itself: it
  downloads the archive pinned when it was built, checks it against the pinned SHA-256,
  and unpacks it into `tools/` — Settings → BUILD → **Get build tools**, or the prompt
  that appears when a build is asked for without them.

Drag a disc image or a project folder onto the window, or use the built-in picker. The
launcher finds the project beside itself, or remembers the folder you choose. "Show
details" reveals the Activity log whenever it is hidden.

`ctest --test-dir launcher/build` renders the main window, Settings, the picker and the
update prompt to BMP files, through
`--render <file> [main|settings|picker|confirm|update|update-banner|click:x,y|settings-click:x,y|update-click:x,y]`,
which needs no window and no desktop. The click modes paint, click that point and paint
again (the `settings-` and `update-` forms open those views first), so the tests cover hit
testing and activation as well as drawing. A render also needs one usable system font; on a minimal
Linux install that means `fonts-dejavu` or `fonts-liberation`.

`launcher/tests/extract_test.cpp` runs the extractor from a console so its output can be
compared with `tools/extract_disc.py`; it is a developer target, not shipped.
`launcher/tests/update_test.cpp` covers the update check without a network: version
comparison, reading a release out of GitHub's JSON, and the helper script, which the test
runs for real against a stand-in install. It also builds a throwaway git repository to
check that an out-of-date clone fast-forwards and a modified one is refused.
`update_test --live` also asks GitHub once, and prints the release it finds.

`--fetch-tools <log>` runs the same download the launcher offers, without a window, and
writes what its steps report to `log`. A release build uses it to prove the pinned archive
still downloads and still matches its digest.

### Windows release

A Windows release is two files, because the toolchain dwarfs the launcher: the launcher is
a couple of megabytes, the tools are about 190 MB, and only a player who has to build the
game needs the second one.

```powershell
# the tool archive the launcher downloads, then the launcher that pins it
powershell -ExecutionPolicy Bypass -File launcher\packaging\build-release.ps1 -SkipBuild -Zip -Split -Version 1.0.1
powershell -ExecutionPolicy Bypass -File launcher\packaging\build-release.ps1 -Lean -SkipBuild -Zip -Version 1.0.1
```

`-Split` writes `launcher/dist/NarutoLauncher-tools-windows-x64-v<Version>.zip` and prints
its SHA-256 and size. Pass those three to CMake when the launcher is configured, so the
build can only ever fetch what it was built against:

```powershell
cmake -S launcher -B launcher/build-release -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DNLA_VERSION=1.0.1 `
  -DNLA_TOOLS_URL=https://github.com/<owner>/<repo>/releases/download/v1.0.1/NarutoLauncher-tools-windows-x64-v1.0.1.zip `
  -DNLA_TOOLS_SHA256=<hex> -DNLA_TOOLS_BYTES=<bytes>
```

`-Zip` without `-Split` still writes the one-folder release,
`launcher/dist/NarutoLauncher-Windows-x64-v<Version>.zip`, with the tools inside it — that
is what to hand someone who wants everything at once and no download on first use. That
folder is about 670 MB before compression, almost all of it MinGW-w64: the script drops
what a build never reaches — the Fortran and Objective-C frontends, gdb, doxygen, MinGW's
own CMake, ctest and cpack, cppcheck, and the info/locale/man tree — which is a further
370 MB. Unzip it to a path with no spaces in it (for example `C:\Games\NarutoLauncher`):
MinGW-w64 cannot run from a path containing spaces, and it is the compiler that builds the
game. `-Lean` skips the tools entirely and leaves the launcher looking on PATH.

`-Version` is the tag without its `v`, and it names the archives. Both archives are counted
against the tree and the script fails if the two disagree, then prints the SHA-256 for the
release notes. They are written with the `tar` that ships with Windows (bsdtar) rather than
`Compress-Archive`, which left a 4 KB archive and no error after minutes on this tree.

The launcher holds no game data: the extracted tree stays where the extractor put it, and
`launcher/build/`, `launcher/build-release/` and `launcher/dist/` are gitignored.

## Automated builds

`.github/workflows/build.yml` builds the launcher on GitHub Actions. The game is not built
there — it needs the user's own disc image.

| Event | What happens |
| --- | --- |
| pull request, push to `main` | compile check on macOS (arm64) and Windows (x64), both running `ctest`, plus `tools/check_publish.py` |
| tag `v*` | the same, then the Windows tool archive, and a launcher configured again to pin its URL; every zip is attached to that tag's release (created if it does not exist yet) |
| manual run | the same as a compile check; `-f bundle=true` also writes the tool archive and exercises the launcher's download of it, but that launcher is left unpinned — a branch has no release to point at — so it uses installed tools |

There is one Windows job, not two: a release run needs exactly what a compile check does,
so the archive and the pin are extra steps in the same job rather than a second build of
SDL2, the launcher and the tests.

The assets are named after the tag: `NarutoLauncher-macOS-arm64-v<version>.zip` holding the
`.app` plus the notices, `NarutoLauncher-Windows-x64-v<version>.zip` holding the launcher,
and `NarutoLauncher-tools-windows-x64-v<version>.zip` holding the toolchain it downloads on
first use. A lean Windows zip is uploaded as a workflow artifact on every run. The tag also
names the version: the workflow passes it as `NLA_VERSION`, which becomes the bundle's
`CFBundleShortVersionString` and, compiled into the launcher, the version it compares
against the newest release. A build from a branch says `0.0.0` and never offers to update
itself. The tool archive is written before the launcher is configured, so its URL, digest
and size are pinned into the build, and the configure step also records the commit and its
date, which is how Settings knows the project folder is older than the launcher.

```sh
gh workflow run build.yml -f bundle=true     # exercise the bundle without a release
```

Windows has no prebuilt SDL2 for every target, so a launcher build needs a prefix first.
`launcher/packaging/build-sdl2.ps1` builds one from source into
`%LOCALAPPDATA%\gcn-recomp\SDL2-<version>-<tag>`, the same prefix gcn-recomp builds and the
launcher's CMake looks for. It also runs outside Windows (any CMake, Ninja and compiler),
which is how CI's use of it is checked.

The macOS build is signed ad hoc, exactly like a local one: an unsigned arm64 bundle will
not start there, and notarisation would need an Apple identity this project does not have.
So the download still needs right-click → Open the first time.

## Analysis (optional)

The dtk outputs are not needed to build or run, only to work on the recompilation. They
are derived from the disc, so they are not committed. `dtk` fetches itself with
`naruto/gcn-recomp/tools/fetch_dtk.sh`; run from the project root:

```sh
DTK=naruto/gcn-recomp/tools/dtk
$DTK dol config naruto/rom/disc/sys/main.dol -o config/GNREDA/config.yml   # once
$DTK dol split config/GNREDA/config.yml build/GNREDA                        # 0.6s
```

## How the disc reaches the game

gcn-recomp's runtime answers the console's DVD reads *by disc offset*, so an extracted
directory alone is not enough: `tools/extract_disc.py` also writes `disc.manifest`, one
line per file giving its disc offset, and `runtime/hw/di.cpp` serves `iso_read()` from
the tree — files from their own extents, the padding between them as zeroes. The result
is checked, not assumed: after extracting, the setup reads the tree back through the
manifest and compares it with the image, and the guest's read sequence from the tree and
from the image are identical (173 of 173 reads, offset and length, over the opening
movie's first 40 seconds).

**The disc's FST does not always say what a disc's FST says.** Retail discs store each
file's offset divided by four (files are four-byte aligned). Both images here — the NKit
one and the full-size one — store them as they are, so the setup probes for which reading
lands on real data instead of trusting the convention. The other reading points at
padding, which is what makes the test conclusive.

## Findings

**`main.dol` SHA-1 `19251f7f15f8b6f7e0cd1c753409151ec49d9af2`** — the build the project
pins in `naruto/CMakeLists.txt`, and what the setup verifies before extracting anything.

**The disc is an NKit image, and dtk reads it directly.** `dtk disc info` reports
`Format: ISO / Lossless: true`; no conversion to a plain ISO was needed. gcn-recomp's own
`extract_dol.py` only understands uncompressed `.iso`/`.ciso`, but it reads the NKit file
fine too because the header and DOL are never compressed — only the unused padding is.
A full-size ISO of the same release extracts to byte-identical content, so either works;
the only files that differ are `boot.bin` (NKit writes its own magic at `0x200`) and
`fst.bin` (each image's own layout).

**dtk found 4015 functions but left part of the SDK unnamed.** `OSSaveContext` came out as
`fn_800E4184`, `OSPanic` as `fn_800E48E4`. Both were identified from their disassembly
(GQR/CR/LR/MSR saves into an `OSContext`; `OSDisableInterrupts` + `vprintf` + `OSReport` +
`PPCHalt`) and renamed in `naruto/analysis/symbols.txt`.

**The one thing that made it run instead of dying: `special_calls.txt` holds the address of
`OSSaveContext` itself, not of the `bl` that calls it.** gcn-recomp emits a `setjmp` at
every `bl` to an address listed in `special_calls.txt`, so that `OSLoadContext` can resume
the thread. Listing the call site (`800E94F8`, what you get from searching the disassembly
for the `bl`) compiles and links, then faults the moment `SelectThread` switches to a saved
context: `longjmp` to a `jmp_buf` that was never filled, jumping to host address `0x8`.
Listing `800E4184` — the function — fixes it. Symptom, for reference: `FAULT: host address
0x8`, `lr=800E94FC`, host thread stopped inside `SelectThread`.

Everything else in `naruto/recomp/` follows the reference projects: `hle.txt` replaces
`OSSaveContext`, `OSLoadContext`, `OSReport`, `OSPanic`, `OSResetSystem` with runtime
implementations, and `idle.txt` marks the backward branch of the SDK scheduler's idle spin
(`800E954C` in `SelectThread`) so the runtime can skip guest time.

## Status

Works, running from the extracted tree with no image present: boot, SDK init, DVD/FST
reads, both logo movies and the opening H4M movie (full-frame video and audio), title
screen with 3D scene, background music and sound effects, and in-game 3D with the HUD
(a Naruto vs Kakashi match rendered at ~52 fps in a 1280×960 window).
Performance: ~570 fps headless (9.5× the game's 60 fps).

Not yet done: gameplay has not been driven with a controller here, so inputs and the
battle logic beyond what the game does on its own are untested; and the earlier "crash
around the title screen" was not reproduced — it looks like it was the two concurrent
instances sharing `saves/`, plus a `SIGINT` reading as a crash (the interrupt handler
dumps the GX FIFO and exits 1).

## Publishing

What ships is the project's own work: the recompilation project, the steering tables, the
launcher and the tools, plus `naruto/analysis/symbols.txt` — addresses, sizes and names,
which is a map and not the game's contents. No `main.dol`, no disc file, no image and no
build output is published, and neither is `naruto/saves/` (a memory card). A binary built
from this additionally carries [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and
`licenses/`, which is a licence obligation, not a nicety — see "Licensing".

`tools/check_publish.py` is the proof, and it is meant to run before every push:

```sh
tools/check_publish.py     # exit 0: nothing derived from the disc is in the publish set
```

It lists what `git add .` would publish and fails on any path from the disc, any
disc-image or disc-asset extension (`.iso`, `.ciso`, `.rvz`, `.wia`, `.gcz`, `.wbfs`,
`.tgc`, `.nfs`, `.h4m`, `.trk`, `.bnr`, `.dol`), any published file over 2 MiB, any
binary that is not an image, and any file whose contents are the pinned `main.dol` under
whatever name. It reads tracked files as well as untracked ones, because `.gitignore` does
not un-track a file that was added before its rule existed. Before `git init` it still
works: it points an empty throwaway repository at the tree so `.gitignore` answers,
instead of trusting a copy of its rules.

The extracted tree is not needed to publish and can be deleted; `tools/setup.sh` rebuilds
it from your own disc image.

## Licensing

The project's own work — `tools/`, `launcher/`, `naruto/CMakeLists.txt`, `naruto/recomp/`,
`naruto/analysis/` and this documentation — is MIT, © 2026 Jack Musajo; see
[LICENSE](LICENSE).

What it builds on is not all under the same terms, so
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) is the map: gcn-recomp is Ethan
Nicholas's MIT, whose notice stays at `naruto/gcn-recomp/LICENSE` and is also reproduced
in the notices file because a built binary contains that code; the glad loaders are
`(WTFPL OR CC0-1.0) AND Apache-2.0`, and the launcher links SDL2 (zlib), which is inside
every launcher binary, plus two public-domain stb headers it vendors in
`launcher/src/third_party/`. The licence texts are in `licenses/`.

**Unofficial.** Not affiliated with, endorsed by or licensed by Nintendo, Shueisha, or the
game's developers and publishers. Naruto and the game are their property; this project
contains none of their code or content and needs the user's own disc to do anything.

## Notes

- `naruto/saves/` holds the memory card image and input logs.
- `--dump-dir=DIR --dump-every=N` writes every Nth presented frame as PNG. Dumps come from
  the present path, so they need a window: `--headless` never presents a frame (it discards
  each batch) and is for measuring, not looking.
- `--headless` runs until it is killed; there is no time limit in the app. `--seconds=N` is
  `naruto_bench`'s. `SIGINT` dumps the GX FIFO and exits, which is why a headless run ends
  with a FIFO dump and exit status 1 — that is the interrupt handler, not a crash.
- `--record=out.mp4` needs ffmpeg with an encoder it can open; Homebrew's ffmpeg failed all
  three trial encoders here, so use `--dump-dir` instead.
- Scripted input: `GCN_INPUT="frame:BUTTON:duration,..."`, e.g. `"900:START:20,1200:A:10"`.
- Nothing from the disc is redistributable. `naruto/rom/`, `naruto/build/`,
  `config/GNREDA/` and `naruto/saves/` are all derived from it and are gitignored; see
  "Publishing". The extracted tree can be rebuilt from the user's own disc at any time
  with `tools/setup.sh`.
