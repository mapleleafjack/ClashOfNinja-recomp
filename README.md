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
| `launcher/` | desktop launcher (PySide6): extract the disc, build, bind controls, play — Windows, macOS and Linux |
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

## Launcher (Windows, macOS, Linux)

`launcher/` is a desktop app for the steps above: choose your disc image, extract and
build, bind a controller, play. Python 3.10 or newer.

```sh
# macOS / Linux
python3 -m venv launcher/.venv
launcher/.venv/bin/python -m pip install -r launcher/requirements.txt
launcher/.venv/bin/python launcher/main.py
```

```bat
:: Windows
py -3 -m venv launcher\.venv
launcher\.venv\Scripts\python -m pip install -r launcher\requirements.txt
launcher\.venv\Scripts\python launcher\main.py
```

Package it as a standalone app with PyInstaller — each OS builds its own, there is no
cross-compiling:

```sh
python -m pip install pyinstaller
python launcher/build.py            # --onefile for a single executable
```

The result is `launcher/dist/Naruto Launcher.app` on macOS; on Windows and Linux it is
the `Naruto Launcher` folder in `launcher/dist` holding `Naruto Launcher.exe` / `Naruto
Launcher`. Put it beside the project — it finds the tree itself — or start it anywhere and
use **File → Locate Project Folder**.

What it does:

- **Game disc** — takes any image nodtool reads, runs `tools/extract_disc.py` with its
  output in the window, then builds with cmake and ninja. It shows the extracted
  `main.dol`'s SHA-1 against the pinned one, so the wrong image is visible before a build.
- **Controllers** — binds a GameCube control by pressing it. Controller input comes from
  the game's own `--list-controls` probe, so the device name, button and axis are what the
  runtime reads; keyboard bindings are captured from the launcher window. The binding is
  written to `naruto/controls.txt`, which the launcher passes with `--controls=`.
  **Reset to Defaults** deletes that file and returns to the built-in mapping.
- **Play** — window scale, music, input-logging, and Stop. The game runs with `naruto/` as
  its working directory, so it uses the same `rom/disc` and `saves/` as the command line.

The launcher holds no game data: the extracted tree stays where `tools/extract_disc.py`
put it, and `launcher/.venv/`, `launcher/build/` and `launcher/dist/` are gitignored.

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
`(WTFPL OR CC0-1.0) AND Apache-2.0`, and the launcher's PySide6 is LGPL-3.0 with
PyInstaller GPL-2.0-with-exception — which is what a distributed launcher bundle has to
carry, and why `build.py`'s default one-folder build is the compliant one and `--onefile`
is not. The licence texts are in `licenses/`.

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
